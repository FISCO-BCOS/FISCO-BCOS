#include "bcos-framework/protocol/Protocol.h"

#include "bcos-utilities/BoostLog.h"
#include <fmt/compile.h>
#include <fmt/format.h>
#include <boost/algorithm/string.hpp>

namespace bcos::protocol
{
int versionCompareTo(std::variant<uint32_t, BlockVersion> const& _v1, BlockVersion const& _v2)
{
    int flag = 0;
    std::visit(
        [&_v2, &flag](auto&& arg) {
            auto ver1 = static_cast<uint32_t>(arg);
            auto ver2 = static_cast<uint32_t>(_v2);
            flag = ver1 > ver2 ? 1 : -1;
            flag = (ver1 == ver2) ? 0 : flag;
        },
        _v1);
    return flag;
}

std::ostream& operator<<(std::ostream& out, bcos::protocol::BlockVersion version)
{
    if (version == bcos::protocol::BlockVersion::RC4_VERSION)
    {
        out << RC4_VERSION_STR;
        return out;
    }

    auto versionNumber = static_cast<uint32_t>(version);
    auto num1 = (versionNumber >> 24) & (0xff);
    auto num2 = (versionNumber >> 16) & (0xff);
    auto num3 = (versionNumber >> 8) & (0xff);

    out << fmt::format(FMT_COMPILE("{}.{}.{}"), num1, num2, num3);
    return out;
}

std::ostream& operator<<(std::ostream& _out, NodeType const& _nodeType)
{
    switch (_nodeType)
    {
    case NodeType::NONE:
        _out << "None";
        break;
    case NodeType::CONSENSUS_NODE:
        _out << "CONSENSUS_NODE";
        break;
    case NodeType::OBSERVER_NODE:
        _out << "OBSERVER_NODE";
        break;
    case NodeType::LIGHT_NODE:
        _out << "LIGHT_NODE";
        break;
    case NodeType::FREE_NODE:
        _out << "NODE_OUTSIDE_GROUP";
        break;
    default:
        _out << "Unknown";
        break;
    }
    return _out;
}

std::optional<ModuleID> stringToModuleID(const std::string& _moduleName)
{
    if (boost::iequals(_moduleName, "raft"))
    {
        return bcos::protocol::ModuleID::Raft;
    }
    else if (boost::iequals(_moduleName, "pbft"))
    {
        return bcos::protocol::ModuleID::PBFT;
    }
    else if (boost::iequals(_moduleName, "amop"))
    {
        return bcos::protocol::ModuleID::AMOP;
    }
    else if (boost::iequals(_moduleName, "block_sync"))
    {
        return bcos::protocol::ModuleID::BlockSync;
    }
    else if (boost::iequals(_moduleName, "txs_sync"))
    {
        return bcos::protocol::ModuleID::TxsSync;
    }
    else if (boost::iequals(_moduleName, "cons_txs_sync"))
    {
        return bcos::protocol::ModuleID::ConsTxsSync;
    }
    else if (boost::iequals(_moduleName, "light_node"))
    {
        return bcos::protocol::ModuleID::LIGHTNODE_GET_BLOCK;
    }
    else
    {
        return std::nullopt;
    }
}

std::string moduleIDToString(ModuleID _moduleID)
{
    switch (_moduleID)
    {
    case ModuleID::PBFT:
        return "pbft";
    case ModuleID::Raft:
        return "raft";
    case ModuleID::BlockSync:
        return "block_sync";
    case ModuleID::TxsSync:
        return "txs_sync";
    case ModuleID::ConsTxsSync:
        return "cons_txs_sync";
    case ModuleID::AMOP:
        return "amop";
    case ModuleID::LIGHTNODE_GET_BLOCK:
    case ModuleID::LIGHTNODE_GET_TRANSACTIONS:
    case ModuleID::LIGHTNODE_GET_RECEIPTS:
    case ModuleID::LIGHTNODE_GET_STATUS:
    case ModuleID::LIGHTNODE_SEND_TRANSACTION:
    case ModuleID::LIGHTNODE_CALL:
    case ModuleID::LIGHTNODE_GET_ABI:
        return "light_node";
    case ModuleID::SYNC_GET_TRANSACTIONS:
        return "sync_get";
    case ModuleID::SYNC_PUSH_TRANSACTION:
        return "sync_push";
    case ModuleID::TREE_PUSH_TRANSACTION:
        return "tree_push";
    default:
        BCOS_LOG(DEBUG) << LOG_BADGE("unrecognized module") << LOG_KV("moduleID", _moduleID);
        return "unrecognized module";
    };
}
}  // namespace bcos::protocol