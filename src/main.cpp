// cppgram echo/utility bot.
// Required environment variables: TG_API_ID, TG_API_HASH, TG_BOT_TOKEN.
// Optional: TG_SESSION (default bot.sqlite).

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>

#include "cppgram/client.h"

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

}  // namespace

int main() {
    try {
        cppgram::ClientOptions opt;
        opt.api_id = std::stoi(env_or_throw("TG_API_ID"));
        opt.api_hash = env_or_throw("TG_API_HASH");
        const char* session = std::getenv("TG_SESSION");
        opt.session_path = (session && *session) ? session : "bot.sqlite";

        cppgram::Client client(opt);
        client.on_error([](cppgram::Client&, const std::string& stage, const std::string& message) {
            std::cerr << "handler error in " << stage << ": " << message << "\n";
        });

        client.connect();
        client.login_bot(env_or_throw("TG_BOT_TOKEN"));
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
