pragma solidity ^0.4.4;
import "Base.sol";


contract ConfigAction is Base {
    struct Item {
        string value;
        uint blockNumber;
    }

   
    function ConfigAction(){        
              
    }
    function get(string key) public constant returns(string, uint) {
        return (data[key].value, data[key].blockNumber);
    }
    
    function set(string key, string value) public  {
        if(data[key].blockNumber == 0) {
            allItems.push(key);
        }
        
        data[key].value = value;
        data[key].blockNumber = block.number;
    }
    
    
    string[] public allItems;
    mapping(string => Item) data;
}