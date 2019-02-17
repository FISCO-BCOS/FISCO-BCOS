FROM ubuntu:16.04

LABEL maintainer service@fisco.com.cn

RUN apt-get -q update && apt-get install -qy --no-install-recommends \
    git clang make build-essential cmake libssl-dev libleveldb-dev libsnappy-dev \
    ca-certificates \
    && git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git \
    && cd FISCO-BCOS \
    && mkdir build && cd build \ 
    && cmake .. \
    && make \
    && make install \
    && cd / && rm -rf FISCO-BCOS \
    && apt-get purge git cmake build-essential -y \ 
    && apt-get autoremove -y \
    && apt-get clean \
    && rm /tmp/* -rf \
    && rm -rf /var/lib/apt/lists/*

EXPOSE 30300 20200 8545

ENTRYPOINT ["/usr/local/bin/fisco-bcos"]
CMD ["--version"]
