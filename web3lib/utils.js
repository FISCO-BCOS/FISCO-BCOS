/**
 * @file: utils.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

const secp256k1 = require('secp256k1')
const createKeccakHash = require('keccak')
const assert = require('assert')
const rlp = require('rlp')
const BN = require('bn.js')

function privateToPublic(privateKey) {
  privateKey = toBuffer(privateKey)
  // skip the type flag and use the X, Y points
  return secp256k1.publicKeyCreate(privateKey, false).slice(1)
}

function privateToAddress(privateKey) {
  return publicToAddress(privateToPublic(privateKey))
}

function publicToAddress(pubKey, sanitize) {
  pubKey = toBuffer(pubKey)
  if (sanitize && (pubKey.length !== 64)) {
    pubKey = secp256k1.publicKeyConvert(pubKey, false).slice(1)
  }
  assert(pubKey.length === 64)
  // Only take the lower 160bits of the hash
  return sha3(pubKey).slice(-20)
}


function toBuffer(v) {
  if (!Buffer.isBuffer(v)) {
    if (Array.isArray(v)) {
      v = Buffer.from(v)
    } else if (typeof v === 'string') {
      if (isHexPrefixed(v)) {
        v = Buffer.from(padToEven(stripHexPrefix(v)), 'hex')
      } else {
        v = Buffer.from(v)
      }
    } else if (typeof v === 'number') {
      v = intToBuffer(v)
    } else if (v === null || v === undefined) {
      v = Buffer.allocUnsafe(0)
    } else if (v.toArray) {
      // converts a BN to a Buffer
      v = Buffer.from(v.toArray())
    } else {
      throw new Error('invalid type')
    }
  }
  return v
}

function isHexPrefixed(str) {
  return str.slice(0, 2) === '0x'
}

function padToEven(a) {
  if (a.length % 2) a = '0' + a
  return a
}

function stripHexPrefix(str) {
  if (typeof str !== 'string') {
    return str
  }
  return isHexPrefixed(str) ? str.slice(2) : str
}

function intToBuffer(i) {
  var hex = intToHex(i)
  return Buffer.from(hex.slice(2), 'hex')
}

function intToHex(i) {
  assert(i % 1 === 0, 'number is not a integer')
  assert(i >= 0, 'number must be positive')
  var hex = i.toString(16)
  if (hex.length % 2) {
    hex = '0' + hex
  }
  return '0x' + hex
}

function setLength(msg, length, right) {
  var buf = zeros(length)
  msg = toBuffer(msg)
  if (right) {
    if (msg.length < length) {
      msg.copy(buf)
      return buf
    }
    return msg.slice(0, length)
  } else {
    if (msg.length < length) {
      msg.copy(buf, length - msg.length)
      return buf
    }
    return msg.slice(-length)
  }
}

function sha3(a, bits) {
  a = toBuffer(a)
  if (!bits) bits = 256
  return createKeccakHash('keccak' + bits).update(a).digest()
}

function baToJSON(ba) {
  if (Buffer.isBuffer(ba)) {
    return '0x' + ba.toString('hex')
  } else if (ba instanceof Array) {
    var array = []
    for (var i = 0; i < ba.length; i++) {
      array.push(baToJSON(ba[i]))
    }
    return array
  }
}

function zeros(bytes) {
  return Buffer.allocUnsafe(bytes).fill(0)
}

function stripZeros(a) {
  a = stripHexPrefix(a)
  var first = a[0]
  while (a.length > 0 && first.toString() === '0') {
    a = a.slice(1)
    first = a[0]
  }
  return a
}

function defineProperties(self, fields, data) {
  self.raw = []
  self._fields = []

  // attach the `toJSON`
  self.toJSON = function (label) {
    if (label) {
      var obj = {}
      self._fields.forEach(function (field) {
        obj[field] = '0x' + self[field].toString('hex')
      })
      return obj
    }
    return baToJSON(this.raw)
  }

  self.serialize = function serialize () {
    return rlp.encode(self.raw)
  }

  fields.forEach(function (field, i) {
    self._fields.push(field.name)
    function getter () {
      return self.raw[i]
    }
    function setter (v) {
      v = toBuffer(v)

      if (v.toString('hex') === '00' && !field.allowZero) {
        v = Buffer.allocUnsafe(0)
      }

      if (field.allowLess && field.length) {
        v = stripZeros(v)
        assert(field.length >= v.length, 'The field ' + field.name + ' must not have more ' + field.length + ' bytes')
      } else if (!(field.allowZero && v.length === 0) && field.length) {
        assert(field.length === v.length, 'The field ' + field.name + ' must have byte length of ' + field.length)
      }

      self.raw[i] = v
    }

    Object.defineProperty(self, field.name, {
      enumerable: true,
      configurable: true,
      get: getter,
      set: setter
    })

    if (field.default) {
      self[field.name] = field.default
    }

    // attach alias
    if (field.alias) {
      Object.defineProperty(self, field.alias, {
        enumerable: false,
        configurable: true,
        set: setter,
        get: getter
      })
    }
  })

  // if the constuctor is passed data
  if (data) {
    if (typeof data === 'string') {
      data = Buffer.from(stripHexPrefix(data), 'hex')
    }

    if (Buffer.isBuffer(data)) {
      data = rlp.decode(data)
    }

    if (Array.isArray(data)) {
      if (data.length > self._fields.length) {
        throw (new Error('wrong number of fields in data'))
      }

      // make sure all the items are buffers
      data.forEach(function (d, i) {
        self[self._fields[i]] = toBuffer(d)
      })
    } else if (typeof data === 'object') {
      const keys = Object.keys(data)
      fields.forEach(function (field) {
        if (keys.indexOf(field.name) !== -1) self[field.name] = data[field.name]
        if (keys.indexOf(field.alias) !== -1) self[field.alias] = data[field.alias]
      })
    } else {
      throw new Error('invalid data')
    }
  }
}

function bufferToInt(buf) {
  return new BN(toBuffer(buf)).toNumber()
}

function rlphash(a) {
  return sha3(rlp.encode(a))
}

function ecrecover(msgHash, v, r, s) {
  var signature = Buffer.concat([setLength(r, 32), setLength(s, 32)], 64)
  var recovery = v - 27
  if (recovery !== 0 && recovery !== 1) {
    throw new Error('Invalid signature v value')
  }
  var senderPubKey = secp256k1.recover(msgHash, signature, recovery)
  return secp256k1.publicKeyConvert(senderPubKey, false).slice(1)
}

function ecsign(msgHash, privateKey) {
  var sig = secp256k1.sign(msgHash, privateKey)

  var ret = {}
  ret.r = sig.signature.slice(0, 32)
  ret.s = sig.signature.slice(32, 64)
  ret.v = sig.recovery + 27
  return ret
}

exports.privateToPublic=privateToPublic
exports.privateToAddress=privateToAddress
exports.publicToAddress=publicToAddress
exports.defineProperties=defineProperties
exports.bufferToInt=bufferToInt
exports.rlphash=rlphash
exports.ecrecover=ecrecover
exports.ecsign=ecsign

exports.BN = BN
exports.rlp = rlp
exports.secp256k1 = secp256k1