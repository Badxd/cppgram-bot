FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libboost-dev \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    libssl-dev \
    nlohmann-json3-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Prebuilt cppgram (headers + libcppgram.a) is copied in, so the library is not compiled on Heroku.
WORKDIR /src/bot
COPY . .

RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCPPGRAM_LIB_DIR=/src/bot/cppgram_lib/lib \
      -DCPPGRAM_INCLUDE_DIR=/src/bot/cppgram_lib/include \
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
