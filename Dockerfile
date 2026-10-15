# Stage 1: Build C++ MiniRedis Engine
FROM ubuntu:22.04 AS cpp-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY server/ ./server/

WORKDIR /app/server/build
RUN cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) miniredis-server

# Stage 2: Build React Dashboard Assets
FROM node:20-alpine AS dashboard-builder

WORKDIR /app/dashboard
COPY dashboard/package*.json ./
RUN npm install

COPY dashboard/ ./
RUN npm run build

# Stage 3: Minimal Production Runtime
FROM node:20-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy C++ server binary into system PATH
COPY --from=cpp-builder /app/server/build/miniredis-server /usr/local/bin/miniredis-server

# Copy Node.js bridge server
COPY bridge/package*.json ./bridge/
RUN cd bridge && npm install --omit=dev
COPY bridge/src/ ./bridge/src/

# Copy built React dashboard static files
COPY --from=dashboard-builder /app/dashboard/dist ./dashboard/dist

# Copy entrypoint script
COPY docker-entrypoint.sh ./
RUN chmod +x docker-entrypoint.sh

ENV PORT=4000 \
    REDIS_HOST=127.0.0.1 \
    REDIS_PORT=6379 \
    DASHBOARD_DIST=/app/dashboard/dist

EXPOSE 4000 6379

ENTRYPOINT ["/app/docker-entrypoint.sh"]
