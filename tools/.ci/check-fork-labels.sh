#!/usr/bin/env bash
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
# Counting rule (repo-wide, docs/plans/2026-09-12-plan-B-impl.md:19): red is
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
