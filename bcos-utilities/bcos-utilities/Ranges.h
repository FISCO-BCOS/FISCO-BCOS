#pragma once

#ifdef USE_STD_RANGES
#include <ranges>
#define RANGES ::std::ranges
#else
#include <range/v3/all.hpp>
#define RANGES ::ranges::cpp20
#endif