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
 * @file: ParseCert.cpp
 * @author: fisco-dev
 *
 * @date: 2017
 */

#include "ParseCert.h"

ParseCert::ParseCert(ba::ssl::verify_context& ctx)
{
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    if (cert)
        ParseInfo(cert, m_isExpire, m_subjectName, m_serialNumber, m_certType);
}

/// get interfaces
bool ParseCert::isExpire() const
{
    return m_isExpire;
}
string ParseCert::serialNumber() const
{
    return m_serialNumber;
}
string ParseCert::subjectName() const
{
    return m_subjectName;
}
int ParseCert::certType() const
{
    return m_certType;
}

ParseCert::~ParseCert() {}

void ParseCert::ParseInfo(X509* cert, bool& is_expired, std::string& subjectName,
    std::string& serialNumber, int& certType)
{
    // subject name of the certificate
    char subject_name[256];
    if (cert)
    {
        X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
        subjectName.insert(0, subject_name);
        int err = 0;
        // lifetime of the certificate
        ASN1_TIME* start = NULL;
        ASN1_TIME* end = NULL;
        time_t ttStart = {0};
        time_t ttEnd = {0};
        start = X509_get_notBefore(cert);
        end = X509_get_notAfter(cert);
        ttStart = ASN1_TIME_get(start, &err);
        ttEnd = ASN1_TIME_get(end, &err);
        time_t t_Now = time(0);
        localtime(&t_Now);
        if (t_Now < ttStart || t_Now > ttEnd)
            m_isExpire = true;
        else
            m_isExpire = false;
        // serial number of the certificate
        ASN1_INTEGER* asn1_i = NULL;
        char* serial = NULL;
        BIGNUM* bignum = NULL;
        asn1_i = X509_get_serialNumber(cert);
        bignum = ASN1_INTEGER_to_BN(asn1_i, NULL);
        serial = BN_bn2hex(bignum);
        serialNumber.insert(0, serial);
        /// free resources
        BN_free(bignum);
        // type of the certificate
        BASIC_CONSTRAINTS* bcons = NULL;
        int crit = 0;
        bcons = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
        certType = 0;
        if (bcons != NULL)
        {
            if (bcons->ca == false)
            {
                certType = 1;
            }
            BASIC_CONSTRAINTS_free(bcons);
        }
    }
}

int ParseCert::mypint(const char** s, int n, int min, int max, int* e)
{
    int retval = 0;
    while (n)
    {
        if (**s < '0' || **s > '9')
        {
            *e = 1;
            return 0;
        }
        retval *= 10;
        retval += **s - '0';
        --n;
        ++(*s);
    }
    if (retval < min || retval > max)
        *e = 1;
    return retval;
}

time_t ParseCert::ASN1_TIME_get(ASN1_TIME* a, int* err)
{
    char days[2][12] = {{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};
    int dummy;
    if (err == NULL)
        err = &dummy;
    int generalized;
    if (a->type == V_ASN1_GENERALIZEDTIME)
        generalized = 1;
    else if (a->type == V_ASN1_UTCTIME)
        generalized = 0;
    else
    {
        *err = 1;
        return 0;
    }
    /// Data should be always null terminated;
    const char* s = (const char*)a->data;
    if (s == NULL || s[a->length] != '\0')
    {
        *err = 1;
        return 0;
    }

    int i, year, isleap, offset;
    time_t retval;
    *err = 0;
    struct tm t;
    if (generalized)
    {
        t.tm_year = mypint(&s, 4, 0, 9999, err) - 1900;
    }
    else
    {
        t.tm_year = mypint(&s, 2, 0, 99, err);
        if (t.tm_year < 50)
            t.tm_year += 100;
    }
    t.tm_mon = mypint(&s, 2, 1, 12, err) - 1;
    t.tm_mday = mypint(&s, 2, 1, 31, err);
    // NOTE: It's not yet clear, if this implementation is 100% correct
    // for GeneralizedTime... but at least misinterpretation is
    // impossible --- we just throw an exception
    t.tm_hour = mypint(&s, 2, 0, 23, err);
    t.tm_min = mypint(&s, 2, 0, 59, err);
    if (*s >= '0' && *s <= '9')
        t.tm_sec = mypint(&s, 2, 0, 59, err);
    else
        t.tm_sec = 0;
    if (*err)
        return 0;  // Format violation

    if (generalized)
    {
        // skip fractional seconds if any
        while (*s == '.' || *s == ',' || (*s >= '0' && *s <= '9'))
            ++s;
        // special treatment for local time
        if (*s == 0)
        {
            t.tm_isdst = -1;
            retval = mktime(&t);  // Local time is easy :)
            if (retval == (time_t)-1)
            {
                *err = 2;
                retval = 0;
            }
            return retval;
        }
    }
    if (*s == 'Z')
    {
        offset = 0;
        ++s;
    }
    else if (*s == '-' || *s == '+')
    {
        i = (*s++ == '-');
        offset = mypint(&s, 2, 0, 12, err);
        offset *= 60;
        offset += mypint(&s, 2, 0, 59, err);
        if (*err)
            return 0;  // Format violation
        if (i)
            offset = -offset;
    }
    else
    {
        *err = 1;
        return 0;
    }
    if (*s)
    {
        *err = 1;
        return 0;
    }
    // And here comes the hard part --- there's no standard function to
    // convert struct tm containing UTC time into time_t without
    // messing global timezone settings (breaks multithreading and may
    // cause other problems) and thus we have to do this "by hand"
    // NOTE: Overflow check does not detect too big overflows, but is
    // sufficient thanks to the fact that year numbers are limited to four
    // digit non-negative values.
    retval = t.tm_sec;
    retval += (t.tm_min - offset) * 60;
    retval += t.tm_hour * 3600;
    retval += (t.tm_mday - 1) * 86400;
    year = t.tm_year + 1900;
    if (sizeof(time_t) == 4)
    {
        // This is just to avoid too big overflows being undetected, finer
        // overflow detection is done below.
        if (year < 1900 || year > 2040)
            *err = 2;
    }
    // FIXME: Does POSIX really say, that all years divisible by 4 are
    // leap years (for consistency)??? Fortunately, this problem does
    // not exist for 32-bit time_t and we should'nt be worried about
    // this until the year of 2100 :)

    isleap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
    for (i = t.tm_mon - 1; i >= 0; --i)
        retval += days[isleap][i] * 86400;
    retval += (year - 1970) * 31536000;
    if (year < 1970)
    {
        retval -= ((1970 - year + 2) / 4) * 86400;
        if (sizeof(time_t) > 4)
        {
            for (i = 1900; i >= year; i -= 100)
            {
                if (i % 400 == 0)
                    continue;
                retval += 86400;
            }
        }
        if (retval >= 0)
            *err = 2;
    }
    else
    {
        retval += ((year - 1970 + 1) / 4) * 86400;
        if (sizeof(time_t) > 4)
        {
            for (i = 2100; i < year; i += 100)
            {
                // The following condition is the reason to
                // start with 2100 instead of 2000
                if (i % 400 == 0)
                    continue;
                retval -= 86400;
            }
        }
        if (retval < 0)
            *err = 2;
    }
    if (*err)
        retval = 0;
    return retval;
}
