#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the ZMQ publisher chainheaderadded to notify about headers being
added to the chain (headertree)"""
import struct
import zmq

from test_framework.address import ADDRESS_BCRT1_UNSPENDABLE, ADDRESS_BCRT1_P2WSH_OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import hash256
from test_framework.util import assert_equal
from time import sleep
from random import randint

from test_framework.util_patched_zmq import ZMQSubscriber

TOPIC = b'chainheaderadded'


def hash256_reversed(byte_str):
    return hash256(byte_str)[::-1]


class ZMQTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2

    def skip_test_if_missing_module(self):
        self.skip_if_no_py3_zmq()
        self.skip_if_no_bitcoind_zmq()

    def run_test(self):
        import zmq
        self.ctx = zmq.Context()
        try:
            self.test_basic()
            self.test_reorg()
        finally:
            # Destroy the ZMQ context.
            self.log.debug("Destroying ZMQ context")
            self.ctx.destroy(linger=None)

    def test_basic(self):
        address = 'tcp://127.0.0.1:{}'.format(randint(20000, 50000))
        socket = self.ctx.socket(zmq.SUB)
        socket.set(zmq.RCVTIMEO, 2000)
        subscriber = ZMQSubscriber(socket, TOPIC)

        self.log.info("Test patched chainheaderadded topic")

        # chainheaderadded should notify for every block header added
        self.restart_node(0, ['-zmqpub%s=%s' %
                          (subscriber.topic.decode(), address)])
        socket.connect(address)
        # Relax so that the subscriber is ready before publishing zmq messages
        sleep(0.2)
        self.connect_nodes(0, 1)

        lastHeight = self.nodes[0].getblockcount()

        self.log.info("Generate 3 blocks in node 0 and receive notifications")
        genhashes_node0 = self.generatetoaddress(
            self.nodes[0], 3, ADDRESS_BCRT1_UNSPENDABLE)
        for block_hash in genhashes_node0:
            hash, height, header = subscriber.receive_multi_payload()
            assert_equal(lastHeight+1, struct.unpack("<I", height)[0])
            assert_equal(block_hash, hash.hex())
            assert_equal(self.nodes[0].getblockheader(
                hash.hex(), False), header.hex())
            lastHeight += 1

        # allow both nodes to sync
        sleep(1)
        self.disconnect_nodes(0, 1)

    def test_reorg(self):
        address = 'tcp://127.0.0.1:{}'.format(randint(20000, 50000))
        socket = self.ctx.socket(zmq.SUB)
        socket.set(zmq.RCVTIMEO, 2000)
        subscriber = ZMQSubscriber(socket, TOPIC)

        self.log.info("Reorg testing ZMQ publisher chainheaderadded")

        # chainheaderadded should notify for every block header added
        self.restart_node(0, ['-zmqpub%s=%s' %
                          (subscriber.topic.decode(), address)])
        socket.connect(address)
        # Relax so that the subscriber is ready before publishing zmq messages
        sleep(0.2)

        self.log.info("Disconnect nodes 0 and 1")
        self.disconnect_nodes(0, 1)

        preForkHeight = self.nodes[0].getblockcount()

        self.log.info("Generate 6 blocks in node 0")
        genhashes_node0 = self.generatetoaddress(
            self.nodes[0], 6, ADDRESS_BCRT1_UNSPENDABLE, sync_fun=self.no_op)
        for _ in genhashes_node0:
            _ = subscriber.receive_multi_payload()

        self.log.info("Generate 3 different blocks in node 1")
        genhashes_node1 = self.generatetoaddress(self.nodes[1],
                                                 3, ADDRESS_BCRT1_P2WSH_OP_TRUE, sync_fun=self.no_op)

        self.log.info("Connect node 0 and node 1")
        self.connect_nodes(0, 1)

        self.log.info(
            "Receive notifications about headers in node 0 even if it has a longer chain")
        for block_hash in genhashes_node1:
            hash, height, header = subscriber.receive_multi_payload()
            assert_equal(preForkHeight+1, struct.unpack("<I", height)[0])
            assert_equal(block_hash, hash.hex())
            assert_equal(self.nodes[0].getblockheader(
                hash.hex(), False), header.hex())
            preForkHeight += 1


if __name__ == '__main__':
    ZMQTest().main()
