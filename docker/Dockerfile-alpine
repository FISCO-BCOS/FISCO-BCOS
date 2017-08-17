FROM alpine:3.6

LABEL maintainer hi@bcos.org.cn

RUN echo -e "http://mirrors.aliyun.com/alpine/edge/testing\nhttp://mirrors.aliyun.com/alpine/v3.6/main\nhttp://mirrors.aliyun.com/alpine/v3.6/community\n" > /etc/apk/repositories
RUN apk update && apk add --no-cache \
    libstdc++ \
    # boost-dev \
    boost-system \
    boost-filesystem \
    boost-random \
    boost-regex \
    boost-thread \
    gmp \
    libcurl libmicrohttpd libcrypto1.0 leveldb 

RUN apk add --no-cache --virtual .build-deps \
    unzip \
    git \
    cmake \
    g++ \
    make \
    curl-dev boost-dev libmicrohttpd-dev openssl-dev leveldb-dev \
    && sed -i -E -e 's/include <sys\/poll.h>/include <poll.h>/' /usr/include/boost/asio/detail/socket_types.hpp \
    && git clone https://github.com/bcosorg/bcos.git \
    && cd bcos && mkdir build && cd build \
    # && sed -i 's/boost_thread/boost_thread-mt/g' ../libdevcore/CMakeLists.txt \
    && cmake -DEVMJIT=OFF -DTESTS=OFF -DTOOLS=OFF -DMINIUPNPC=OFF .. \
    && make \
    && make install \
    && mkdir /nodedata && cd ../docker && cp nodeConfig/node-0/* /nodedata \
    && cd / && rm -rf bcos \
    && apk del .build-deps \
    && rm /var/cache/apk/* -f 
    
EXPOSE 35500 53300

CMD ["/usr/local/bin/bcoseth","--genesis","/nodedata/genesis.json", "--config","/nodedata/config.json"]
