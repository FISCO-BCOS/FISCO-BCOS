#!/usr/bin/env bash
# needs bash >= 4 (declare -A): CI runs ubuntu; on macOS use homebrew bash
# "Copyright [2026] <fisco-bcos>"
# @ function: every event name listed in docs/ops/log-events.md must have exactly one
#             LOG_DESC("<name>") in the module sources; a duplicate or a missing one fails.
#             A badge-qualified entry (`PBFT:BlockCommitted`) is searched only inside that
#             module's directory, so two modules may share an event name with different badges.
#             With bcos-ops/bcos-ops/log/EventNames.h present, every handbook name must also
#             appear there (the `log` commands consume the same constants).
# @ file    : check_log_events.sh
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MANUAL="${ROOT}/docs/ops/log-events.md"
EVENT_NAMES_H="${ROOT}/bcos-ops/bcos-ops/log/EventNames.h"

declare -A MODULE_DIRS=(
    [PBFT]="bcos-pbft bcos-sealer"
    [TXPOOL]="bcos-txpool bcos-tx-validator bcos-framework/bcos-framework/txpool"
    [SYNC]="bcos-sync"
    [SCHEDULER]="bcos-scheduler/src"
    [BASELINE]="transaction-scheduler"
    [EXECUTOR]="bcos-executor/src"
    [LEDGER]="bcos-ledger"
    [RPC]="bcos-rpc/bcos-rpc"
    [GATEWAY]="bcos-gateway/bcos-gateway"
    [FRONT]="bcos-front/bcos-front"
)
ALL_DIRS="${MODULE_DIRS[PBFT]} ${MODULE_DIRS[TXPOOL]} ${MODULE_DIRS[SYNC]} ${MODULE_DIRS[SCHEDULER]} ${MODULE_DIRS[EXECUTOR]} ${MODULE_DIRS[LEDGER]} ${MODULE_DIRS[RPC]} ${MODULE_DIRS[GATEWAY]} ${MODULE_DIRS[FRONT]}"

if [ ! -f "${MANUAL}" ]; then
    echo "missing ${MANUAL}"
    exit 1
fi

# event rows: a table line whose first cell is a backticked name, optionally `BADGE:Name`
# the header table of channel names and the old->new mapping table are skipped by the
# <!-- events --> ... <!-- /events --> markers in the manual
entries=$(awk '/<!-- events -->/{on=1;next} /<!-- \/events -->/{on=0} on' "${MANUAL}" |
    grep -E '^\| *`[A-Za-z0-9_: ^+#]+` *\|' |
    sed -E 's/^\| *`([^`]+)` *\|.*/\1/' | sort -u)

if [ -z "${entries}" ]; then
    echo "no event rows found between <!-- events --> markers in ${MANUAL}"
    exit 1
fi

failed=0
count=0
while IFS= read -r entry; do
    [ -z "${entry}" ] && continue
    badge=""
    name="${entry}"
    if [[ "${entry}" == *:* ]]; then
        badge="${entry%%:*}"
        name="${entry#*:}"
    fi
    # decorated names: the handbook lists the plain event name; the source carries the prefix
    dirs="${ALL_DIRS}"
    if [ -n "${badge}" ]; then
        dirs="${MODULE_DIRS[${badge}]:-}"
        if [ -z "${dirs}" ]; then
            echo "FAIL ${entry}: unknown badge ${badge}"
            failed=1
            continue
        fi
    fi
    hits=$(cd "${ROOT}" && grep -rn --include='*.cpp' --include='*.h' \
        -E "LOG_DESC\(\"([\^+# ]*)${name}\"\)" ${dirs} | wc -l | tr -d ' ')
    count=$((count + 1))
    if [ "${hits}" != "1" ]; then
        echo "FAIL ${entry}: expected exactly 1 LOG_DESC, found ${hits}"
        cd "${ROOT}" && grep -rn --include='*.cpp' --include='*.h' \
            -E "LOG_DESC\(\"([\^+# ]*)${name}\"\)" ${dirs} | sed 's/^/    /'
        failed=1
    fi
    if [ -f "${EVENT_NAMES_H}" ] && ! grep -q "\"${name}\"" "${EVENT_NAMES_H}"; then
        echo "FAIL ${entry}: not present in bcos-ops/bcos-ops/log/EventNames.h"
        failed=1
    fi
done <<< "${entries}"

if [ "${failed}" -ne 0 ]; then
    exit 1
fi
echo "check_log_events: ${count} event names, each with exactly one LOG_DESC"
