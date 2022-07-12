#pragma once


#ifdef USE_STD_RANGES
namespace bcos
#include <bcos-utilities/Ranges.h>
#define RANGES ::std::ranges
#else
#include <range/v3/all.hpp>
#define RANGES ::ranges
#endif