#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the ZMQ publisher chaintipchanged to notify about a changed
chain tip"""
import struct
import zmq

from test_framework.address import ADDRESS_BCRT1_UNSPENDABLE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import hash256
from test_framework.util import assert_equal
from time import sleep
from random import randint

from test_framework.util_patched_zmq import ZMQSubscriber


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
        socket.set(zmq.RCVTIMEO, 60000)
        subscriber = ZMQSubscriber(socket, b'chaintipchanged')

        self.log.info("Test patched chaintipchanged topic")

        self.restart_node(0, ['-zmqpub%s=%s' %
                          (subscriber.topic.decode(), address)])
        socket.connect(address)
        self.connect_nodes(0, 1)
        # Relax so that the subscriber is ready before publishing zmq messages
        sleep(0.2)

        lastHeight = self.nodes[0].getblockcount()

        self.log.info(
            "Generate 3 block in nodes[0] and receive all notifications")
        genhashes_node0 = self.generatetoaddress(
            self.nodes[0], 3, ADDRESS_BCRT1_UNSPENDABLE)
        for block_hash in genhashes_node0:
            hash, height, header = subscriber.receive_multi_payload()
            assert_equal(struct.unpack("<I", height)[0], lastHeight+1)
            assert_equal(block_hash, hash.hex())
            assert_equal(self.nodes[0].getblockheader(
                hash.hex(), False), header.hex())
            # update last height and hash
            lastHeight += 1

    def test_reorg(self):
        address = 'tcp://127.0.0.1:{}'.format(randint(20000, 50000))
        socket = self.ctx.socket(zmq.SUB)
        socket.set(zmq.RCVTIMEO, 1000)
        subscriber = ZMQSubscriber(socket, b'chaintipchanged')

        self.log.info("Reorg testing ZMQ publisher chaintipchanged")
        self.restart_node(0, ['-zmqpub%s=%s' %
                          (subscriber.topic.decode(), address)])
        socket.connect(address)
        # Relax so that the subscriber is ready before publishing zmq messages
        sleep(0.2)

        self.disconnect_nodes(0, 1)

        self.log.info("Generate ten blocks in node 0")
        genhashes_node0 = self.generatetoaddress(self.nodes[0],
                                                 10, ADDRESS_BCRT1_UNSPENDABLE, sync_fun=self.no_op)
        for _ in genhashes_node0:
            subscriber.receive_multi_payload()

        self.log.info("Generate six blocks in node 1")
        self.generatetoaddress(
            self.nodes[1], 6, ADDRESS_BCRT1_UNSPENDABLE, sync_fun=self.no_op)

        self.log.info("Connect both nodes")
        self.connect_nodes(0, 1)

        self.log.info(
            "Node 0 should not change the tip for a block generated by node 1 as node 0 has a longer chain")
        try:
            subscriber.receive_multi_payload()
        except zmq.error.Again as e:
            self.log.info("ZMQ subscriber timed out as expected: {}".format(e))


if __name__ == '__main__':
    ZMQTest().main()
