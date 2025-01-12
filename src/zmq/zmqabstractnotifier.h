// Copyright (c) 2015-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ZMQ_ZMQABSTRACTNOTIFIER_H
#define BITCOIN_ZMQ_ZMQABSTRACTNOTIFIER_H


#include <memory>
#include <string>

class CBlockIndex;
class CTransaction;
class CZMQAbstractNotifier;
typedef int64_t CAmount;
enum class MemPoolRemovalReason;

using CZMQNotifierFactory = std::unique_ptr<CZMQAbstractNotifier> (*)();

class CZMQAbstractNotifier
{
public:
    static const int DEFAULT_ZMQ_SNDHWM {100000};

    CZMQAbstractNotifier() : psocket(nullptr), outbound_message_high_water_mark(DEFAULT_ZMQ_SNDHWM) { }
    virtual ~CZMQAbstractNotifier();

    template <typename T>
    static std::unique_ptr<CZMQAbstractNotifier> Create()
    {
        return std::make_unique<T>();
    }

    std::string GetType() const { return type; }
    void SetType(const std::string &t) { type = t; }
    std::string GetAddress() const { return address; }
    void SetAddress(const std::string &a) { address = a; }
    int GetOutboundMessageHighWaterMark() const { return outbound_message_high_water_mark; }
    void SetOutboundMessageHighWaterMark(const int sndhwm) {
        if (sndhwm >= 0) {
            outbound_message_high_water_mark = sndhwm;
        }
    }

    virtual bool Initialize(void *pcontext) = 0;
    virtual void Shutdown() = 0;

    // Notifies of ConnectTip result, i.e., new active tip only
    virtual bool NotifyBlock(const CBlockIndex *pindex);
    // Notifies of every block connection
    virtual bool NotifyBlockConnect(const CBlockIndex *pindex);
    // Notifies of every block disconnection
    virtual bool NotifyBlockDisconnect(const CBlockIndex *pindex);
    // Notifies of every mempool acceptance
    virtual bool NotifyTransactionAcceptance(const CTransaction &transaction, uint64_t mempool_sequence);
    // Notifies of every mempool removal, except inclusion in blocks
    virtual bool NotifyTransactionRemoval(const CTransaction &transaction, uint64_t mempool_sequence);
    // Notifies of every mempool removal, including inclusion in blocks. Includes the removal reason.
    virtual bool NotifyTransactionRemovalReason(const CTransaction &transaction, const MemPoolRemovalReason reason);
    // Notifies of transactions added to mempool or appearing in blocks
    virtual bool NotifyTransaction(const CTransaction &transaction);
    // Notifies of transactions added to mempool (only!) with the transaction fee.
    virtual bool NotifyTransactionFee(const CTransaction &transaction, const CAmount fee);
    // Notifies of transactions replaced in the mempool.
    virtual bool NotifyTransactionReplaced(const CTransaction &tx_replaced, const CAmount fee_replaced, const CTransaction &tx_replacement, const CAmount fee_replacement);
    // Notifies of transactions confirmed with information about the block.
    virtual bool NotifyMempoolTransactionConfirmed(const CTransaction &transaction, const CBlockIndex *pindex);
    // Notifies of changed chain tips.
    virtual bool NotifyChainTipChanged(const CBlockIndex *pindex);
    // Notifies of a block connection to the chain.
    virtual bool NotifyChainBlockConnected(const CBlockIndex *pindex);
    // Notifies of a header connection to the chian.
    virtual bool NotifyChainHeaderAdded(const CBlockIndex *pindex);
protected:
    void *psocket;
    std::string type;
    std::string address;
    int outbound_message_high_water_mark; // aka SNDHWM
};

#endif // BITCOIN_ZMQ_ZMQABSTRACTNOTIFIER_H
