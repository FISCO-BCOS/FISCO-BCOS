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
    m_selectSetBlockHash = m_env->GetMethodID(m_selectClaz, "SetBlockHash", "(Ljava/lang/String;)V");
    m_selectSetNum = m_env->GetMethodID(m_selectClaz, "SetCondition", "(Ljava/lang/String;)V");
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

    m_listClaz = m_env->FindClass("java.util.List");
    m_listInitMethod = m_env->GetMethodID(m_listClaz, "<init>", "()V");
    m_listAddMethod = m_env->GetMethodID(m_listClaz, "add", "(Ljava/lang/Object;)Z");

    // m_dbService = channelService;
}

Entries::Ptr SQLStorageJNI::select(
    h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    auto selectRequest = m_env->NewObject(m_selectClaz, m_selectInit);
    if(!selectRequest) {
    	BOOST_THROW_EXCEPTION(StorageException(-1, "Create selectRequest failed"));
    }

    //m_env->CallObjectMethod(m_dbService, );
    m_env->CallVoidMethod(selectRequest, m_selectSetTable, m_env->NewStringUTF(tableInfo->name.c_str()));
    m_env->CallVoidMethod(selectRequest, m_selectSetKey, m_env->NewStringUTF(key.c_str()));
    m_env->CallVoidMethod(selectRequest, m_selectSetBlockHash, m_env->NewStringUTF(hash.hex().c_str()));
    m_env->CallVoidMethod(selectRequest, m_selectSetNum, num);

    auto listConditions = m_env->NewObject(m_listClaz, m_listInitMethod);
    if(condition) {
		for(auto it: *(condition->getConditions())) {
			auto conditionItem = m_env->NewObject(m_listClaz, m_listInitMethod);

			m_env->CallBooleanMethod(conditionItem, m_listAddMethod, m_env->NewStringUTF(it.first.c_str()));
			if (it.second.left.second == it.second.right.second && it.second.left.first &&
				it.second.right.first)
			{
				m_env->CallBooleanMethod(conditionItem, m_listAddMethod, m_env->NewStringUTF(boost::lexical_cast<std::string>(Condition::eq).c_str()));
				m_env->CallBooleanMethod(conditionItem, m_listAddMethod, m_env->NewStringUTF(it.second.right.second.c_str()));
			}
			else
			{
				if (it.second.left.second != condition->unlimitedField())
				{
					if (it.second.left.first)
					{
						m_env->CallBooleanMethod(conditionItem, m_listAddMethod, m_env->NewStringUTF(boost::lexical_cast<std::string>(Condition::ge).c_str()));
					}
					else
					{
						m_env->CallBooleanMethod(conditionItem, m_listAddMethod, m_env->NewStringUTF(boost::lexical_cast<std::string>(Condition::gt).c_str()));
					}
					m_env->CallBooleanMethod(conditionItem, m_listAddMethod, m_env->NewStringUTF(it.second.right.second.c_str()));
				}

				if (it.second.right.second != condition->unlimitedField())
				{
					if (it.second.right.first)
					{
						m_env->CallBooleanMethod(conditionItem, m_listAddMethod, m_env->NewStringUTF(boost::lexical_cast<std::string>(Condition::le).c_str()));
					}
					else
					{
						m_env->CallBooleanMethod(conditionItem, m_listAddMethod, m_env->NewStringUTF(boost::lexical_cast<std::string>(Condition::lt).c_str()));
					}
					m_env->CallBooleanMethod(conditionItem, m_listAddMethod, m_env->NewStringUTF(it.second.right.second.c_str()));
				}
			}

			m_env->CallBooleanMethod(listConditions, m_listAddMethod, conditionItem);
		}
    }

    m_env->CallVoidMethod(selectRequest, m_selectSetCondition, listConditions);

    auto dbSelectResponse = m_env->CallObjectMethod(m_dbService, m_dbSelectMethod, selectRequest);
    auto columns = m_env->CallObjectMethod(dbSelectResponse, m_selectResponseGetColumns);
    auto datas = m_env->CallObjectMethod(dbSelectResponse, m_selectResponseGetData);
    auto size = m_env->CallIntMethod(datas, m_listSizeMethod);

    auto entries = std::make_shared<Entries>();
    for(size_t i=0; i<size; ++i) {
    	auto entryMap = m_env->CallObjectMethod(datas, m_listGetMethod, i);

    	auto mapSize = m_env->CallObjectMethod(entryMap, m_listSizeMethod);
    	for(size_t j=0; j<size; ++j) {
    		auto entryObj = m_env->CallObjectMethod(entryMap, m_listGetMethod, j);
    		auto columnObj = m_env->CallObjectMethod(columns, m_listGetMethod, j);


    	}
    }




}

#endif
