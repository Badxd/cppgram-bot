FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libboost-dev \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    libssl-dev \
    nlohmann-json3-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# The cppgram library is fetched at build time. Pin a branch or tag with --build-arg CPPGRAM_REF=<ref>.
ARG CPPGRAM_REPO=https://github.com/Badxd/cppgram.git
ARG CPPGRAM_REF=main
RUN git clone --depth 1 --branch "${CPPGRAM_REF}" "${CPPGRAM_REPO}" /src/cppgram

WORKDIR /src/bot
COPY . .

RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS_RELEASE="-O0 -DNDEBUG" \
      -DCPPGRAM_SOURCE_DIR=/src/cppgram \
    && cmake --build build --target cppgram_bot --parallel 2

FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libcurl4 \
    libsqlite3-0 \
    libssl3 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

# Heroku's filesystem is ephemeral, so the session database lives in /tmp and is recreated after a restart.
WORKDIR /tmp
COPY --from=builder /src/bot/build/cppgram_bot /app/cppgram_bot

CMD ["/app/cppgram_bot"]
