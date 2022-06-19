FROM ubuntu:20.04

RUN apt update && \
    apt install -y --no-install-recommends python3-pip g++ make cmake && \
    pip3 install conan

RUN conan profile new default --detect &&\
    sed -i 's/compiler.libcxx=libstdc++/compiler.libcxx=libstdc++11/g' /root/.conan/profiles/default

ADD profiles /pre/profiles
ADD conanfile.txt /pre

RUN mkdir /pre/build && cd /pre/build && \
    conan install .. --profile ../profiles/release-native --build missing

ADD . /final

RUN cd /final && \
    make release

ENTRYPOINT ["/final/build-release/bin/run", "--listen", "12000"]
