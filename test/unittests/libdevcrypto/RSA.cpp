/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <libdevcrypto/Rsa.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace dev;
using namespace dev::test;

BOOST_AUTO_TEST_SUITE(Crypto)

BOOST_FIXTURE_TEST_SUITE(RSA, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testRSA)
{
    string plaintext = "helloworld";
    string pubKey =
        "-----BEGIN PUBLIC KEY-----\n"
        "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDTeT0c/cZirKf80x8ZUfnMQGg+\n"
        "/9UzxRjUEvFYnHvNdOnZousQIrb4wZEbWSxhRT8LP5fcGkczmz5I1LTOhXZAUAuo\n"
        "EF7wCxkHKlDFIaZxdDsAfA3S6lmWgLbYFqjaEUBkYZSrPSQHw+Rt7YmiqD3j43Qm\n"
        "C+iOOyRyQLP4z1RCOwIDAQAB\n"
        "-----END PUBLIC KEY-----\n";

    string privateKey =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MIICXwIBAAKBgQDTeT0c/cZirKf80x8ZUfnMQGg+/9UzxRjUEvFYnHvNdOnZousQ\n"
        "Irb4wZEbWSxhRT8LP5fcGkczmz5I1LTOhXZAUAuoEF7wCxkHKlDFIaZxdDsAfA3S\n"
        "6lmWgLbYFqjaEUBkYZSrPSQHw+Rt7YmiqD3j43QmC+iOOyRyQLP4z1RCOwIDAQAB\n"
        "AoGBALoAWhNk1pg1um+ylhjkNG1FfStkQ/cL9eGaY7MHuBCnM4RcOppvnjW/s0y1\n"
        "q0ZG7MONBJnwdw3aDvdqNzmqw60FPnl8CVOG3rJMF3SPhRvUcgOlKWdo0K5idl7H\n"
        "CHEICyusD6Eo0Q2XoIyYw3IqSK29VYcjmLLozvilB/xEFtWJAkEA+Or5Umtq3AQb\n"
        "/nWy8rp5PaOal42QIHsQT8z5g1tcZV7uLdSrru/dejdzfNfBLXr6CwRFErK9jvrO\n"
        "wWuvxQXN1QJBANl9iNNoPU/vqmur4rUttcaj3klPW2nkP3cn0VNijzCDnhP47ZkX\n"
        "rGxVaVskUM+a+7CadU+io2gBUxPEyoH6B88CQQCS36MJnNRKyinydWSHkLwlQLnh\n"
        "HuiiIbs4OwwnE+tq7R7A8DH1YRdgHAQK8AvOWDfd9EEFjW4IRbllq7LlIE2ZAkEA\n"
        "t+cmY1ypO4Z0nEbjlD/qjOTTeTnZGlkeMStCHTghy+v/JvQ+NE2IRrKSO7chfeqX\n"
        "GGYC/CuR8Mft77Ffazh4kQJBALhlv7xXzuoIwL3cEm2vYDep/36UDLzTjIMuIrAk\n"
        "pCD/xT6tpgF8yEXxJim03XIOHEd1rgmrLn92R5fcu+RykMI=\n"
        "-----END RSA PRIVATE KEY-----";

    boost::filesystem::ofstream file("./rsa_demo_private.pem");
    file << privateKey;
    file.close();

    string signData = dev::crypto::RSAKeySign("./rsa_demo_private.pem", plaintext);
    string ret = dev::crypto::RSAKeyVerify(pubKey, signData);
    BOOST_CHECK(ret == plaintext);

    string errPk =
        "-----BEGIN PUBLIC KEY-----\n"
        "MIGeMA0GCSqGSIb3DQEBAQUAA4GMADCBiAKBgHxdj0WTrrvusWaIZD6XZ5qMiDhi\n"
        "XeQC+84nQTK7Ny8ITGgPPWZFwKd33C+LYoRO7ShLfEpKAqm8RR15O0EzxpvCdWUz\n"
        "zjv7gUpoksi7Tf2LR2fiH0yEDHHNc2nK22c1MPOO7ZJrT4e2REBH6+E/gOsGXmRX\n"
        "klatCWSW6rxbcoQdAgMBAAE=\n"
        "-----END PUBLIC KEY-----";

    ret = dev::crypto::RSAKeyVerify(errPk, signData);
    BOOST_CHECK(ret == "");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
