pragma solidity ^0.4.4;

import "Base.sol";
import "StringTool.sol";
import "SystemProxy.sol";
import "ContractAbiMgr.sol";
import "ConsensusControlMgr.sol";
import "ConsensusControlInterface.sol";
import "LibEthLog.sol";


contract NodeActionV3 is Base ,StringTool{
    using LibEthLog for *;

    struct NodeInfo{
        string  id;       
        string  name;
        string  agency;      
        string  caHash;     
        uint    idx;
        uint    blockNumber;
    }

    mapping(string =>NodeInfo) m_nodeData;
    string[] m_nodeIds;
    address private systemAddr;

    function NodeActionV3() {        
    }
   
    function setSystemAddr(address addr) {
        systemAddr = addr;
    }

    function updateIdx() internal {
        uint startCore=0;
        for( uint i=0;i<m_nodeIds.length;i++){
            m_nodeData[m_nodeIds[i]].idx=startCore++;            
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
                        return consensusControl.addNode(stringToBytes32(nodeinfo.agency));
                    } else {
                        return consensusControl.delNode(stringToBytes32(nodeinfo.agency));
                    }
                }
            }
            // 添加其他的在节点注册退出阶段时候回调功能
        }
        return true;
    }
    function registerNode (string _id,string _name, string _agency,string _caHash)  returns(bool) {
        
        
        bool find=false;
        for( uint i=0;i<m_nodeIds.length;i++){
            if( equal(_id , m_nodeIds[i]) ){
                find=true;
                break;
            }
        }
        if( find )
            return false;
        
        m_nodeIds.push(_id);
        var info = NodeInfo(_id,_name,_agency,_caHash,0,block.number);

        if (!nodeCallback(info, true))
            return false;

        m_nodeData[_id]=info;

        updateIdx();
        return true;
    }
    
    function cancelNode(string _id)  returns(bool) {
        
        if( m_nodeIds.length < 2 )
            return false;

        for( uint i=0;i<m_nodeIds.length;i++){
        
            if( equal(_id , m_nodeIds[i]) ){
                if (!nodeCallback(m_nodeData[_id], false))
                    return false;

                if( m_nodeIds.length > 0 ){
                    m_nodeIds[i]=m_nodeIds[m_nodeIds.length-1];
                }

                m_nodeIds.length--;
                delete m_nodeData[_id];
                 
                updateIdx();
                return true;
            }
        }        

        return false;
    }   

    function getNode(string _id) public constant returns(string,string,string,uint,uint){
            
        return (m_nodeData[_id].name, m_nodeData[_id].agency, m_nodeData[_id].caHash, m_nodeData[_id].idx, m_nodeData[_id].blockNumber);
    }

    function getNodeByIdx(uint _index) public constant returns(string,string,string,uint,uint) {
        var id = getNodeId(_index);
        return getNode(id);
    }

    function getNodeAgencyByIdx(uint _index) returns (bytes32) {
        var (,agency,,,) = getNodeByIdx(_index);
        return stringToBytes32(agency);
    }


    function getNodeIdx(string _id) public constant returns(uint){
            
        return (m_nodeData[_id].idx);
    }
    
    function getNodeIdsLength() public constant returns(uint){
        return m_nodeIds.length;
    }
    function getNodeId(uint _index) public constant returns(string){
        return m_nodeIds[_index];
    }
}