#ifndef __TARS_JSON_H__
#define __TARS_JSON_H__

#include <string.h>
#include <cassert>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>


#include <bcos-cpp-sdk/utilities/tx/tars/tup/TarsType.h>
#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_common.h>
#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_json.h>


#pragma GCC diagnostic ignored "-Wunused-parameter"
namespace tars
{
class JsonInput
{
public:
    template <typename T>
    static void readJson(T& c, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<std::is_same<T, bool>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeBoolean)
        {
            c = (T) dynamic_cast<JsonValueBoolean*>(p.get())->value;
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename T>
    static void readJson(T& c, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value,
            void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeNum)
        {
            c = (T) dynamic_cast<JsonValueNum*>(p.get())->lvalue;
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename T>
    static void readJson(T& n, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<std::is_floating_point<T>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeNum)
        {
            n = (T) dynamic_cast<JsonValueNum*>(p.get())->value;
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
        else if (p && p->getType() == eJsonTypeNull)
        {
            n = std::numeric_limits<T>::quiet_NaN();
        }
    }

    template <typename T>
    static void readJson(T& c, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<std::is_enum<T>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeNum)
        {
            c = (T) dynamic_cast<JsonValueNum*>(p.get())->lvalue;
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename T>
    static void readJson(T& s, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<std::is_same<T, std::string>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeString)
        {
            s = dynamic_cast<JsonValueString*>(p.get())->value;
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'string' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    static void readJson(char* buf, const UInt32 bufLen, UInt32& readLen, const JsonValuePtr& p,
        bool isRequire = true)
    {
        if (p && p->getType() == eJsonTypeString)
        {
            JsonValueString* pString = dynamic_cast<JsonValueString*>(p.get());
            if ((UInt32)pString->value.size() > bufLen)
            {
                char s[128];
                snprintf(
                    s, sizeof(s), "invalid char * size, size: %u", (UInt32)pString->value.size());
                throw TC_Json_Exception(s);
            }
            memcpy(buf, pString->value.c_str(), pString->value.size());
            readLen = (UInt32)pString->value.size();
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'char *' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    /// 读取结构数组
    template <typename T>
    static void readJson(
        T* v, const UInt32 len, UInt32& readLen, const JsonValuePtr& p, bool isRequire = true)
    {
        if (p && p->getType() == eJsonTypeArray)
        {
            JsonValueArray* pArray = dynamic_cast<JsonValueArray*>(p.get());
            if (pArray->value.size() > len)
            {
                char s[128];
                snprintf(s, sizeof(s), "read 'T *' invalid size, size: %u",
                    (uint32_t)pArray->value.size());
                throw TC_Json_Exception(s);
            }
            size_t i = 0;
            for (auto it = pArray->value.begin(); it != pArray->value.end(); ++it, ++i)
            {
                readJson(v[i], *it);
            }

            readLen = pArray->value.size();
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'T *' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    //    template<typename T>
    //    static void readJson(T& v, const JsonValuePtr & p, bool isRequire = true, typename
    //    jce::disable_if<jce::is_convertible<T*, TarsStructBase*>, void ***>::type dummy = 0)
    //    {
    //        Int32 n = 0;
    //        readJson(n, p, isRequire);
    //        v = (T) n;
    //    }

    /// 读取结构
    template <typename T>
    static void readJson(T& v, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<std::is_convertible<T*, TarsStructBase*>::value, void***>::type
            dummy = 0)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            v.readFromJson(pObj);
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'Char' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename V, typename Cmp, typename Alloc>
    static void readJson(
        std::map<std::string, V, Cmp, Alloc>& m, const JsonValuePtr& p, bool isRequire = true)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<std::string, V> pr;
                pr.first = iter->first;
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename V, typename H, typename Cmp, typename Alloc>
    static void readJson(std::unordered_map<std::string, V, H, Cmp, Alloc>& m, const JsonValuePtr& p,
        bool isRequire = true)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<std::string, V> pr;
                pr.first = iter->first;
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    // char特殊处理
    template <typename V, typename Cmp, typename Alloc>
    static void readJson(
        std::map<Char, V, Cmp, Alloc>& m, const JsonValuePtr& p, bool isRequire = true)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<Char, V> pr;
                pr.first = TC_Common::strto<Int32>(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename V, typename Cmp, typename Alloc>
    static void readJson(
        std::map<unsigned char, V, Cmp, Alloc>& m, const JsonValuePtr& p, bool isRequire = true)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<Char, V> pr;
                pr.first = TC_Common::strto<Int32>(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename K, typename V, typename H, typename Cmp>
    static void readJson(std::map<K, V, H, Cmp>& m, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<std::is_integral<K>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<K, V> pr;
                pr.first = TC_Common::strto<K>(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename K, typename V, typename H, typename Cmp>
    static void readJson(std::map<K, V, H, Cmp>& m, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<std::is_floating_point<K>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<K, V> pr;
                pr.first = TC_Common::strto<K>(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename K, typename V, typename Cmp, typename Alloc>
    static void readJson(std::map<K, V, Cmp, Alloc>& m, const JsonValuePtr& p,
        bool isRequire = true,
        typename std::enable_if<std::is_enum<K>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<K, V> pr;
                pr.first = (K)TC_Common::strto<int>(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename K, typename V, typename Cmp, typename Alloc>
    static void readJson(std::map<K, V, Cmp, Alloc>& m, const JsonValuePtr& p,
        bool isRequire = true,
        typename std::enable_if<std::is_convertible<K*, TarsStructBase*>::value, void***>::type
            dummy = 0)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<K, V> pr;
                pr.first.readFromJsonString(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
        //    	char s[128];
        //    	snprintf(s, sizeof(s), "map key is not Basic type. map key is only
        //      string|bool|num"); 	throw TC_Json_Exception(s);
    }

    template <typename K, typename V, typename H, typename Cmp, typename Alloc>
    static void readJson(std::unordered_map<K, V, H, Cmp, Alloc>& m, const JsonValuePtr& p,
        bool isRequire = true,
        typename std::enable_if<std::is_integral<K>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<K, V> pr;
                pr.first = TC_Common::strto<K>(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename K, typename V, typename H, typename Cmp, typename Alloc>
    static void readJson(std::unordered_map<K, V, H, Cmp, Alloc>& m, const JsonValuePtr& p,
        bool isRequire = true,
        typename std::enable_if<std::is_floating_point<K>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<K, V> pr;
                pr.first = TC_Common::strto<K>(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }


    template <typename K, typename V, typename Cmp, typename Alloc>
    static void readJson(std::unordered_map<K, V, Cmp, Alloc>& m, const JsonValuePtr& p,
        bool isRequire = true,
        typename std::enable_if<std::is_enum<K>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<K, V> pr;
                pr.first = TC_Common::strto<int>(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }


    template <typename K, typename V, typename Cmp, typename Alloc>
    static void readJson(std::unordered_map<K, V, Cmp, Alloc>& m, const JsonValuePtr& p,
        bool isRequire = true,
        typename std::enable_if<std::is_convertible<K*, TarsStructBase*>::value, void***>::type
            dummy = 0)
    {
        if (p && p->getType() == eJsonTypeObj)
        {
            JsonValueObj* pObj = dynamic_cast<JsonValueObj*>(p.get());
            auto iter = pObj->value.begin();
            for (; iter != pObj->value.end(); ++iter)
            {
                std::pair<K, V> pr;
                pr.first.readFromJsonString(iter->first);
                readJson(pr.second, iter->second);
                m.insert(pr);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'map' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }

        //    	char s[128];
        //    	snprintf(s, sizeof(s), "map key is not Basic type. map key is only
        //      string|bool|num"); 	throw TC_Json_Exception(s);
    }

    template <typename T, typename Alloc>
    static void readJson(std::vector<T, Alloc>& v, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<!std::is_same<T, bool>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeArray)
        {
            JsonValueArray* pArray = dynamic_cast<JsonValueArray*>(p.get());
            v.resize(pArray->value.size());
            size_t i = 0;
            for (auto it = pArray->value.begin(); it != pArray->value.end(); ++it, ++i)
            {
                readJson(v[i], *it);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'vector' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename T, typename Alloc>
    static void readJson(std::vector<T, Alloc>& v, const JsonValuePtr& p, bool isRequire = true,
        typename std::enable_if<std::is_same<T, bool>::value, void***>::type dummy = 0)
    {
        if (p && p->getType() == eJsonTypeArray)
        {
            JsonValueArray* pArray = dynamic_cast<JsonValueArray*>(p.get());
            v.resize(pArray->value.size());
            size_t i = 0;
            for (auto it = pArray->value.begin(); it != pArray->value.end(); ++it, ++i)
            {
                // vector<bool> 特殊处理
                bool b;
                readJson(b, *it);
                v[i] = b;
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'vector' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }


    template <typename T, typename Cmp, typename Alloc>
    static void readJson(std::set<T, Cmp, Alloc>& v, const JsonValuePtr& p, bool isRequire = true)
    {
        if (p && p->getType() == eJsonTypeArray)
        {
            JsonValueArray* pArray = dynamic_cast<JsonValueArray*>(p.get());
            size_t i = 0;
            for (auto it = pArray->value.begin(); it != pArray->value.end(); ++it, ++i)
            {
                T t;
                readJson(t, *it);
                v.insert(t);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'vector' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }

    template <typename K, typename H, typename Cmp, typename Alloc>
    static void readJson(
        std::unordered_set<K, H, Cmp, Alloc>& v, const JsonValuePtr& p, bool isRequire = true)
    {
        if (p && p->getType() == eJsonTypeArray)
        {
            JsonValueArray* pArray = dynamic_cast<JsonValueArray*>(p.get());
            size_t i = 0;
            for (auto it = pArray->value.begin(); it != pArray->value.end(); ++it, ++i)
            {
                K t;
                readJson(t, *it);
                v.insert(t);
            }
        }
        else if (isRequire)
        {
            char s[128];
            snprintf(s, sizeof(s), "read 'vector' type mismatch, get type: %d.", p->getType());
            throw TC_Json_Exception(s);
        }
    }
};

class JsonOutput
{
public:
    template <class T>
    static JsonValueBooleanPtr writeJson(
        T b, typename std::enable_if<std::is_same<T, bool>::value, void***>::type dummy = 0)
    {
        return new JsonValueBoolean(b);
    }

    template <class T>
    static JsonValueNumPtr writeJson(
        T b, typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value,
                 void***>::type dummy = 0)
    {
        return (new JsonValueNum((int64_t)b, true));
    }

    template <class T>
    static JsonValueNumPtr writeJson(
        T b, typename std::enable_if<std::is_floating_point<T>::value, void***>::type dummy = 0)
    {
        return (new JsonValueNum(b, false));
    }

    template <class T>
    static JsonValueStringPtr writeJson(const T& b,
        typename std::enable_if<std::is_same<T, std::string>::value, void***>::type dummy = 0)
    {
        return (new JsonValueString(b));
    }

    template <typename T>
    static JsonValueNumPtr writeJson(
        const T& v, typename std::enable_if<std::is_enum<T>::value, void***>::type dummy = 0)
    {
        return writeJson((Int32)v);
    }

    static JsonValueStringPtr writeJson(const char* buf, const UInt32 len)
    {
        return (new JsonValueString(std::string(buf, len)));
    }

    template <typename V, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::map<std::string, V, Cmp, Alloc>& m)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(i->first, writeJson(i->second)));
        }
        return pObj;
    }

    template <typename V, typename H, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::unordered_map<std::string, V, H, Cmp, Alloc>& m)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(i->first, writeJson(i->second)));
        }
        return pObj;
    }

    template <typename K, typename V, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::map<K, V, Cmp, Alloc>& m,
        typename std::enable_if<std::is_integral<K>::value, void***>::type dummy = 0)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(TC_Common::tostr(i->first), writeJson(i->second)));
        }
        return pObj;
    }

    template <typename K, typename V, typename H, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::unordered_map<K, V, H, Cmp, Alloc>& m,
        typename std::enable_if<std::is_integral<K>::value, void***>::type dummy = 0)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(TC_Common::tostr(i->first), writeJson(i->second)));
        }
        return pObj;
    }

    template <typename K, typename V, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::map<K, V, Cmp, Alloc>& m,
        typename std::enable_if<std::is_floating_point<K>::value, void***>::type dummy = 0)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(TC_Common::tostr(i->first), writeJson(i->second)));
        }
        return pObj;
    }

    template <typename K, typename V, typename H, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::unordered_map<K, V, H, Cmp, Alloc>& m,
        typename std::enable_if<std::is_floating_point<K>::value, void***>::type dummy = 0)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(TC_Common::tostr(i->first), writeJson(i->second)));
        }
        return pObj;
    }

    template <typename K, typename V, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::map<K, V, Cmp, Alloc>& m,
        typename std::enable_if<std::is_enum<K>::value, void***>::type dummy = 0)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(TC_Common::tostr(i->first), writeJson(i->second)));
        }
        return pObj;
    }

    template <typename K, typename V, typename H, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::unordered_map<K, V, H, Cmp, Alloc>& m,
        typename std::enable_if<std::is_enum<K>::value, void***>::type dummy = 0)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(TC_Common::tostr(i->first), writeJson(i->second)));
        }
        return pObj;
    }

    template <typename K, typename V, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::map<K, V, Cmp, Alloc>& m,
        typename std::enable_if<std::is_convertible<K*, TarsStructBase*>::value, void***>::type
            dummy = 0)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(i->first.writeToJsonString(), writeJson(i->second)));
        }
        return pObj;
    }

    template <typename K, typename V, typename H, typename Cmp, typename Alloc>
    static JsonValueObjPtr writeJson(const std::unordered_map<K, V, H, Cmp, Alloc>& m,
        typename std::enable_if<std::is_convertible<K*, TarsStructBase*>::value, void***>::type
            dummy = 0)
    {
        JsonValueObjPtr pObj = new JsonValueObj();
        for (auto i = m.begin(); i != m.end(); ++i)
        {
            pObj->value.insert(make_pair(i->first.writeToJsonString(), writeJson(i->second)));
        }
        return pObj;
    }

    template <typename T, typename Alloc>
    static JsonValueArrayPtr writeJson(const std::vector<T, Alloc>& v,
        typename std::enable_if<!std::is_same<T, bool>::value, void***>::type dummy = 0)
    {
        JsonValueArrayPtr pArray = new JsonValueArray();
        pArray->value.resize(v.size());
        for (size_t i = 0; i < v.size(); i++)
        {
            pArray->value[i] = writeJson(v[i]);
        }
        return pArray;
    }

    template <typename T, typename Alloc>
    static JsonValueArrayPtr writeJson(const std::vector<T, Alloc>& v,
        typename std::enable_if<std::is_same<T, bool>::value, void***>::type dummy = 0)
    {
        JsonValueArrayPtr pArray = new JsonValueArray();
        pArray->value.resize(v.size());
        for (size_t i = 0; i < v.size(); i++)
        {
            // vector<bool> 特殊处理
            pArray->value[i] = writeJson((bool)v[i]);
        }
        return pArray;
    }

    template <typename T, typename Cmp, typename Alloc>
    static JsonValueArrayPtr writeJson(const std::set<T, Cmp, Alloc>& v)
    {
        JsonValueArrayPtr pArray = new JsonValueArray();
        pArray->value.resize(v.size());
        auto it = v.begin();
        int i = 0;
        while (it != v.end())
        {
            pArray->value[i] = (writeJson(*it));
            ++it;
            ++i;
        }
        return pArray;
    }

    template <typename T, typename H, typename Cmp, typename Alloc>
    static JsonValueArrayPtr writeJson(const std::unordered_set<T, H, Cmp, Alloc>& v)
    {
        JsonValueArrayPtr pArray = new JsonValueArray();
        pArray->value.resize(v.size());
        auto it = v.begin();
        int i = 0;
        while (it != v.end())
        {
            pArray->value[i] = (writeJson(*it));
            ++it;
            ++i;
        }
        return pArray;
    }

    template <typename T>
    static JsonValueArrayPtr writeJson(const T* v, const UInt32 len)
    {
        JsonValueArrayPtr pArray = new JsonValueArray();
        pArray->value.resize(len);
        for (size_t i = 0; i < len; ++i)
        {
            pArray->value[i] = (writeJson(v[i]));
        }
        return pArray;
    }

    template <typename T>
    static JsonValueObjPtr writeJson(const T& v,
        typename std::enable_if<std::is_convertible<T*, TarsStructBase*>::value, void***>::type
            dummy = 0)
    {
        return JsonValueObjPtr::dynamicCast(v.writeToJson());
    }
};
////////////////////////////////////////////////////////////////////////////////////////////////////
}  // namespace tars


#endif
