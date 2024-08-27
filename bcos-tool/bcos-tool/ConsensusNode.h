#pragma once
#include <bcos-utilities/Common.h>
#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/serialization/version.hpp>
#include <string>
#include <vector>

namespace bcos::ledger
{
struct ConsensusNode
{
    ConsensusNode() = default;
    ConsensusNode(std::string _nodeID, u256 _weight, std::string _type, std::string _enableNumber,
        uint64_t _termWeight)
      : nodeID(std::move(_nodeID)),
        voteWeight(std::move(_weight)),
        type(std::move(_type)),
        enableNumber(std::move(_enableNumber)),
        termWeight(_termWeight)
    {}

    friend class boost::serialization::access;
    std::string nodeID;
    u256 voteWeight;
    std::string type;
    std::string enableNumber;
    uint64_t termWeight = 0;

    template <typename Archive>
    void serialize(Archive& archive, unsigned int version)
    {
        archive & nodeID;
        archive & voteWeight;
        archive & type;
        archive & enableNumber;

        if (version > 0)
        {
            if constexpr (Archive::is_saving::value)
            {
                if (termWeight > 0)
                {
                    archive & termWeight;
                }
            }
            else
            {
                archive & termWeight;
            }
        }
    }
};

using ConsensusNodeList = std::vector<ConsensusNode>;

inline ConsensusNodeList decodeConsensusList(const std::string_view& value)
{
    boost::iostreams::stream<boost::iostreams::array_source> inputStream(
        value.data(), value.size());
    boost::archive::binary_iarchive archive(inputStream,
        boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking);

    ConsensusNodeList consensusList;
    archive >> consensusList;

    return consensusList;
}

inline std::string encodeConsensusList(const ConsensusNodeList& consensusList)
{
    std::string value;
    boost::iostreams::stream<boost::iostreams::back_insert_device<std::string>> outputStream(value);
    boost::archive::binary_oarchive archive(outputStream,
        boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking);

    archive << consensusList;
    outputStream.flush();

    return value;
}

}  // namespace bcos::ledger

BOOST_CLASS_VERSION(bcos::ledger::ConsensusNode, 1)