// cppgram utility bot: echo, ids, invites, bot command lists, and session-string login.
// Required env: TG_API_ID, TG_API_HASH, TG_BOT_TOKEN.
// Optional env:
//   TG_SESSION         base name for the sqlite session files (default "bot")
//   TG_SESSION_STRING  base64 session string; used to log in without a token
//   PRINT_SESSION=1    print SESSION_STRING= once at startup (treat the log as a secret)

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "cppgram/client.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/session_string.h"

namespace {

std::string env_or_throw(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(std::string("missing environment variable ") + name);
    }
    return value;
}

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

// Escapes text for Telegram's HTML parse mode so user input cannot inject tags.
std::string html_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Returns the text after "/command" (trimmed), or an empty string.
std::string args_of(const std::string& text) {
    const std::size_t space = text.find(' ');
    if (space == std::string::npos) return "";
    std::size_t start = space;
    while (start < text.size() && text[start] == ' ') ++start;
    return text.substr(start);
}

std::string utc_now() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[64];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&t));
    return buf;
}

const std::map<int, std::string> kDcAddresses = {
    {1, "149.154.175.53"},
    {2, "149.154.167.51"},
    {3, "149.154.175.100"},
    {4, "149.154.167.91"},
    {5, "91.108.56.130"},
};

// Telegram answers "USER_MIGRATE_N" when the account lives on DC N.
int migrate_target(const std::string& message) {
    const std::size_t pos = message.find("MIGRATE_");
    if (pos == std::string::npos) return 0;
    return std::atoi(message.c_str() + pos + 8);
}

// Starts on DC2 and follows migrate hints until the bot logs in. Each DC gets its own session file.
std::unique_ptr<cppgram::Client> connect_and_login(const std::string& api_id, const std::string& api_hash,
                                                    const std::string& token, const std::string& session_base) {
    std::deque<int> queue{2};
    std::map<int, bool> tried;
    while (!queue.empty()) {
        const int dc = queue.front();
        queue.pop_front();
        if (tried[dc]) continue;
        tried[dc] = true;

        const auto addr = kDcAddresses.find(dc);
        if (addr == kDcAddresses.end()) continue;

        cppgram::ClientOptions opt;
        opt.api_id = std::stoi(api_id);
        opt.api_hash = api_hash;
        opt.dc_id = dc;
        opt.dc_ip = addr->second;
        opt.dc_port = 443;
        opt.session_path = session_base + "-dc" + std::to_string(dc) + ".sqlite";

        auto client = std::make_unique<cppgram::Client>(opt);
        try {
            client->connect();
            client->login_bot(token);
            std::cout << "login ok on DC " << dc << "\n";
            return client;
        } catch (const cppgram::mtproto::RpcError& e) {
            const int target = migrate_target(e.what());
            std::cerr << "login on DC " << dc << " failed: " << e.what() << "\n";
            if (target != 0 && !tried[target]) queue.push_front(target);
            client->disconnect();
        }
    }
    throw std::runtime_error("bot could not log in on any DC");
}

// Restores a session from a string made by export_session_string(). The DC comes from the string.
std::unique_ptr<cppgram::Client> connect_from_string(const std::string& api_id, const std::string& api_hash,
                                                      const std::string& text, const std::string& session_base) {
    cppgram::mtproto::SessionData data;
    if (!cppgram::decode_session_string(text, data)) {
        throw std::runtime_error("TG_SESSION_STRING is malformed");
    }
    const auto addr = kDcAddresses.find(data.dc_id);
    if (addr == kDcAddresses.end()) throw std::runtime_error("session string names an unknown DC");

    cppgram::ClientOptions opt;
    opt.api_id = std::stoi(api_id);
    opt.api_hash = api_hash;
    opt.dc_id = data.dc_id;
    opt.dc_ip = addr->second;
    opt.dc_port = 443;
    opt.session_path = session_base + "-string.sqlite";
    opt.session_string = text;

    auto client = std::make_unique<cppgram::Client>(opt);
    client->connect();
    std::cout << "login ok from session string (DC " << data.dc_id << ")\n";
    return client;
}

}  // namespace

int main() {
    try {
        const std::string api_id = env_or_throw("TG_API_ID");
        const std::string api_hash = env_or_throw("TG_API_HASH");
        const std::string session_base = env_or_empty("TG_SESSION").empty() ? "bot" : env_or_empty("TG_SESSION");
        const std::string session_string = env_or_empty("TG_SESSION_STRING");

        std::unique_ptr<cppgram::Client> client_ptr;
        if (!session_string.empty()) {
            client_ptr = connect_from_string(api_id, api_hash, session_string, session_base);
        } else {
            client_ptr = connect_and_login(api_id, api_hash, env_or_throw("TG_BOT_TOKEN"), session_base);
        }
        cppgram::Client& client = *client_ptr;
        client.on_error([](cppgram::Client&, const std::string& stage, const std::string& message) {
            std::cerr << "handler error in " << stage << ": " << message << "\n";
        });

        const auto me = client.get_me();
        std::cout << "logged in as @" << me.username << " id=" << me.id << "\n";

        if (env_or_empty("PRINT_SESSION") == "1") {
            std::cout << "SESSION_STRING=" << client.export_session_string() << "\n";
        }

        client.on_message(cppgram::filters::command("start"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              c.send_text(m.chat,
                                          "<b>Hi!</b> I am a cppgram bot.\nSend /help to see what I can do.",
                                          cppgram::ParseMode::Html);
                          });

        client.on_message(cppgram::filters::command("help"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              c.send_text(m.chat,
                                          "<b>Commands</b>\n"
                                          "/start - greeting\n"
                                          "/help - this list\n"
                                          "/echo &lt;text&gt; - repeat text back\n"
                                          "/id - chat and user ids\n"
                                          "/time - server time (UTC)\n"
                                          "/invite &lt;hash&gt; - preview an invite link (no join)\n"
                                          "/botcmds - list the bot's command menu\n"
                                          "/resetcmds - clear the bot's command menu\n"
                                          "/userfeatures - what needs a user account (not runnable by a bot)",
                                          cppgram::ParseMode::Html);
                          });

        client.on_message(cppgram::filters::command("echo"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              const std::string text = args_of(m.text);
                              if (text.empty()) {
                                  c.send_text(m.chat, "usage: /echo &lt;text&gt;", cppgram::ParseMode::Html);
                                  return;
                              }
                              c.send_text(m.chat, html_escape(text), cppgram::ParseMode::Html);
                          });

        client.on_message(cppgram::filters::command("id"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              // In a private chat the other person is the chat itself, so use it when no sender is set.
                              std::int64_t user_id = 0;
                              if (m.sender) {
                                  user_id = m.sender->id;
                              } else if (m.chat.kind == cppgram::PeerKind::User) {
                                  user_id = m.chat.id;
                              }
                              c.send_text(m.chat,
                                          "chat id: " + std::to_string(m.chat.id) +
                                              "\nuser id: " + std::to_string(user_id));
                          });

        client.on_message(cppgram::filters::command("time"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              c.send_text(m.chat, utc_now());
                          });

        client.on_message(cppgram::filters::command("invite"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              const std::string hash = args_of(m.text);
                              if (hash.empty()) {
                                  c.send_text(m.chat, "usage: /invite <hash>");
                                  return;
                              }
                              try {
                                  const cppgram::ChatInvitePreview p = c.check_chat_invite(hash);
                                  if (p.already_joined) {
                                      c.send_text(m.chat, "the account is already a member of this chat");
                                      return;
                                  }
                                  std::string out = "title: " + p.title +
                                                    "\nmembers: " + std::to_string(p.participants_count) +
                                                    "\npublic: " + (p.is_public ? "yes" : "no") +
                                                    "\njoin needs approval: " + (p.request_needed ? "yes" : "no");
                                  if (p.peek) out += "\npreview expires (unix): " + std::to_string(p.expires);
                                  c.send_text(m.chat, out);
                              } catch (const std::exception& e) {
                                  c.send_text(m.chat, std::string("error: ") + e.what());
                              }
                          });

        client.on_message(cppgram::filters::command("botcmds"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              try {
                                  const auto cmds = c.get_bot_commands();
                                  if (cmds.empty()) {
                                      c.send_text(m.chat, "no commands set");
                                      return;
                                  }
                                  std::string out = "commands:";
                                  for (const auto& cmd : cmds) {
                                      out += "\n/" + cmd.command + " - " + cmd.description;
                                  }
                                  c.send_text(m.chat, out);
                              } catch (const std::exception& e) {
                                  c.send_text(m.chat, std::string("error: ") + e.what());
                              }
                          });

        client.on_message(cppgram::filters::command("resetcmds"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              try {
                                  const bool ok = c.reset_bot_commands();
                                  c.send_text(m.chat, ok ? "command menu cleared" : "server said no");
                              } catch (const std::exception& e) {
                                  c.send_text(m.chat, std::string("error: ") + e.what());
                              }
                          });

        client.on_message(cppgram::filters::command("userfeatures"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              c.send_text(m.chat,
                                          "Ye features user account chahte hain, bot se nahi chalte:\n"
                                          "stories (post, read, react, hide), star gifts (save, convert, upgrade, transfer, collections),\n"
                                          "business todo/checklists, polls stop, payment receipts, folder invites,\n"
                                          "account (cloud password status, phone), connected business bots list.");
                          });

        std::cout << "listening, press Ctrl+C to stop\n";
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
