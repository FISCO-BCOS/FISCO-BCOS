#include "libdevcore/CommonData.h"
#include "libdevcore/FixedHash.h"
#include "libdevcrypto/Hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <string>

using namespace std;
using namespace dev;

int main(int, char** argv)
{
    auto source = dev::fromHex(argv[1]);
    auto addr = right160(dev::sha3(bytesConstRef((const unsigned char*)source.data(), 64)));
    cout << addr << endl;
    return 0;
}
