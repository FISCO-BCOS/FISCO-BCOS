pragma solidity ^0.4.4;

contract ContractAbiMgr {

    struct AbiInfo {
        string  contractname; //contract name
        string  version;      //version
        string  abi;          //abi
        address addr;         //contract address
        uint256 blocknumber;  //block number
        uint256 timestamp;    //timestamp
    }

    event AddAbi(string name,string contractname,string version,string abi,address addr,uint256 blocknumber,uint256 timestamp);
    event UpdateAbi(string name,string contractname,string version,string abi,address addr,uint256 blocknumber,uint256 timestamp);

    //name => AbiInfo
    mapping(string=>AbiInfo) private map_abi_infos;
    string[] private names;

    //add one abi info, when this contract abi info is already exist , it will end with failed.
    function addAbi(string name,string contractname,string version,string abi,address addr) public {
        //check if this contract abi info is already exist.
        uint256 old_timestamp = getTimeStamp(name);
        if (old_timestamp != 0) {
            //this abi info already exist;
            return;
        }

        names.push(name);
        map_abi_infos[name] = AbiInfo(contractname,version,abi,addr,block.number,block.timestamp);
        AddAbi(name,contractname,version,abi,addr,block.number,block.timestamp);
    }

    //update one abi info, when this contract abi info is not exist , it will end with failed.
    function updateAbi(string name,string contractname,string version,string abi,address addr) public {
        //check if this contract abi info is not exist.
        uint256 old_timestamp = getTimeStamp(name);
        if (old_timestamp == 0) {
            //this abi info is not exist;
            return;
        }

        map_abi_infos[name] = AbiInfo(contractname,version,abi,addr,block.number,block.timestamp);
        UpdateAbi(name,contractname,version,abi,addr,block.number,block.timestamp);
    }

    //get member address
    function getAddr(string name) constant public returns(address){
        return map_abi_infos[name].addr;
    }

    //get member abi
    function getAbi(string name) constant public returns(string){
        return map_abi_infos[name].abi;
    }

    //get member contract name
    function getContractName(string name) constant public returns(string){
        return map_abi_infos[name].contractname;
    }

    //get member version
    function getVersion(string name) constant public returns(string){
        return map_abi_infos[name].version;
    }

    //get member blocknumber
    function getBlockNumber(string name) constant public returns(uint256){
        return map_abi_infos[name].blocknumber;
    }

    //get member timestamp
    function getTimeStamp(string name) constant public returns(uint256){
        return map_abi_infos[name].timestamp;
    }

    //get length of names
    function getAbiCount() constant public returns(uint256){
        return names.length;
    }
    
    //get all members by name
    function getAll(string name) constant public returns(string,address,string,string,uint256,uint256){
        return (map_abi_infos[name].abi,
        map_abi_infos[name].addr,
        map_abi_infos[name].contractname,
        map_abi_infos[name].version,
        map_abi_infos[name].blocknumber,
        map_abi_infos[name].timestamp
        );
    }
    
    //get all members by index
    function getAllByIndex(uint256 index) constant public returns(string,address,string,string,uint256,uint256){
        if(index < names.length){
            return (map_abi_infos[names[index]].abi,
            map_abi_infos[names[index]].addr,
            map_abi_infos[names[index]].contractname,
            map_abi_infos[names[index]].version,
            map_abi_infos[names[index]].blocknumber,
            map_abi_infos[names[index]].timestamp
            );
        }

        return ("",0x00,"","",0,0); 
    }
}