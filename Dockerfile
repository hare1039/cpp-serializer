FROM gcc:12

RUN apt-get update && \
    apt-get install -y --no-install-recommends python3-pip make cmake ninja-build ccache && \
    pip3 install conan

RUN conan profile new default --detect &&\
    sed -i 's/compiler.libcxx=libstdc++/compiler.libcxx=libstdc++11/g' /root/.conan/profiles/default

ADD profiles /pre/profiles
ADD conanfile.txt /pre

RUN --mount=type=cache,target=/ccache \
    mkdir /pre/build && cd /pre/build && \
    conan install .. --profile ../profiles/release-native --build missing && \
    conan install .. --profile ../profiles/debug --build missing

ADD . /final

ARG debug

RUN --mount=type=cache,target=/final/build \
    cd /final && \
    bash -c 'echo debug=$debug; if [[ -z "$debug" ]]; then make release; else make debug; fi' && \
    cp /final/build/bin/* /bin && \
    chmod +x /bin/run && chmod +x /bin/slsfs-client

ENTRYPOINT ["/bin/run", "--listen", "12000"]
