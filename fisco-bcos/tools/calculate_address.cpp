#include "libdevcore/CommonData.h"
#include "libdevcore/FixedHash.h"
#include "libdevcrypto/CryptoInterface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <iostream>
#include <string>

using namespace std;
using namespace dev;

int main(int argc, char** argv)
{
    if (argc > 3 || argc < 2)
    {
        cout << "usage: ./calculate_address [-g] hexstr" << endl;
        exit(-1);
    }

    auto useSMCrypto = false;
    if (argc == 3 && strcmp(argv[1], "-g") == 0)
    {
        useSMCrypto = true;
    }
    auto source = dev::fromHex(argv[argc - 1]);

    if (useSMCrypto)
    {
        auto addr = right160(sm3(bytesConstRef((const unsigned char*)source.data(), 64)));
        cout << addr << endl;
        return 0;
    }
    auto addr = right160(keccak256(bytesConstRef((const unsigned char*)source.data(), 64)));
    cout << addr << endl;
    return 0;
}
