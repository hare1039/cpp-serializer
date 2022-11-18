FROM gcc:12

RUN apt-get update && \
    apt-get install -y --no-install-recommends python3-pip make cmake ninja-build ccache git wget libzookeeper-mt-dev gdb && \
    pip3 install conan

RUN conan profile new default --detect &&\
    conan profile update settings.compiler.libcxx=libstdc++11 default

ADD profiles /pre/profiles
ADD conanfile.txt /pre

RUN --mount=type=cache,target=/ccache \
    mkdir /pre/build && cd /pre/build && \
    conan install .. --profile ../profiles/release-native --build missing && \
    conan install .. --profile ../profiles/debug --build missing

ADD zookeeper-cpp /zookeeper-cpp

RUN mkdir /zookeeper-cpp/build && cd /zookeeper-cpp/build && \
    conan install .. --profile /pre/profiles/release-native --build missing && \
    cmake .. -G Ninja\
             -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_CXX_FLAGS='-std=c++17 -include "utility" -DZKPP_FUTURE_USE_BOOST=1' && \
    cmake --build . && ninja install

ADD . /final

ARG debug

RUN --mount=type=cache,target=/final/build \
    cd /final && \
    bash -c 'if [[ -z "$debug" ]]; then make release; else make debug; fi' && \
    cp /final/build/bin/* /bin && \
    chmod +x /bin/run && chmod +x /bin/slsfs-client

ENTRYPOINT ["/bin/run"]
