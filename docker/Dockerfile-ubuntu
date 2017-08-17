FROM ubuntu:16.04

LABEL maintainer hi@bcos.org.cn

## tencentyun
# RUN echo "deb http://mirrors.tencentyun.com/ubuntu xenial main restricted universe multiverse \n \
#     deb http://mirrors.tencentyun.com/ubuntu xenial-updates main restricted universe multiverse \n \
#     deb http://mirrors.tencentyun.com/ubuntu-security xenial-security main restricted universe multiverse \n \
#     deb-src http://mirrors.tencentyun.com/ubuntu xenial main restricted universe multiverse \n \
#     deb-src http://mirrors.tencentyun.com/ubuntu xenial-updates main restricted universe multiverse" > /etc/apt/sources.list 

## aliyun
RUN echo "deb http://mirrors.aliyun.com/ubuntu/ xenial main restricted \n \
deb-src http://archive.ubuntu.com/ubuntu xenial main restricted #Added by software-properties \n \
deb-src http://mirrors.aliyun.com/ubuntu/ xenial main restricted multiverse universe #Added by software-properties \n \
deb http://mirrors.aliyun.com/ubuntu/ xenial-updates main restricted \n \
deb-src http://mirrors.aliyun.com/ubuntu/ xenial-updates main restricted multiverse universe #Added by software-properties \n \
deb http://mirrors.aliyun.com/ubuntu/ xenial universe \n \
deb http://mirrors.aliyun.com/ubuntu/ xenial-updates universe \n \
deb http://mirrors.aliyun.com/ubuntu/ xenial multiverse \n \
deb http://mirrors.aliyun.com/ubuntu/ xenial-updates multiverse \n \
deb http://mirrors.aliyun.com/ubuntu/ xenial-backports main restricted universe multiverse \n \
deb-src http://mirrors.aliyun.com/ubuntu/ xenial-backports main restricted universe multiverse #Added by software-properties \n \
deb http://archive.canonical.com/ubuntu xenial partner \n \
deb-src http://archive.canonical.com/ubuntu xenial partner \n \
deb http://mirrors.aliyun.com/ubuntu/ xenial-security main restricted \n \
deb-src http://mirrors.aliyun.com/ubuntu/ xenial-security main restricted multiverse universe #Added by software-properties \n \
deb http://mirrors.aliyun.com/ubuntu/ xenial-security universe \n \
deb http://mirrors.aliyun.com/ubuntu/ xenial-security multiverse" > /etc/apt/sources.list 

RUN apt-get -q update && apt-get install -qy --no-install-recommends \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    libleveldb-dev \
    libcurl4-openssl-dev \
    libgmp-dev \
    libmicrohttpd-dev \
    libminiupnpc-dev \
    libssl-dev \
    ca-certificates \
    && git clone https://github.com/bcosorg/bcos.git \
    && cd bcos && mkdir build && cd build \ 
    && cmake -DEVMJIT=OFF -DTESTS=OFF -DTOOLS=Off -DMINIUPNPC=OFF .. \
    && make \
    && make install \
    && mkdir /nodedata && cd ../docker && cp nodeConfig/node-0/* /nodedata \
    && cd / && rm -rf bcos \
    # clean 
    && apt-get purge git cmake build-essential -y \ 
    && apt-get autoremove -y \
    && apt-get clean \
    && rm /tmp/* -rf \
    && rm -rf /var/lib/apt/lists/*

EXPOSE 35500 53300

CMD ["/usr/local/bin/bcoseth","--genesis","/nodedata/genesis.json", "--config","/nodedata/config.json"]

