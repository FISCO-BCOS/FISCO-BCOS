function(fisco_patch_fmt_apple_consteval)
    if(NOT APPLE)
        return()
    endif()

    if(DEFINED VCPKG_TARGET_TRIPLET)
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
endfunction()
