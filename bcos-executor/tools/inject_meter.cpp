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
 * @brief the tool of gas injector
 * @file inject_meter.cpp
 * @author: xingqiangbai
 * @date: 2021-11-01
 */

#include "src/binary-reader-ir.h"
#include "src/binary-reader.h"
#include "src/error-formatter.h"
#include "src/ir.h"
#include "src/option-parser.h"
#include "src/stream.h"
#include "src/validator.h"
#include "src/wast-lexer.h"
#include "vm/gas_meter/GasInjector.h"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace std;
using namespace bcos;
using namespace wabt;

static std::unique_ptr<FileStream> s_log_stream;

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        cerr << "The number of parameters not equal to 2" << endl;
        return 0;
    }
    std::vector<uint8_t> file_data;
    auto result = wabt::ReadFile(argv[1], &file_data);
    filesystem::path p(argv[1]);
    if (Succeeded(result))
    {
        wasm::GasInjector injector(wasm::GetInstructionTable());
        auto ret = injector.InjectMeter(file_data);
        if (ret.status == wasm::GasInjector::Status::Success)
        {
            Errors errors;
            Module module;
            const bool kStopOnFirstError = true;
            // s_log_stream = FileStream::CreateStdout();
            auto defaultFeature = Features();
            ReadBinaryOptions options(
                defaultFeature, s_log_stream.get(), true, kStopOnFirstError, true);
            result = ReadBinaryIr(p.filename().generic_string().c_str(),
                (const char*)ret.byteCode->data(), ret.byteCode->size(), options, &errors, &module);
            if (Succeeded(result))
            {
                ValidateOptions voptions(defaultFeature);
                result = ValidateModule(&module, &errors, voptions);
                if (result == Result::Ok)
                {
                    ofstream out("metric_" + p.filename().generic_string(),
                        std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);
                    out.write((const char*)ret.byteCode->data(), ret.byteCode->size());
                    out.close();
                    cout << "InjectMeter success" << endl;
                    return 0;
                }
            }
            cout << "validate failed" << endl;
            FormatErrorsToFile(errors, Location::Type::Binary);
        }
        cerr << "InjectMeter failed, reason:"
             << (ret.status == wasm::GasInjector::Status::InvalidFormat ? "invalid format" :
                                                                          "bad instruction")
             << endl;
    }
    cerr << "Read file failed" << endl;
    return -1;
}