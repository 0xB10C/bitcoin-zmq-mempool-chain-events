// Copyright (c) 2015-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <zmq/zmqabstractnotifier.h>

#include <cassert>

const int CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM;

CZMQAbstractNotifier::~CZMQAbstractNotifier()
{
    assert(!psocket);
}

bool CZMQAbstractNotifier::NotifyBlock(const CBlockIndex * /*CBlockIndex*/)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyTransaction(const CTransaction &/*transaction*/)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyTransactionFee(const CTransaction &/*transaction*/, const CAmount fee)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyBlockConnect(const CBlockIndex * /*CBlockIndex*/)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyBlockDisconnect(const CBlockIndex * /*CBlockIndex*/)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyTransactionAcceptance(const CTransaction &/*transaction*/, uint64_t mempool_sequence)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyTransactionRemoval(const CTransaction &/*transaction*/, uint64_t mempool_sequence)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyTransactionRemovalReason(const CTransaction &/*transaction*/, const MemPoolRemovalReason reason)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyTransactionReplaced(const CTransaction &/* replaced tx */, const CAmount/*replaced fee*/, const CTransaction&/*replacement tx*/, const CAmount/*replacement fee*/)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyMempoolTransactionConfirmed(const CTransaction &/*transaction*/, const CBlockIndex *)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyChainTipChanged(const CBlockIndex *)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyChainBlockConnected(const CBlockIndex *)
{
    return true;
}

bool CZMQAbstractNotifier::NotifyChainHeaderAdded(const CBlockIndex *)
{
    return true;
}
