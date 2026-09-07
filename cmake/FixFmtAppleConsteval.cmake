function(fisco_patch_fmt_apple_consteval)
    set(_patch_apple FALSE)
    set(_patch_clang20 FALSE)
    if(APPLE)
        set(_patch_apple TRUE)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 20)
        # fmt 11.0.2's consteval FMT_COMPILE_STRING path is rejected by clang >= 20
        # ("call to consteval function is not a constant expression" in format-inl.h).
        set(_patch_clang20 TRUE)
    else()
        return()
    endif()

    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(_fmt_base
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/fmt/base.h")
    elseif(DEFINED VCPKG_TARGET_TRIPLET)
        set(_fmt_base
            "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include/fmt/base.h")
    else()
        file(GLOB _fmt_base_candidates "${CMAKE_BINARY_DIR}/vcpkg_installed/*/include/fmt/base.h")
        if(NOT _fmt_base_candidates)
            return()
        endif()
        list(GET _fmt_base_candidates 0 _fmt_base)
    endif()

    if(NOT EXISTS "${_fmt_base}")
        return()
    endif()

    file(READ "${_fmt_base}" _fmt_base_content)

    if(_patch_apple)
        string(FIND "${_fmt_base_content}"
            "#elif defined(__apple_build_version__) && __apple_build_version__ < 14000029L"
            _apple_guard_pos)
        if(_apple_guard_pos EQUAL -1)
            return()
        endif()
        string(REPLACE
            "#elif defined(__apple_build_version__) && __apple_build_version__ < 14000029L\n#  define FMT_USE_CONSTEVAL 0  // consteval is broken in Apple clang < 14."
            "#elif defined(__apple_build_version__)\n#  define FMT_USE_CONSTEVAL 0  // Disabled by FISCO-BCOS: fmt 11.0.2 consteval path is broken on current Apple clang/libc++."
            _fmt_base_content "${_fmt_base_content}")
        file(WRITE "${_fmt_base}" "${_fmt_base_content}")
        message(STATUS "Patched fmt consteval guard for Apple toolchain: ${_fmt_base}")
    elseif(_patch_clang20)
        string(FIND "${_fmt_base_content}"
            "#elif FMT_CLANG_VERSION >= 2000"
            _clang_guard_pos)
        if(NOT _clang_guard_pos EQUAL -1)
            return()
        endif()
        string(REPLACE
            "#elif defined(__cpp_consteval)\n#  define FMT_USE_CONSTEVAL 1"
            "#elif FMT_CLANG_VERSION >= 2000\n#  define FMT_USE_CONSTEVAL 0  // Disabled by FISCO-BCOS: fmt 11.0.2 consteval path is broken on clang >= 20.\n#elif defined(__cpp_consteval)\n#  define FMT_USE_CONSTEVAL 1"
            _fmt_base_content "${_fmt_base_content}")
        file(WRITE "${_fmt_base}" "${_fmt_base_content}")
        message(STATUS "Patched fmt consteval guard for clang >= 20: ${_fmt_base}")
    endif()
endfunction()
