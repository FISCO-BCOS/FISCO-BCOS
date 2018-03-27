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

    event AddAbi(string cns_name,string contractname,string version,string abi,address addr,uint256 blocknumber,uint256 timestamp);
    event UpdateAbi(string cns_name,string contractname,string version,string abi,address addr,uint256 blocknumber,uint256 timestamp);
    event AddOldAbi(string cns_name,string contractname,string version,string abi,address addr,uint256 blocknumber,uint256 timestamp);
    event AbiNotExist(string cns_name,string contractname,string version);
    event AbiExist(string cns_name,string contractname,string version,string abi,address addr,uint256 bn,uint256 t);

    //name => AbiInfo current call cns
    mapping(string=>AbiInfo) private map_cur_cns_info;
    //name => all AbiInfos
    mapping(string=>AbiInfo[]) private map_history_cns_info;
    //
    string[] private names;

    //add one history abi info
    function addOldAbi(string cns_name, AbiInfo abi) private {
        map_history_cns_info[cns_name].push(abi);
        AddOldAbi(cns_name, abi.contractname, abi.version, abi.abi, abi.addr, abi.blocknumber, abi.timestamp);
    }

    //获取cns_name对应的被覆盖的abi address信息的个数
    function getHistoryAbiC(string cns_name) constant public returns(uint256) {
        return  map_history_cns_info[cns_name].length;
    }

    //根据cns_name 索引号获取被覆盖的abi address信息
    function getHistoryAllByIndex(string cns_name, uint256 index) constant public returns(string,address,string,string,uint256,uint256) {

        if(index < map_history_cns_info[cns_name].length) {
            return (map_history_cns_info[cns_name][index].abi,
            map_history_cns_info[cns_name][index].addr,
            map_history_cns_info[cns_name][index].contractname,
            map_history_cns_info[cns_name][index].version,
            map_history_cns_info[cns_name][index].blocknumber,
            map_history_cns_info[cns_name][index].timestamp
            );
        }

        return ("",0x00,"","",0,0); 
    }

    //add one abi info, when this contract abi info is already exist , it will end with failed.
    function addAbi(string cns_name,string contractname,string version,string abi,address addr) public {
        //check if this contract abi info is not exist.
        AbiInfo memory old = map_cur_cns_info[cns_name];
        if(old.timestamp != 0) {
            AbiExist(cns_name, old.contractname, old.version, old.abi, old.addr, old.blocknumber, old.timestamp);
            return ;
        }

        names.push(cns_name);
        map_cur_cns_info[cns_name] = AbiInfo(contractname,version,abi,addr,block.number,block.timestamp);
        AddAbi(cns_name,contractname,version,abi,addr,block.number,block.timestamp);
    }

    //update one abi info, when this contract abi info is not exist , it will end with failed.
    function updateAbi(string cns_name,string contractname,string version,string abi,address addr) public {
        //check if this contract abi info is already exist.
        AbiInfo memory old = map_cur_cns_info[cns_name];
        if(old.timestamp == 0) {
            AbiNotExist(cns_name, contractname, version);
            return ;
        }

        addOldAbi(cns_name, old);
        map_cur_cns_info[cns_name] = AbiInfo(contractname,version,abi,addr,block.number,block.timestamp);
        UpdateAbi(cns_name,contractname,version,abi,addr,block.number,block.timestamp);
    }

    //get member address
    function getAddr(string cns_name) constant public returns(address) {
        return map_cur_cns_info[cns_name].addr;
    }

    //get member abi
    function getAbi(string cns_name) constant public returns(string) {
        return map_cur_cns_info[cns_name].abi;
    }

    //get member contract name
    function getContractName(string cns_name) constant public returns(string) {
        return map_cur_cns_info[cns_name].contractname;
    }

    //get member version
    function getVersion(string cns_name) constant public returns(string) {
        return map_cur_cns_info[cns_name].version;
    }

    //get member blocknumber
    function getBlockNumber(string cns_name) constant public returns(uint256) {
        return map_cur_cns_info[cns_name].blocknumber;
    }

    //get member timestamp
    function getTimeStamp(string cns_name) constant public returns(uint256) {
        return map_cur_cns_info[cns_name].timestamp;
    }

    //get length of names
    function getAbiCount() constant public returns(uint256) {
        return names.length;
    }
    
    //get all members by name
    function getAll(string cns_name) constant public returns(string,address,string,string,uint256,uint256) {
        return (map_cur_cns_info[cns_name].abi,
        map_cur_cns_info[cns_name].addr,
        map_cur_cns_info[cns_name].contractname,
        map_cur_cns_info[cns_name].version,
        map_cur_cns_info[cns_name].blocknumber,
        map_cur_cns_info[cns_name].timestamp
        );
    }
    
    //get all members by index
    function getAllByIndex(uint256 index) constant public returns(string,address,string,string,uint256,uint256) {
        if(index < names.length){
            return (map_cur_cns_info[names[index]].abi,
            map_cur_cns_info[names[index]].addr,
            map_cur_cns_info[names[index]].contractname,
            map_cur_cns_info[names[index]].version,
            map_cur_cns_info[names[index]].blocknumber,
            map_cur_cns_info[names[index]].timestamp
            );
        }

        return ("",0x00,"","",0,0); 
    }
}
