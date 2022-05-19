# Patch: Mempool and Chain event publishers for ZMQ

This is a patch-set to Bitcoin Core that adds more functionality to the ZMQ interface.
While the patches does not touch consensus or wallet code, it's not recommended to use
this node in a consensus critical environment where user funds are at risk.

## Changes:

### add: multi-payload ZMQ multipart messages

A new internal function overwrite for `zmq_send_multipart()` is added.
This allows us to send ZMQ multipart messages with a variable amount (zero to many) of payload parts.

```
ZMQ multipart message structure
Before: | topic | payload | sequence |
After:  | topic | timestamp | payload_0 | payload_1 | ... | payload_n | sequence |
```

### change: increase default ZMQ high water mark

The previous default of 1_000 did (correctly) drop messages when publishing
many ZMQ messages at the same time. The default high water mark is increased to
100_000.

### add: mempooladded ZMQ publisher

A new ZMQ publisher with the topic `mempooladded` is added. The command line
option `-zmqpubmempooladded=<address>` sets the address for the publisher and
`-zmqpubmempooladdedhwm=<n>` sets a custom outbound message high water mark. The
publisher notifies when a transaction is added to the mempool after the mempool
is loaded and passes the txid, the raw transaction and the fee paid.

The functional tests for this ZMQ publisher can be run with `python3
test/functional/test_runner.py test/functional/interface_zmq_mempooladd.py`.
Make sure `bitcoind` is compiled with a wallet otherwise the tests are skipped.

```
ZMQ multipart message structure
| topic | timestamp | txid | rawtx | fee | sequence |
```

- `topic` equals `mempooladded`
- `timestamp` are the milliseconds since 01/01/1970 as int64 in Little Endian
- `txid` is the transaction id
- `rawtx` is a serialized Bitcoin transaction
- `fee` is a `int64` in Little Endian
- `sequence` is a `uint32` in Little Endian

