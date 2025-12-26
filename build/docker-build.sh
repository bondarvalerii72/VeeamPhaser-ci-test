#!/bin/sh -e

NJOBS=${NJOBS:-4}

build_image() {
    docker build -f build/Dockerfile -t phaser-build .
}

build_windows() {
    if [ -n "$DEBUG" ]; then
        ADD="--verbose"
    else
        ADD="-j${NJOBS}"
    fi
    TARGET=${TARGET:-all}
    docker run --rm -v `pwd`:/src -w /src phaser-build sh -c "cmake -B build/build-windows -DCMAKE_TOOLCHAIN_FILE=build/cmake/mingw_toolchain.cmake && cmake --build build/build-windows $ADD --target ${TARGET}"
}

build_linux() {
    if [ `uname -m` = "x86_64" ]; then
        TARGET="all test"
    else
        # cannot run x64 tests on ARM
        TARGET="all"
    fi
    docker run --rm -v `pwd`:/src -w /src phaser-build sh -c "cmake -B build/build-linux -DCMAKE_TOOLCHAIN_FILE=build/cmake/x64_toolchain.cmake && cmake --build build/build-linux -j${NJOBS} --target ${TARGET}"
}

build_docs() {
    echo "[.] Generating documentation with Doxygen..."
    docker run --rm -v `pwd`:/src -w /src/build phaser-build doxygen Doxyfile
    echo "[.] Documentation generated in docs/html/index.html"
}

if [ $# -eq 0 ]; then
    set -- all
fi

while [ -n "$1" ]; do
    case $1 in
        a|all)
            build_image
            build_linux
            build_windows
            build_docs
            ;;
        c|clean)
            for f in build/build-windows build/build-linux docs; do
                echo "[.] removing $f"
                rm -rf $f
            done
            ;;
        d|docs)
            build_image
            build_docs
            ;;
        l|linux)
            build_image
            build_linux
            ;;
        w|windows)
            build_image
            build_windows
            ;;
        *)
            echo "Usage: $0 [all|clean|docs|linux|windows].."
            ;;
    esac
    shift
done
