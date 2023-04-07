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

#ifndef __TC_AUTOPTR_H
#define __TC_AUTOPTR_H

#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_ex.h>
#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_platform.h>
#include <atomic>
#include <typeinfo> 

namespace tars
{

/**
* @brief Null Pointer Exception
* @brief 空指针异常
*/
struct TC_AutoPtrNull_Exception : public TC_Exception
{
    TC_AutoPtrNull_Exception(const std::string &buffer) : TC_Exception(buffer){};
    ~TC_AutoPtrNull_Exception() {};
};

/**
 * @brief Smart Pointer Base Class
 * @brief 智能指针基类
 * 
 * All classes that require smart pointer support need to inherit from this object. 
 * 所有需要智能指针支持的类都需要从该对象继承，
 *  
 */
class UTIL_DLL_API TC_HandleBase
{
public:

    /**
     * @brief Copy
     * @brief 复制
     *
     * @return TC_HandleBase&
     */
    TC_HandleBase& operator=(const TC_HandleBase&)
    {
        return *this;
    }

    /**
     * @brief Increase Count
     * @brief 增加计数
     */
    void incRef() { ++_atomic; }

    /**
     * @brief Decrease Count/减少计数
     * 
     * 当计数==0时, 且需要删除数据时, 释放对象
     * When 'count==0' and you need to delete data, you can use this to release object.
     * 
     */
    void decRef()
    {
        if((--_atomic) == 0 && !_bNoDelete)
        {
            _bNoDelete = true;
            delete this;
        }
    }

    /**
     * @brief Get Count
     * @brief 获取计数.
     *
     * @return int value of count
     * @return int 计数值
     */
    int getRef() const        { return _atomic; }

    /**
	 * @brief Set Automatically-Release Off
     * @brief 设置不自动释放. 
	 *  
     * @param b Determine whether to be deleted automatically or not, true or false.
     * @param b 是否自动删除,true or false
     */
    void setNoDelete(bool b)  { _bNoDelete = b; }

protected:

    /**
     * @brief Constructor
     * @brief 构造函数    
     */
    TC_HandleBase() : _atomic(0), _bNoDelete(false)
    {
    }

    /**
     * @brief Copy Constructor
     * @brief 拷贝构造
     */
    TC_HandleBase(const TC_HandleBase&) : _atomic(0), _bNoDelete(false)
    {
    }

    /**
     * @brief Destructor
     * @brief 析构
     */
    virtual ~TC_HandleBase()
    {
    }

protected:

    /**
     * Count
     * 计数
     */
    std::atomic<int>	  _atomic;

    /**
     * Determine whether to be deleted automatically or not
     * 是否自动删除
     */
    bool        _bNoDelete;
};

/**
 * @brief Smart Pointer Template Class
 * @brief 智能指针模板类. 
 *  
 * This template class an product thread-safe smart pointer which can be placed in a container.
 * The smart pointer which is defined by this class can be implemented by reference counting.
 * The pointer can be passed in a container.
 * 
 * template<typename T> T MUST BE inherited from TC_HandleBase
 * 
 * 可以放在容器中,且线程安全的智能指针. 
 * 通过它定义智能指针，该智能指针通过引用计数实现， 
 * 可以放在容器中传递.   
 * 
 * template<typename T> T必须继承于TC_HandleBase 
 */
template<typename T>
class TC_AutoPtr
{
public:

    /**
     * Element Type
     * 元素类型
     */
    typedef T element_type;

    /**
     * @brief Initialize with native pointer, count +1
	 * @brief 用原生指针初始化, 计数+1. 
	 *  
     * @param p
     */
    TC_AutoPtr(T* p = nullptr)
    {
        _ptr = p;

        if(_ptr)
        {
            _ptr->incRef();
        }
    }

    /**
     * @brief Initialize with the native pointer of other smart pointer r, count +1.
	 * @brief 用其他智能指针r的原生指针初始化, 计数+1. 
	 *  
     * @param Y
     * @param r
     */
    template<typename Y>
    TC_AutoPtr(const TC_AutoPtr<Y>& r)
    {
        _ptr = r._ptr;

        if(_ptr)
        {
            _ptr->incRef();
        }
    }

    /**
     * @brief Copy constructor, count +1
	 * @brief 拷贝构造, 计数+1. 
	 *  
     * @param r
     */
    TC_AutoPtr(const TC_AutoPtr& r)
    {
        _ptr = r._ptr;

        if(_ptr)
        {
            _ptr->incRef();
        }
    }

    /**
     * @brief Destructor
     * @brief 析构
     */
    ~TC_AutoPtr()
    {
        if(_ptr)
        {
            _ptr->decRef();
        }
    }

    /**
     * @brief Assignment, normal pointer
	 * @brief 赋值, 普通指针. 
	 *  
	 * @param p 
     * @return TC_AutoPtr&
     */
    TC_AutoPtr& operator=(T* p)
    {
        if(_ptr != p)
        {
            if(p)
            {
                p->incRef();
            }

            T* ptr = _ptr;
            _ptr = p;

            if(ptr)
            {
                ptr->decRef();
            }
        }
        return *this;
    }

    /**
     * @brief Assignment, other type of smart pointer
	 * @brief 赋值, 其他类型智能指针. 
	 *  
     * @param Y
	 * @param r 
     * @return TC_AutoPtr&
     */
    template<typename Y>
    TC_AutoPtr& operator=(const TC_AutoPtr<Y>& r)
    {
        if(_ptr != r._ptr)
        {
            if(r._ptr)
            {
                r._ptr->incRef();
            }

            T* ptr = _ptr;
            _ptr = r._ptr;

            if(ptr)
            {
                ptr->decRef();
            }
        }
        return *this;
    }

    /**
     * @brief Assignment, other ruling pointer of this type.
	 * @brief 赋值, 该类型其他执政指针. 
	 *  
	 * @param r 
     * @return TC_AutoPtr&
     */
    TC_AutoPtr& operator=(const TC_AutoPtr& r)
    {
        if(_ptr != r._ptr)
        {
            if(r._ptr)
            {
                r._ptr->incRef();
            }

            T* ptr = _ptr;
            _ptr = r._ptr;

            if(ptr)
            {
                ptr->decRef();
            }
        }
        return *this;
    }

    /**
     * @brief Replace other types of smart pointers with current types of smart pointers
	 * @brief 将其他类型的智能指针换成当前类型的智能指针. 
	 *  
     * @param Y
	 * @param r 
     * @return TC_AutoPtr
     */
    template<class Y>
    static TC_AutoPtr dynamicCast(const TC_AutoPtr<Y>& r)
    {
        return TC_AutoPtr(dynamic_cast<T*>(r._ptr));
    }

    /**
     * @brief Convert pointers of other native types into smart pointers of the current type
	 * @brief 将其他原生类型的指针转换成当前类型的智能指针. 
	 *  
     * @param Y
	 * @param p 
     * @return TC_AutoPtr
     */
    template<class Y>
    static TC_AutoPtr dynamicCast(Y* p)
    {
        return TC_AutoPtr(dynamic_cast<T*>(p));
    }

    /**
     * @brief Get Native Pointer
     * @brief 获取原生指针.
     *
     * @return T*
     */
    T* get() const
    {
        return _ptr;
    }

    /**
     * @brief Transfer
     * @brief 调用.
     *
     * @return T*
     */
    T* operator->() const
    {
        if(!_ptr)
        {
            throwNullHandleException();
        }

        return _ptr;
    }

    /**
     * @brief Reference
     * @brief 引用.
     *
     * @return T&
     */
    T& operator*() const
    {
        if(!_ptr)
        {
            throwNullHandleException();
        }

        return *_ptr;
    }

    /**
     * @brief To define whether it is effective or not.
     * @brief 是否有效.
     *
     * @return bool
     */
    operator bool() const
    {
        return _ptr ? true : false;
    }

    /**
     * @brief Swap pointer
	 * @brief 交换指针. 
	 *  
     * @param other
     */
    void swap(TC_AutoPtr& other)
    {
        std::swap(_ptr, other._ptr);
    }

protected:

    /**
     * @brief Throw Exception
     * @brief 抛出异常
     */
    void throwNullHandleException() const;

public:
    T*          _ptr;

};

/**
 * @brief Throw Exception
 * @brief 抛出异常. 
 *  
 * @param T
 * @param file
 * @param line
 */
template<typename T> inline void
TC_AutoPtr<T>::throwNullHandleException() const
{
    throw TC_AutoPtrNull_Exception("autoptr null handle error![" + std::string(typeid(T).name()) +"]");
}

/**
 * @brief Determine '=='.
 * @brief ==判断. 
 *  
 * @param T
 * @param U
 * @param lhs
 * @param rhs
 *
 * @return bool
 */
template<typename T, typename U>
inline bool operator==(const TC_AutoPtr<T>& lhs, const TC_AutoPtr<U>& rhs)
{
    T* l = lhs.get();
    U* r = rhs.get();

	// Compare pointers directly instead of comparing values of the two pointers.
    // 改为直接比较指针，而不是比较值
	return (l == r);
}

/**
 * @brief Determine '!='.
 * @brief 不等于判断. 
 *  
 * @param T
 * @param U
 * @param lhs
 * @param rhs
 *
 * @return bool
 */
template<typename T, typename U>
inline bool operator!=(const TC_AutoPtr<T>& lhs, const TC_AutoPtr<U>& rhs)
{
    T* l = lhs.get();
    U* r = rhs.get();

	// Compare pointers directly instead of comparing values of the two pointers.
    // 改为直接比较指针，而不是比较值
	return (l != r);
}

/**
 * @brief Determine '<', can be used in map and other conrainers.
 * @brief 小于判断, 用于放在map等容器中. 
 *  
 * @param T
 * @param U
 * @param lhs
 * @param rhs
 *
 * @return bool
 */
template<typename T, typename U>
inline bool operator<(const TC_AutoPtr<T>& lhs, const TC_AutoPtr<U>& rhs)
{
    T* l = lhs.get();
    U* r = rhs.get();
    if(l && r)
    {
        // return *l < *r;
        // Compare pointers directly instead of comparing values of the two pointers.
		// 改为直接比较指针，而不是比较值
		return (l < r);
    }
    else
    {
        return !l && r;
    }
}

}

#endif
