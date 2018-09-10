/**
 * @file: transactionObject.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

const ethUtil = require('./utils.js')
const BN = ethUtil.BN
var config=require('./config.js')

// secp256k1n/2
const N_DIV_2 = new BN('7fffffffffffffffffffffffffffffff5d576e7357a4501ddfe92f46681b20a0', 16)
function Transaction(data) {
  data = data || {};
  var fields;
  if(config.EncryptType == 1){
    // Define Properties
    fields = [{
      name: 'randomid',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 'gasPrice',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 'gasLimit',
      alias: 'gas',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 'blockLimit',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    },{
      name: 'to',
      allowZero: true,
      length: 20,
      default: new Buffer([])
    }, {
      name: 'value',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 'data',
      alias: 'input',
      allowZero: true,
      default: new Buffer([])
    }, {
      name: 'pub',
      length: 64,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 'r',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 's',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }]} else{
    // Define Properties
    fields = [{
      name: 'randomid',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 'gasPrice',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 'gasLimit',
      alias: 'gas',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 'blockLimit',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    },{
      name: 'to',
      allowZero: true,
      length: 20,
      default: new Buffer([])
    }, {
      name: 'value',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 'data',
      alias: 'input',
      allowZero: true,
      default: new Buffer([])
    }, {
      name: 'v',
      length: 1,
      default: new Buffer([0x1c])
    }, {
      name: 'r',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }, {
      name: 's',
      length: 32,
      allowLess: true,
      default: new Buffer([])
    }]}

    /**
     * Returns the rlp encoding of the transaction
     * @method serialize
     * @return {Buffer}
     */
    // attached serialize
    ethUtil.defineProperties(this, fields, data)

    /**
     * @prop {Buffer} from (read only) sender address of this transaction, mathematically derived from other parameters.
     */
    Object.defineProperty(this, 'from', {
      enumerable: true,
      configurable: true,
      get: this.getSenderAddress.bind(this)
    })

    // calculate chainId from signature
    var sigV = ethUtil.bufferToInt(this.v)
    var chainId = Math.floor((sigV - 35) / 2)
    if (chainId < 0) chainId = 0

    // set chainId
    this._chainId = chainId || data.chainId || 0
    this._homestead = true
  }

  /**
   * If the tx's `to` is to the creation address
   * @return {Boolean}
   */
  Transaction.prototype.toCreationAddress=function () {
    return this.to.toString('hex') === ''
  }

  /**
   * Computes a sha3-256 hash of the serialized tx
   * @param {Boolean} [includeSignature=true] whether or not to inculde the signature
   * @return {Buffer}
   */
  Transaction.prototype.hash=function (includeSignature) {
    if (includeSignature === undefined) includeSignature = true
    // backup original signature
    const rawCopy = this.raw.slice(0)

    // modify raw for signature generation only
    if (this._chainId > 0) {
      includeSignature = true
      this.v = this._chainId
      this.r = 0
      this.s = 0
    }

    // generate rlp params for hash
	console.log(this.raw.length)
    var txRawForHash = includeSignature ? this.raw : this.raw.slice(0, this.raw.length - 3)
    //var txRawForHash = includeSignature ? this.raw : this.raw.slice(0, 7)

    // restore original signature
    this.raw = rawCopy.slice()

    // create hash
    return ethUtil.rlphash(txRawForHash)
  }

  /**
   * returns the public key of the sender
   * @return {Buffer}
   */
  Transaction.prototype.getChainId=function() {
    return this._chainId
  }

  /**
   * returns the sender's address
   * @return {Buffer}
   */
  Transaction.prototype.getSenderAddress = function() {
    if (this._from) {
      return this._from
    }
    const pubkey = this.getSenderPublicKey()
    this._from = ethUtil.publicToAddress(pubkey)
    return this._from
  }

  /**
   * returns the public key of the sender
   * @return {Buffer}
   */
  Transaction.prototype.getSenderPublicKey =function() {
    if (!this._senderPubKey || !this._senderPubKey.length) {
      if (!this.verifySignature()) throw new Error('Invalid Signature')
    }
    return this._senderPubKey
  }

  /**
   * Determines if the signature is valid
   * @return {Boolean}
   */
  Transaction.prototype.verifySignature =function() {
    const msgHash = this.hash(false)
    // All transaction signatures whose s-value is greater than secp256k1n/2 are considered invalid.
    if (this._homestead && new BN(this.s).cmp(N_DIV_2) === 1) {
      return false
    }

    try {
      var v = ethUtil.bufferToInt(this.v)
      if (this._chainId > 0) {
        v -= this._chainId * 2 + 8
      }
      this._senderPubKey = ethUtil.ecrecover(msgHash, v, this.r, this.s)
    } catch (e) {
      return false
    }

    return !!this._senderPubKey
  }

  /**
   * sign a transaction with a given a private key
   * @param {Buffer} privateKey
   */
  Transaction.prototype.sign =function(privateKey) {
    const msgHash = this.hash(false)
    const sig = ethUtil.ecsign(msgHash, privateKey)
    if (this._chainId > 0) {
      sig.v += this._chainId * 2 + 8
    }
    Object.assign(this, sig)
  }

  /**
   * The amount of gas paid for the data in this tx
   * @return {BN}
   */
  Transaction.prototype.getDataFee=function() {
    const data = this.raw[5]
    const cost = new BN(0)
    for (var i = 0; i < data.length; i++) {
      data[i] === 0 ? cost.iaddn(fees.txDataZeroGas.v) : cost.iaddn(fees.txDataNonZeroGas.v)
    }
    return cost
  }

  /**
   * the minimum amount of gas the tx must have (DataFee + TxFee + Creation Fee)
   * @return {BN}
   */
  Transaction.prototype.getBaseFee =function() {
    const fee = this.getDataFee().iaddn(fees.txGas.v)
    if (this._homestead && this.toCreationAddress()) {
      fee.iaddn(fees.txCreation.v)
    }
    return fee
  }

  /**
   * the up front amount that an account must have for this transaction to be valid
   * @return {BN}
   */
  Transaction.prototype.getUpfrontCost =function() {
    return new BN(this.gasLimit)
      .imul(new BN(this.gasPrice))
      .iadd(new BN(this.value))
  }

  /**
   * validates the signature and checks to see if it has enough gas
   * @param {Boolean} [stringError=false] whether to return a string with a dscription of why the validation failed or return a Bloolean
   * @return {Boolean|String}
   */
  Transaction.prototype.validate =function(stringError) {
    const errors = []
    if (!this.verifySignature()) {
      errors.push('Invalid Signature')
    }

    if (this.getBaseFee().cmp(new BN(this.gasLimit)) > 0) {
      errors.push([`gas limit is to low. Need at least ${this.getBaseFee()}`])
    }

    if (stringError === undefined || stringError === false) {
      return errors.length === 0
    } else {
      return errors.join(' ')
    }
  }

exports.Transaction=Transaction;
