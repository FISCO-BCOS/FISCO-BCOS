# FISCO-BCOS hive client image (Ethereum L1 EL mode).
#
# This Dockerfile wraps PREBUILT binaries: it expects
#   bin/fisco-bcos       (the node binary, build/fisco-bcos-air/fisco-bcos)
#   bin/eth-sync-check   (the genesis/enode helper, build/tools/eth-sync-check/eth-sync-check)
# in the build context. Use tools/hive/build-local.sh to collect them.
#
# The optional `tag` build argument records the client version into /version.txt.
# Base tracks the host toolchain: binaries built with gcc-16 need
# GLIBCXX_3.4.35, which ubuntu:24.04's libstdc++ predates.
FROM ubuntu:26.04

ARG tag=local

# Package versions are intentionally unpinned: the image tracks ubuntu:26.04 as
# the glibc/libstdc++ baseline and must float with its security updates.
# hadolint ignore=DL3008
RUN sed -i 's|//archive.ubuntu.com|//mirrors.aliyun.com|; s|//security.ubuntu.com|//mirrors.aliyun.com|' /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl jq openssl xxd libssl3t64 \
    # ASan runtime is only needed when the collected binaries are a sanitizer
    # build; install whichever soname the base carries, tolerate absence.
    && (apt-get install -y --no-install-recommends libasan9 libubsan1 \
        || apt-get install -y --no-install-recommends libasan8 libubsan1 || true) \
    && rm -rf /var/lib/apt/lists/*

# Default genesis (Cancun-at-genesis, post-merge). hive bind-mounts the
# simulator-supplied /genesis.json over this when a test provides one.
COPY genesis.json /genesis.json

COPY bin/fisco-bcos /usr/local/bin/fisco-bcos
COPY bin/eth-sync-check /usr/local/bin/eth-sync-check

COPY bcos.sh /bcos.sh
COPY enode.sh /hive-bin/enode.sh
COPY config.ini.tpl /bcos/config.ini.tpl

RUN chmod +x /bcos.sh /hive-bin/enode.sh \
    && mkdir -p /bcos \
    && echo "fisco-bcos ${tag}" > /version.txt

# 8545: JSON-RPC HTTP, 8551: Engine API, 30303: devp2p (outbound-only for now)
EXPOSE 8545 8551 30303 30303/udp

WORKDIR /bcos
ENTRYPOINT ["/bcos.sh"]
