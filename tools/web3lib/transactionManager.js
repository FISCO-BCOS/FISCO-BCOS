/**
 * @file: transactionManager.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var Tx = require('./transactionObject.js');

function signTransaction(tx_data,privKey,callback)
{
    // convert string private key to a Buffer Object
    var privateKey = new Buffer(privKey, 'hex');
    var tx = new Tx.Transaction(tx_data);
    tx.sign(privateKey);
    // Build a serialized hex version of the Tx
    var serializedTx = '0x' + tx.serialize().toString('hex');
    if( null !== callback)
    {
    	callback(serializedTx);
    	return ;
    }
    else
    {
    	return serializedTx;
    }
}

exports.signTransaction=signTransaction;