<p align="center">
  <a href="https://dashboard.heroku.com/new?template=https://github.com/Badxd/cppgram-bot"><img src="https://img.shields.io/badge/Deploy%20to-Heroku-430098?style=for-the-badge&logo=heroku&logoColor=white" alt="Deploy to Heroku"></a>
  


# cppgram bot

Small command bot built on the cppgram MTProto client.

Commands: `/start`, `/help`, `/echo <text>`, `/id`, `/time`.

## Build

Option A, from a cppgram source checkout:

```sh
cmake -S . -B build -DCPPGRAM_SOURCE_DIR=/path/to/cppgram
cmake --build build
```

Option B, from an installed cppgram (`cmake --install` into a prefix on `CMAKE_PREFIX_PATH`):

```sh
cmake -S . -B build
cmake --build build
```

## Run

Credentials come only from environment variables. Never put them in code or in git:

```sh
export TG_API_ID=12345
export TG_API_HASH=your_api_hash
export TG_BOT_TOKEN=your_bot_token
./build/cppgram_bot
```

Optional: `TG_SESSION=path/to/bot.sqlite` (default `bot.sqlite`) keeps the login and update state.

## Prebuilt cppgram (no library build)

Put the prebuilt library next to this project so the folder looks like this:

```
cppgram-bot/
  cppgram_lib/
    include/
    lib/libcppgram.a
  src/main.cpp
  CMakeLists.txt
  Dockerfile
```

Extract `cppgram_lib.tar.xz` into the project folder (`tar -xJf cppgram_lib.tar.xz`). Then:

```sh
cmake -S . -B build -DCPPGRAM_LIB_DIR=$PWD/cppgram_lib/lib -DCPPGRAM_INCLUDE_DIR=$PWD/cppgram_lib/include
cmake --build build --target cppgram_bot
```

On Heroku the Dockerfile uses the same folder, so cppgram is never compiled there.
