
// convert a (hex) string to a bignum object
function parseBigInt(str,r) {
  return new BigInteger(str,r);
}

function _pem_extractEncodedData(sPEMString, sHead) {
  var s = sPEMString;
  s = s.replace("-----BEGIN " + sHead + "-----", "");
  s = s.replace("-----END " + sHead + "-----", "");
  s = s.replace(/[ \n]+/g, "");
  return s;
}

function _rsapem_getPosArrayOfChildrenFromHex(hPrivateKey) {
  var a = new Array();
  var v1 = _asnhex_getStartPosOfV_AtObj(hPrivateKey, 0);
  var n1 = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, v1);
  var e1 = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, n1);
  var d1 = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, e1);
  var p1 = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, d1);
  var q1 = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, p1);
  var dp1 = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, q1);
  var dq1 = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, dp1);
  var co1 = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, dq1);
  a.push(v1, n1, e1, d1, p1, q1, dp1, dq1, co1);
  return a;
}

function _rsapem_getHexValueArrayOfChildrenFromHex(hPrivateKey) {
  var posArray = _rsapem_getPosArrayOfChildrenFromHex(hPrivateKey);
  var v =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[0]);
  var n =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[1]);
  var e =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[2]);
  var d =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[3]);
  var p =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[4]);
  var q =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[5]);
  var dp = _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[6]);
  var dq = _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[7]);
  var co = _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[8]);
  var a = new Array();
  a.push(v, n, e, d, p, q, dp, dq, co);
  return a;
}

function _rsapem_getPosArrayOfChildrenFromPublicKeyHex(hPrivateKey) {
  var a = new Array();
  var header = _asnhex_getStartPosOfV_AtObj(hPrivateKey, 0);
  var keys = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, header);
  a.push(header, keys);
  return a;
}

function _rsapem_getPosArrayOfChildrenFromPrivateKeyHex(hPrivateKey) {
  var a = new Array();
  var integer = _asnhex_getStartPosOfV_AtObj(hPrivateKey, 0);
  var header = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, integer);
  var keys = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, header);
  a.push(integer, header, keys);
  return a;
}

function _rsapem_getHexValueArrayOfChildrenFromPublicKeyHex(hPrivateKey) {
  var posArray = _rsapem_getPosArrayOfChildrenFromPublicKeyHex(hPrivateKey);
  var headerVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[0]);
  var keysVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[1]);

  var keysSequence = keysVal.substring(2);
  posArray = _rsapem_getPosArrayOfChildrenFromPublicKeyHex(keysSequence);
  var modulus =  _asnhex_getHexOfV_AtObj(keysSequence, posArray[0]);
  var publicExp =  _asnhex_getHexOfV_AtObj(keysSequence, posArray[1]);

  var a = new Array();
  a.push(modulus, publicExp);
  return a;
}


function _rsapem_getHexValueArrayOfChildrenFromPrivateKeyHex(hPrivateKey) {
  var posArray = _rsapem_getPosArrayOfChildrenFromPrivateKeyHex(hPrivateKey);
  var integerVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[0]);
  var headerVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[1]);
  var keysVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[2]);

  var keysSequence = keysVal.substring(2);
  return _rsapem_getHexValueArrayOfChildrenFromHex(keysSequence);
}

function _rsapem_readPrivateKeyFromPkcs1PemString(keyPEM) {
  var keyB64 = _rsapem_extractEncodedData(keyPEM);
  var keyHex = b64tohex(keyB64) // depends base64.js
  var a = _rsapem_getHexValueArrayOfChildrenFromHex(keyHex);
  this.setPrivateEx(a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8]);
}

function _rsapem_readPrivateKeyFromPkcs8PemString(keyPEM) {
  var keyB64 = _rsapem_extractEncodedData(keyPEM);
  var keyHex = b64tohex(keyB64) // depends base64.js
  var a = _rsapem_getHexValueArrayOfChildrenFromPrivateKeyHex(keyHex);
  this.setPrivateEx(a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8]);
}

function _rsapem_readPublicKeyFromX509PemString(keyPEM) {
  var keyB64 = _rsapem_extractEncodedData(keyPEM);
  var keyHex = b64tohex(keyB64) // depends base64.js
  var a = _rsapem_getHexValueArrayOfChildrenFromPublicKeyHex(keyHex);
  this.setPublic(a[0],a[1]);
}

/**
* Pad string with leading zeros, to use even number of bytes.
*/
function _rsapem_padWithZero(numString) {
    if (numString.length % 2 == 1) {
        return "0" + numString;
    }
    return numString;
}

/**
* Encode length in DER format (if length <0x80, then one byte, else first byte is 0x80 + length of length :) + n-bytes of length).
*/
function _rsapem_encodeLength(length) {
    if (length >= parseInt("80", 16)) {
        var realLength = _rsapem_padWithZero(length.toString(16));
        var lengthOfLength = (realLength.length / 2);
        return (parseInt("80", 16) + lengthOfLength).toString(16) + realLength;
    } else {
        return _rsapem_padWithZero(length.toString(16));
    }
}

/**
* Encode number in DER encoding ("02" + length + number).
*/
function _rsapem_derEncodeNumber(number) {
    var numberString = _rsapem_padWithZero(number.toString(16));
    if (numberString[0] > '7') {
    	numberString = "00" + numberString;
    }
    var lenString = _rsapem_encodeLength(numberString.length / 2);
    return "02" + lenString + numberString;
}

/**
* Converts private & public part of given key to ASN1 Hex String.
*/
function _rsapem_privateKeyToPkcs1HexString(rsaKey) {
    var result = _rsapem_derEncodeNumber(0);
    result += _rsapem_derEncodeNumber(rsaKey.n);
    result += _rsapem_derEncodeNumber(rsaKey.e);
    result += _rsapem_derEncodeNumber(rsaKey.d);
    result += _rsapem_derEncodeNumber(rsaKey.p);
    result += _rsapem_derEncodeNumber(rsaKey.q);
    result += _rsapem_derEncodeNumber(rsaKey.dmp1);
    result += _rsapem_derEncodeNumber(rsaKey.dmq1);
    result += _rsapem_derEncodeNumber(rsaKey.coeff);

    var fullLen = _rsapem_encodeLength(result.length / 2);
    return '30' + fullLen + result;
}

/**
* Converts private & public part of given key to PKCS#8 Hex String.
*/
function _rsapem_privateKeyToPkcs8HexString(rsaKey) {
	var zeroInteger = "020100";
    var encodedIdentifier = "06092A864886F70D010101";
    var encodedNull = "0500";
    var headerSequence = "300D" + encodedIdentifier + encodedNull;
    var keySequence = _rsapem_privateKeyToPkcs1HexString(rsaKey);

    var keyOctetString = "04" + _rsapem_encodeLength(keySequence.length / 2) + keySequence;

    var mainSequence = zeroInteger + headerSequence + keyOctetString;
    return "30" + _rsapem_encodeLength(mainSequence.length / 2) + mainSequence;
}

/**
* Converts public part of given key to ASN1 Hex String.
*/
function _rsapem_publicKeyToX509HexString(rsaKey) {
    var encodedIdentifier = "06092A864886F70D010101";
    var encodedNull = "0500";
    var headerSequence = "300D" + encodedIdentifier + encodedNull;

    var keys = _rsapem_derEncodeNumber(rsaKey.n);
    keys += _rsapem_derEncodeNumber(rsaKey.e);

    var keySequence = "0030" + _rsapem_encodeLength(keys.length / 2) + keys;
    var bitstring = "03" + _rsapem_encodeLength(keySequence.length / 2) + keySequence;

    var mainSequence = headerSequence + bitstring;

    return "30" + _rsapem_encodeLength(mainSequence.length / 2) + mainSequence;
}

/**
* Output private & public part of the key in PKCS#1 PEM format.
*/
function _rsapem_privateKeyToPkcs1PemString() {
    var b64Cert = hex2b64(_rsapem_privateKeyToPkcs1HexString(this));
    var s = "-----BEGIN RSA PRIVATE KEY-----\n";
    s = s + _rsa_splitKey(b64Cert, 64);
    s = s + "\n-----END RSA PRIVATE KEY-----";
    return s;
}

/**
* Output private & public part of the key in PKCS#8 PEM format.
*/
function _rsapem_privateKeyToPkcs8PemString() {
    var b64Cert = hex2b64(_rsapem_privateKeyToPkcs8HexString(this));
    var s = "-----BEGIN RSA PRIVATE KEY-----\n";
    s = s + _rsa_splitKey(b64Cert, 64);
    s = s + "\n-----END RSA PRIVATE KEY-----";
    return s;
}

/**
* Output public part of the key in x509 PKCS#1 PEM format.
*/
function _rsapem_publicKeyToX509PemString() {
    var b64Cert = hex2b64(_rsapem_publicKeyToX509HexString(this));
    var s = "-----BEGIN PUBLIC KEY-----\n";
    s = s + _rsa_splitKey(b64Cert, 64);
    s = s + "\n-----END PUBLIC KEY-----";
    return s;
}

function _rsa_splitKey(key, line) {
    var splitKey = "";
    for (var i = 0; i < key.length; i++) {
        if (i % line == 0 && i != 0 && i != (key.length - 1)) {
            splitKey += "\n";
        }
        splitKey += key[i];
    }

    return splitKey;
}

function _paillier_padHex(s, n) {
    var slen = s.length;
    if(slen < n) {
        for(var i = 0; i < n - slen; i++) {
            s = "0" + s;
        }
    }
    return s;
}

function _paillier_encrypt(m) {
    var rng = new SecureRandom();
    var random = null;
    for (;;)
    {
        for (;;)
        {
            random = new BigInteger(this.n.bitLength(), 1, rng);
            if (random.isProbablePrime(64)) break;
        }
        if(random.signum() == 1) break;
    }

    if(m.signum() == -1) {
        m = m.mod(this.n);
    }

    var nsquare = this.n.multiply(this.n);
    var g = this.n.add(BigInteger.ONE);
    var cipher = g.modPow(m, nsquare).multiply(random.modPow(this.n, nsquare)).mod(nsquare);
    var nstr = this.n.toString(16);
    var nlen = Math.ceil(this.n.bitLength() / 8);
    return _paillier_padHex(nlen.toString(16), 4) + nstr + _paillier_padHex(cipher.toString(16), nlen * 4);
}

function _paillier_decrypt(ciphertext) {
    var nlen = parseInt("0x" + c1.substr(0,4)) * 2;
    var nstr = ciphertext.substr(4, nlen);
    var cstr = ciphertext.substr(nlen + 4);
    var n1 = parseBigInt(nstr, 16);
    var intCiphertext = parseBigInt(cstr, 16);
    if(n1.compareTo(this.n) != 0) return null;

    var lambda = this.p.subtract(BigInteger.ONE).multiply(this.q.subtract(BigInteger.ONE));
    var mu = lambda.modInverse(this.n);
    var nsquare = this.n.multiply(this.n);
    var message = intCiphertext.modPow(lambda, nsquare).subtract(BigInteger.ONE).divide(this.n).multiply(mu).mod(this.n);
    var maxValue = BigInteger.ONE.shiftLeft(this.n.bitLength() / 2);
    if(message.bitLength() > this.n.bitLength() / 2) {
        return message.subtract(this.n);
    }
    else {
        return message;
    }
}

function _paillier_add(c1, c2) {
    var nlen1 = parseInt("0x" + c1.substr(0,4)) * 2;
    var nstr1 = c1.substr(4, nlen1);
    var cstr1 = c1.substr(nlen1 + 4);
    var nlen2 = parseInt("0x" + c2.substr(0,4)) * 2;
    var nstr2 = c2.substr(4, nlen2);
    var cstr2 = c2.substr(nlen2 + 4);
    var n1 = parseBigInt(nstr1, 16);
    var n2 = parseBigInt(nstr2, 16);

    if(n2.compareTo(n1) != 0) {
        return null;
    }

    var ct1 = parseBigInt(cstr1, 16);
    var ct2 = parseBigInt(cstr2, 16);
    var nsquare = n1.multiply(n1);
    var ct = ct1.multiply(ct2).mod(nsquare);
    return _paillier_padHex(nlen1.toString(16), 4) + nstr1 + _paillier_padHex(ct.toString(16), nlen1 * 2);
}

RSAKey.prototype.readPrivateKeyFromPkcs1PemString = _rsapem_readPrivateKeyFromPkcs1PemString;
RSAKey.prototype.privateKeyToPkcs1PemString = _rsapem_privateKeyToPkcs1PemString;

RSAKey.prototype.readPrivateKeyFromPkcs8PemString = _rsapem_readPrivateKeyFromPkcs8PemString;
RSAKey.prototype.privateKeyToPkcs8PemString = _rsapem_privateKeyToPkcs8PemString;

RSAKey.prototype.readPublicKeyFromX509PEMString = _rsapem_readPublicKeyFromX509PemString;
RSAKey.prototype.publicKeyToX509PemString = _rsapem_publicKeyToX509PemString;

RSAKey.prototype.paillierEncrypt = _paillier_encrypt;
RSAKey.prototype.paillierDecrypt = _paillier_decrypt;
RSAKey.prototype.paillierAdd = _paillier_add;
RSAKey.prototype.splitKey = _rsa_splitKey;

