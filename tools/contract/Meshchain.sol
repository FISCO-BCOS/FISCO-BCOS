pragma solidity ^0.4.2;

import "LibTrieProof.sol";
import "LibVerifySign.sol";

contract Meshchain {

	/*
		errcode:
		10000:热点账户已存在
		10001:用户已存在
		10002:用户状态不正常
		10003:用户不存在
		10004:热点账户不存在
		10005:热点账户状态不正常
		10006:用户余额不足
		10007:冻结余额不合法
		10008:热点账户余额为0
		10009:没有可释放的金额
		10010:非热点账户
		10011:非影子户
		10012:trie proof验证失败

		10013:影子户不存在
		10014:影子户状态不正常
		10015:影子户已存在
		10016:公钥列表不存在
		10017:验证签名失败
		10018:金额非法
	*/

    struct UserInfo {
       uint blockNumber;
       bytes32 uid;
       uint assets;
       uint8 status;//0:正常 1:冻结 2:不可用
    }

	struct MerchantInfo {
           uint blockNumber;
           bytes32 merchantId;
           string name;
           uint frozenAssets;//冻结的金额 单位分 todo:改为数组好点？
           uint assets;//可用的金额 单位分
           uint8 status;//账户状态 0:正常 1:冻结 2:不可用
           uint8 mtype;//0:热点户 1:影子户
    }

    mapping(bytes32 => UserInfo) users;
	mapping(bytes32 => MerchantInfo) merchants;
	bytes32[] public merchantIds;
	mapping(string => bool) pubKeys;//公钥列表,主要拿来当做白名单

    event retLog(int code);
	event assetsLog(int code, uint availAssets, uint frozenAssets);

	//热点账户注册
    function registerMerchant(bytes32 merchantId, string name, uint8 mtype) public returns(bool) {
        if (merchants[merchantId].blockNumber > 0) {
        	if (mtype == 0) {
        		retLog(10000);
        	} else {
        		retLog(10015);
        	}

        	return false;
        }

        merchants[merchantId] = MerchantInfo(block.number, merchantId, name, 0, 0, 0, mtype);
        merchantIds.push(merchantId);
        retLog(0);
        return true;
    }


    //用户注册
    function registerUser(bytes32 uid) public returns(bool) {
        if (users[uid].blockNumber > 0) {
        	retLog(10001);
        	return false;
        }

        users[uid] = UserInfo(block.number, uid, 0, 0);
        retLog(0);
        return true;
    }


	//用户充值
	function userDeposit(bytes32 uid, uint assets) public returns(bool) {
		if (users[uid].blockNumber <= 0) {
			users[uid] = UserInfo(block.number, uid, 0, 0);
		}

		UserInfo user = users[uid];
		if (user.status != 0) {
			retLog(10002);
			return false;
		}

		if (assets == 0) {
			retLog(10018);
			return false;
		}

		user.assets = user.assets + assets;
		users[uid] = user;
		retLog(0);
		return true;
	}

    //用户消费
    function consume(bytes32 uid, bytes32 merchantId, uint assets) public returns(bool) {
        if (merchants[merchantId].blockNumber <= 0) {
            retLog(10004);
	    	return false;
        }

        UserInfo user = users[uid];
        MerchantInfo merchant = merchants[merchantId];
        uint8 userStatus = user.status;
        uint8 merchantStatus = merchant.status;

        if (userStatus != 0) {
            retLog(10002);
            return false;
        }

        if (merchantStatus != 0) {
            retLog(10005);
            return false;
        }

        if (user.assets < assets) {
        	retLog(10006);
        	return false;
        }

		user.assets = user.assets - assets;
        merchant.assets += assets;
        users[uid] = user;
        merchants[merchantId] = merchant;
        retLog(0);
        return true;
    }

    //影子户出账
    function merchantWithdrawal(bytes32 merchantId) {
		if (merchants[merchantId].blockNumber <= 0) {
			assetsLog(10013, 0, 0);
			return;
		}

		//先直接扣完
		MerchantInfo merchant = merchants[merchantId];
		if (merchant.status != 0) {
			assetsLog(10014, merchant.assets, merchant.frozenAssets);
			return;
		}

		//非影子户不允许
		if (merchant.mtype != 1) {
			assetsLog(10011, 0, 0);
			return;
		}

		if (merchant.assets == 0) {
			assetsLog(10008, 0, 0);
			return;
		}


		if (merchant.frozenAssets > 0) {
			//还有资金冻结状态中
			assetsLog(10007, merchant.assets, merchant.frozenAssets);
			return;
		}

		merchant.frozenAssets = merchant.assets;
		merchant.assets = 0;

		//merchant.frozenAssets = merchant.frozenAssets + 1;
		//merchant.assets = merchant.assets - 1;
		merchants[merchantId] = merchant;
		assetsLog(0, merchant.assets, merchant.frozenAssets);
    }

    //释放冻结 status 0 : 扣除成功  其他扣除失败
    function confirmWithdrawal(bytes32 merchantId, uint frozenAssets, uint8 status) {
    	if (merchants[merchantId].blockNumber <= 0) {
    		retLog(10013);
    		return;
    	}

    	MerchantInfo merchant = merchants[merchantId];
    	if (merchant.status != 0) {
    		retLog(10014);
    		return;
    	}

		//非影子户不允许
		if (merchant.mtype != 1) {
			retLog(10011);
			return;
		}

    	if (merchant.frozenAssets <= 0) {
    		//没有可释放的金额
    		retLog(10009);
    		return;
    	}

   		if (merchant.frozenAssets < frozenAssets) {
   			retLog(10007);
   			return;
   		}

    	merchant.frozenAssets -= frozenAssets;
    	if (status != 0) {
    		merchant.assets += frozenAssets;
    	}

    	merchants[merchantId] = merchant;
    	retLog(0);
    }

	//验证签名 pubs,signs用";"分隔
	function verifySign(string hash, string pubs, string signs, string idxs) constant public returns(uint) {
		uint ret = LibVerifySign.verifySign(hash, pubs, signs, idxs);
    	if (ret != 0) {
    		return 10017;
    	}

		return ret;
	}

	//热点户入账,目前demo只验证一个公钥存在即可
    function verifyProofAndDeposit(string root, string proofs, string key, string targetValue, bytes32 merchantId, uint assets) public returns(bool) {
    	//should verify first and Deposit
		uint ret = LibTrieProof.verifyProof(root, proofs, key, targetValue);
		if (ret != 0) {
			assetsLog(10012, 0, 0);
			return false;
		}

    	if (merchants[merchantId].blockNumber <= 0) {
    		assetsLog(10004, 0, 0);
    		return false;
    	}

    	MerchantInfo merchant = merchants[merchantId];
    	if (merchant.status != 0) {
    		assetsLog(10005, 0, 0);
    		return false;
    	}

		//非热点户不允许
		if (merchant.mtype != 0) {
			assetsLog(10010, 0, 0);
			return false;
		}

    	merchant.assets += assets;
    	merchants[merchantId] = merchant;
    	assetsLog(0, merchant.assets, merchant.frozenAssets);
    	return true;
    }

    function getAllMerchantIds() constant public returns(bytes32[]) {
    	return merchantIds;
    }

	//查看商户的资产
	function getMerchantAssets(bytes32 merchantId) constant public returns(uint, uint) {
		if (merchants[merchantId].blockNumber <= 0) {
			return (0, 0);
		}
		
    	MerchantInfo merchant = merchants[merchantId];
        return (merchant.assets,merchant.frozenAssets);
    }

	//商户是否存在
	function checkMerchantExisted(bytes32 merchantId) constant public returns(bool) {
		if (merchants[merchantId].blockNumber > 0) {
			return (true);
		}

    	return (false);
    }

    //查看用户的资产
    function getUserAssets(bytes32 uid) constant public returns(uint) {
    	if (users[uid].blockNumber <= 0) {
    			return (0);
    	}

        UserInfo user = users[uid];
        return (user.assets);
    }



    function addPub(string pub) public returns (bool){
    	pubKeys[pub] = true;
    }

    function checkPub(string pub) constant public returns(bool) {
    	if (pubKeys[pub]) {
    		return false;
    	}

    	return true;
    }
}
