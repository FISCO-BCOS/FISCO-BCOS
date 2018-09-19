pragma solidity ^0.4.4;

contract Node{
    string public m_id;
    string public m_ip;
    uint    public m_p2pport;
    uint    public m_rpcport;
    uint    public m_type;
    string public m_desc;
    string public m_cahash;
    string public m_agencyinfo;
    
    function Node(string id,string ip,uint p2pport,uint rpcport,uint t,string desc,string cahash,string agencyinfo){
        m_id=id;
        m_ip=ip;
        m_p2pport=p2pport;
        m_rpcport=rpcport;
        m_type=t;
        m_desc=desc;
        m_cahash=cahash;
        m_agencyinfo=agencyinfo;
    }
}
                                                                                    