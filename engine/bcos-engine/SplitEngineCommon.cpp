/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "SplitEngineCommon.h"

namespace bcos::engine::split_detail
{

bool isGetPayloadVersionCompatible(ApiVersion requestVersion, std::uint32_t payloadVersion)
{
    switch (requestVersion)
    {
    case ApiVersion::V1:
        return payloadVersion == 1;
    case ApiVersion::V2:
        return payloadVersion <= 2;
    case ApiVersion::V3:
        return payloadVersion <= 3;
    case ApiVersion::V4:
        return payloadVersion <= 4;
    case ApiVersion::V5:
        return payloadVersion == 3;
    }
    return false;
}

}  // namespace bcos::engine::split_detail
