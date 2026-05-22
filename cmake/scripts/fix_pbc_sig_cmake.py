#!/usr/bin/env python3
"""Fix pbc_sig CMakeLists.txt: remove backslash line continuations in warnflags.

The original tarball may contain either single backslash (normal CMake line
continuation) or double backslash (escaped) continuations that produce invalid
ninja build files.  Remove them all so the warnflags become a single line.
"""
import re
with open('CMakeLists.txt') as f:
    c = f.read()
c = re.sub(
    r'set\(warnflags.*?redundant-decls"\s*\)',
    lambda m: re.sub(r' *\\\\+\n +', ' ', m.group()),
    c, flags=re.DOTALL)
with open('CMakeLists.txt', 'w') as f:
    f.write(c)
print("Fixed warnflags in CMakeLists.txt")
