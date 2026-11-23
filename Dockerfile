# ---- build stage ----
FROM ubuntu:24.04 AS build

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential cmake ca-certificates libasio-dev libsoci-dev libpq-dev libboost-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)" \
    && mkdir -p /out && cp -a /usr/lib/*/libsoci_*.so* /out/

# ---- runtime stage ----
FROM ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libpq5 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /out/ /usr/local/lib/
RUN ldconfig

RUN useradd --system --create-home --home-dir /app app
WORKDIR /app
COPY --from=build /src/build/IPInventoryApi /app/IPInventoryApi
COPY static /app/static
USER app

ENV PORT=18080 \
    STATIC_DIR=/app/static

EXPOSE 18080
CMD ["/app/IPInventoryApi"]
