// SPDX-License-Identifier: UNLICENSED
pragma solidity ^0.8.0;

interface BalancePrecompiled {
    function getBalance(address account) view external returns (uint256);
    function addBalance(address account, uint256 amount) external;
    function subBalance(address account, uint256 amount) external;
    function transfer(address from, address to, uint256 amount) external;
    function registerCaller(address account) external;
    function unregisterCaller(address account) external;
}

interface CommitteeManager {
    function isGovernor(address account) external view returns (bool);
}

    struct IndexValue { uint keyIndex; bool value; }
    struct KeyFlag { address key; bool deleted; }

    struct itmap {
        mapping(address => IndexValue) data;
        KeyFlag[] keys;
        uint size;
    }

    type Iterator is uint;

library IterableMapping {
    function insert(itmap storage self, address key, bool value) internal returns (bool replaced) {
        uint keyIndex = self.data[key].keyIndex;
        self.data[key].value = value;
        if (keyIndex > 0)
            return true;
        else {
            keyIndex = self.keys.length;
            self.keys.push();
            self.data[key].keyIndex = keyIndex + 1;
            self.keys[keyIndex].key = key;
            self.size++;
            return false;
        }
    }

    function remove(itmap storage self, address key) internal returns (bool success) {
        uint keyIndex = self.data[key].keyIndex;
        if (keyIndex == 0)
            return false;
        delete self.data[key];
        self.keys[keyIndex - 1].deleted = true;
        self.size --;
    }

    function contains(itmap storage self, address key) internal view returns (bool) {
        return self.data[key].keyIndex > 0;
    }

    function iterateStart(itmap storage self) internal view returns (Iterator) {
        return iteratorSkipDeleted(self, 0);
    }

    function iterateValid(itmap storage self, Iterator iterator) internal view returns (bool) {
        return Iterator.unwrap(iterator) < self.keys.length;
    }

    function iterateNext(itmap storage self, Iterator iterator) internal view returns (Iterator) {
        return iteratorSkipDeleted(self, Iterator.unwrap(iterator) + 1);
    }

    function iterateGet(itmap storage self, Iterator iterator) internal view returns (address key, bool value) {
        uint keyIndex = Iterator.unwrap(iterator);
        key = self.keys[keyIndex].key;
        value = self.data[key].value;
    }

    function iteratorSkipDeleted(itmap storage self, uint keyIndex) private view returns (Iterator) {
        while (keyIndex < self.keys.length && self.keys[keyIndex].deleted)
            keyIndex++;
        return Iterator.wrap(keyIndex);
    }
}

contract Economic {
    BalancePrecompiled balancePrecompiled = BalancePrecompiled(address(bytes20(bytes("0x0000000000000000000000000000000000001011"))));
    CommitteeManager committeeMgr = CommitteeManager(address(bytes20(bytes("0x0000000000000000000000000000000000010001"))));

    address owner;
    itmap chargersMap;
    using IterableMapping for itmap;

    constructor() {
        owner = msg.sender;
    }

    modifier onlyGovernor() {
        require(committeeMgr.isGovernor(msg.sender), "you must be governor");
        _;
    }

    modifier onlyCharger() {
        require(chargersMap.contains(msg.sender), "you must be charger");
        _;
    }

    modifier onlyOwner() {
        require(msg.sender == owner, "you must be owner");
        _;
    }

    function charge(address userAccount, uint256 gasValue) onlyCharger public {
        balancePrecompiled.addBalance(userAccount, gasValue);
    }
    function deduct(address userAccount, uint256 gasValue) onlyCharger public {
        balancePrecompiled.subBalance(userAccount, gasValue);
    }
    function getBalance(address userAccount) public view returns(uint256){
        return balancePrecompiled.getBalance(userAccount);
    }
    function grantCharger(address chargerAccount) onlyOwner public returns(bool success){
        return chargersMap.insert(chargerAccount, true);
    }
    function revokeCharger(address chargerAccount) onlyOwner public returns(bool success){
        require(chargersMap.contains(chargerAccount), "charger has not been granted before");
        return chargersMap.remove(chargerAccount);
    }
    function listChargers() public view returns(address[] memory){
        address[] memory output = new address[](chargersMap.size);
        uint256 index = 0;
        for (
            Iterator i = chargersMap.iterateStart();
            chargersMap.iterateValid(i);
            i = chargersMap.iterateNext(i)
        ) {
            (address charger, ) = chargersMap.iterateGet(i);
            output[index] = charger;
            index++;
        }
        return output;
    }

    function enable() onlyGovernor public {
        balancePrecompiled.registerCaller(address(this));
    }
    function disable() onlyGovernor public {
        balancePrecompiled.unregisterCaller(address(this));
    }
}