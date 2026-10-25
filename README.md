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
