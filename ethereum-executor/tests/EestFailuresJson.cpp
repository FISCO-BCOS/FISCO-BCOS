/**
 * @file EestFailuresJson.cpp
 * @brief Implementation of structured failure serialization for the EEST runner.
 */
#include "EestFailuresJson.h"

namespace bcos::test
{
std::string writeFailuresJson(std::vector<FailureDetail> const& details)
{
    Json::Value root(Json::arrayValue);
    for (auto const& d : details)
    {
        Json::Value obj(Json::objectValue);
        obj["testName"] = d.testName;
        obj["forkName"] = d.forkName;
        obj["reason"] = d.reason;
        obj["category"] = d.category;
        obj["dataIndex"] = d.dataIndex;
        obj["gasIndex"] = d.gasIndex;
        obj["valueIndex"] = d.valueIndex;
        root.append(obj);
    }
    Json::StyledWriter writer;
    return writer.write(root);
}

std::string firstMismatchCategory(std::string const& errorStr)
{
    static const char* const markers[] = {
        "    NONCE ", "    BALANCE ", "    CODE ", "    STORAGE "};
    static const char* const categories[] = {"nonce", "balance", "code", "storage"};

    size_t bestPos = std::string::npos;
    int bestIdx = -1;
    for (int i = 0; i < 4; ++i)
    {
        auto pos = errorStr.find(markers[i]);
        if (pos != std::string::npos && (bestPos == std::string::npos || pos < bestPos))
        {
            bestPos = pos;
            bestIdx = i;
        }
    }
    return (bestIdx >= 0) ? std::string(categories[bestIdx]) : std::string{};
}

}  // namespace bcos::test
