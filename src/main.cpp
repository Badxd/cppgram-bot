// cppgram echo/utility bot.
// Required environment variables: TG_API_ID, TG_API_HASH, TG_BOT_TOKEN.
// Optional: TG_SESSION (default bot.sqlite).

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>

#include <deque>
#include <map>
#include <memory>

#include "cppgram/client.h"
#include "cppgram/mtproto/session.h"

namespace {

std::string env_or_throw(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(std::string("missing environment variable ") + name);
    }
    return value;
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

}  // namespace

int main() {
    try {
        const std::string api_id = env_or_throw("TG_API_ID");
        const std::string api_hash = env_or_throw("TG_API_HASH");
        const std::string token = env_or_throw("TG_BOT_TOKEN");
        const char* session = std::getenv("TG_SESSION");
        const std::string session_base = (session && *session) ? session : "bot";

        std::unique_ptr<cppgram::Client> client_ptr = connect_and_login(api_id, api_hash, token, session_base);
        cppgram::Client& client = *client_ptr;
        client.on_error([](cppgram::Client&, const std::string& stage, const std::string& message) {
            std::cerr << "handler error in " << stage << ": " << message << "\n";
        });

        const auto me = client.get_me();
        std::cout << "logged in as @" << me.username << "\n";

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
                                          "/time - server time (UTC)",
                                          cppgram::ParseMode::Html);
                          });

        client.on_message(cppgram::filters::command("echo"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              const std::string body = args_of(m.text);
                              if (body.empty()) {
                                  c.send_text(m.chat, "Usage: /echo &lt;text&gt;", cppgram::ParseMode::Html);
                                  return;
                              }
                              c.send_text(m.chat, html_escape(body), cppgram::ParseMode::Html);
                          });

        client.on_message(cppgram::filters::command("id"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              const std::int64_t user_id = m.sender ? m.sender->id : 0;
                              c.send_text(m.chat,
                                          "chat id: " + std::to_string(m.chat.id) +
                                              "\nuser id: " + std::to_string(user_id));
                          });

        client.on_message(cppgram::filters::command("time"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              c.send_text(m.chat, utc_now());
                          });

        std::cout << "listening, press Ctrl+C to stop\n";
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
