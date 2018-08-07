var sm2 = require('./sm_sm2');
var sm3 = require('./sm_sm3');

function signRS(ecprvhex,msg){
    var keyPair = sm2.SM2KeyPair(null,ecprvhex);
    var pubKeyHex = keyPair.pub.getX().toString(16)+keyPair.pub.getY().toString(16);
    var _msg = Array.from(msg);

    var signData= keyPair.sign(_msg);
    //console.log("pubKeyHexLen:",pubKeyHex.length," pubKeyHex:",pubKeyHex);
    //console.log("sign.R:",signData.r," rLen:",signData.r.length," sLen:",signData.s.length," sign.S:",signData.s);

    var rHex = "000000000000000000000" + signData.r;
    var sHex = "000000000000000000000" + signData.s;
    var rHexLen = rHex.length - 64;
    var sHexLen = sHex.length - 64;
    rHex = rHex.substr(rHexLen,64);
    sHex = sHex.substr(sHexLen,64);

    var r = new Buffer(rHex,'hex');
    var s = new Buffer(sHex,'hex');
    var pub = new Buffer(pubKeyHex, 'hex');
    return {'r': r, 's': s,'pub':pub};
}

function priToPub(ecprvhex){
    var keyPair = sm2.SM2KeyPair(null,ecprvhex);
    var pubKeyHex = keyPair.pub.getX().toString(16)+keyPair.pub.getY().toString(16);
    return new Buffer(pubKeyHex,'hex');
}

function sm3Digest(msg){
	var _sm3 = new sm3();
    var rawData = Array.from(msg);
    var digest = _sm3.sum(rawData);
    var hashHex = Array.from(digest, function(byte) {return ('0' + (byte & 0xFF).toString(16)).slice(-2);}).join('');
    return hashHex;
}

exports.sm3Digest = sm3Digest;
exports.signRS = signRS;
exports.priToPub = priToPub;

/*var hashData = sm3Digest("trans()");
console.log("hashData:",hashData);

var pri = "c9a497f262b30acc933891257c5652f04d2de8b01bd0fbf939e497f215f5394f";
var pub = "04756185a0cd1a3b240bd8fd400b0f34083f557cb3a2a80f0cfb8f9f093f21ed8e349908e3d641db6fe43b152d1cb2180834a398d5e3314403d01178339b6447af";
var msg = "123456";
var signData = signRS(pri,msg);
var verifyRS = verifyRS(pub,msg,signData);
console.log("verifyRSResult:",verifyRS);*/

/*var genKey = sm2GenKey("sm2");
var pri = genKey.ecprvhex;
var pub = genKey.ecpubhex;
console.log("genpri:" + pri + " genpub:" + pub);

var pri = "c9a497f262b30acc933891257c5652f04d2de8b01bd0fbf939e497f215f5394f";
var pub = "04756185a0cd1a3b240bd8fd400b0f34083f557cb3a2a80f0cfb8f9f093f21ed8e349908e3d641db6fe43b152d1cb2180834a398d5e3314403d01178339b6447af";
var msg = "123456";

var _sign = sm2Sign(pri,msg);
var lresult = verify(pub,msg,_sign);
console.log("verifyResult:" + lresult);

var hashData = sm3Digest(msg);

console.log("sm3HashData:" + hashData);
console.log("address:" + hashData.substr(0,40));*/