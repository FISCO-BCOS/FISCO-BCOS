#include <bcos-crypto/hasher/IPPCryptoHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <boost/core/demangle.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/lexical_cast.hpp>
#include <array>
#include <chrono>
#include <iostream>
#include <iterator>
#include <random>
#include <span>
#include <type_traits>
#include <vector>

using namespace bcos;
using namespace bcos::crypto::hasher;

template <Hasher HasherType>
auto hasherTest(std::string_view name, RANGES::random_access_range auto&& input,
    size_t totalSize, bool multiThread)
{
    std::vector<std::array<std::byte, 32>> results{std::size(input)};

    std::cout << "  " << name;
    auto begin = std::chrono::high_resolution_clock::now();

#pragma omp parallel if (multiThread)
    {
        HasherType hasher;

#pragma omp for
        for (RANGES::range_size_t<decltype(input)> i = 0; i < std::size(input); ++i)
        {
            hasher.update(input[i]);
            results[i] = hasher.final();
        }
    }

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - begin)
                        .count();
    auto throughput = ((double)totalSize / ((double)duration / 1000.0)) / 1024.0 / 1024.0;
    std::cout << " duration: " << duration << "ms throughput: " << throughput << "MB/s"
              << std::endl;

    return results;
}

template <size_t blockSize>
void testHash(size_t count, bool multiThread)
{
    constexpr auto valueSize = blockSize * 1024;
    std::vector<std::array<std::byte, valueSize>> inputData;
    inputData.resize(count);

    // Generate random data
#pragma omp parallel
    {
        std::mt19937 random(std::random_device{}());

#pragma omp for
        for (typename decltype(inputData)::size_type i = 0; i < inputData.size(); ++i)
        {
            auto& output = inputData[i];
            std::span<unsigned int> view{
                (unsigned int*)output.data(), output.size() / sizeof(unsigned int)};
            for (auto& num : view)
            {
                num = random();
            }
        }
    }

    hasherTest<openssl::OpenSSL_SHA2_256_Hasher>(
        "OpenSSL_SHA2_256", inputData, valueSize * count, multiThread);
    hasherTest<openssl::OPENSSL_SM3_Hasher>(
        "OpenSSL_SM3", inputData, valueSize * count, multiThread);

    hasherTest<ippcrypto::IPPCrypto_SHA2_256_Hasher>(
        "IPPCrypto_SHA2_256", inputData, valueSize * count, multiThread);
    hasherTest<ippcrypto::IPPCrypto_SM3_256_Hasher>(
        "IPPCrypto_SM3", inputData, valueSize * count, multiThread);
}

int main(int argc, char* argv[])
{
    boost::ignore_unused(argc, argv);
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " [count] [enable multithread(0/1)]" << std::endl;
        return 1;
    }

    auto count = boost::lexical_cast<size_t>(argv[1]);
    auto multiThread = boost::lexical_cast<bool>(argv[2]);

    std::cout << "Testing 1k chunk..." << std::endl;
    testHash<1>(count, multiThread);
    std::cout << "Testing 4k chunk..." << std::endl;
    testHash<4>(count, multiThread);
    std::cout << "Testing 8k chunk..." << std::endl;
    testHash<8>(count, multiThread);
    std::cout << "Testing 16k chunk..." << std::endl;
    testHash<16>(count, multiThread);

    return 0;
}