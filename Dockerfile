FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    xz-utils \
    libboost-dev \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    libssl-dev \
    nlohmann-json3-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src/bot
COPY . .

# Prebuilt cppgram (headers + libcppgram.a) downloaded from the GitHub release.
ARG CPPGRAM_LIB_URL=https://github.com/Badxd/cppgram/releases/download/V1/cppgram_lib.tar.xz
RUN curl -fsSL "$CPPGRAM_LIB_URL" | tar -xJ -C /src/bot

RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCPPGRAM_LIB_DIR=/src/bot/cppgram_lib/lib \
      -DCPPGRAM_INCLUDE_DIR=/src/bot/cppgram_lib/include \
    && cmake --build build --target cppgram_bot --parallel 2

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libcurl4t64 \
    libsqlite3-0 \
    libssl3t64 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp
COPY --from=builder /src/bot/build/cppgram_bot /app/cppgram_bot

CMD ["/app/cppgram_bot"]
