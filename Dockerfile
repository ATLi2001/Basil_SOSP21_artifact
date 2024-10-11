ARG nproc=2

FROM amd64/ubuntu:20.04
ENV TZ="America/New_York" \
    DEBIAN_FRONTEND=noninteractive

WORKDIR /home

RUN apt-get update
RUN apt-get upgrade

RUN apt-get install -y git wget psmisc lsof
RUN apt install -y python3-pip
RUN pip3 install numpy
RUN apt-get install -y autoconf automake libtool curl make g++ unzip valgrind cmake gnuplot pkg-config ant
RUN apt install -y libsodium-dev libgflags-dev libssl-dev libevent-dev libevent-openssl-2.1-7 libevent-pthreads-2.1-7 libboost-all-dev libuv1-dev ncurses-term

RUN mkdir dependencies
WORKDIR /home/dependencies

# Installing google test
RUN git clone https://github.com/google/googletest.git
WORKDIR /home/dependencies/googletest
RUN git checkout release-1.10.0
RUN cmake CMakeLists.txt
RUN make -j $(nproc)
RUN make install
RUN ldconfig
WORKDIR /home/dependencies
RUN cp -r googletest /usr/src/gtest-1.10.0

# Installing protobuf
RUN git clone https://github.com/protocolbuffers/protobuf.git
WORKDIR /home/dependencies/protobuf
RUN git checkout v3.5.1
RUN ./autogen.sh
RUN ./configure
RUN make -j $(nproc)
RUN make check -j $(nproc)
RUN make install
RUN ldconfig
WORKDIR /home/dependencies

# Installing secp256k1
RUN git clone https://github.com/bitcoin-core/secp256k1.git
WORKDIR /home/dependencies/secp256k1
RUN ./autogen.sh
RUN ./configure
RUN make -j $(nproc)
RUN make check -j $(nproc)
RUN make install
RUN ldconfig
WORKDIR /home/dependencies

# Installing cryptopp
RUN git clone https://github.com/weidai11/cryptopp.git
WORKDIR /home/dependencies/cryptopp
RUN make -j $(nproc)
RUN make install
RUN ldconfig
WORKDIR /home/dependencies

# Installing BLAKE3
RUN git clone https://github.com/BLAKE3-team/BLAKE3
WORKDIR /home/dependencies/BLAKE3/c
RUN gcc -fPIC -shared -O3 -o libblake3.so blake3.c blake3_dispatch.c blake3_portable.c blake3_sse2_x86-64_unix.S blake3_sse41_x86-64_unix.S blake3_avx2_x86-64_unix.S blake3_avx512_x86-64_unix.S
RUN cp libblake3.so /usr/local/lib/
RUN ldconfig
WORKDIR /home/dependencies

# Installing ed25519-donna
RUN git clone https://github.com/floodyberry/ed25519-donna
WORKDIR /home/dependencies/ed25519-donna
RUN gcc -fPIC -shared -O3 -m64 -o libed25519_donna.so ed25519.c -lssl -lcrypto
RUN cp libed25519_donna.so /usr/local/lib
RUN ldconfig
WORKDIR /home/dependencies

# Installing Intel TBB
RUN wget https://registrationcenter-download.intel.com/akdlm/IRC_NAS/e6ff8e9c-ee28-47fb-abd7-5c524c983e1c/l_BaseKit_p_2024.2.1.100.sh
RUN sh ./l_BaseKit_p_2024.2.1.100.sh -a --silent --eula accept --components intel.oneapi.lin.tbb.devel
# RUN source /opt/intel/oneapi/setvars.sh

WORKDIR /home
