FROM ubuntu:20.04 as builder
LABEL maintainer service@fisco.com.cn

WORKDIR /

ARG SOURCE_BRANCH
ENV DEBIAN_FRONTEND=noninteractive \
    SOURCE=${SOURCE_BRANCH:-master}
RUN apt-get -q update && apt-get install -qy --no-install-recommends \
    curl git clang make build-essential cmake libssl-dev zlib1g-dev ca-certificates \
    libgmp-dev flex bison patch libzstd-dev\
    && ln -fs /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
    && apt-get install -qy --no-install-recommends tzdata \
    && dpkg-reconfigure --frontend noninteractive tzdata \
    && rm -rf /var/lib/apt/lists/* 

RUN curl https://sh.rustup.rs -sSf | bash -s -- -y

ENV PATH="/root/.cargo/bin:${PATH}"

# Check cargo is visible
RUN cargo --help

RUN git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git && cd FISCO-BCOS \
    && git checkout ${SOURCE} && cmake . && make -j2 && strip fisco-bcos-air/fisco-bcos

FROM ubuntu:20.04
LABEL maintainer service@fisco.com.cn

RUN apt-get -q update && apt-get install -qy --no-install-recommends libssl-dev zlib1g-dev libzstd-dev\
    && ln -fs /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
    && apt-get install -qy --no-install-recommends tzdata \
    && dpkg-reconfigure --frontend noninteractive tzdata \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /FISCO-BCOS/fisco-bcos-air/fisco-bcos /usr/local/bin/

EXPOSE 30300 20200

ENTRYPOINT ["/usr/local/bin/fisco-bcos"]
CMD ["--version"]