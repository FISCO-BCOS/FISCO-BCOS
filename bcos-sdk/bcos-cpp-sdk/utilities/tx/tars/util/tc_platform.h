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
 
#pragma once


#if defined _WIN32 || defined _WIN64
#define TARGET_PLATFORM_WINDOWS		1

#elif __APPLE__
#define TARGET_PLATFORM_IOS			1

#elif defined ANDROID
#define TARGET_PLATFORM_ANDROID		1
#define TARGET_PLATFORM_LINUX		1

#elif __linux__
#define TARGET_PLATFORM_LINUX		1

#else
#error Unsupported platform.

#endif


#if TARGET_PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>

#elif TARGET_PLATFORM_LINUX
#include <unistd.h>
#endif


//UTIL 动态库编译的导入和导出
#if TARGET_PLATFORM_WINDOWS

#ifdef UTIL_DLL_EXPORT
#define UTIL_DLL_API __declspec(dllexport)
#else

#ifdef UTIL_USE_DLL
#define UTIL_DLL_API __declspec(dllimport)
#else
#define UTIL_DLL_API 
#endif

#endif

#else
#define UTIL_DLL_API 
#endif

//servant 动态库编译的导入和导出
#if TARGET_PLATFORM_WINDOWS

#ifdef SVT_DLL_EXPORT
#define SVT_DLL_API __declspec(dllexport)
#else

#ifdef SVT_USE_DLL
#define SVT_DLL_API __declspec(dllimport)
#else
#define SVT_DLL_API 
#endif

#endif

#else
#define SVT_DLL_API 
#endif

