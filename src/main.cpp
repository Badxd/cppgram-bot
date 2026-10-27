// cppgram merged utility: Echo Bot + User Session Generator
//
// Compile:
// g++ -std=c++20 -O3 main.cpp -I./include -L./build -lcppgram -lssl -lcrypto -lpthread -o cppgram_app
//
// Run Bot:
// ./cppgram_app
//
// Run Session Generator:
// ./cppgram_app --gen-session

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>
#include <deque>
#include <map>
#include <memory>
#include <variant>
#include <filesystem>
#include <termios.h>
#include <unistd.h>
#include <cstdio>

#include "cppgram/client.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/mtproto/connection.h"

namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;

// ==============================================================================
// SHARED HELPERS
// ==============================================================================
namespace {

std::string env_or_throw(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(std::string("missing environment variable ") + name);
    }
    return value;
}

std::string env_or(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

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
    {1, "149.154.175.53"}, {2, "149.154.167.51"}, {3, "149.154.175.100"},
    {4, "149.154.167.91"}, {5, "91.108.56.130"},
};

int migrate_target(const std::string& message) {
    const auto pos = message.find("MIGRATE");
    if (pos == std::string::npos) return 0;
    std::size_t i = pos + 9;
    int dc = 0;
    while (i < message.size() && message[i] >= '0' && message[i] <= '9') dc = dc * 10 + (message[i++] - '0');
    return dc;
}

bool has(const std::string& message, const char* what) { return message.find(what) != std::string::npos; }

std::string ask(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    std::getline(std::cin, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    while (!line.empty() && line.front() == ' ') line.erase(line.begin());
    return line;
}

std::string ask_hidden(const std::string& prompt) {
    std::cout << prompt << std::flush;
    termios old{};
    const bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) {
        termios noecho = old;
        noecho.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &noecho);
    }
    std::string line;
    std::getline(std::cin, line);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        std::cout << "\n";
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

std::string describe_code_type(const tl::auth_SentCodeType& t) {
    if (std::holds_alternative<ty::auth_sentCodeTypeApp>(t.value)) return "Telegram app";
    if (std::holds_alternative<ty::auth_sentCodeTypeSms>(t.value)) return "SMS";
    if (std::holds_alternative<ty::auth_sentCodeTypeCall>(t.value)) return "Phone call";
    return "Telegram";
}

} // namespace

// ==============================================================================
// 1. BOT LOGIC
// ==============================================================================
std::unique_ptr<cppgram::Client> connect_and_login_bot(const std::string& api_id, const std::string& api_hash,
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
            std::cout << "Bot login ok on DC " << dc << "\n";
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

int run_bot() {
    try {
        const std::string api_id = env_or_throw("TG_API_ID");
        const std::string api_hash = env_or_throw("TG_API_HASH");
        const std::string token = env_or_throw("TG_BOT_TOKEN");
        const std::string session_base = env_or("TG_SESSION", "bot");

        auto client_ptr = connect_and_login_bot(api_id, api_hash, token, session_base);
        cppgram::Client& client = *client_ptr;
        
        client.on_error([](cppgram::Client&, const std::string& stage, const std::string& message) {
            std::cerr << "handler error in " << stage << ": " << message << "\n";
        });

        const auto me = client.get_me();
        std::cout << "Logged in as @" << me.username << "\n";

        client.on_message(cppgram::filters::command("start"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              c.send_text(m.chat, "<b>Hi!</b> I am a cppgram bot.\nSend /help to see what I can do.", cppgram::ParseMode::Html);
                          });

        client.on_message(cppgram::filters::command("help"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              c.send_text(m.chat, "<b>Commands</b>\n/start\n/help\n/echo &lt;text&gt;\n/id\n/time", cppgram::ParseMode::Html);
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
                              c.send_text(m.chat, "chat id: " + std::to_string(m.chat.id) + "\nuser id: " + std::to_string(user_id));
                          });

        client.on_message(cppgram::filters::command("time"),
                          [](cppgram::Client& c, const cppgram::Message& m) {
                              c.send_text(m.chat, utc_now());
                          });

        std::cout << "Bot listening... press Ctrl+C to stop\n";
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal bot error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

// ==============================================================================
// 2. SESSION GENERATOR LOGIC
// ==============================================================================
int run_session_generator() {
    std::string api_id_text = env_or("TG_API_ID");
    std::string api_hash = env_or("TG_API_HASH");
    std::string phone = env_or("TG_PHONE");

    std::cout << "=== cppgram session string generator ===\n"
              << "Get api_id / api_hash from https://my.telegram.org\n\n";
    if (api_id_text.empty()) api_id_text = ask("API ID: ");
    if (api_hash.empty()) api_hash = ask("API HASH: ");
    if (phone.empty()) phone = ask("Phone number (e.g. +919876543210): ");
    
    if (api_id_text.empty() || api_hash.empty() || phone.empty()) {
        std::cerr << "api_id, api_hash and phone are all required\n";
        return 2;
    }
    if (phone.front() != '+') phone.insert(phone.begin(), '+');
    
    int api_id = 0;
    try {
        api_id = std::stoi(api_id_text);
    } catch (...) {
        std::cerr << "API ID must be a number\n";
        return 2;
    }

    const std::string session_file = "cppgram_gen_" + std::to_string(getpid()) + ".session";
    auto cleanup = [&] {
        std::error_code ec;
        std::filesystem::remove(session_file, ec);
    };

    int dc = 2;
    for (int attempt = 0; attempt < 6; ++attempt) {
        const auto addr = kDcAddresses.find(dc);
        if (addr == kDcAddresses.end()) {
            std::cerr << "unknown DC " << dc << "\n";
            cleanup();
            return 1;
        }
        cleanup();

        cppgram::ClientOptions opts;
        opts.api_id = api_id;
        opts.api_hash = api_hash;
        opts.dc_id = dc;
        opts.dc_ip = addr->second;
        opts.dc_port = 443;
        opts.session_path = session_file;
        opts.debug = env_or("TG_DEBUG", "0") == "1";

        cppgram::Client client(opts);
        try {
            std::cout << "Connecting to DC " << dc << "...\n";
            client.connect();

            fn::auth_sendCode send;
            send.phone_number = phone;
            send.api_id = api_id;
            send.api_hash = api_hash;
            send.settings = std::make_shared<tl::CodeSettings>(tl::CodeSettings{ty::codeSettings{}});
            const tl::auth_SentCode sent = client.invoke(send);

            std::string code_hash;
            bool authorized = false;
            if (const auto* s = std::get_if<ty::auth_sentCode>(&sent.value)) {
                code_hash = s->phone_code_hash;
                std::cout << "Login code sent via " << (s->type ? describe_code_type(*s->type) : "Telegram") << ".\n";
            } else if (std::holds_alternative<ty::auth_sentCodeSuccess>(sent.value)) {
                authorized = true;
            } else {
                std::cerr << "Telegram asked for unsupported verification step.\n";
                client.disconnect();
                cleanup();
                return 1;
            }

            if (!authorized) {
                for (int tries = 0; tries < 3 && !authorized; ++tries) {
                    const std::string code = ask("Enter the login code: ");
                    fn::auth_signIn sign;
                    sign.phone_number = phone;
                    sign.phone_code_hash = code_hash;
                    sign.phone_code = code;
                    try {
                        const tl::auth_Authorization auth = client.invoke(sign);
                        if (std::holds_alternative<ty::auth_authorizationSignUpRequired>(auth.value)) {
                            std::cerr << "No Telegram account for this number. Register in app first.\n";
                            client.disconnect();
                            cleanup();
                            return 1;
                        }
                        authorized = true;
                    } catch (const cppgram::mtproto::RpcError& e) {
                        const std::string msg = e.what();
                        if (has(msg, "SESSION_PASSWORD_NEEDED")) {
                            for (int p = 0; p < 3 && !authorized; ++p) {
                                const std::string pw = ask_hidden("2FA password (input hidden): ");
                                try {
                                    client.check_password(pw);
                                    authorized = true;
                                } catch (const cppgram::mtproto::RpcError& pe) {
                                    if (has(pe.what(), "PASSWORD_HASH_INVALID")) {
                                        std::cerr << "Wrong password, try again.\n";
                                    } else {
                                        throw;
                                    }
                                }
                            }
                            if (!authorized) throw std::runtime_error("too many wrong 2FA passwords");
                        } else if (has(msg, "PHONE_CODE_INVALID")) {
                            std::cerr << "Wrong code, try again.\n";
                        } else if (has(msg, "PHONE_CODE_EXPIRED")) {
                            throw std::runtime_error("the code expired; run again");
                        } else {
                            throw;
                        }
                    }
                }
                if (!authorized) throw std::runtime_error("too many wrong codes");
            }

            const cppgram::User me = client.get_me();
            const std::string session = client.export_session_string();
            std::cout << "\nLogged in as: " << me.first_name << (me.username.empty() ? "" : " (@" + me.username + ")")
                      << "  id=" << me.id << "\n"
                      << "\n================ SESSION STRING ================\n"
                      << session << "\n"
                      << "================================================\n"
                      << "Keep it SECRET.\n";
            
            try {
                const cppgram::PeerRef saved = client.resolve("me");
                client.send_text(saved, "Session string (cppgram)\n\n" + session + "\n\nKeep this secret.");
                std::cout << "Also sent to Saved Messages.\n";
            } catch (const std::exception& e) {
                std::cerr << "Could not send to Saved Messages: " << e.what() << "\n";
            }

            client.disconnect();
            cleanup();
            return 0;
        } catch (const cppgram::mtproto::RpcError& e) {
            const std::string msg = e.what();
            const int target = migrate_target(msg);
            client.disconnect();
            if (target != 0 && target != dc) {
                std::cout << "Switching to DC " << target << "...\n";
                dc = target;
                continue;
            }
            if (has(msg, "PHONE_NUMBER_INVALID")) std::cerr << "Invalid phone number.\n";
            else if (has(msg, "API_ID_INVALID")) std::cerr << "API ID / API HASH is wrong.\n";
            else if (has(msg, "PHONE_NUMBER_BANNED")) std::cerr << "This number is banned.\n";
            else std::cerr << "Telegram error: " << msg << "\n";
            cleanup();
            return 1;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            client.disconnect();
            cleanup();
            return 1;
        }
    }
    cleanup();
    std::cerr << "Too many DC migrations.\n";
    return 1;
}

// ==============================================================================
// 3. ENTRY POINT
// ==============================================================================
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--gen-session") {
        return run_session_generator();
    }
    return run_bot();
}
