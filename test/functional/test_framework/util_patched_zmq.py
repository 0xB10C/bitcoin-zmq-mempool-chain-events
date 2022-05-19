#!/usr/bin/env python3
"""Utility functionality for the patched ZMQ interface."""

import struct
import time
from io import BytesIO

import zmq

from test_framework.util import assert_equal


class ZMQSubscriber:
    def __init__(self, socket, topic):
        self.sequence = 0
        self.socket = socket
        self.topic = topic
        self.socket.setsockopt(zmq.SUBSCRIBE, self.topic)

    def receive_multi_payload(self):
        """receives a multipart zmq message with zero, one or multiple payloads
        and checks the topic and sequence number"""
        msg = self.socket.recv_multipart()

        # Message should consist of at least three parts
        # (topic, timestamp and sequence)
        assert(len(msg) >= 3)
        topic = msg[0]
        timestamp = msg[1]
        sequence = msg[-1]

        # Topic should match the subscriber topic.
        assert_equal(topic, self.topic)

        # Timestamp should be roughly in the range of the current timestamp.
        timestamp = struct.unpack('<q', timestamp)[-1]
        timestamp = timestamp / 1000  # convert to seconds
        diff_seconds = time.time() - timestamp
        assert diff_seconds < 5  # seconds
        assert diff_seconds > -5  # seconds

        # Sequence should be incremental.
        assert_equal(struct.unpack('<I', sequence)[-1], self.sequence)
        self.sequence += 1
        return msg[2:-1]
