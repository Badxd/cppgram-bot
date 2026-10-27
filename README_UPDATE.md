# cppgram utility bot (updated)

## Env (Heroku config vars ya VPS env)
- TG_API_ID, TG_API_HASH: my.telegram.org se
- TG_BOT_TOKEN: bot token (pehli baar login ke liye)
- TG_SESSION_STRING: session string (baad mein, token ki zaroorat nahi)
- PRINT_SESSION=1: ek baar SESSION_STRING= line print karega (log ko secret samjho)

## Session string kaise banegi
1. TG_BOT_TOKEN aur PRINT_SESSION=1 ke saath bot chalao.
2. Log mein SESSION_STRING=... dikhegi, usko copy karo.
3. Heroku/VPS mein TG_SESSION_STRING set karo, aur TG_BOT_TOKEN hata do.
4. Bot dobara chalao: login ok from session string dikhna chahiye.

## Naye commands
/invite <hash>, /botcmds, /resetcmds, /userfeatures

Sab user-account wale features (stories, gifts, business, etc) bot se nahi chalte, /userfeatures dekho.
