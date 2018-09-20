/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief: unit test for ParseCert
 *
 * @file ParseCert.cpp
 * @author: yujiechen
 * @date 2018-09-19
 */
#include <libp2p/ParseCert.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ParseCertTest, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testParseCert)
{
    std::string path = getTestPath().string() + "/fisco-bcos-data/node.crt";
    BIO* bio = BIO_new_file(path.c_str(), "r");
    if (bio)
    {
        X509* cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        if (cert)
        {
            ParseCert cert_parser;
            BOOST_CHECK(cert_parser.isExpire() == true);
            BOOST_CHECK(cert_parser.serialNumber().empty() == true);
            BOOST_CHECK(cert_parser.subjectName().empty() == true);
            BOOST_CHECK(cert_parser.certType() == 0);
            std::string m_subject;
            std::string m_serial;
            int m_certType;
            bool m_expire;
            cert_parser.ParseInfo(cert, m_expire, m_subject, m_serial, m_certType);
            cert_parser.setSubjectName(m_subject);
            cert_parser.setExpiration(m_expire);
            cert_parser.setCertType(m_certType);
            cert_parser.setSerialNumber(m_serial);
            BOOST_CHECK(cert_parser.isExpire() == false);
            BOOST_CHECK(cert_parser.serialNumber().empty() == false);
            BOOST_CHECK(cert_parser.subjectName().empty() == false);
            BOOST_CHECK(cert_parser.certType() == 1);
        }
        else
            std::cout << "#### load certificate failed" << std::endl;
    }
    else
        std::cout << "### EXIT FOR BIO failed" << std::endl;
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
