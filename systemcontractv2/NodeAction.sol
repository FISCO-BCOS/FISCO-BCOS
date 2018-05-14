pragma solidity ^0.4.11;

import "Base.sol";
import "StringTool.sol";
import "SystemProxy.sol";
import "ContractAbiMgr.sol";
import "ConsensusControlMgr.sol";
import "ConsensusControlInterface.sol";
import "LibEthLog.sol";


contract NodeAction is Base, StringTool {
    using LibEthLog for *;
    
    struct NodeInfo {
        string  id;       
        string    ip;            
        uint    port;
        NodeType    category;
        string  desc;    // 节点描述 
        string  CAhash;  // 节点机构证书哈希
        string agencyinfo;   
        uint    idx;
        uint    blocknumber;
    }
    
    mapping(string =>NodeInfo) m_nodedata;
    string[] m_nodeids;
    address private systemAddr;
    
    function NodeAction() {   
    }

    function setSystemAddr(address addr) {
        systemAddr = addr;
    }

    //变更之后 更新idx
    function updateIdx() internal {
        uint startCore=0;//核心节点开始
        for( var i=0;i<m_nodeids.length;i++){
            if( NodeType.Core == m_nodedata[m_nodeids[i]].category)
                m_nodedata[m_nodeids[i]].idx=startCore++;            
        }
        for( var j=0;j<m_nodeids.length;j++){
            if( NodeType.Core != m_nodedata[m_nodeids[j]].category)
                m_nodedata[m_nodeids[j]].idx=startCore++;            
        }
                
    }

    function nodeCallback(NodeInfo nodeinfo, bool isRegister) internal returns (bool) {
        if (0x0 != systemAddr) {
            LibEthLog.INFO().append("[ConsensusControl] load systemAddr:").commit();
            SystemProxy systemProxy = SystemProxy(systemAddr);
            var (addr,,) = systemProxy.getRoute("ConsensusControlMgr");
            if (0x0 != addr) {
                LibEthLog.INFO().append("[ConsensusControl] load ConsensusControlMgr:").commit();
                ConsensusControlMgr mgr = ConsensusControlMgr(addr);
                address controlAddr = mgr.getAddr();
                if (0x0 != controlAddr) {
                    LibEthLog.INFO().append("[ConsensusControl] load controlAddr:").commit();
                    ConsensusControlInterface consensusControl = ConsensusControlInterface(controlAddr);
                    if (isRegister) {
                        return consensusControl.addNode(stringToBytes32(nodeinfo.agencyinfo));
                    } else {
                        return consensusControl.delNode(stringToBytes32(nodeinfo.agencyinfo));
                    }
                }
            }
            // 添加其他的在节点注册退出阶段时候回调功能
        }
        return true;
    }

    //登记节点信息
    function registerNode (string _id,string _ip,uint _port,NodeType _category,string _desc,string _CAhash,string _agencyinfo,uint _idx)  returns(bool) {     
        bool find=false;
        for( uint i=0;i<m_nodeids.length;i++){
            if( equal(_id , m_nodeids[i]) ){
                find=true;
                break;
            }
        }
        if( find )
            return false;

        m_nodeids.push(_id);
        var info = NodeInfo(_id,_ip,_port,_category,_desc,_CAhash,_agencyinfo,_idx,block.number);

        if (!nodeCallback(info, true))
            return false;

        m_nodedata[_id] = info;

        updateIdx();

        return true;
       
    }
    // 注销节点信息
    function cancelNode(string _id)  returns(bool) {
        
        if( m_nodeids.length < 2 )
            return false;

        for( uint i=0;i<m_nodeids.length;i++){
        
            if( equal(_id , m_nodeids[i]) ){

                if (!nodeCallback(m_nodedata[_id], false))
                    return false;
            
                if( m_nodeids.length > 0 ){
                    m_nodeids[i]=m_nodeids[m_nodeids.length-1];
                }

                m_nodeids.length--;
  
                delete m_nodedata[_id];
                
                updateIdx();
                return true;
            }
        }        

        

        return false;
    }   

  
    //查询节点信息
    function getNode(string _id) public constant returns(string,uint,NodeType,string,string,string,uint){
            
        return (m_nodedata[_id].ip,m_nodedata[_id].port,m_nodedata[_id].category,m_nodedata[_id].desc,m_nodedata[_id].CAhash ,m_nodedata[_id].agencyinfo,m_nodedata[_id].blocknumber);
    }

    function getNodeByIdx(uint _index) public constant returns(string,uint,NodeType,string,string,string,uint) {
        var id = getNodeId(_index);
        return getNode(id);
    }

    function getNodeAgencyByIdx(uint _index) returns (bytes32) {
        var (,,,,,agencyinfo,) = getNodeByIdx(_index);
        return stringToBytes32(agencyinfo);
    }

    function getNodeIdx(string _id) public constant returns(uint){
            
        return (m_nodedata[_id].idx);
    }
    
    function getNodeIdsLength() public constant returns(uint){
        return m_nodeids.length;
    }
    function getNodeId(uint _index) public constant returns(string){
        return m_nodeids[_index];
    }
}