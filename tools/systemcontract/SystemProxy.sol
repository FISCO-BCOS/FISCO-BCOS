pragma solidity ^0.4.4;

contract SystemProxy {
    struct SystemContract {
        address _addr;
        bool _cache;
        uint _blocknumber;
    }
    
    mapping (string => SystemContract) private _routes; //路由表
    string[] _name;

    //mapping (address => string) private _names;
    
    //获取路由
    function getRoute(string key) public constant returns(address, bool,uint) {
        return (_routes[key]._addr, _routes[key]._cache,_routes[key]._blocknumber);
    }
    
    //设置路由
    function setRoute(string key, address addr, bool cache) public {
        _routes[key] = SystemContract(addr, cache,block.number);
        _name.push(key);
    }
    function getRouteNameByIndex(uint index) public constant returns(string){
        return _name[index];
    }
    function getRouteSize() public constant returns(uint){
        return _name.length;
    }
}