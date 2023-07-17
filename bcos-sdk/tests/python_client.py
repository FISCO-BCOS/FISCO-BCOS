#!/usr/bin/env python3

import bcos

cryptoSuite = bcos.newCryptoSuite(False)
keyPair = cryptoSuite.generateKeyPair()

transactionFactory = bcos.TransactionFactoryImpl(cryptoSuite)
transaction = transactionFactory.createTransaction()

rpcClient = bcos.RPCClient(
    "fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 20021 -t 60000")
receipt = rpcClient.sendTransaction(transaction)
