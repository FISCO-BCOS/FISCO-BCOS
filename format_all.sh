#!/bin/bash

find . -name "*.h" -o -name "*.cpp" | grep -v -P '^\.\/deps|evmc' | while read line;do
	clang-format "$line" -i
done
