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
 * @file random_perf.cpp
 * @author: octopus
 * @date 2022-04-07
 */

#include <bcos-utilities/FixedBytes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <memory>
#include <string>

void usage()
{
    printf("Desc: random biginteger perf test\n");
    printf("Usage: ./random_perf count\n");
    printf("Example:\n");
    printf("    ./random_perf 30000\n");
    exit(0);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        usage();
    }

    long long count = std::stoul(argv[1]);

    printf("[Random Gen Test] ===>>>> count: %lld\n", count);

    long long i = 0;
    long long _10Per = count / 10;

    auto startPoint = std::chrono::high_resolution_clock::now();
    while (i++ < count)
    {
        if (i % _10Per == 0)
        {
            std::cerr << " ..process : " << ((double)i / count) * 100 << "%" << std::endl;
        }

        // auto fixBytes = bcos::FixedBytes<32>().generateRandomFixedBytes();
        // auto u256Value = transactionBuilder->genRandomUint256();
        // (void)u256Value;
    }

    auto endPoint = std::chrono::high_resolution_clock::now();
    auto elapsedMS =
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(endPoint - startPoint)
            .count();

    printf(
        " [Random Gen Test] total count: %lld, total elapsed(ms): %lld, "
        "count/s: %lld \n",
        count, elapsedMS, 1000 * count / elapsedMS);

    return 0;
}
