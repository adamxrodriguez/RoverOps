FROM node:22-bookworm-slim AS dashboard
WORKDIR /app/dashboard
COPY dashboard/package*.json ./
RUN npm ci
COPY dashboard/ ./
RUN npm run build

FROM ubuntu:24.04 AS cpp
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY CMakeLists.txt ./
COPY core/ core/
COPY service/ service/
COPY tests/ tests/
COPY third_party/ third_party/
COPY config/ config/
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel 2 \
    && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 roverops
WORKDIR /app
COPY --from=cpp /app/build/roverops ./roverops
COPY --from=dashboard /app/dashboard/dist ./dashboard/dist
COPY config/ config/
COPY --chmod=755 deploy/start.sh ./start.sh
USER roverops
ENV PORT=8080
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s \
  CMD curl --fail "http://127.0.0.1:${PORT}/api/health" || exit 1
ENTRYPOINT ["/app/start.sh"]
