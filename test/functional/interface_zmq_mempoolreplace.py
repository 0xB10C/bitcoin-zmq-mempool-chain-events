#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the ZMQ publisher mempoolreplaced to notify us on a transaction that
was replaced by another."""


import struct
from time import sleep
from random import randint

from test_framework.messages import COIN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.util_patched_zmq import ZMQSubscriber


class ZMQTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-acceptnonstdtxn=1"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_py3_zmq()
        self.skip_if_no_bitcoind_zmq()
        self.skip_if_no_wallet()

    def run_test(self):
        import zmq
        self.ctx = zmq.Context()
        try:
            self.test_mempool_replaced()
        finally:
            # Destroy the ZMQ context.
            self.log.debug("Destroying ZMQ context")
            self.ctx.destroy(linger=None)

    def test_mempool_replaced(self):
        import zmq
        address = 'tcp://127.0.0.1:{}'.format(randint(20000, 50000))
        socket = self.ctx.socket(zmq.SUB)
        socket.set(zmq.RCVTIMEO, 60000)
        topic = b"mempoolreplaced"

        node = self.nodes[0]

        self.log.info("Testing ZMQ publisher mempoolreplaced")
        subscriber = ZMQSubscriber(socket, topic)
        self.restart_node(0, ["-zmqpub%s=%s" % (topic.decode(), address)])
        # Relax so that the subscriber is ready before publishing zmq messages
        sleep(0.2)
        socket.connect(address)

        self.log.info("Sending transaction that will be replaced")
        txid_replaced = node.sendtoaddress(
            node.getnewaddress(), 1.0, "", "", False, True)
        fee_replaced = node.getmempoolentry(
            txid_replaced)["fees"]["base"] * COIN
        tx_replaced = node.getrawtransaction(txid_replaced)

        self.log.info("feebump the transaction")
        replacement = node.bumpfee(txid_replaced)
        tx_replacement = node.getrawtransaction(replacement["txid"])

        self.log.info("Testing that the replacement is received")
        r_replaced_txid, r_replaced_rawtx, r_replaced_tx_fee, r_replacement_txid, r_replacement_rawtx, r_replacement_tx_fee = subscriber.receive_multi_payload()
        assert_equal(txid_replaced, r_replaced_txid.hex())
        assert_equal(tx_replaced, r_replaced_rawtx.hex())
        assert_equal(fee_replaced, int(
            struct.unpack("<q", r_replaced_tx_fee)[0]))
        assert_equal(replacement["txid"], r_replacement_txid.hex())
        assert_equal(tx_replacement, r_replacement_rawtx.hex())
        assert_equal(replacement["fee"] * COIN,
                     int(struct.unpack("<q", r_replacement_tx_fee)[0]))


if __name__ == '__main__':
    ZMQTest().main()
