/*
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
 */
/**
 * @brief : Global configure of the node
 * @author: jimmyshi
 * @date: 2018-11-30
 */

#pragma once

#include <libdevcore/Exceptions.h>
#include <libdevcore/easylog.h>
#include <libethcore/EVMSchedule.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <atomic>
#include <string>
#include <vector>


#define GLOBALCONFIGURE_LOG(LEVEL) LOG(LEVEL) << "[GlobalConfigure]"

namespace dev
{
enum VERSION : uint32_t
{
    RC1_VERSION = 1,
    RC2_VERSION = 2,
    RC3_VERSION = 3
};
DEV_SIMPLE_EXCEPTION(UnknowSupportVersion);
class GlobalConfigure
{
public:
    static GlobalConfigure& instance()
    {
        static GlobalConfigure ins;
        return ins;
    }
    GlobalConfigure() : shouldExit(false) {}
    VERSION const& version() const { return m_version; }
    void setCompress(bool const& compress) { m_compress = compress; }

    bool compressEnabled() const { return m_compress; }

    void setChainId(int64_t _chainId) { m_chainId = _chainId; }
    int64_t chainId() const { return m_chainId; }

    void setSupportedVersion(std::string const& _supportedVersion, VERSION _versionNumber)
    {
        m_supportedVersion = _supportedVersion;
        m_version = _versionNumber;
    }
    std::string const& supportedVersion() { return m_supportedVersion; }

    void setEVMSchedule(dev::eth::EVMSchedule const& _schedule) { m_evmSchedule = _schedule; }
    dev::eth::EVMSchedule const& evmSchedule() const { return m_evmSchedule; }


    static uint32_t getVersionNumber(const std::string& _version)
    {
        // 0MNNPPTS, M=MAJOR N=MINOR P=PATCH T=TWEAK S=STATUS
        std::vector<std::string> versions;
        uint32_t versionNumber = 0;
        boost::split(versions, _version, boost::is_any_of("."));
        if (versions.size() != 3)
        {
            BOOST_THROW_EXCEPTION(UnknowSupportVersion() << errinfo_comment(_version));
        }
        try
        {
            for (size_t i = 0; i < versions.size(); ++i)
            {
                versionNumber += boost::lexical_cast<uint32_t>(versions[i]);
                versionNumber <<= 8;
            }
        }
        catch (const boost::bad_lexical_cast& e)
        {
            GLOBALCONFIGURE_LOG(ERROR) << LOG_KV("what", boost::diagnostic_information(e));
            BOOST_THROW_EXCEPTION(UnknowSupportVersion() << errinfo_comment(_version));
        }
        return versionNumber;
    }

    struct DiskEncryption
    {
        bool enable = false;
        std::string keyCenterIP;
        int keyCenterPort;
        std::string cipherDataKey;
        std::string dataKey;
    } diskEncryption;

    /// default block time
    const unsigned c_intervalBlockTime = 1000;
    /// omit empty block or not
    const bool c_omitEmptyBlock = true;
    /// default blockLimit
    const unsigned c_blockLimit = 1000;

    /// default compress threshold: 1KB
    const uint64_t c_compressThreshold = 1024;

    std::atomic_bool shouldExit;

private:
    VERSION m_version = RC3_VERSION;
    bool m_compress;
    int64_t m_chainId = 1;
    std::string m_supportedVersion;
    dev::eth::EVMSchedule m_evmSchedule = dev::eth::DefaultSchedule;
};

#define g_BCOSConfig GlobalConfigure::instance()

}  // namespace dev