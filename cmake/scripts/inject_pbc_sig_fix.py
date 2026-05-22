#!/usr/bin/env python3
"""Inject pbc_sig warnflags fix into cmake/ProjectPbcSig.cmake.
Called from PATCH_COMMAND in ProjectGroupSig.cmake.
"""
with open("cmake/ProjectPbcSig.cmake") as f:
    content = f.read()

# Check if already injected (idempotent)
if "fix_pbc_sig_cmake.py" in content:
    print("Fix already injected, nothing to do")
    exit(0)

# Insert fix before "&& cp .../config.guess <SOURCE_DIR>"
old = "&& cp ${CMAKE_CURRENT_LIST_DIR}/config.guess <SOURCE_DIR>"
# The fix: copy fix script into pbc_sig build dir, then run it
new = ("&& cp ${CMAKE_SOURCE_DIR}/cmake/fix_pbc_sig_cmake.py ."
       " && python3 fix_pbc_sig_cmake.py"
       " && cp ${CMAKE_CURRENT_LIST_DIR}/config.guess <SOURCE_DIR>")

if old in content:
    content = content.replace(old, new)
    with open("cmake/ProjectPbcSig.cmake", "w") as f:
        f.write(content)
    print("Injected pbc_sig warnflags fix into cmake/ProjectPbcSig.cmake")
else:
    print("ERROR: pattern not found in cmake/ProjectPbcSig.cmake")
    exit(1)
