FROM ubuntu:18.04

LABEL maintainer service@fisco.com.cn

WORKDIR /

RUN apt-get -q update && apt-get install -qy --no-install-recommends \
    git clang make build-essential cmake libssl-dev zlib1g-dev ca-certificates \
    && export DEBIAN_FRONTEND=noninteractive \
    && ln -fs /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
    && apt-get install -qy --no-install-recommends tzdata \
    && dpkg-reconfigure --frontend noninteractive tzdata \
    && git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git \
    && cd FISCO-BCOS && git checkout master \
    && cmake . && make && make install \
    && cd / && rm -rf FISCO-BCOS \
    && apt-get purge git cmake build-essential -y \ 
    && apt-get autoremove -y \
    && apt-get clean \
    && rm  -rf /tmp/* \
    && rm -rf /var/lib/apt/lists/*

EXPOSE 30300 20200 8545

ENTRYPOINT ["/usr/local/bin/fisco-bcos"]
CMD ["--version"]
