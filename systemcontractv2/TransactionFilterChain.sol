pragma solidity ^0.4.4;
import "TransactionFilterBase.sol";

contract TransactionFilterChain {
    address[] private filters; //filters列表
    
    function process(address origin, address from, address to, string func, string input) public constant returns(bool) {
        for(var i=0; i<filters.length; ++i) {
            if(!TransactionFilterBase(filters[i]).process(origin, from, to, func, input)) {
                return false;
            }
        }
        
        return true;
    }
    
    function addFilter(address filter/*, string desc*/) public returns(uint) {
        filters.push(filter);
        
        return filters.length - 1;
    }
    
    function delFilter(uint index) public {
        if(filters.length == 0) {
            return;
        }
        
        for(uint i = index; i < filters.length - 1; ++i) {
            filters[i] = filters[i + 1];
        }
        
        --filters.length;
    }
    
    function setFilter(uint index, address filter) public {
        filters[index] = filter;
    }
    
    function getFilter(uint index) public constant returns(address) {
        return filters[index];
    }
    
    function getFiltersLength() public constant returns(uint) {
        return filters.length;
    }
}