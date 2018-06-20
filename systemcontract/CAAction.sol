pragma solidity ^0.4.4;

import "Base.sol";
import "StringTool.sol";


contract CAAction is Base,StringTool {
    struct CaInfo
      {
        string  serial;  
        string pubkey;
        string name;  
        uint    blocknumber;  
      }

    mapping(string =>CaInfo) m_cadata;
    string[] m_serials;
    address private m_systemproxy;

    function CAAction(address _systemproxy) {
         m_systemproxy = _systemproxy;
    }
   
    function add(string _serial,string _pubkey,string _name) {
        bool find=false;
        for( uint i=0;i<m_serials.length;i++){
            if( equal(_serial , m_serials[i]) ){
                find=true;
                break;
            }
        }
        if( false == find ){
            m_serials.push(_serial);
            m_cadata[_serial]=CaInfo(_serial,_pubkey,_name,block.number);
        }
       
    }
   
    function remove(string _serial) returns(bool){

        for( uint i=0;i<m_serials.length;i++){
            if( equal(_serial , m_serials[i]) ){
               if( m_serials.length > 1 ){
                    m_serials[i]=m_serials[m_serials.length-1];
               }
               m_serials.length--;
               delete m_cadata[_serial];
               return true;
            }
        }
        
        return false;
    }

    function get(string _serial) constant returns(string,string,string,uint){
        return (m_cadata[_serial].serial,m_cadata[_serial].pubkey,m_cadata[_serial].name,m_cadata[_serial].blocknumber);
    }
    function getHashsLength() constant returns(uint){
        return m_serials.length;
    }
    function getHash(uint _index) constant returns(string){
        return m_serials[_index];
    }
}