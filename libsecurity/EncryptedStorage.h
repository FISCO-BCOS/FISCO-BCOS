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
 * @brief : Encrypt Storage
 * @author: jimmyshi
 * @date: 2019-05-22
 */

#pragma once
#include <libdevcore/Common.h>
#include <libstorage/Storage.h>

namespace dev
{
namespace storage
{
class EncryptedStorage : public Storage
{
public:
    typedef std::shared_ptr<EncryptedStorage> Ptr;
    virtual ~EncryptedStorage(){};
    Entries::Ptr select(h256 hash, int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr) override;
    size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override;

    bool onlyDirty() override { return false; };

    void setGroupID(dev::GROUP_ID const& groupID) { m_backend->setGroupID(groupID); }
    dev::GROUP_ID groupID() const { return m_backend->groupID(); }
    void setBackend(Storage::Ptr backend);

    void setDataKey(const bytes& _dataKey) { m_dataKey = _dataKey; };

private:
    Entries::Ptr encryptEntries(Entries::Ptr inEntries);
    Entries::Ptr decryptEntries(Entries::Ptr inEntries);

private:
    Storage::Ptr m_backend;
    bytes m_dataKey = bytes();
};
}  // namespace storage
}  // namespace dev