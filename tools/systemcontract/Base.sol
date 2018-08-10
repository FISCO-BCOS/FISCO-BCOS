pragma solidity ^0.4.4;

/*
import "SystemProxy.sol";
import "SystemAction.sol";
*/


contract Base{
    event LogMessage(address addr, uint code,string msg); // Event

	/*
    address  m_systemproxy;

    modifier onlySystemAdmin(){
   
        if(SystemAction(SystemProxy(m_systemproxy).getSystemAction()).isAdmin(msg.sender)){
            _;
        }
            
  
    }
    */

    enum Role{
        None,
        General,
        GroupAdmin,
        SystemAdmin,
        Max
    }
    // 所有权限 、赋帐号权限的权限 、部署合约权限 、普通交易权限、查询权限
    enum PermissionFlag{
        All,
        Grant,//goupadmin  拥有
        Deploy,
        Tx,
        Call
    }
    enum FilterType{
        Account,
        Node
    }
    enum FilterCheckType{
        CheckDeploy,
        CheckTx,
        CheckCall
    }

    //检查场景
    enum FilterCheckScene{
        None,
        CheckDeploy,
        CheckTx,
        CheckCall,
        CheckDeployAndTxAndCall,
        PackTranscation,//打包交易场景  要校验 accountfilter、干预filter 要处理
        ImportBlock,     //bc import 新块  要校验 accountfilter、干预filter 要处理
        BlockExecuteTransation // Block::execute 执行交易  通用入口
    }

    enum FilterCheckCode{
        Ok,
        NoDeployPermission,
        NoTxPermission,
        NoCallPermission,
        NoGraoupAdmin,  //不是组管理员帐号
        NoAdmin,        //不是链管理员帐号
        InterveneAccount,//帐号被干预
        InterveneContract,//合约被干预
        InterveneContractFunc,//合约接口被干预
        NodeNoRegister, //节点未登记
        NodeNoregister, //节点未登记
        NodeCAError,     //节点机构证书无效
        NodeCANoExist,   //节点机构证书不存在
        NodeSignError,  //节点签名错误
        Other
    }

    //节点类型
    enum NodeType{
        None,
        Core,   // 核心 
        Full,   // 全节点 
        Light   //  轻节点 
    }

    enum CaStatus{
        Invalid,    //失效
        Ok       //有效
    }

    
}