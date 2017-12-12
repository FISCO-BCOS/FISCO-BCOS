pragma solidity ^0.4.4;

import "Base.sol";
import "StringTool.sol";


contract CAAction is Base,StringTool {
    struct CaInfo
      {
        string  hash;  // 节点机构证书哈希
        string pubkey;// 公钥
        string orgname;  // 机构名称
        uint notbefore;
        uint notafter;
        CaStatus status;
        string    whitelist;    //;分割
        string    blacklist;

        uint    blocknumber;  

        
      }

    mapping(string =>CaInfo) m_cadata;
    string[] m_hashs;
    
    
    function CAAction(address _systemproxy) {
       
        
        
    }
   

    //更新证书信息
    function update (string _hash,string _pubkey,string _orgname,uint _notbefore,uint _notafter,CaStatus _status,string _whitelist,string _blacklist)  returns(bool) {
        
        
        bool find=false;
        for( uint i=0;i<m_hashs.length;i++){
            if( equal(_hash , m_hashs[i]) ){
                find=true;
                break;
            }
        }
        if( find == false ){
            m_hashs.push(_hash);
        }
        
        
        m_cadata[_hash]=CaInfo(_hash,_pubkey,_orgname,_notbefore,_notafter,_status,_whitelist,_blacklist,block.number);

        return true;
       
    }
   
   //更新证书状态
    function updateStatus(string _hash,CaStatus _status) returns(bool){

        bool find=false;
        for( uint i=0;i<m_hashs.length;i++){
            if( equal(_hash , m_hashs[i]) ){
                find=true;
                break;
            }
        }
        if( find == false ){
            return false;
        }
        
        m_cadata[_hash].status=_status;

        return true;
    }

    //查询节点信息
    function get(string _hash) constant returns(string,string,string,uint,uint,CaStatus,uint){
        return (m_cadata[_hash].hash,m_cadata[_hash].pubkey,m_cadata[_hash].orgname,m_cadata[_hash].notbefore,m_cadata[_hash].notafter,m_cadata[_hash].status,m_cadata[_hash].blocknumber);
    }
    //查询节点ip列表
    function getIp(string _hash) constant returns(string,string){
        return (m_cadata[_hash].whitelist,m_cadata[_hash].blacklist);
    }
    



    function getHashsLength() constant returns(uint){
        return m_hashs.length;
    }
    function getHash(uint _index) constant returns(string){
        return m_hashs[_index];
    }
}