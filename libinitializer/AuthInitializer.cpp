#include "AuthInitializer.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-executor/src/precompiled/extension/CommitteeBin.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "libinitializer/Common.h"

void bcos::initializer::AuthInitializer::init(protocol::BlockNumber _number,
    const std::shared_ptr<ProtocolInitializer>& _protocol,
    const std::shared_ptr<tool::NodeConfig>& _nodeConfig, const protocol::Block::Ptr& block)
{
    // hex bin code to bytes
    bytes code;
    boost::algorithm::unhex(
        _nodeConfig->smCryptoType() ? committeeSmBin : committeeBin, std::back_inserter(code));


    // constructor (address[] initGovernors,    = [authAdminAddress]
    //        uint32[] memory weights,          = [1]
    //        uint8 participatesRate,           = 0
    //        uint8 winRate)                    = 0
    auto authAdmin = Address(_nodeConfig->authAdminAddress());
    std::vector<Address> initGovernors({authAdmin});
    std::vector<string32> weights({bcos::codec::toString32(h256(1))});
    INITIALIZER_LOG(INFO) << LOG_BADGE("AuthInitializer")
                          << LOG_KV("authAdminAddress", _nodeConfig->authAdminAddress());

    // bytes code + abi encode constructor params
    codec::abi::ContractABICodec abi(*_protocol->cryptoSuite()->hashImpl());
    bytes input = code + abi.abiIn("", initGovernors, weights, codec::toString32(h256(0)),
                             codec::toString32(h256(0)));

    auto tx = _protocol->blockFactory()->transactionFactory()->createTransaction(0,
        std::string(precompiled::AUTH_COMMITTEE_ADDRESS), input, u256(_number).str(), 500,
        _nodeConfig->chainId(), _nodeConfig->groupId(), utcTime());
    tx->forceSender(authAdmin.asBytes());
    block->appendTransaction(tx);
}
