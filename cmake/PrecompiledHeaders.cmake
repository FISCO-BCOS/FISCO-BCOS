if (PRECOMPILED_HEADER)
    add_library(precompiled-headers INTERFACE)
    target_precompile_headers(precompiled-headers
    INTERFACE 
        # STL
        <string>
        <vector>
        <array>
        <map>
        <unordered_map>
        <concepts>
        <set>
        <unordered_set>
        <queue>
        <stack>
        <deque>
        <string_view>
        <tuple>
        <optional>
        <variant>
        <any>
        <iostream>
        <fstream>
        <sstream>
        <algorithm>
        <functional>
        <span>
        <memory>
        <utility>
        <numeric>
        <limits>
        <mutex>
        <atomic>
        <thread>
        <chrono>
        <iterator>
        <stdexcept>
        <type_traits>
        <initializer_list>
        <compare>
        <cstdint>
        <exception>
        <future>
        <ratio>
        <forward_list>

        # Boost
        <boost/container/small_vector.hpp>
        <boost/algorithm/hex.hpp>
        <boost/exception/diagnostic_information.hpp>
        <boost/throw_exception.hpp>
        <boost/log/attributes/constant.hpp>
        <boost/log/attributes/scoped_attribute.hpp>
        <boost/log/sources/severity_channel_logger.hpp>
        <boost/log/trivial.hpp>
        <boost/multi_index/hashed_index.hpp>
        <boost/multi_index/identity.hpp>
        <boost/multi_index/key.hpp>
        <boost/multi_index/mem_fun.hpp>
        <boost/multi_index/ordered_index.hpp>
        <boost/multi_index/sequenced_index.hpp>
        <boost/multi_index_container.hpp>
        <boost/archive/basic_archive.hpp>
        <boost/iostreams/device/back_inserter.hpp>
        <boost/iostreams/stream.hpp>
        <boost/algorithm/string/join.hpp>
        <boost/beast/core.hpp>
        <boost/beast/http.hpp>
        <boost/beast/http/vector_body.hpp>
        <boost/beast/websocket.hpp>
        <boost/beast/ssl/ssl_stream.hpp>
        <boost/asio/strand.hpp>
        <boost/asio/ssl/stream.hpp>
        <boost/asio/dispatch.hpp>
        <boost/asio/ssl.hpp>

        # gsl
        <gsl/span>

        # fmt
        <fmt/format.h>

        # tbb
        <oneapi/tbb/blocked_range.h>
        <oneapi/tbb/combinable.h>
        <oneapi/tbb/parallel_pipeline.h>
        <oneapi/tbb/parallel_for_each.h>
        <oneapi/tbb/parallel_for.h>
        <oneapi/tbb/concurrent_unordered_set.h>
        <oneapi/tbb/concurrent_unordered_map.h>
        <oneapi/tbb/concurrent_hash_map.h>
        <tbb/parallel_for.h>
        <tbb/concurrent_hash_map.h>
        <tbb/concurrent_unordered_map.h>
        <tbb/concurrent_set.h>

        # range
        <range/v3/all.hpp>
    )

    include(GNUInstallDirs)
    install(TARGETS precompiled-headers EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(DIRECTORY "precompiled-headers" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")  
endif()