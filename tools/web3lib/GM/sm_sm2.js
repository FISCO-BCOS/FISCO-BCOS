/**
 * SM2 elliptic curve
 *
 * Support SM2 key pair generation and signature.
 */

var sm3 = require('./sm_sm3');
var utils = require('./sm_utils');
var elliptic = require('elliptic');
var BN = require('bn.js');
var DRBG = require('hmac-drbg');
var hash = require('hash.js');
var inherits = require('inherits');

var _drbg = new DRBG({
  hash: hash.sha256,
  entropy: 'UQi4W3Y2bJfzleYy+oEZ2kA9A+9jrmwewST9vmBZNgMmFyzzH0S9Vol/UK',
  nonce: '0123456789avcdef',
  pers: '0123456789abcdef'
});

/**
 * The SM2 elliptic curve
 */
function SM2Curve(params) {
  if (!(this instanceof SM2Curve)) {
    return new SM2Curve(params);
  }

  elliptic.curve.short.call(this, params);
}
inherits(SM2Curve, elliptic.curve.short);

var _sm2Params = {
  type: 'SM2',
  prime: null,
  p: 'FFFFFFFE FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF 00000000 FFFFFFFF FFFFFFFF',
  a: 'FFFFFFFE FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF 00000000 FFFFFFFF FFFFFFFC',
  b: '28E9FA9E 9D9F5E34 4D5A9E4B CF6509A7 F39789F5 15AB8F92 DDBCBD41 4D940E93',
  n: 'FFFFFFFE FFFFFFFF FFFFFFFF FFFFFFFF 7203DF6B 21C6052B 53BBF409 39D54123',
  hash: sm3,
  gRed: false,
  g: [
    '32C4AE2C 1F198119 5F990446 6A39C994 8FE30BBF F2660BE1 715A4589 334C74C7',
    'BC3736A2 F4F6779C 59BDCEE3 6B692153 D0A9877C C62A4740 02DF32E5 2139F0A0'
  ]

}

exports.curve = SM2 = SM2Curve(_sm2Params);


/**
 * Return a point on the curve.
 * Will throw error if (x,y) is not on curve.
 *
 * @param {string} x - coordinate x in hex string, should not be null
 * @param {string} y - coordinate y in hex string
 * @param {string='even'} parity - determine the value of y, could be 'odd' or 'even', ignored when y is not null
 */
function _sm2Point(x, y, parity) {
  if (x == null) {
    return SM2.point();
  }

  var pt;
  if (y != null) {
    pt = SM2.point(x, y);
    if (!SM2.validate(pt)) {
      throw 'point is not on curve';
    }
  } else {
    var px = new BN(x, 16).toRed(SM2.red);
    var py = px.redSqr().redMul(px);
    py = py.redIAdd(px.redMul(SM2.a)).redIAdd(SM2.b).redSqrt();
    if ((parity === 'odd') != py.fromRed().isOdd()) {
      py = py.redNeg();
    }
    pt = SM2.point(px, py);
  }

  return pt;
}

/**
 * SM2 public and private key pair
 *
 * Either `pub` and `pri` can be a hex string or byte array or null.
 * If `pub` is a string, it should be the same format as output of pubToString().
 */
function SM2KeyPair(pub, pri) {
  if (!(this instanceof SM2KeyPair)) {
    return new SM2KeyPair(pub, pri);
  }
  this.curve = SM2; // curve parameter
  this.pub = null; // public key, should be a point on the curve
  this.pri = null; // private key, should be a integer

  var validPub = false;
  var validPri = false;

  if (pub != null) {
    if (typeof pub === 'string') {
      this._pubFromString(pub);
    } else if (Array.isArray(pub)) {
      this._pubFromBytes(pub);
    } else if ('x' in pub && pub.x instanceof BN &&
               'y' in pub && pub.y instanceof BN) {
      // pub is already the Point object
      this.pub = pub;
      validPub = true;
    } else {
      throw 'invalid public key';
    }
  }
  if (pri != null) {
    if (typeof pri === 'string') {
      this.pri = new BN(pri, 16);
    } else if (pri instanceof BN) {
      this.pri = pri;
      validPri = true;
    } else {
      throw 'invalid private key';
    }

    // calculate public key
    if (this.pub == null) {
      this.pub = SM2.g.mul(this.pri);
    }
  }

  if (!(validPub && validPri) && !this.validate()) {
    throw 'invalid key';
  }
}
exports.SM2KeyPair = SM2KeyPair;

/**
 * Generate a SM2 key pair
 */
exports.genKeyPair = function _genKeyPair() {
  var pri = 0;
  var limit = SM2.n.sub(new BN(2));
  // generate 32 bytes private key in range [1, n-1]
  do {
    pri = new BN(_drbg.generate(32, 'hex', utils.random(64)));
  } while (pri.cmp(limit) > 0);

  return new SM2KeyPair(null, pri);
}


/**
 * @private
 * Parse public key from hex string.
 */
SM2KeyPair.prototype._pubFromString = function (s) {
  var err = 'invalid key string';
  if (s.length < 66) {
    throw err;
  }
  var x = s.slice(2, 66);
  switch (s.slice(0, 2)) {
    case '00':
      throw 'public key should not be infinity';
    case '02':
      this.pub = _sm2Point(x, null, 'even');
      break;
    case '03':
      this.pub = _sm2Point(x, null, 'odd');
      break;
    case '04':
    case '06':
    case '07':
      if (s.length < 130) {
        throw err;
      }
      this.pub = _sm2Point(x, s.slice(66, 130));
      break;
    default:
      throw err;
  }
}

/**
 * @private
 * Parse public key from byte array.
 */
SM2KeyPair.prototype._pubFromBytes = function (b) {
  var err = 'unrecognized key';
  if (b.length < 33) {
    throw err;
  }
  var x = b.slice(1, 33);
  switch (b[0]) {
    case 0x00:
      throw 'public key should not be infinity';
    case 0x02:
      this.pub = _sm2Point(x, null, 'even');
      break;
    case 0x03:
      this.pub = _sm2Point(x, null, 'odd');
      break;
    case 0x04:
    case 0x06:
    case 0x07:
      if (b.length < 65) {
        throw err;
      }
      this.pub = _sm2Point(x, b.slice(33, 65));
      break;
    default:
      throw err;
  }
}

/**
 * Check whether the public key is valid.
 *
 * @return {bool}
 */
SM2KeyPair.prototype.validate = function() {
  if (this.pub != null) {
    if (this.pub.isInfinity()) {
      return false;
    }

    if (!this.curve.validate(this.pub)) {
      return false;
    }

    if (!this.pub.mul(this.curve.n).isInfinity()) {
      return false;
    }
  }

  if (this.pri != null) {
    if (this.pri.cmp(this.curve.n.sub(new BN(2))) > 0) {
      return false;
    }

    if (this.pub != null && !this.pub.eq(this.curve.g.mul(this.pri))) {
      return false;
    }
  }

  return true;
}


/**
 * Convert the public key to the hex string format
 *
 * @param {Number} [mode='nocompress'] - compressing mode, available values:
 *    'compress', 'nocompress', 'mix'
 */
SM2KeyPair.prototype.pubToString = function(mode) {
  var s = '';
  switch (mode) {
    case 'compress':
      if (this.pub.getY().isEven()) {
        s = '02';
      } else {
        s = '03';
      }
      return s + this.pub.getX().toString(16, 32);
    case 'mix':
      if (this.pub.getY().isEven()) {
        s = '06';
      } else {
        s = '07';
      }
      break;
    default:
      s = '04'
  }
  return s + this.pub.getX().toString(16, 32) + this.pub.getY().toString(16, 32);
}

/**
 * Convert the public key to a byte array.
 * The value of X and Y will be stored in big endian.
 *
 * @param {string} mode - compressing mode, same as pubToString.
 */
SM2KeyPair.prototype.pubToBytes = function(mode) {
  var a = [];
  switch (mode) {
    case 'compress':
      if (this.pub.getY().isEven()) {
        a.push(0x02);
      } else {
        a.push(0x03);
      }
      return a.concat(this.pub.getX().toArray('be', 32));
    case 'mix':
      if (this.pub.getY().isEven()) {
        a.push(0x06);
      } else {
        a.push(0x07);
      }
      break;
    default:
      a.push(0x04);
  }
  return a.concat(this.pub.getX().toArray('be', 32)).concat(this.pub.getY().toArray('be', 32));
}


/**
 * Generate signature to the message
 *
 * The input message will combine with extras(a constant user id, the
 * curve parameters and public key), and use SM3 hashing function to
 * generate digest.
 *
 * @param {string|byte array} msg
 *
 * @return {SM2KeyPair} Signature (r, s). Both part is a hex string.
 */
SM2KeyPair.prototype.sign = function(msg) {
  if (this.pri == null) {
    throw 'cannot sign message without private key';
  }
  if (typeof msg === 'string')
    return this.signDigest(new sm3().sum(this._combine(utils.strToBytes(msg))));
  else
    return this.signDigest(new sm3().sum(this._combine(msg)));
}

/**
 * Verify the signature (r,s)
 *
 * @param {string|byte array} msg
 * @param {string} r - signature.r part in hex string
 * @param {string} s - signature.s part in hex string
 *
 * @return {bool} true if verification passed.
 */
SM2KeyPair.prototype.verify = function(msg, r, s) {
  if (this.pub == null) {
    throw 'cannot verify signature without public key';
  }
  return this.verifyDigest(new sm3().sum(this._combine(msg)), r, s);
}

/**
 * Generate signature to the message without combination with extras.
 */
SM2KeyPair.prototype.signRaw = function(msg) {
  return this.signDigest(new sm3().sum(msg));
}

/**
 * Verify signature (r, s) generated by signRaw()
 */
SM2KeyPair.prototype.verifyRaw = function(msg, r, s) {
  return this.verifyDigest(new sm3().sum(msg), r, s);
}

/**
 * Generate signature for the message digest
 *
 * The input data should be a 256bits hash digest.
 *
 * @param {string|byte array} digest - the digest of the message
 * @return {object}  signature with r and s parts
 */
SM2KeyPair.prototype.signDigest = function(digest) {
  var signature = {
    r: "",
    s: ""
  }
  while (true) {
    var k = new BN(_drbg.generate(32, 'hex', utils.random(64)), 16).umod(this.curve.n);
    var kg = this.curve.g.mul(k);
    var r = utils.hashToBN(digest).add(kg.getX()).umod(this.curve.n);
    // r = 0
    if (r.isZero()) {
      continue;
    }
    // r + k = n
    if (r.add(k).eq(this.curve.n)) {
      continue;
    }

    var t1 = new BN(1).add(this.pri).invm(this.curve.n);
    var t2 = k.sub(r.mul(this.pri)).umod(this.curve.n);
    var s = t1.mul(t2).umod(this.curve.n);
    if (!s.isZero()) {
      signature.r = r.toString(16);
      signature.s = s.toString(16);
      break;
    }
  }

  return signature;
}


/**
 * Verify the signature to the digest
 *
 * @param {string|byte array} digest - digest of the message
 * @param {string} r - hex string of signature.r
 * @param {string} s - hex string of signature.s
 *
 * @return {bool} true if verification passed
 */
SM2KeyPair.prototype.verifyDigest = function(digest, r, s) {
  var bnr = new BN(r, 16);
  if (bnr.cmp(this.curve.n) >= 0) {
    return false;
  }

  var bns = new BN(s, 16);
  if (bns.cmp(this.curve.n) >= 0) {
    return false;
  }

  var t = bnr.add(bns).umod(this.curve.n);
  if (t.isZero()) {
    return false;
  }

  var q = this.curve.g.mul(bns).add(this.pub.mul(t));
  var R = utils.hashToBN(digest).add(q.getX()).umod(this.curve.n);
  if (!R.eq(bnr)) {
    return false;
  }

  return true;
}

SM2KeyPair.prototype._combine = function(msg) {
  var za = [0x00, 0x80, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38];
  za = za.concat(this.curve.a.fromRed().toArray());
  za = za.concat(this.curve.b.fromRed().toArray());
  za = za.concat(this.curve.g.getX().toArray());
  za = za.concat(this.curve.g.getY().toArray());
  za = za.concat(this.pub.getX().toArray());
  za = za.concat(this.pub.getY().toArray());

  h = new sm3();
  za = h.sum(za);

  if (typeof msg === 'string')
    return za.concat(utils.strToBytes(msg))
  else
    return za.concat(msg);
}

SM2KeyPair.prototype.toString = function() {
  var s = "public: ";
  if (this.pub) {
    s += "(" + this.pub.getX().toString(16) + ", " + this.pub.getY().toString(16) + ")";
  } else {
    s += "null";
  }
  s += ", private: ";
  if (this.pri) {
    s += this.pri.toString(16);
  } else {
    s += "null";
  }
  return s;
}

