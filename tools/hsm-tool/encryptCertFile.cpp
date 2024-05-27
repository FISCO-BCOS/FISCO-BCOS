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
 * @file encryptCertFile.cpp
 * @author: lucasli
 * @date 2023-02-23
 */

#include "boost/filesystem.hpp"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-security/bcos-security/HsmDataEncryption.h>
#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/throw_exception.hpp>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

using namespace std;
using namespace bcos;

namespace fs = boost::filesystem;
namespace po = boost::program_options;

po::options_description main_options("Main for encrypt ssl cert file with HSM");

po::variables_map initCommandLine(int argc, const char* argv[])
{
    main_options.add_options()("help,h", "help of encrypt ssl cert file with HSM")(
        "path,p", po::value<string>()->default_value(""), "[CertFilePath]")("config,c",
        po::value<std::string>()->default_value("./config.ini"),
        "config file path, eg. config.ini");
    po::variables_map vm;
    try
    {
        po::store(po::parse_command_line(argc, argv, main_options), vm);
        po::notify(vm);
    }
    catch (...)
    {
        std::cout << "invalid input" << std::endl;
        exit(0);
    }
    if (vm.count("help") || vm.count("h"))
    {
        std::cout << main_options << std::endl;
        exit(0);
    }
    return vm;
}

int main(int argc, const char* argv[])
{
    boost::property_tree::ptree pt;
    auto params = initCommandLine(argc, argv);
    auto certFilePath = params["path"].as<string>();
    auto configPath = params["config"].as<string>();
    if (!fs::exists(certFilePath) || !fs::exists(configPath))
    {
        std::cout << "the certFilePath or the configPath is empty, certFilePath: " << certFilePath
                  << ", configPath: " << configPath << std::endl;
        return 0;
    }

    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>(keyFactory);
    nodeConfig->loadConfig(configPath);
    std::cout << "loadConfig success. hsmLibPath: " << nodeConfig->hsmLibPath()
              << ", encKeyIndex: " << nodeConfig->encKeyIndex() << std::endl;
    auto dataEncryption = std::make_shared<bcos::security::HsmDataEncryption>(nodeConfig);

    // copy cert file
    ifstream certFile(certFilePath);
    std::string copyFileName = certFilePath + ".bak";
    ofstream copyCertFile(copyFileName);
    copyCertFile << certFile.rdbuf();
    copyCertFile.close();
    certFile.close();

    // encrypt cert file
    auto encContents = dataEncryption->encryptFile(certFilePath);
    std::string encContentsStr((char*)encContents->data(), encContents->size());
    ofstream encCertFile(certFilePath, std::ios::trunc);
    encCertFile << encContentsStr;
    encCertFile.close();

    std::cout << "Encrypt success! Cert file: " << certFilePath << ", copy cert file as "
              << copyFileName << std::endl;
    return 0;
}