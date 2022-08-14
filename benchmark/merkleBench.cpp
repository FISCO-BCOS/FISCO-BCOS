#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-protocol/ParallelMerkleProof.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/algorithm/hex.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>

using Hasher = bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher;

std::string generateTestData(int count)
{
    std::string buffer;
    buffer.resize(count * Hasher::HASH_SIZE);
#pragma omp parallel
    {
        Hasher hasher;
#pragma omp for
        for (int i = 0; i < count; ++i)
        {
            hasher.update(i);
            std::span<char> view(&buffer[i * Hasher::HASH_SIZE], Hasher::HASH_SIZE);
            hasher.final(view);
        }
    }

    return buffer;
}

void testOldMerkle(const std::vector<bcos::bytes>& datas)
{
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::SM3>(), nullptr, nullptr);
    auto timePoint = std::chrono::high_resolution_clock::now();

    auto root = bcos::protocol::calculateMerkleProofRoot(cryptoSuite, datas);

    auto duration = std::chrono::high_resolution_clock::now() - timePoint;
    std::cout << "Root[old]: " << root << " "
              << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << "ms"
              << std::endl;
}

void testNewMerkle(const std::vector<bcos::bytes>& datas)
{
    auto timePoint = std::chrono::high_resolution_clock::now();
    std::vector<bcos::bytes> out;

    bcos::crypto::merkle::Merkle<Hasher, 16> merkle;
    merkle.generateMerkle(datas, out);

    auto root = *(out.rbegin());
    std::string rootString;
    boost::algorithm::hex_lower(
        (char*)root.data(), (char*)(root.data() + root.size()), std::back_inserter(rootString));

    auto duration = std::chrono::high_resolution_clock::now() - timePoint;
    std::cout << "Root[new]: " << rootString << " "
              << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << "ms"
              << std::endl;
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description options("Merkle benchmark");

    // clang-format off
    options.add_options()
        ("type,t", boost::program_options::value<int>()->default_value(0), "0 for old merkle, 1 for new merkle")
        ("prepare,p", boost::program_options::value<int>()->default_value(0), "Prepare test data, count of hashes")
        ("filename,f", boost::program_options::value<std::string>()->default_value("merkle_test.data"), "Test data file name")
        ;
    // clang-format on
    boost::program_options::variables_map vm;
    boost::program_options::store(
        boost::program_options::parse_command_line(argc, argv, options), vm);

    if (vm.empty())
    {
        options.print(std::cout);
        return -1;
    }

    auto filename = vm["filename"].as<std::string>();
    auto count = vm["prepare"].as<int>();
    if (count)
    {
        auto data = generateTestData(count);
        std::ofstream fileOutput(
            filename, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
        fileOutput << count;
        fileOutput << data;
        fileOutput.close();

        std::cout << "Write " << count << " data successed!" << std::endl;

        return 0;
    }

    std::ifstream fileInput(filename, std::ios_base::in | std::ios_base::binary);
    fileInput >> count;
    std::vector<bcos::bytes> inputDatas;
    inputDatas.reserve(count);
    for (auto i = 0; i < count; ++i)
    {
        bcos::bytes data(Hasher::HASH_SIZE);
        fileInput.read((char*)data.data(), (std::streamsize)data.size());

        inputDatas.emplace_back(std::move(data));
    }

    auto type = vm["type"].as<int>();
    if (type)
    {
        testNewMerkle(inputDatas);
    }
    else
    {
        testOldMerkle(inputDatas);
    }
    fileInput.close();
}