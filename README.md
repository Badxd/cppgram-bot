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


## Session string generator

`cppgram_session_generator` logs in to a **user account** (phone number, login code, and the 2FA password if set)
and prints a session string you can use as `ClientOptions::session_string`.

```sh
cmake --build build --target cppgram_session_generator
./build/cppgram_session_generator
```

It asks for API ID, API HASH (from https://my.telegram.org) and the phone number. You can also set `TG_API_ID`,
`TG_API_HASH` and `TG_PHONE` instead. With Docker: `docker run -it <image> /app/cppgram_session_generator`.

The session string gives full access to the account. Never share it or commit it. To revoke it, terminate the
"cppgram" session in Telegram under Settings > Devices.

It also sends the session string to the account's own Saved Messages, so you can copy it from your phone. If
that send fails (for example a very fresh account with Saved Messages restricted), the string printed in the
terminal is still valid.
