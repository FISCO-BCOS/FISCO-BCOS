pragma solidity ^0.4.4;

contract Set{
    event Warn(uint code,uint setid,string msg);
    
    bytes32[] public m_users;
    mapping(bytes32=>uint) m_usermap;
    
    address[] public m_nodelist;
    
    uint public m_setid;    
    uint public m_maxnum;
    uint public m_warnnum;

    uint public m_status;
    
   function Set(uint maxnum,uint warnnum){
       m_maxnum=maxnum;
       m_warnnum=warnnum;
       m_status=0;
   }
  
    function setId(uint id)public {
        m_setid=id;
    }
    function registerRoute(bytes32 user) public returns(bool){
        if( isIn(user) )
            return true;
            
        if( isFull() )
            return false;
            
        m_users.push(user);
        m_usermap[user]=block.number;
        return true;
    }
    
    function addNode(address node) public{
        m_nodelist.push(node);
        
    }
    
    
    function removeNode(address node) public{
        for( var i=0;i<m_nodelist.length;i++){
            if(m_nodelist[i] == node ){
                if( m_nodelist.length> 0 )
                    m_nodelist[i]=m_nodelist[m_nodelist.length-1];
                    
                m_nodelist.length--;
                
            }
            
        }
    }
    
    function setStatus(uint status) public {
        m_status=status;
    }
    function nodeList()constant public returns(address[]){
        return m_nodelist;
    }
    function userList()constant public returns(bytes32[]){
        return m_users;
    }
    
    function getNodeListNum() constant public returns(uint){        
        return m_nodelist.length;
    }
    function getUsersNum() constant public returns(uint){        
        return m_users.length;
    }

    function isFull() constant public returns(bool){
        if( m_users.length >=m_warnnum ){
            Warn(1,m_setid,"SET Touch Warnnum");
        }
        
        if( m_users.length >= m_maxnum )
            return true;
            
        return false;
    }
    
    function isIn(bytes32 user)constant public returns(bool){
        if( m_usermap[user] >0  )
            return true;
        
        return false;
    }
    
}
                                                                                    
