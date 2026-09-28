# Stage 1: Build C++ MiniRedis binary
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

WORKDIR /app/build
RUN cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) miniredis-server

# Stage 2: Minimal runtime image
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/miniredis-server /usr/local/bin/miniredis-server

EXPOSE 6379

ENTRYPOINT ["miniredis-server"]
CMD ["6379", "0.0.0.0"]
