#pragma once

#include <string>
#include <libdevcore/FixedHash.h>

using namespace std;

static dev::h256 const c_genesisStateRootForWe;
static std::string const c_genesisJsonForWe = std::string() +
R"E(
{
	"nonce": "0x0",
	"difficulty": "0x0",
	"mixhash": "0x0",
	"coinbase": "0x0",
	"timestamp": "0x0",
	"parentHash": "0x0",
	"extraData": "0x0",
	"gasLimit": "0x13880000000000",
	"god":"0x4d23de3297034cdd4a58db35f659a9b61fc7577b",
	"alloc": {}, 	
	"initMinerNodes":["de0fa385816b505799433e54b88788e21cb42092a6ff5bcaa2285d7ace906e5e6ce8ef2b30134ff276a5834d58721291acc5864e07e6d52469b79b28e699dfde"]
}
)E";

static std::string const c_configJsonForWe = std::string() +
R"E(
{
	"sealEngine": "SinglePoint",
	"systemproxyaddress":"0x0",
	"listenip":"0.0.0.0",
	"rpcport":"8545",
	"p2pport":"30303",
	"wallet":"keys.info",
	"keystoredir":"keystore/",
	"datadir":"/mydata/nodedata-1/",
	"vm":"interpreter",
	"networkid":"12345",
	"logverbosity":"4",
	"coverlog":"OFF",
	"eventlog":"ON",
	"logconf":"log.conf",
	"params": {
		"accountStartNonce": "0x0",
		"maximumExtraDataSize": "0x0",
		"tieBreakingGas": false,
		"blockReward": "0x0",
		"networkID" : "0x0"
	},
	"accounts": {
		"0000000000000000000000000000000000000001": { "precompiled": { "name": "ecrecover", "linear": { "base": 3000, "word": 0 } } },
		"0000000000000000000000000000000000000002": { "precompiled": { "name": "sha256", "linear": { "base": 60, "word": 12 } } },
		"0000000000000000000000000000000000000003": { "precompiled": { "name": "ripemd160", "linear": { "base": 600, "word": 120 } } },
		"0000000000000000000000000000000000000004": { "precompiled": { "name": "identity", "linear": { "base": 15, "word": 3 } } },
		"004f9d39c8424e7f86004622cef21a0bbe140bfa": { "balance": "999999999999000000000000000000" },
		"00dcf3367eab3b34d6a598f453d1aee9146b50f3": { "balance": "999999999999000000000000000000" }
	},
	"NodeextraInfo":[
	{
		"Nodeid":"de0fa385816b505799433e54b88788e21cb42092a6ff5bcaa2285d7ace906e5e6ce8ef2b30134ff276a5834d58721291acc5864e07e6d52469b79b28e699dfde",
		"Nodedesc": "node1",
		"Agencyinfo": "node1",
		"Peerip": "127.0.0.1",
		"Identitytype": 1,
		"Port":30303,
		"Idx":0
	}
	]
}
)E";

