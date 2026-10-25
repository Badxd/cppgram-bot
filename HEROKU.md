# Deploy the cppgram bot to Heroku

The bot runs as a `worker` (long polling). It does not listen on `$PORT`.

## 1. Rotate credentials first

If a bot token or API hash was ever pasted in chat or a log, revoke it in BotFather and generate a new one.

## 2. Create the app with the container stack

```sh
heroku login
heroku create APP_NAME --stack container
```

For an existing app: `heroku stack:set container --app APP_NAME`, then check with `heroku stack:get --app APP_NAME`.

## 3. Set config vars

Set them in the dashboard (Settings → Config Vars), or from the CLI without leaving them in shell history:

```sh
read -rs TG_BOT_TOKEN && heroku config:set TG_BOT_TOKEN="$TG_BOT_TOKEN" --app APP_NAME
```

Do the same for `TG_API_ID` and `TG_API_HASH`.

## 4. Deploy

```sh
git init -b main
git add .
git commit -m "cppgram bot"
heroku git:remote --app APP_NAME
git push heroku main
heroku ps:scale worker=1 --app APP_NAME
heroku logs --tail --app APP_NAME
```

The first build compiles the cppgram library and takes a while. To pin a version, change `CPPGRAM_REF` in the Dockerfile (for example to a tag such as `v1.0.0`) and push again.

## Notes

- The session database is in `/tmp` and is lost on every restart. The bot logs in again with the token on start, which is fine for a command bot.
- Heroku dynos restart daily. That is normal.
