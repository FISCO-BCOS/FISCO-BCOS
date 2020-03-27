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

#include <libethcore/EVMSchedule.h>
#include <atomic>
#include <string>
namespace dev
{
enum VERSION : uint32_t
{
    RC1_VERSION = 1,
    RC2_VERSION = 2,
    RC3_VERSION = 3,
    V2_0_0 = 0x02000000,
    V2_1_0 = 0x02010000,
    V2_2_0 = 0x02020000,
    V2_3_0 = 0x02030000,
    V2_4_0 = 0x02040000,
    V2_5_0 = 0x02050000,
};

enum ProtocolVersion : uint32_t
{
    v1 = 1,
    v2 = 2,
    // TODO: update SDK protocol to V3 after sdk-2.1.1 released
    v3 = 3,
    maxVersion = v3,
    minVersion = v1,
};

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

    void setConfDir(std::string _confDir) { m_confDir = _confDir; }
    const std::string& confDir() { return m_confDir; }
    void setDataDir(std::string _dataDir) { m_dataDir = _dataDir; }
    const std::string& dataDir() { return m_dataDir; }

    void setEnableStat(bool _enableStat) { m_enableStat = _enableStat; }

    bool const& enableStat() const { return m_enableStat; }

    struct DiskEncryption
    {
        bool enable = false;
        std::string keyCenterIP;
        int keyCenterPort;
        std::string cipherDataKey;
        std::string dataKey;
    } diskEncryption;

    struct Binary
    {
        std::string version;
        std::string buildTime;
        std::string buildInfo;
        std::string gitBranch;
        std::string gitCommitHash;
    } binaryInfo;

    /// default block time
    const unsigned c_intervalBlockTime = 1000;
    /// omit empty block or not
    const bool c_omitEmptyBlock = true;
    /// default blockLimit
    const unsigned c_blockLimit = 1000;
    /// default compress threshold: 1KB
    const uint64_t c_compressThreshold = 1024;
    const uint64_t c_binaryLogSize = 128 * 1024 * 1024;

    std::atomic_bool shouldExit;

private:
    VERSION m_version = RC3_VERSION;
    bool m_compress;
    int64_t m_chainId = 1;
    std::string m_supportedVersion;
    dev::eth::EVMSchedule m_evmSchedule = dev::eth::DefaultSchedule;
    std::string m_confDir;
    std::string m_dataDir;
    bool m_enableStat;
};

#define g_BCOSConfig GlobalConfigure::instance()

}  // namespace dev