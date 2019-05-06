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
/** @file SQLStorageJNI.h
 *  @author mo nan
 *  @date 20190424
 */

#ifdef USE_JNI

#pragma once

#include "Storage.h"
#include <jni.h>
#include <json/json.h>
#include <libdevcore/FixedHash.h>
#include <memory>

namespace dev
{
namespace storage
{
class SQLStorageJNI : public Storage
{
public:
    typedef std::shared_ptr<SQLStorageJNI> Ptr;

    SQLStorageJNI();
    virtual ~SQLStorageJNI(){};

    void init();

    Entries::Ptr select(h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override;
    size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override;
    bool onlyDirty() override;

    virtual void setFatalHandler(std::function<void(std::exception&)> fatalHandler)
    {
        m_fatalHandler = fatalHandler;
    }

private:
    std::function<void(std::exception&)> m_fatalHandler;

    std::shared_ptr<JavaVM> m_jvm;
    std::shared_ptr<JNIEnv> m_env;

    jclass m_dbServiceClaz;
    jobject m_dbService;

    jclass m_selectClaz;
    jmethodID m_selectInit;
    jmethodID m_selectSetCondition;
    jmethodID m_selectSetBlockHash;
    jmethodID m_selectSetNum;
    jmethodID m_selectSetTable;
    jmethodID m_selectSetKey;

    jclass m_selectResponseClaz;
    jmethodID m_selectResponseGetColumns;
    jmethodID m_selectResponseGetData;

    jmethodID m_dbSelectMethod;

    jclass m_commitClaz;
    jmethodID m_commitInit;
    jmethodID m_dbCommitMethod;

    jclass m_listClaz;
    jmethodID m_listInitMethod;
    jmethodID m_listAddMethod;
    jmethodID m_listGetMethod;
    jmethodID m_listSizeMethod;

    std::string m_classpath;
};

}  // namespace storage

}  // namespace dev

#endif
