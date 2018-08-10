var proxy="http://127.0.0.1:8545";

var encryptType = 0;// 0:ECDSA 1:sm2Withsm3
//console.log('RPC='+proxy);
var output="./output/";
//console.log('Ouputpath='+output);

module.exports={
	HttpProvider:proxy,
	Ouputpath:output,
	EncryptType:encryptType,
	privKey:"bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd",
	account:"0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3"
}
