pragma solidity ^0.4.4;

import "Set.sol";

contract RouteManger{
    uint public m_chainid;
    string public m_chainname;
    
    address[] public m_sets;
    string[] public m_setNames;
    mapping(bytes32 => mapping(string=>uint)) m_users;
    
    uint public m_nowset;

    event registerRetLog(bool ok,uint set);
    
    function RouteManger(uint chainid,string chainname){
        m_chainid=chainid;
        m_chainname=chainname;
    }
    
    function registerSet(address set, string name) public {
        m_sets.push(set);
		m_setNames.push(name);
        Set(set).setId(m_sets.length-1);
    }
    
    function registerRoute(bytes32 user) public returns(bool) {
        if( m_users[user]["blocknumber"] > 0 ) {
			registerRetLog(true, m_users[user]["setid"]);
			return true;
		}
       
        while( m_nowset < m_sets.length ){
            Set set=Set(m_sets[m_nowset]);
            if( set.isFull() ){
                m_nowset++;
            }
            else{
                bool register=set.registerRoute(user);
                if( register ){
                    m_users[user]["blocknumber"]=block.number;
                    m_users[user]["setid"]=m_nowset;
		    		registerRetLog(true, m_users[user]["setid"]);
                    return true;
                }
            }
            
        }
        
        
		registerRetLog(false, 0);
        return false;
        
    }

    function getSetAddress(uint idx) constant public returns (bool, address) {
    	if (idx >= m_sets.length) {
    		return (false, 0x0000000000000000000000000000000000000000);
    	}

    	return (true, m_sets[idx]);
    }

    function getRoute(bytes32 user) constant public returns(bool,uint){
        if( m_users[user]["blocknumber"] > 0 )
            return (true,m_users[user]["setid"]);
            
        return (false,0);
    }
    
    function getSetsNum() constant public returns(uint){
        
        return m_sets.length;
    }
    
}
