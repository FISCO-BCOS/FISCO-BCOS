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

#ifndef USE_JNI

#include "SQLStorageJNI.h"
#include <jni.h>
#include <libdevcore/easylog.h>
#include "StorageException.h"

using namespace dev;
using namespace dev::storage;

void SQLStorageJNI::init()
{
    JavaVMInitArgs vmArg;
    vmArg.version = JNI_VERSION_1_8;

    JavaVMOption options[1];

    // classpath
    m_classpath = "-cp'amdb/conf/:amdb/lib/*:amdb/apps/*'";
    options[0].optionString = new char[m_classpath.size() + 1];
    strncpy(options[0].optionString, m_classpath.c_str(), m_classpath.size());
    options[0].extraInfo = NULL;
    vmArg.options = options;
    vmArg.nOptions = 1;
    vmArg.options = options;

    JavaVM* jvm = NULL;
    JNIEnv* env = NULL;
    ::JNI_CreateJavaVM(&jvm, (void**)&env, (void*)&vmArg);

    m_jvm.reset(jvm);
    m_env.reset(env);

    auto appClaz =
        m_env->FindClass("org/springframework/context/support/ClassPathXmlApplicationContext");
    if (!appClaz)
    {
        BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find class applicationContext"));
    }
    auto appInitFunc = m_env->GetMethodID(appClaz, "<init>", "()V");
    if (!appInitFunc)
    {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find class applicationContext init function"));
    }
    auto appObj = m_env->NewObject(appClaz, appInitFunc);
    if (!appObj)
    {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Create applicationContext error"));
    }

    auto dbServiceClaz = m_env->FindClass("org/bcos/amdb/service/DBService");
    if (!dbServiceClaz)
    {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find class DBService"));
    }

    auto getBeanFunc = m_env->GetMethodID(
        appClaz, "getBean", "(Ljava/lang/String;)Lorg/fisco/bcos/channel/client/Service;");
    if (!getBeanFunc)
    {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find method getBean"));
    }

    auto dbChannelServiceName = m_env->NewStringUTF("DBChannelService");

    auto channelService = m_env->CallObjectMethod(appObj, getBeanFunc, dbChannelServiceName);
    if (!channelService)
    {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot get bean of dbService"));
    }

    auto channelServiceClaz = m_env->FindClass("org/fisco/bcos/channel/client/Service");
    if (!dbServiceClaz)
    {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find class ChannelService"));
    }

    auto runFunc = m_env->GetMethodID(channelServiceClaz, "run", "()V");
    if (runFunc)
    {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find run func"));
    }

    m_env->CallVoidMethod(channelService, runFunc);

    m_dbServiceClaz = m_env->FindClass("org/bcos/amdb/service/DBService");
    if (m_dbServiceClaz)
    {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find DBService class"));
    }

    auto dbServiceName = m_env->NewStringUTF("DBService");
    m_dbService = m_env->CallObjectMethod(appObj, getBeanFunc, dbServiceName);
    if(!m_dbService) {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find DBService bean"));
    }

    m_selectClaz = m_env->FindClass("org/bcos/amdb/dto/SelectRequest");
    if(!m_selectClaz) {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find SelectRequest class"));
    }

    m_selectInit = m_env->GetMethodID(m_selectClaz, "<init>", "()V");
    if(!m_selectInit) {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find SelectRequest init method"));
    }

    m_selectSetCondition = m_env->GetMethodID(m_selectClaz, "SetCondition", "(Ljava/util/List;)V");
    m_selectSetBlockHash = m_env->;GetMethodID(m_selectClaz, "SetCondition", "(Ljava/lang/String;)V")
    jmethodID m_selectSetNum;
    jmethodID m_selectSetTable;
    jmethodID m_selectSetKey;

    m_selectResponseClaz = m_env->FindClass("org/bcos/amdb/dto/SelectResponse");
    if(!m_selectResponseClaz) {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find SelectResponse class"));
    }

    m_dbSelectMethod = m_env->GetMethodID(m_dbServiceClaz, "select", "(Lorg/bcos/amdb/dto/SelectResponse;)Lorg/bcos/amdb/dto/SelectRequest");
    if(!m_dbSelectMethod) {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find DBService select method"));
    }

    m_commitClaz = m_env->FindClass("org/bcos/amdb/dto/CommitRequest");
    if(!m_commitClaz) {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find CommitRequest class"));
    }

    m_commitInit = m_env->GetMethodID(m_commitClaz, "<init>", "()V");
    if(m_commitInit) {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find CommitRequest init method"));
    }

    m_dbCommitMethod = m_env->GetMethodID(m_dbServiceClaz, "commit", "(Lorg/bcos/amdb/dto/CommitResponse;)Lorg/bcos/amdb/dto/CommitRequest");
    if(!m_dbSelectMethod) {
		BOOST_THROW_EXCEPTION(StorageException(-1, "Cannot find DBService commit method"));
	}

    // m_dbService = channelService;
}

Entries::Ptr SQLStorageJNI::select(
    h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    (void)hash;
    (void)num;
    (void)tableInfo;
    (void)key;
    (void)condition;

    auto selectRequest = m_env->NewObject(m_selectClaz, m_selectInit);
    if(!selectRequest) {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Create selectRequest failed"));
    }

    //m_env->CallObjectMethod(m_dbService, );

    return Entries::Ptr();
}

#endif
