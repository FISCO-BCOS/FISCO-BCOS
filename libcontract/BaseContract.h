/******************************************************************************
 * Copyright (c) 2012-2020, Juzhen TECHNOLOGIES (SHENZHEN) LTD.
 * File        : BaseContract.h
 * Version     : ethereum
 * Description : -

 * modification history
 * --------------------
 * author: fuxideng 2017-3-2 8:52:55
 * --------------------
*******************************************************************************/
#ifndef __ETHEREUM_BASECONTRACT_H
#define __ETHEREUM_BASECONTRACT_H

/******************************************************************************
 * BaseContract - 内部合约访问的基础类
 * DESCRIPTION: 
 *         1. 提供合约的基础访问能力
 *         2. 其他合约从该合约继承定义具体的成员和实现访问方法

 * modification history
 * --------------------
 * author: fuxideng 2017-3-2 8:53:13
 * --------------------
*******************************************************************************/

#include <libethereum/Client.h>

namespace contract
{
class BaseContract
{
public:
    BaseContract(dev::eth::Client * client, dev::Address contractAddr);
    virtual ~BaseContract();
    
public:
    BaseContract* setFunction(const std::string& name);
    BaseContract* addParam(int value);
    BaseContract* addParam(const dev::Address& value);
    BaseContract* addParam(const std::string& value);
    const dev::bytes& call();
	int call(int& result);
    const dev::bytes& response();
    void reset();
    
private:
    dev::eth::Client * m_client;
    dev::Address m_contractAddr;
    std::string m_functionName;
    int m_paramNum;
    std::map<int, dev::bytes> m_fixedParams;
    std::map<int, dev::bytes> m_dynamicParams;
    dev::eth::ExecutionResult m_res;
};
}

#endif  /* __ETHEREUM_BASECONTRACT_H */

