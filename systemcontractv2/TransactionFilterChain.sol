pragma solidity ^0.4.4;
import "TransactionFilterBase.sol";
import "AuthorityFilter.sol";

contract TransactionFilterChain {
    address[] private filters;                  //filters list
    
    function process(address origin, address from, address to, string func, string input) public constant returns(bool) {
        for(uint256 i=0; i<filters.length; ++i) {
            if(!TransactionFilterBase(filters[i]).process(origin, from, to, func, input)) {
                return false;
            }
        }
        return true;
    }

    function deploy(address origin) public constant returns(bool) {
        for(uint256 i=0; i<filters.length; ++i) {
            if(!TransactionFilterBase(filters[i]).deploy(origin)) {
                return false;
            }
        }
        return true;
    }
    
    function addFilter(address filter) public returns(uint256) {
        filters.push(filter);
        return filters.length - 1;
    }
	
    function addFilterAndInfo(string name, string version ,string desc) public returns(uint256) {
        address filter = new AuthorityFilter();
        AuthorityFilter(filter).setName(name);
        AuthorityFilter(filter).setVersion(version);
        AuthorityFilter(filter).setDesc(desc);
        filters.push(filter);
        
        return filters.length - 1;
    }
    
    function delFilter(uint256 index) public {
        if(filters.length == 0) {
            return;
        }
        
        for(uint256 i = index; i < filters.length - 1; ++i) {
            filters[i] = filters[i + 1];
        }
        
        --filters.length;
    }
    
    function setFilter(uint256 index, address filter) public {
        filters[index] = filter;
    }
    
    function getFilter(uint256 index) public constant returns(address) {
        return filters[index];
    }
    
    function getFiltersLength() public constant returns(uint256) {
        return filters.length;
    }
}
