/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief scale codec
 * @file Scale.h
 */
#pragma once
#include "Common.h"
#include "ScaleDecoderStream.h"
#include "ScaleEncoderStream.h"
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp>
#include <gsl/span>
#include <vector>

namespace bcos
{
namespace codec
{
namespace scale
{
/**
 * @brief convenience function for encoding primitives data to stream
 * @tparam Args primitive types to be encoded
 * @param args data to encode
 * @return encoded data
 */
template <typename... Args>
void encode(std::shared_ptr<bytes> _encodeData, Args&&... _args)
{
    ScaleEncoderStream s;
    (s << ... << std::forward<Args>(_args));
    *_encodeData = s.data();
}

template <typename... Args>
bytes encode(Args&&... _args)
{
    ScaleEncoderStream s;
    (s << ... << std::forward<Args>(_args));
    return s.data();
}

/**
 * @brief convenience function for decoding primitives data from stream
 * @tparam T primitive type that is decoded from provided span
 * @param span of bytes with encoded data
 * @return decoded T
 */
template <class T>
void decode(T& _decodedObject, gsl::span<byte const> _span)
{
    ScaleDecoderStream s(_span);
    s >> _decodedObject;
}

template <class T>
T decode(gsl::span<byte const> _span)
{
    T t;
    decode(t, _span);
    return t;
}
}  // namespace scale
}  // namespace codec
}  // namespace bcos
