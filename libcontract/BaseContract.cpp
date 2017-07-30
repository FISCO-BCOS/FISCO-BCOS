/******************************************************************************
 * Copyright (c) 2012-2020, Juzhen TECHNOLOGIES (SHENZHEN) LTD.
 * File        : BaseContract.cpp
 * Version     : ethereum
 * Description : -

 * modification history
 * --------------------
 * author: fuxideng 2017-3-2 8:54:43
 * --------------------
*******************************************************************************/

#include "BaseContract.h"

using namespace dev;
using namespace contract;

BaseContract::BaseContract(eth::Client * client, dev::Address contractAddr) : m_client(client), m_contractAddr(contractAddr)
{
     m_paramNum = 0;
}

BaseContract::~BaseContract()
{

}

BaseContract* BaseContract::setFunction(const std::string& name)
{
    if (!name.empty())
    {
        m_functionName = name;
    }

    return this;
}

BaseContract* BaseContract::addParam(int value)
{
    m_fixedParams[m_paramNum++] = h256(value).asBytes();
    return this;
}

BaseContract* BaseContract::addParam(const dev::Address& value)
{
    m_fixedParams[m_paramNum++] = bytes(16, 0) + value.asBytes();
    return this;
}

BaseContract* BaseContract::addParam(const std::string& value)
{
	bytes data = h256(u256(value.size())).asBytes();
	data.resize(data.size() + (value.size() + 31) / 32 * 32);
	bytesConstRef(&value).populate(bytesRef(&data).cropped(32));

	m_dynamicParams[m_paramNum++] = data;
	return this;
}

const bytes& BaseContract::call()
{
    m_res.output.clear();
    if (m_client == NULL || m_contractAddr.data() == NULL || m_functionName.empty())
    {
        return m_res.output;
    }
    
    dev::bytes request = dev::sha3(m_functionName).ref().cropped(0, 4).toBytes();
    int offset = 32 * (m_fixedParams.size() + m_dynamicParams.size());
    for (int i=0; i<m_paramNum; ++i)
    {
        if (m_fixedParams.find(i) != m_fixedParams.end())
        {
            request += m_fixedParams[i];
        }
        else 
        {
            request += h256(u256(offset)).asBytes();
            offset += m_dynamicParams[i].size();
        }
    }

    for (std::map<int, dev::bytes>::iterator itr = m_dynamicParams.begin(); itr != m_dynamicParams.end(); ++itr)
    {
        request += itr->second;
    }
    
	try
	{
		m_res = m_client->call(m_contractAddr, request, 99999999);
		reset();
	}
	catch(...)
	{
	    reset();
		return m_res.output;
	}

    return response();
}

int BaseContract::call(int& result)
{
	result = -1;
    m_res.output.clear();
    if (m_client == NULL || m_contractAddr.data() == NULL || m_functionName.empty())
    {
        return -1;
    }
    
    dev::bytes request = dev::sha3(m_functionName).ref().cropped(0, 4).toBytes();
    int offset = 32 * (m_fixedParams.size() + m_dynamicParams.size());
    for (int i=0; i<m_paramNum; ++i)
    {
        if (m_fixedParams.find(i) != m_fixedParams.end())
        {
            request += m_fixedParams[i];
        }
        else 
        {
            request += h256(u256(offset)).asBytes();
            offset += m_dynamicParams[i].size();
        }
    }

    for (std::map<int, dev::bytes>::iterator itr = m_dynamicParams.begin(); itr != m_dynamicParams.end(); ++itr)
    {
        request += itr->second;
    }
    
	try
	{
		m_res = m_client->call(m_contractAddr, request, 99999999);
		if (0 != m_client->getResultInt(m_res, result))
        {
            reset();
            return -1;
        }
        
        reset();
		
	}
	catch(...)
	{
	    reset();
		return -1;
	}

    return 0;
}


const bytes& BaseContract::response()
{
    return m_res.output;
}

void BaseContract::reset()
{
    m_functionName = "";
    m_paramNum = 0;
    m_fixedParams.clear();
    m_dynamicParams.clear();
}

