#include "libdevcore/CommonData.h"
#include "libdevcore/FixedHash.h"
#include "libdevcrypto/CryptoInterface.h"
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
#ifdef FISCO_GM
    crypto::initSMCtypro();
#endif
    auto addr = right160(crypto::Hash(bytesConstRef((const unsigned char*)source.data(), 64)));
    cout << addr << endl;
    return 0;
}
