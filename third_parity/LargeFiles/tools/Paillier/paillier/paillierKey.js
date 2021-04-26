
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

function _pem_getPosArrayOfChildrenFromHex(hPrivateKey) {
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

function _pem_getHexValueArrayOfChildrenFromHex(hPrivateKey) {
  var posArray = _pem_getPosArrayOfChildrenFromHex(hPrivateKey);
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

function _pem_getPosArrayOfChildrenFromPublicKeyHex(hPrivateKey) {
  var a = new Array();
  var header = _asnhex_getStartPosOfV_AtObj(hPrivateKey, 0);
  var keys = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, header);
  a.push(header, keys);
  return a;
}

function _pem_getPosArrayOfChildrenFromPrivateKeyHex(hPrivateKey) {
  var a = new Array();
  var integer = _asnhex_getStartPosOfV_AtObj(hPrivateKey, 0);
  var header = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, integer);
  var keys = _asnhex_getPosOfNextSibling_AtObj(hPrivateKey, header);
  a.push(integer, header, keys);
  return a;
}

function _pem_getHexValueArrayOfChildrenFromPublicKeyHex(hPrivateKey) {
  var posArray = _pem_getPosArrayOfChildrenFromPublicKeyHex(hPrivateKey);
  var headerVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[0]);
  var keysVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[1]);

  var keysSequence = keysVal.substring(2);
  posArray = _pem_getPosArrayOfChildrenFromPublicKeyHex(keysSequence);
  var modulus =  _asnhex_getHexOfV_AtObj(keysSequence, posArray[0]);
  var publicExp =  _asnhex_getHexOfV_AtObj(keysSequence, posArray[1]);

  var a = new Array();
  a.push(modulus, publicExp);
  return a;
}


function _pem_getHexValueArrayOfChildrenFromPrivateKeyHex(hPrivateKey) {
  var posArray = _pem_getPosArrayOfChildrenFromPrivateKeyHex(hPrivateKey);
  var integerVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[0]);
  var headerVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[1]);
  var keysVal =  _asnhex_getHexOfV_AtObj(hPrivateKey, posArray[2]);

  var keysSequence = keysVal.substring(2);
  return _pem_getHexValueArrayOfChildrenFromHex(keysSequence);
}

function _pem_readPrivateKeyFromPkcs1PemString(keyPEM) {
  var keyB64 = _pem_extractEncodedData(keyPEM);
  var keyHex = b64tohex(keyB64) // depends base64.js
  var a = _pem_getHexValueArrayOfChildrenFromHex(keyHex);
  this.setPrivateEx(a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8]);
}

function _pem_readPrivateKeyFromPkcs8PemString(keyPEM) {
  var keyB64 = _pem_extractEncodedData(keyPEM);
  var keyHex = b64tohex(keyB64) // depends base64.js
  var a = _pem_getHexValueArrayOfChildrenFromPrivateKeyHex(keyHex);
  this.setPrivateEx(a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8]);
}

function _pem_readPublicKeyFromX509PemString(keyPEM) {
  var keyB64 = _pem_extractEncodedData(keyPEM);
  var keyHex = b64tohex(keyB64) // depends base64.js
  var a = _pem_getHexValueArrayOfChildrenFromPublicKeyHex(keyHex);
  this.setPublic(a[0],a[1]);
}

/**
* Pad string with leading zeros, to use even number of bytes.
*/
function _pem_padWithZero(numString) {
    if (numString.length % 2 == 1) {
        return "0" + numString;
    }
    return numString;
}

/**
* Encode length in DER format (if length <0x80, then one byte, else first byte is 0x80 + length of length :) + n-bytes of length).
*/
function _pem_encodeLength(length) {
    if (length >= parseInt("80", 16)) {
        var realLength = _pem_padWithZero(length.toString(16));
        var lengthOfLength = (realLength.length / 2);
        return (parseInt("80", 16) + lengthOfLength).toString(16) + realLength;
    } else {
        return _pem_padWithZero(length.toString(16));
    }
}

/**
* Encode number in DER encoding ("02" + length + number).
*/
function _pem_derEncodeNumber(number) {
    var numberString = _pem_padWithZero(number.toString(16));
    if (numberString[0] > '7') {
    	numberString = "00" + numberString;
    }
    var lenString = _pem_encodeLength(numberString.length / 2);
    return "02" + lenString + numberString;
}

/**
* Converts private & public part of given key to ASN1 Hex String.
*/
function _pem_privateKeyToPkcs1HexString(paillierKey) {
    var result = _pem_derEncodeNumber(0);
    result += _pem_derEncodeNumber(paillierKey.n);
    result += _pem_derEncodeNumber(paillierKey.e);
    result += _pem_derEncodeNumber(paillierKey.d);
    result += _pem_derEncodeNumber(paillierKey.p);
    result += _pem_derEncodeNumber(paillierKey.q);
    result += _pem_derEncodeNumber(paillierKey.dmp1);
    result += _pem_derEncodeNumber(paillierKey.dmq1);
    result += _pem_derEncodeNumber(paillierKey.coeff);

    var fullLen = _pem_encodeLength(result.length / 2);
    return '30' + fullLen + result;
}

/**
* Converts private & public part of given key to PKCS#8 Hex String.
*/
function _pem_privateKeyToPkcs8HexString(paillierKey) {
	var zeroInteger = "020100";
    var encodedIdentifier = "06092A864886F70D010101";
    var encodedNull = "0500";
    var headerSequence = "300D" + encodedIdentifier + encodedNull;
    var keySequence = _pem_privateKeyToPkcs1HexString(paillierKey);

    var keyOctetString = "04" + _pem_encodeLength(keySequence.length / 2) + keySequence;

    var mainSequence = zeroInteger + headerSequence + keyOctetString;
    return "30" + _pem_encodeLength(mainSequence.length / 2) + mainSequence;
}

/**
* Converts public part of given key to ASN1 Hex String.
*/
function _pem_publicKeyToX509HexString(paillierKey) {
    var encodedIdentifier = "06092A864886F70D010101";
    var encodedNull = "0500";
    var headerSequence = "300D" + encodedIdentifier + encodedNull;

    var keys = _pem_derEncodeNumber(paillierKey.n);
    keys += _pem_derEncodeNumber(paillierKey.g);

    var keySequence = "0030" + _pem_encodeLength(keys.length / 2) + keys;
    var bitstring = "03" + _pem_encodeLength(keySequence.length / 2) + keySequence;

    var mainSequence = headerSequence + bitstring;

    return "30" + _pem_encodeLength(mainSequence.length / 2) + mainSequence;
}

/**
* Output private & public part of the key in PKCS#1 PEM format.
*/
function _pem_privateKeyToPkcs1PemString() {
    var b64Cert = hex2b64(_pem_privateKeyToPkcs1HexString(this));
    var s = "-----BEGIN PRIVATE KEY-----\n";
    s = s + _paillier_splitKey(b64Cert, 64);
    s = s + "\n-----END PRIVATE KEY-----";
    return s;
}

/**
* Output private & public part of the key in PKCS#8 PEM format.
*/
function _pem_privateKeyToPkcs8PemString() {
    var b64Cert = hex2b64(_pem_privateKeyToPkcs8HexString(this));
    var s = "-----BEGIN PRIVATE KEY-----\n";
    s = s + _paillier_splitKey(b64Cert, 64);
    s = s + "\n-----END PRIVATE KEY-----";
    return s;
}

/**
* Output public part of the key in x509 PKCS#1 PEM format.
*/
function _pem_publicKeyToX509PemString() {
    var b64Cert = hex2b64(_pem_publicKeyToX509HexString(this));
    var s = "-----BEGIN PUBLIC KEY-----\n";
    s = s + _paillier_splitKey(b64Cert, 64);
    s = s + "\n-----END PUBLIC KEY-----";
    return s;
}

function _paillier_splitKey(key, line) {
    var splitKey = "";
    for (var i = 0; i < key.length; i++) {
        if (i % line == 0 && i != 0 && i != (key.length - 1)) {
            splitKey += "\n";
        }
        splitKey += key[i];
    }

    return splitKey;
}

function _paillier_setPublic(N,G) {
  if(N != null && G != null && N.length > 0 && G.length > 0) {
    this.n = parseBigInt(N,16);
    this.g = parseBigInt(G,16);
  }
  else
    alert("Invalid Paillier public key");
}

// Set the private key fields N, e, and d from hex strings
function _paillier_setPrivate(N,E,D) {
  if(N != null && E != null && N.length > 0 && E.length > 0) {
    this.n = parseBigInt(N,16);
    this.e = parseInt(E,16);
    this.d = parseBigInt(D,16);
  }
  else
    alert("Invalid Paillier private key");
}

// Set the private key fields N, e, d and CRT params from hex strings
function _paillier_setPrivateEx(N,E,D,P,Q,DP,DQ,C) {
  if(N != null && E != null && N.length > 0 && E.length > 0) {
    this.n = parseBigInt(N,16);
    this.e = parseInt(E,16);
    this.d = parseBigInt(D,16);
    this.p = parseBigInt(P,16);
    this.q = parseBigInt(Q,16);
    this.dmp1 = parseBigInt(DP,16);
    this.dmq1 = parseBigInt(DQ,16);
    this.coeff = parseBigInt(C,16);
  }
  else
    alert("Invalid Paillier private key");
}

function _paillier_genKeyPair(B) {
    var rng = new SecureRandom();
    var qs = B>>1;
    for(;;) {
       for(;;) {
          this.p = new BigInteger(B-qs,1,rng);
          if(this.p.isProbablePrime(64)) break;
        }
        for(;;) {
          this.q = new BigInteger(qs,1,rng);
          if(this.q.isProbablePrime(64)) break;
        }
        var p1 = this.p.subtract(BigInteger.ONE);
        var q1 = this.q.subtract(BigInteger.ONE);
        this.n = this.p.multiply(this.q);
        if(this.n.bitLength() != B) continue;
        var nsquare = this.n.multiply(this.n);
        this.d = this.p.subtract(BigInteger.ONE).multiply(this.q.subtract(BigInteger.ONE));
        this.g = this.n.add(BigInteger.ONE);
        if (this.g.modPow(this.d, nsquare).subtract(BigInteger.ONE).divide(this.n).gcd(this.n).intValue() == 1) {
            this.dmp1 = this.d.mod(p1);
            this.dmq1 = this.d.mod(q1);
            this.coeff = this.q.modInverse(this.p);
            break;
        }
    }
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
    var rng = new SeededRandom();
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
    var cipher = this.g.modPow(m, nsquare).multiply(random.modPow(this.n, nsquare)).mod(nsquare);
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

    var mu = this.d.modInverse(this.n);
    var nsquare = this.n.multiply(this.n);
    var message = intCiphertext.modPow(this.d, nsquare).subtract(BigInteger.ONE).divide(this.n).multiply(mu).mod(this.n);
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

function PaillierKey() {
    this.n = null;
    this.e = 0;
    this.g = null;
    this.d = null;
    this.p = null;
    this.q = null;
    this.dmp1 = null;
    this.dmq1 = null;
    this.coeff = null;

}

PaillierKey.prototype.readPublicKeyFromX509PEMString = _pem_readPublicKeyFromX509PemString;
PaillierKey.prototype.publicKeyToX509PemString = _pem_publicKeyToX509PemString;
PaillierKey.prototype.readPrivateKeyFromPkcs1PemString = _pem_readPrivateKeyFromPkcs1PemString;
PaillierKey.prototype.privateKeyToPkcs1PemString = _pem_privateKeyToPkcs1PemString;
PaillierKey.prototype.readPrivateKeyFromPkcs8PemString = _pem_readPrivateKeyFromPkcs8PemString;
PaillierKey.prototype.privateKeyToPkcs8PemString = _pem_privateKeyToPkcs8PemString;
PaillierKey.prototype.setPublic = _paillier_setPublic;
PaillierKey.prototype.setPrivate = _paillier_setPrivate;
PaillierKey.prototype.setPrivateEx = _paillier_setPrivateEx;
PaillierKey.prototype.genKeyPair = _paillier_genKeyPair;
PaillierKey.prototype.encrypt = _paillier_encrypt;
PaillierKey.prototype.decrypt = _paillier_decrypt;
PaillierKey.prototype.add = _paillier_add;

