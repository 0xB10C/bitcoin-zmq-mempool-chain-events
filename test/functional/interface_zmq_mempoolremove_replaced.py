#!/usr/bin/env python3
# Copyright (c) 2015-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the ZMQ publisher mempoolremoved to notify about transactions that
were replaced."""


from time import sleep
from random import randint

from test_framework.test_framework import BitcoinTestFramework
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
            self.test_mempool_removed()
        finally:
            # Destroy the ZMQ context.
            self.log.debug("Destroying ZMQ context")
            self.ctx.destroy(linger=None)

    def test_mempool_removed(self):
        import zmq
        address = 'tcp://127.0.0.1:{}'.format(randint(20000, 50000))
        socket = self.ctx.socket(zmq.SUB)
        socket.set(zmq.RCVTIMEO, 2000)
        topic = b"mempoolremoved"
        node = self.nodes[0]

        self.log.info(
            "Testing ZMQ publisher mempoolremoved with reason REPLACED")
        subscriber = ZMQSubscriber(socket, topic)
        self.restart_node(0, ["-zmqpub%s=%s" % (topic.decode(), address)])
        # Relax so that the subscriber is ready before publishing zmq messages
        sleep(0.2)
        socket.connect(address)

        self.log.info("Sending transaction that will be replaced")
        txid_replaced = node.sendtoaddress(
            node.getnewaddress(), 1.0, "", "", False, True)

        self.log.info("feebump the transaction")
        node.bumpfee(txid_replaced)

        self.log.info("We should be notified about the bumped transaction")
        expected = {txid_replaced: 'REPLACED'}
        subscriber.check_mempoolremoved_messages(expected)


if __name__ == '__main__':
    ZMQTest().main()
