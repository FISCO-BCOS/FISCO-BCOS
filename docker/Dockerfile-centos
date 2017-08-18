FROM centos:7

LABEL maintainer hi@bcos.org.cn

# aliyun
# RUN curl -o /etc/yum.repos.d/CentOS-Base.repo http://mirrors.aliyun.com/repo/Centos-7.repo \
#     && yum clean all && yum makecache fast && yum -y update

RUN yum -y -q install epel-release && yum -q -y install \
    git \
    cmake3 \
    make \
    gcc-c++ \
    boost-devel \
    leveldb-devel \
    curl-devel \
    libmicrohttpd-devel \
    gmp-devel \
    openssl-devel \
    && git clone https://github.com/bcosorg/bcos.git \
    && cd bcos && mkdir build && cd build \ 
    && cmake3 -DEVMJIT=OFF -DTESTS=OFF -DTOOLS=Off -DMINIUPNPC=OFF .. \
    && make \
    && make install \
    && mkdir /nodedata && cd ../docker && cp nodeConfig/node-0/* /nodedata \
    && cd / && rm -rf bcos \
    && yum -y remove git cmake3 make gcc-c++ unzip \
    && yum clean all 

EXPOSE 35500 53300

CMD ["/usr/local/bin/bcoseth","--genesis","/nodedata/genesis.json", "--config","/nodedata/config.json"]
