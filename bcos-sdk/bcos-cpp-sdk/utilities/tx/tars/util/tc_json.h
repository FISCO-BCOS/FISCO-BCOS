
/**
 * Tencent is pleased to support the open source community by making Tars available.
 *
 * Copyright (C) 2016THL A29 Limited, a Tencent company. All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use this file except 
 * in compliance with the License. You may obtain a copy of the License at
 *
 * https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software distributed 
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR 
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the 
 * specific language governing permissions and limitations under the License.
 */

#ifndef __TC_JSON_H
#define __TC_JSON_H

#include <string>
#include <unordered_map>
#include <vector>
#include <assert.h>
#include <stdio.h>

#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_autoptr.h>

namespace tars
{

/**
* 编解码抛出的异常
* Exception thrown by codec
*/
struct TC_Json_Exception : public TC_Exception
{
	TC_Json_Exception(const std::string &buffer) : TC_Exception(buffer){};
	~TC_Json_Exception() throw(){};
};

enum eJsonType
{
	eJsonTypeString,
	eJsonTypeNum,
	eJsonTypeObj,
	eJsonTypeArray,
	eJsonTypeBoolean,
	eJsonTypeNull,
};

/*
 * json类型的基类。没有任何意义
 * Base class of type json.No meaning.
 */
class JsonValue : public TC_HandleBase
{
public:
	virtual eJsonType getType()=0;
	virtual ~JsonValue()
	{
	}
};
typedef TC_AutoPtr<JsonValue> JsonValuePtr;

/*
 * json类型 null
 * json type   null type   
 */
class JsonValueNull : public JsonValue
{
public:
	JsonValueNull()
	{
	}

	eJsonType getType()
	{
		return eJsonTypeNull;
	}
	virtual ~JsonValueNull()
	{
	}
};
typedef TC_AutoPtr<JsonValueNull> JsonValueNullPtr;

/*
 * json类型 string类型 例如"dd\ndfd"
 * json type   std::string type   for example: "dd\ndfd"
 */
class JsonValueString : public JsonValue
{
public:
	JsonValueString(const std::string & s):value(s)
	{
	}
	JsonValueString()
	{

	}

	eJsonType getType()
	{
		return eJsonTypeString;
	}
	virtual ~JsonValueString()
	{
	}
	std::string value;
};
typedef TC_AutoPtr<JsonValueString> JsonValueStringPtr;

/*
 * json类型 number类型 例如 1.5e8
 * json type   number type   for example: 1.5e8
 */
class JsonValueNum : public JsonValue
{
public:
	JsonValueNum(double d,bool b=false):value(d),lvalue(d),isInt(b)
	{
	}
	JsonValueNum(int64_t d,bool b=true):value(d), lvalue(d),isInt(b)
	{
	}
	JsonValueNum()
	{
		isInt=false;
		value=0.0f;
		lvalue = 0;
	}
	eJsonType getType()
	{
		return eJsonTypeNum;
	}
	virtual ~JsonValueNum(){}
public:
	double value;
	int64_t lvalue;
	bool isInt;
};
typedef TC_AutoPtr<JsonValueNum> JsonValueNumPtr;

/*
 * json类型 object类型 例如 {"aa","bb"} 
 * json type   object type  for example: {"aa","bb"} 
 */
class JsonValueObj: public JsonValue
{
public:
	eJsonType getType()
	{
		return eJsonTypeObj;
	}
	const JsonValuePtr &get(const char *name)
	{
		auto it = value.find(name);

		if(it == value.end())
		{
			char s[64];
			snprintf(s, sizeof(s), "get obj error(key is not exists)[key:%s]", name);
			throw TC_Json_Exception(s);
		}

		return it->second;
	}

	virtual ~JsonValueObj(){}
public:
	std::unordered_map<std::string,JsonValuePtr> value;
};
typedef TC_AutoPtr<JsonValueObj> JsonValueObjPtr;

/*
 * json类型 array类型 例如 ["aa","bb"] 
 * json type  array type  for example: ["aa","bb"] 
 */
class JsonValueArray: public JsonValue
{
public:
	eJsonType getType()
	{
		return eJsonTypeArray;
	}
	void push_back(const JsonValuePtr & p)
	{
		value.push_back(p);
	}
	virtual ~JsonValueArray(){}
public:
	std::vector<JsonValuePtr> value;
};
typedef TC_AutoPtr<JsonValueArray> JsonValueArrayPtr;

/*
 * json类型 boolean类型 例如 true
 * json type  boolean type  for example: true 
 */
class JsonValueBoolean : public JsonValue
{
public:
	JsonValueBoolean() {}
	JsonValueBoolean(bool b): value(b){}

	eJsonType getType()
	{
		return eJsonTypeBoolean;
	}
	bool getValue()
	{
		return value;
	}
	virtual ~JsonValueBoolean(){}
public:
	bool value;
};
typedef TC_AutoPtr<JsonValueBoolean> JsonValueBooleanPtr;

/*
 * 分析json字符串用到的 读字符的类
 * Classes for parsing read characters used in JSON strings
 */
class BufferJsonReader
{
	/*buffer*/
	const char *        _buf;		///< 缓冲区
	/*buffer length*/
	size_t              _buf_len;	///< 缓冲区长度
	/*current location*/
	size_t              _cur;		///< 当前位置

public:

	BufferJsonReader () :_buf(NULL),_buf_len(0), _cur(0) {}

	void reset() { _cur = 0;}

	void setBuffer(const char * buf, size_t len)
	{
		_buf = buf;
		_buf_len = len;
		_cur = 0;
	}

	void setBuffer(const std::vector<char> &buf)
	{
		_buf = buf.data();
		_buf_len = buf.size();
		_cur = 0;
	}

	size_t getCur()
	{
		return _cur;
	}

	const char * getPoint()
	{
		return _buf+_cur;
	}

	char read()
	{
		check();
		_cur ++;
		return *(_buf+_cur-1);
	}

	char get()
	{
		check();
		return *(_buf+_cur);
	}

	char getBack()
	{
		assert(_cur>0);
		return *(_buf+_cur-1);
	}

	void back()
	{
		assert(_cur>0);
		_cur--;
	}

	void check()
	{
		if (_cur + 1 > _buf_len)
		{
			char s[64];
			snprintf(s, sizeof(s), "buffer overflow when peekBuf, over %u.", (uint32_t)_buf_len);
			throw TC_Json_Exception(s);
		}
	}

	bool hasEnd()
	{
		return _cur >= _buf_len;
	}
};

/*
 * 分析json的类。都是static
 * Analyze json's classes.All static.
 */
class TC_Json
{
public:
	//json类型到字符串的转换
	//Conversion of JSON type to std::string
	static std::string writeValue(const JsonValuePtr & p, bool withSpace = false);
	static void writeValue(const JsonValuePtr & p, std::string& ostr, bool withSpace = false);
	static void writeValue(const JsonValuePtr & p, std::vector<char>& buf, bool withSpace = false);

	//json字符串到json结构的转换
	//Conversion of JSON std::string to JSON structure
	static JsonValuePtr getValue(const std::string & str);
    static JsonValuePtr getValue(const std::vector<char>& buf);
private:
	//string 类型到json字符串
	//string type to json string
	static void writeString(const JsonValueStringPtr & p, std::string& ostr);
	static void writeString(const std::string & s, std::string& ostr);

	//num 类型到json字符串
	//num type to json string
	static void writeNum(const JsonValueNumPtr & p, std::string& ostr);

	//obj 类型到json字符串
	//obj type to json string
	static void writeObj(const JsonValueObjPtr & p, std::string& ostr, bool withSpace = false);

	//array 类型到json字符串
	//array type to json string
	static void writeArray(const JsonValueArrayPtr & p, std::string& ostr, bool withSpace = false);

	//boolean 类型到json字符串
	//boolean type to json string
	static void writeBoolean(const JsonValueBooleanPtr & p, std::string& ostr);

	//读取json的value类型 也就是所有的json类型 如果不符合规范会抛异常
	//Reading json's value type means that all JSON types throw an exception if they do not conform to the specification
	static JsonValuePtr getValue(BufferJsonReader & reader);
	//读取json的object 如果不符合规范会抛异常
	//Reading json's object throws an exception if it does not conform to the specification
	static JsonValueObjPtr getObj(BufferJsonReader & reader);
	//读取json的array(数组) 如果不符合规范会抛异常
	//Reading json's array (array) throws an exception if it does not conform to the specification
	static JsonValueArrayPtr getArray(BufferJsonReader & reader);
	//读取json的string 如 "dfdf" 如果不符合规范会抛异常
	//A std::string reading json, such as "dfdf", throws an exception if it does not conform to the specification
	static JsonValueStringPtr getString(BufferJsonReader & reader,char head='\"');
	//读取json的数字 如 -213.56 如果不符合规范会抛异常
	//Reading JSON numbers such as -213.56 throws an exception if it does not conform to the specification
	static JsonValueNumPtr getNum(BufferJsonReader & reader,char head);
	//读取json的boolean值  如 true false 如果不符合规范会抛异常
	//Reading json's Boolean value such as true false throws an exception if it does not conform to the specification
	static JsonValueBooleanPtr getBoolean(BufferJsonReader & reader,char c);
	//读取json的 null 如果不符合规范会抛异常
	//Reading json's null throws an exception if it does not conform to the specification
	//static JsonValuePtr getNull(BufferJsonReader & reader,char c);
	static JsonValueNullPtr getNull(BufferJsonReader & reader,char c);
	//获取16进制形式的值 如\u003f 如果不符合规范会抛异常
	//Judging whether a character meets json's definition of a blank character Gets a value in hexadecimal form such as \u003f Throws an exception if it does not conform to the specification
	static uint32_t getHex(BufferJsonReader & reader);
	//判断一个字符是否符合json定义的空白字符
	//Determines whether a character matches a json-defined blank character
	static bool isspace(char c);
};


class TC_JsonWriteOstream
{
public:
    static void writeValue(const JsonValuePtr & p, std::ostream& ostr, bool withSpace = false);
private:
	//string 类型到json字符串
	//stirng type to json string
	static void writeString(const JsonValueStringPtr & p, std::ostream& ostr);
	static void writeString(const std::string & s, std::ostream& ostr);

	//num 类型到json字符串
	//num type to json string
	static void writeNum(const JsonValueNumPtr & p, std::ostream& ostr);

	//obj 类型到json字符串
	//obj type to json string
	static void writeObj(const JsonValueObjPtr & p, std::ostream& ostr, bool withSpace = false);

	//array 类型到json字符串
	//array type to json string
	static void writeArray(const JsonValueArrayPtr & p, std::ostream& ostr, bool withSpace = false);

	//boolean 类型到json字符串
	//boolean type to json string
	static void writeBoolean(const JsonValueBooleanPtr & p, std::ostream& ostr);
};
}

#endif
