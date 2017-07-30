var abi_arr='[{"constant":false,"inputs":[{"name":"size","type":"uint256"}],"name":"add","outputs":[],"payable":false,"type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"uint256"}],"payable":false,"type":"function"},{"inputs":[],"payable":false,"type":"constructor"},{"anonymous":false,"inputs":[{"indexed":false,"name":"bidder","type":"address"},{"indexed":false,"name":"amount","type":"uint256"}],"name":"HighestBidIncreased","type":"event"}]';
var contract_addr='0x52d0e7c989c06ff92553b17eced8c5099e353f5c';//合约地址
var ip="127.0.0.1";
var rpcport=8545;
var ipc_path="./data/geth.ipc";	//ipc文件
var account="0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3";		//区块链节点帐号
var pwd="";			//帐号密码
var privKey="bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad77dd";

exports.abi_arr = abi_arr;
exports.contract_addr = contract_addr;
exports.ip = ip;
exports.rpcport = rpcport;
exports.ipc_path = ipc_path;
exports.account = account;
exports.pwd = pwd;
exports.privKey = privKey;