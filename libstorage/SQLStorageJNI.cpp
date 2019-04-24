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

#include "SQLStorageJNI.h"
#include <libdevcore/easylog.h>
#include <jni.h>

using namespace dev;
using namespace dev::storage;

void SQLStorageJNI::init() {
	JavaVMInitArgs vmArg;
	vmArg.version = JNI_VERSION_1_8;

	JavaVMOption options[1];

	m_classpath = "-cp'amdb/conf/:amdb/lib/*:amdb/apps/*'";
	options[0].optionString = new char[m_classpath.size() + 1];
	strncpy(options[0].optionString, m_classpath.c_str(), m_classpath.size());
	options[0].extraInfo = NULL;
	vmArg.options = options;
	vmArg.nOptions = 1;
	vmArg.options = options;

	JavaVM *jvm = NULL;
	JNIEnv *env = NULL;
	::JNI_CreateJavaVM(&jvm, (void**)&env, (void*)&vmArg);

	m_jvm.reset(jvm);
	m_env.reset(env);

	auto appClaz = m_env->FindClass("org/springframework/context/support/ClassPathXmlApplicationContext");
	if(!appClaz) {
		STORAGE_LOG(ERROR) << "Cannot find class applicationContext";
	}
	auto appInitFunc = m_env->GetMethodID(appClaz, "<init>", "()V");
	if(!appInitFunc) {
		STORAGE_LOG(ERROR) << "Cannot find class applicationContext init function";
	}
	auto appObj = m_env->NewObject(appClaz, appInitFunc);
	if(!appObj) {
		STORAGE_LOG(ERROR) << "Create applicationContext error";
	}

	auto getBeanFunc = m_env->GetMethodID(appClaz, "getBean", "(Ljava/lang/String;)Lorg/fisco/bcos/channel/client/Service;");
	if(getBeanFunc) {
		STORAGE_LOG(ERROR) << "Cannot find method getBean";
	}

	auto dbServiceName = m_env->NewStringUTF("DBChannelService");

	auto dbService = m_env->CallObjectMethod(appObj, getBeanFunc, dbServiceName);
	if(!dbService) {
		STORAGE_LOG(ERROR) << "Cannot get bean of dbService";
	}

	m_dbService = dbService;
}

Entries::Ptr SQLStorageJNI::select(h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) {
	auto appClaz = m_env->FindClass("org/springframework/context/support/ClassPathXmlApplicationContext");
	if(!appClaz) {
		STORAGE_LOG(ERROR) << "Cannot find class applicationContext";
	}

	auto appObj = m_env->NewObject(appClaz, appInitFunc);
	if(!appObj) {
		STORAGE_LOG(ERROR) << "Create applicationContext error";
	}
}
