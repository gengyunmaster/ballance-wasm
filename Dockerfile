FROM emscripten/emsdk:6.0.13 AS build

ARG BUILD_PARALLELISM=4

SHELL ["/bin/bash", "-euo", "pipefail", "-c"]
WORKDIR /opt/ballance

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl fonts-liberation git ninja-build python3 && rm -rf /var/lib/apt/lists/*
COPY . .

RUN mkdir -p .tools .deps \
    && ln -s "$EMSDK" .tools/emsdk \
    && ln -s "$(dirname "$(command -v cmake)")" .tools/cmake \
    && ln -s "$(dirname "$(command -v ninja)")" .tools/ninja \
    && curl -fL --retry 3 https://github.com/libsdl-org/SDL/releases/download/release-3.4.14/SDL3-3.4.14.tar.gz -o .deps/SDL3-3.4.14.tar.gz \
    && tar -xzf .deps/SDL3-3.4.14.tar.gz -C .deps \
    && rm .deps/SDL3-3.4.14.tar.gz \
    && emcmake cmake -S .deps/SDL3-3.4.14 -B .deps/build-sdl-wasm -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/ballance/.deps/sdl-wasm \
        -DSDL_SHARED=OFF \
        -DSDL_STATIC=ON \
        -DSDL_TESTS=OFF \
        -DSDL_EXAMPLES=OFF \
        -DSDL_PTHREADS=ON \
    && cmake --build .deps/build-sdl-wasm --parallel "${BUILD_PARALLELISM}" \
    && cmake --install .deps/build-sdl-wasm \
    && ./scripts/build-wasm.sh \
    && cmake --build build/wasm-release --target stage --parallel "${BUILD_PARALLELISM}" \
    && ./scripts/test-wasm.sh \
    && mkdir -p /opt/ballance-runtime \
    && cp -a build/wasm-release/stage/Bin/. /opt/ballance-runtime/

FROM nginx:1.29-alpine

COPY docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /opt/ballance-runtime /usr/share/nginx/html
