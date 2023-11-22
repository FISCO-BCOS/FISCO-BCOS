FROM ubuntu:20.04 as builder
LABEL maintainer service@fisco.com.cn

WORKDIR /

ARG SOURCE_BRANCH
ARG TARGETARCH

ENV DEBIAN_FRONTEND=noninteractive \
    SOURCE=${SOURCE_BRANCH:-master-2.0}

RUN apt-get -q update && apt-get install -qy --no-install-recommends \
    git clang make build-essential wget libssl-dev zlib1g-dev ca-certificates \
    libgmp-dev flex bison patch \
    && ln -fs /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
    && apt-get install -qy --no-install-recommends tzdata \
    && dpkg-reconfigure --frontend noninteractive tzdata \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q https://github.com/Kitware/CMake/releases/download/v3.21.0/cmake-3.21.0-Linux-x86_64.sh \
    && mkdir /opt/cmake \
    && sh cmake-3.21.0-Linux-x86_64.sh --prefix=/opt/cmake --skip-license \
    && ln -s /opt/cmake/bin/cmake /usr/local/bin/cmake \
    && rm cmake-3.21.0-Linux-x86_64.sh \

RUN git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git && cd FISCO-BCOS \
    && git checkout ${SOURCE} && if [ "${TARGETARCH}" != "amd64" ]; then cmake -DARCH_NATIVE=on . ;else cmake .;fi && make -j2 && strip bin/fisco-bcos

FROM ubuntu:20.04
LABEL maintainer service@fisco.com.cn

RUN apt-get -q update && apt-get install -qy --no-install-recommends libssl-dev zlib1g-dev \
    && ln -fs /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
    && apt-get install -qy --no-install-recommends tzdata \
    && dpkg-reconfigure --frontend noninteractive tzdata \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /FISCO-BCOS/bin/fisco-bcos /usr/local/bin/

EXPOSE 30300 20200 8545

ENTRYPOINT ["/usr/local/bin/fisco-bcos"]
CMD ["--version"]

