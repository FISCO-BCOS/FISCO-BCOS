/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: ParseCert.h
 * @author: fisco-dev
 *
 * @date: 2017
 */

#pragma once
#include "Common.h"
#include <string>
using namespace std;
class ParseCert
{
public:
    ParseCert() : m_isExpire(true), m_certType(0) {}

    ParseCert(ba::ssl::verify_context& ctx);
    ~ParseCert();
    bool isExpire() const;
    string serialNumber() const;
    string subjectName() const;
    /// set interfaces
    void setSubjectName(std::string const& _subjectName) { m_subjectName = _subjectName; }
    void setExpiration(bool const isExpired) { m_isExpire = isExpired; }
    void setCertType(const int certType) { m_certType = certType; }
    void setSerialNumber(std::string const& serialNumber) { m_serialNumber = serialNumber; }

    int certType() const;
    void ParseInfo(X509* cert, bool& is_expired, std::string& subjectName,
        std::string& serialNumber, int& certType);

private:
    int mypint(const char** s, int n, int min, int max, int* e);
    time_t ASN1_TIME_get(ASN1_TIME* a, int* err);

    bool m_isExpire;        // the certificate is expired or not
    string m_serialNumber;  // serial number of the certificate
    int m_certType;         // type of the certificate (include CA and user certificate)
    string m_subjectName;   // subject of the certificate (0 represents CA, 1 represents user
                            // certificate)
};
