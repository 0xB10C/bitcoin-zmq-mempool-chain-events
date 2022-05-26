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

#### Mempool-remove event with removal reason

A new ZMQ publisher with the topic `mempoolremoved` is added. The command line
option `-zmqpubmempoolremoved=<address>` sets the address for the publisher and
`-zmqpubmempoolremovedhwm=<n>` sets a custom outbound message high water mark.
The publisher notifies when a transaction is removed from the mempool and passes
the txid, the raw transaction, and the removal reason.

| Value | Name | Description |
|:-----:|:----------:|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 0 | Expiry | Transactions in the Mempool can expire. The default expiry timeout is 336 hours (2 weeks). |
| 1 | Size limit | As the internal data structure storing the Mempool gets close to `maxmempool` (default 300MB) low feerate transactions are evicted. |
| 2 | Reorg | Transactions that become invalid after a reorg are evicted. Note: Often transactions are still valid after a reorg. A transaction that might become valid is, for example, a transaction that spends a now not-mature coinbase output. |
| 3 | Block | Transactions included in a block of the most-work chain are removed from the Mempool. |
| 4 | Conflict | Transactions conflicting with an in-block transaction are removed. |
| 5 | Replaced | Transactions that are replaced are removed from the Mempool.  |                                                                                                                                                                      |

The functional tests for this ZMQ publisher can be run with `python3
test/functional/test_runner.py test/functional/interface_zmq_mempoolremove*`.
Make sure `bitcoind` is compiled with a wallet otherwise the tests are skipped.

```
ZMQ multipart message structure
| topic | timestamp | txid | rawtx | removal reason | sequence |
```

- `topic` equals `mempoolremoved`
- `timestamp` are the milliseconds since 01/01/1970 as int64 in Little Endian
- `txid` is the transaction id
- `rawtx` is a serialized Bitcoin transaction
- `removal reason` is an `int` in Little Endian
- `sequence` is an `uint32` in Little Endian

#### Mempool-replaced event with both transactions

A new ZMQ publisher with the topic `mempoolreplaced` is added. The command-line
option `-zmqpubmempoolreplaced=<address>` sets the address for the publisher and
`-zmqpubmempoolreplacedhwm=<n>` sets a custom outbound message high water mark.
The publisher notifies when a transaction in the mempool is replaced. This
includes both the transaction id and raw transaction of the replaced and the
replacement transaction as well as their fees.

The functional tests for this ZMQ publisher can be run with `python3
test/functional/test_runner.py interface_zmq_mempoolreplace.py`.

```
ZMQ multipart message structure
| topic | timestamp | txid replaced | rawtx replaced | fee replaced | txid replacement | rawtx replacement | fee replacement | sequence |
```

- `topic` equals `mempoolreplaced`
- `timestamp` are the milliseconds since 01/01/1970 as int64 in Little Endian
- `txid replaced` is the txid of the replaced transaction
- `rawtx replaced` is the serialized Bitcoin transaction that is replaced
- `fee replaced` is the fee of the replaced transaction as a `int64_t` in Little Endian
- `txid replacement` is the txid of the replacement transaction
- `rawtx replacement` is the serialized Bitcoin transaction that is the replacement
- `fee replacement` is the fee of the replacement transaction as a `int64_t` in Little Endian
- `sequence` is an `uint32` in Little Endian

#### Mempool-confirmed event with block header and height

A new ZMQ publisher with the topic `mempoolconfirmed` is added. The command line
option `-zmqpubmempoolconfirmed=<address>` sets the address for the publisher
and `-zmqpubmempoolconfirmedhwm=<n>` sets a custom outbound message high water
mark. The publisher notifies when a transaction is included in a block and
passes the txid, the raw transaction, the block height and the block hash.

The functional tests for this ZMQ publisher can be run with `python3
test/functional/test_runner.py
test/functional/interface_zmq_mempoolconfirmed.py`. Make sure bitcoind is
compiled with a wallet otherwise the tests are skipped.

```
ZMQ multipart message structure
| topic | timestamp | txid | rawtx | block height | block hash | header | sequence |
```

- `topic` equals `mempoolconfirmed`
- `timestamp` are the milliseconds since 01/01/1970 as int64 in Little Endian
- `txid` is the transaction id
- `rawtx` is a serialized Bitcoin transaction
- `block height` is the block height as `int32` in Little Endian
- `block hash` is the block hash
- `header` is the 80-byte serialized block header
- `sequence` is a `uint32` in Little Endian

