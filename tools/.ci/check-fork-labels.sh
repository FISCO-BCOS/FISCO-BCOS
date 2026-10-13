#!/usr/bin/env bash
# Copyright (c) 2026 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# =============================================================================
# WI-32 gate 3/4: fork-label coverage guard.
#
# Boost.Test decorators tag every fork-scoped case with `fork-<name>` labels.
# `--list_content=DOT` renders, for each leaf case, its decorators as
# `...|labels: @fork-regolith @fork-canyon ...`, so a fork that loses all of its
# tagged cases (or its decorator) becomes visible without running anything.
#
# The nine labels are the EL fork ladder Regolith -> Karst. Each fork-labelled
# test binary must still expose all nine: presence of `@fork-<name>` is enough,
# because a label only appears on a leaf case that carries it (so present == at
# least one covered case). One binary losing a label means a fork's whole lane
# silently dropped out of the matrix.
#
# Counting rule (repo-wide): red is
# decided by the process exit code / a positive-count assertion, NEVER by the
# substring "errors detected" — Boost's success banner contains it.
#
# Usage: check-fork-labels.sh <test-binary> [<test-binary> ...]
# Exit:  0 only when every binary lists all nine fork labels.
# =============================================================================
set -uo pipefail

# Order matches the fork ladder; keep in sync with the label decorators.
readonly LABELS="fork-regolith fork-canyon fork-ecotone fork-fjord fork-granite \
fork-holocene fork-isthmus fork-jovian fork-karst"

# review F27: this list is a third copy of the fork ladder, so derive the expected
# value from the pinned table (gen_official_genesis.py EL_FORKS) at runtime and
# refuse to run on drift. python3 missing / import failure degrades to a warning
# naming the risk rather than failing a gate for an environment reason.
FORK_LADDER_PY="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/opstack-genesis/gen_official_genesis.py"
derived=""
if command -v python3 >/dev/null 2>&1 && [ -f "$FORK_LADDER_PY" ]; then
    derived="$(python3 -c '
import importlib.util, sys
spec = importlib.util.spec_from_file_location("g", sys.argv[1])
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
print(" ".join("fork-" + fork for fork in mod.EL_FORKS))
' "$FORK_LADDER_PY" 2>/dev/null || true)"
fi
if [ -z "$derived" ]; then
    echo "::warning::could not derive the fork ladder from $FORK_LADDER_PY (python3 unavailable or import failed); LABELS below is unverified against the pinned table" >&2
elif [ "$derived" != "$LABELS" ]; then
    echo "::error::fork ladder drift: LABELS='$LABELS' but gen_official_genesis.py EL_FORKS derives '$derived' — rename the labels or the pinned table, not one side" >&2
    exit 1
fi

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <test-binary> [<test-binary> ...]" >&2
    exit 2
fi

rc=0
for bin in "$@"; do
    if [ ! -x "$bin" ]; then
        echo "::error::$bin is missing or not executable (build the test target first)" >&2
        rc=1
        continue
    fi

    out="$("$bin" --list_content=DOT 2>&1)"
    code=$?
    if [ "$code" -ne 0 ]; then
        echo "::error::$bin --list_content=DOT exited $code" >&2
        rc=1
        continue
    fi

    missing=""
    for label in $LABELS; do
        # No label is a prefix of another, so a fixed-string match is exact.
        n="$(printf '%s\n' "$out" | grep -oF "@$label" | wc -l | tr -d ' ')"
        if [ "$n" -eq 0 ]; then
            missing="$missing $label"
        else
            echo "[$bin] $label: $n case(s)"
        fi
    done

    if [ -n "$missing" ]; then
        echo "::error::$bin lost fork-label coverage:$missing (all nine labels are required)" >&2
        rc=1
    else
        echo "[$bin] all nine fork labels present"
    fi
done
exit $rc
