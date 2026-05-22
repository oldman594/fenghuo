default:
    @just --list --unsorted

build:
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build

test: build
    ctest --test-dir build --output-on-failure

clean:
    rm -rf build

container-build:
    base/container/podman/cli/fenghuo.sh build

container-shell:
    base/container/podman/cli/fenghuo.sh shell

container-test:
    base/container/podman/cli/fenghuo.sh run just test
