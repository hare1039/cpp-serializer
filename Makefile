RELEASE_FLAGS=-static -s
VERBOSE=-DCMAKE_VERBOSE_MAKEFILE=ON

CC ?= cc
CXX ?= c++

.PHONY: release debug from-docker setup

from-docker:
	docker build -t hare1039/transport:0.0.1 .

debug-from-docker:
	docker build -t hare1039/transport:0.0.1 . --build-arg debug=true


release-all: release debug
	echo "build all"

release:
	mkdir -p build && \
	cd build && \
	conan install .. --profile ../profiles/release-native --build missing && \
	cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_C_COMPILER=${CC}   \
             -DCMAKE_CXX_COMPILER=${CXX} && \
	cmake --build .

debug:
	mkdir -p build && \
    cd build && \
    conan install .. --profile ../profiles/debug --build missing && \
    cmake .. -DCMAKE_BUILD_TYPE=Debug \
             -DCMAKE_C_COMPILER=${CC}   \
             -DCMAKE_CXX_COMPILER=${CXX} && \
    cmake --build .

clean:
	rm -rf build-*
