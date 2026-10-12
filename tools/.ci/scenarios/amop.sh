#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 12 amop: a background `amop sub` and a foreground `amop pub` on node0's
#             plaintext WebSocket RPC (AMOP does not ride the unix socket; ci_check_air.sh
#             turns [rpc] enable_ssl off on node0 of every chain for this). Asserts the
#             round trip, the no-subscriber refusal (code 3001) and that a subscriber that
#             exited is gone. Replaces java-sdk AmopTest.
# @ file    : amop.sh

# amop_wait_subscribed <stderr file> <topic> <seconds>: `amop sub` prints "subscribed <topic>"
# once the node acknowledged the topic
amop_wait_subscribed()
{
    local waited=0
    while (( waited < ${3} * 10 )); do
        /usr/bin/grep -q "^subscribed ${2}\$" "${1}" 2>/dev/null && return 0
        sleep 0.1
        waited=$((waited + 1))
    done
    scenario_fail "amop sub did not report 'subscribed ${2}' within ${3}s: $(cat "${1}")"
    return 1
}

scenario_12_amop()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 12 amop "java-sdk AmopTest"
    local topic="ci-${chain}-${RANDOM}"
    local message="hello from ${chain}"
    local work
    work=$(mktemp -d "${TMPDIR:-/tmp}/ci-amop-${chain}.XXXXXX") || return 1
    "${fisco_bcos_path}" amop sub "${topic}" --count 1 --wait 30 --node-dir "${node_dir}" --json \
        > "${work}/sub.out" 2> "${work}/sub.err" &
    local sub_pid=$!
    if ! amop_wait_subscribed "${work}/sub.err" "${topic}" 10; then
        kill "${sub_pid}" 2>/dev/null
        rm -rf "${work}"
        return 1
    fi
    # the node registers the topic before it answers the subscriber, so one publish is enough;
    # a 3001 (NotFoundPeerByTopicSendMsg) on the first try is retried once
    local published attempt rc
    for attempt in 1 2; do
        published=$(ops "${node_dir}" amop pub "${topic}" "${message}" --wait 10) && rc=0 || rc=$?
        if (( rc == 0 )); then
            break
        fi
        if (( attempt == 1 )) && jq -e '.error.code == 3001' >/dev/null 2>&1 <<<"${published}"; then
            echo "scenario 12: first publish saw 3001, retrying once"
            sleep 1
            continue
        fi
        scenario_fail "amop pub ${topic} exited ${rc}: ${published}"
        kill "${sub_pid}" 2>/dev/null
        rm -rf "${work}"
        return 1
    done
    assert_jq "${published}" '.ok == true' "amop pub ok" || { kill "${sub_pid}" 2>/dev/null; rm -rf "${work}"; return 1; }
    # the subscriber echoes the payload back as the reply
    assert_jq "${published}" '.response == "'"${message}"'"' "amop pub got the subscriber's echo" || { kill "${sub_pid}" 2>/dev/null; rm -rf "${work}"; return 1; }
    if ! wait "${sub_pid}"; then
        scenario_fail "amop sub exited non-zero: $(cat "${work}/sub.out") $(cat "${work}/sub.err")"
        rm -rf "${work}"
        return 1
    fi
    local received
    received=$(cat "${work}/sub.out")
    assert_jq "${received}" '.topic == "'"${topic}"'" and .data == "'"${message}"'"' "amop sub printed the message" || { rm -rf "${work}"; return 1; }
    # no subscriber on another topic → the node refuses with 3001
    local refused
    refused=$(ops_expect_exit 2 "${node_dir}" amop pub "${topic}-nobody" x --wait 5) || { rm -rf "${work}"; return 1; }
    assert_jq "${refused}" '.error.code == 3001' "amop pub without subscriber is refused with 3001" || { rm -rf "${work}"; return 1; }
    # the subscriber exited after its one message (unsubscribe): the first topic is gone too
    refused=$(ops_expect_exit 2 "${node_dir}" amop pub "${topic}" "again" --wait 5) || { rm -rf "${work}"; return 1; }
    assert_jq "${refused}" '.error.code == 3001' "amop pub after the subscriber left is refused" || { rm -rf "${work}"; return 1; }
    rm -rf "${work}"
    echo "scenario 12 ok: amop sub/pub round trip, 3001 without a subscriber, on ${chain}"
}
