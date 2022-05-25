// Copyright (c) 2015-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <zmq/zmqpublishnotifier.h>

#include <chain.h>
#include <chainparams.h>
#include <netbase.h>
#include <node/blockstorage.h>
#include <rpc/server.h>
#include <streams.h>
#include <util/system.h>
#include <validation.h> // For cs_main
#include <zmq/zmqutil.h>

#include <zmq.h>

#include <cstdarg>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>

using node::ReadBlockFromDisk;

static std::multimap<std::string, CZMQAbstractPublishNotifier*> mapPublishNotifiers;

static const char *MSG_HASHBLOCK = "hashblock";
static const char *MSG_HASHTX    = "hashtx";
static const char *MSG_RAWBLOCK  = "rawblock";
static const char *MSG_RAWTX     = "rawtx";
static const char *MSG_SEQUENCE  = "sequence";

static const char *MSG_MEMPOOLADDED = "mempooladded";
static const char *MSG_MEMPOOLREMOVED = "mempoolremoved";
static const char *MSG_MEMPOOLREPLACED = "mempoolreplaced";

// Internal function to send multipart message
static int zmq_send_multipart(void *sock, const void* data, size_t size, ...)
{
    va_list args;
    va_start(args, size);

    while (1)
    {
        zmq_msg_t msg;

        int rc = zmq_msg_init_size(&msg, size);
        if (rc != 0)
        {
            zmqError("Unable to initialize ZMQ msg");
            va_end(args);
            return -1;
        }

        void *buf = zmq_msg_data(&msg);
        memcpy(buf, data, size);

        data = va_arg(args, const void*);

        rc = zmq_msg_send(&msg, sock, data ? ZMQ_SNDMORE : 0);
        if (rc == -1)
        {
            zmqError("Unable to send ZMQ msg");
            zmq_msg_close(&msg);
            va_end(args);
            return -1;
        }

        zmq_msg_close(&msg);

        if (!data)
            break;

        size = va_arg(args, size_t);
    }
    va_end(args);
    return 0;
}

static bool IsZMQAddressIPV6(const std::string &zmq_address)
{
    const std::string tcp_prefix = "tcp://";
    const size_t tcp_index = zmq_address.rfind(tcp_prefix);
    const size_t colon_index = zmq_address.rfind(":");
    if (tcp_index == 0 && colon_index != std::string::npos) {
        const std::string ip = zmq_address.substr(tcp_prefix.length(), colon_index - tcp_prefix.length());
        CNetAddr addr;
        LookupHost(ip, addr, false);
        if (addr.IsIPv6()) return true;
    }
    return false;
}

static int zmq_send_multipart(void *sock, const zmq_message& message)
{
    for (size_t i = 0; i < message.size(); i++) {
        auto const& part = message[i];
        zmq_msg_t msg;

        int rc = zmq_msg_init_size(&msg, part.size());
        if (rc != 0) {
            zmqError("Unable to initialize ZMQ msg");
            return -1;
        }

        void* buf = zmq_msg_data(&msg);
        std::memcpy(buf, part.data(), part.size());

        rc = zmq_msg_send(&msg, sock, (i < (message.size() - 1)) ? ZMQ_SNDMORE : 0);
        if (rc == -1) {
            zmqError("Unable to send ZMQ msg");
            zmq_msg_close(&msg);
            return -1;
        }

        zmq_msg_close(&msg);
    }

    LogPrint(BCLog::ZMQ, "sent message with %d parts\n", message.size());
    return 0;
}

// converts an uint256 hash into a zmq_message_part (hash is reversed)
static zmq_message_part hashToZMQMessagePart(const uint256 hash) {
    zmq_message_part part_hash;
    for (int i = 31; i >= 0; i--)
        part_hash.push_back((std::byte)hash.begin()[i]);
    return part_hash;
}

// converts a CTransaction into a zmq_message_part (by serializing it)
static zmq_message_part transactionToZMQMessagePart(const CTransaction& transaction) {
    zmq_message_part part_transaction;
    CDataStream ss_transaction(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
    ss_transaction << transaction;
    part_transaction.assign(ss_transaction.begin(), ss_transaction.end());
    return part_transaction;
}

// converts an int64_t into a zmq_message_part
static zmq_message_part int64ToZMQMessagePart(const int64_t value) {
    zmq_message_part part;
    for (size_t i = 0; i < sizeof(int64_t); i++) {
      part.push_back((std::byte) (value >> i*8));
    }
    return part;
}

// returns the current time in milliseconds as zmq_message_part
static zmq_message_part getCurrentTimeMillis() {
    return zmq_message_part(int64ToZMQMessagePart(GetTimeMillis()));
}

bool CZMQAbstractPublishNotifier::Initialize(void *pcontext)
{
    assert(!psocket);

    // check if address is being used by other publish notifier
    std::multimap<std::string, CZMQAbstractPublishNotifier*>::iterator i = mapPublishNotifiers.find(address);

    if (i==mapPublishNotifiers.end())
    {
        psocket = zmq_socket(pcontext, ZMQ_PUB);
        if (!psocket)
        {
            zmqError("Failed to create socket");
            return false;
        }

        LogPrint(BCLog::ZMQ, "zmq: Outbound message high water mark for %s at %s is %d\n", type, address, outbound_message_high_water_mark);

        int rc = zmq_setsockopt(psocket, ZMQ_SNDHWM, &outbound_message_high_water_mark, sizeof(outbound_message_high_water_mark));
        if (rc != 0)
        {
            zmqError("Failed to set outbound message high water mark");
            zmq_close(psocket);
            return false;
        }

        const int so_keepalive_option {1};
        rc = zmq_setsockopt(psocket, ZMQ_TCP_KEEPALIVE, &so_keepalive_option, sizeof(so_keepalive_option));
        if (rc != 0) {
            zmqError("Failed to set SO_KEEPALIVE");
            zmq_close(psocket);
            return false;
        }

        // On some systems (e.g. OpenBSD) the ZMQ_IPV6 must not be enabled, if the address to bind isn't IPv6
        const int enable_ipv6 { IsZMQAddressIPV6(address) ? 1 : 0};
        rc = zmq_setsockopt(psocket, ZMQ_IPV6, &enable_ipv6, sizeof(enable_ipv6));
        if (rc != 0) {
            zmqError("Failed to set ZMQ_IPV6");
            zmq_close(psocket);
            return false;
        }

        rc = zmq_bind(psocket, address.c_str());
        if (rc != 0)
        {
            zmqError("Failed to bind address");
            zmq_close(psocket);
            return false;
        }

        // register this notifier for the address, so it can be reused for other publish notifier
        mapPublishNotifiers.insert(std::make_pair(address, this));
        return true;
    }
    else
    {
        LogPrint(BCLog::ZMQ, "zmq: Reusing socket for address %s\n", address);
        LogPrint(BCLog::ZMQ, "zmq: Outbound message high water mark for %s at %s is %d\n", type, address, outbound_message_high_water_mark);

        psocket = i->second->psocket;
        mapPublishNotifiers.insert(std::make_pair(address, this));

        return true;
    }
}

void CZMQAbstractPublishNotifier::Shutdown()
{
    // Early return if Initialize was not called
    if (!psocket) return;

    int count = mapPublishNotifiers.count(address);

    // remove this notifier from the list of publishers using this address
    typedef std::multimap<std::string, CZMQAbstractPublishNotifier*>::iterator iterator;
    std::pair<iterator, iterator> iterpair = mapPublishNotifiers.equal_range(address);

    for (iterator it = iterpair.first; it != iterpair.second; ++it)
    {
        if (it->second==this)
        {
            mapPublishNotifiers.erase(it);
            break;
        }
    }

    if (count == 1)
    {
        LogPrint(BCLog::ZMQ, "zmq: Close socket at address %s\n", address);
        int linger = 0;
        zmq_setsockopt(psocket, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_close(psocket);
    }

    psocket = nullptr;
}

bool CZMQAbstractPublishNotifier::SendZmqMessage(const char *command, const void* data, size_t size)
{
    assert(psocket);

    /* send three parts, command & data & a LE 4byte sequence number */
    unsigned char msgseq[sizeof(uint32_t)];
    WriteLE32(msgseq, nSequence);
    int rc = zmq_send_multipart(psocket, command, strlen(command), data, size, msgseq, (size_t)sizeof(uint32_t), nullptr);
    if (rc == -1)
        return false;

    /* increment memory only sequence number after sending */
    nSequence++;

    return true;
}

bool CZMQAbstractPublishNotifier::SendZmqMessage(const char *command, const std::vector<zmq_message_part>& payload)
{
    assert(psocket);

    std::string commandStr(command);
    zmq_message_part part_command(strlen(command));
    std::transform(commandStr.begin(), commandStr.end(), part_command.begin(), [] (char c) { return std::byte(c); });
    zmq_message_part part_sequence = int32ToZMQMessagePart(nSequence);

    zmq_message message = {};
    message.push_back(part_command);
    message.push_back(getCurrentTimeMillis());
    for (size_t i = 0; i < payload.size(); i++) {
        message.push_back(payload[i]);
    }
    message.push_back(part_sequence);

    int rc = zmq_send_multipart(psocket, message);
    if (rc == -1)
        return false;

    // increment memory only sequence number after sending
    nSequence++;

    return true;
}

bool CZMQPublishHashBlockNotifier::NotifyBlock(const CBlockIndex *pindex)
{
    uint256 hash = pindex->GetBlockHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish hashblock %s to %s\n", hash.GetHex(), this->address);
    uint8_t data[32];
    for (unsigned int i = 0; i < 32; i++) {
        data[31 - i] = hash.begin()[i];
    }
    return SendZmqMessage(MSG_HASHBLOCK, data, 32);
}

bool CZMQPublishHashTransactionNotifier::NotifyTransaction(const CTransaction &transaction)
{
    uint256 hash = transaction.GetHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish hashtx %s to %s\n", hash.GetHex(), this->address);
    uint8_t data[32];
    for (unsigned int i = 0; i < 32; i++) {
        data[31 - i] = hash.begin()[i];
    }
    return SendZmqMessage(MSG_HASHTX, data, 32);
}

bool CZMQPublishRawBlockNotifier::NotifyBlock(const CBlockIndex *pindex)
{
    LogPrint(BCLog::ZMQ, "zmq: Publish rawblock %s to %s\n", pindex->GetBlockHash().GetHex(), this->address);

    const Consensus::Params& consensusParams = Params().GetConsensus();
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
    {
        LOCK(cs_main);
        CBlock block;
        if(!ReadBlockFromDisk(block, pindex, consensusParams))
        {
            zmqError("Can't read block from disk");
            return false;
        }

        ss << block;
    }

    return SendZmqMessage(MSG_RAWBLOCK, &(*ss.begin()), ss.size());
}

bool CZMQPublishRawTransactionNotifier::NotifyTransaction(const CTransaction &transaction)
{
    uint256 hash = transaction.GetHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish rawtx %s to %s\n", hash.GetHex(), this->address);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
    ss << transaction;
    return SendZmqMessage(MSG_RAWTX, &(*ss.begin()), ss.size());
}

// Helper function to send a 'sequence' topic message with the following structure:
//    <32-byte hash> | <1-byte label> | <8-byte LE sequence> (optional)
static bool SendSequenceMsg(CZMQAbstractPublishNotifier& notifier, uint256 hash, char label, std::optional<uint64_t> sequence = {})
{
    unsigned char data[sizeof(hash) + sizeof(label) + sizeof(uint64_t)];
    for (unsigned int i = 0; i < sizeof(hash); ++i) {
        data[sizeof(hash) - 1 - i] = hash.begin()[i];
    }
    data[sizeof(hash)] = label;
    if (sequence) WriteLE64(data + sizeof(hash) + sizeof(label), *sequence);
    return notifier.SendZmqMessage(MSG_SEQUENCE, data, sequence ? sizeof(data) : sizeof(hash) + sizeof(label));
}

bool CZMQPublishSequenceNotifier::NotifyBlockConnect(const CBlockIndex *pindex)
{
    uint256 hash = pindex->GetBlockHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish sequence block connect %s to %s\n", hash.GetHex(), this->address);
    return SendSequenceMsg(*this, hash, /* Block (C)onnect */ 'C');
}

bool CZMQPublishSequenceNotifier::NotifyBlockDisconnect(const CBlockIndex *pindex)
{
    uint256 hash = pindex->GetBlockHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish sequence block disconnect %s to %s\n", hash.GetHex(), this->address);
    return SendSequenceMsg(*this, hash, /* Block (D)isconnect */ 'D');
}

bool CZMQPublishSequenceNotifier::NotifyTransactionAcceptance(const CTransaction &transaction, uint64_t mempool_sequence)
{
    uint256 hash = transaction.GetHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish hashtx mempool acceptance %s to %s\n", hash.GetHex(), this->address);
    return SendSequenceMsg(*this, hash, /* Mempool (A)cceptance */ 'A', mempool_sequence);
}

bool CZMQPublishSequenceNotifier::NotifyTransactionRemoval(const CTransaction &transaction, uint64_t mempool_sequence)
{
    uint256 hash = transaction.GetHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish hashtx mempool removal %s to %s\n", hash.GetHex(), this->address);
    return SendSequenceMsg(*this, hash, /* Mempool (R)emoval */ 'R', mempool_sequence);
}

bool CZMQPublishMempolAddedNotifier::NotifyTransactionFee(const CTransaction &transaction, const CAmount fee)
{
    uint256 txid = transaction.GetHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish mempooladded %s\n", txid.GetHex());

    std::vector<zmq_message_part> payload = {};
    payload.push_back(hashToZMQMessagePart(txid));
    payload.push_back(transactionToZMQMessagePart(transaction));
    payload.push_back(int64ToZMQMessagePart(fee));

    return SendZmqMessage(MSG_MEMPOOLADDED, payload);
}

bool CZMQPublishMempoolRemovedNotifier::NotifyTransactionRemovalReason(const CTransaction &transaction, const MemPoolRemovalReason reason)
{
    uint256 txid = transaction.GetHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish mempoolremoved %s\n", txid.GetHex());

    std::vector<zmq_message_part> payload = {};
    payload.push_back(hashToZMQMessagePart(txid));
    payload.push_back(transactionToZMQMessagePart(transaction));
    payload.push_back(int32ToZMQMessagePart(static_cast<int32_t>(reason)));

    return SendZmqMessage(MSG_MEMPOOLREMOVED, payload);
}

bool CZMQPublishMempoolReplacedNotifier::NotifyTransactionReplaced(const CTransaction &tx_replaced, const CAmount fee_replaced, const CTransaction &tx_replacement, const CAmount fee_replacement)
{
    uint256 hash_replaced = tx_replaced.GetHash();
    uint256 hash_replacement = tx_replacement.GetHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish mempoolreplaced %s by %s\n", hash_replaced.GetHex(), hash_replacement.GetHex());

    std::vector<zmq_message_part> payload = {};
    payload.push_back(hashToZMQMessagePart(hash_replaced));
    payload.push_back(transactionToZMQMessagePart(tx_replaced));
    payload.push_back(int64ToZMQMessagePart(fee_replaced));
    payload.push_back(hashToZMQMessagePart(hash_replacement));
    payload.push_back(transactionToZMQMessagePart(tx_replacement));
    payload.push_back(int64ToZMQMessagePart(fee_replacement));

    return SendZmqMessage(MSG_MEMPOOLREPLACED, payload);
}

