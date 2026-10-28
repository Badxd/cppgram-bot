// cppgram full bot + user session generator.
// Compile: g++ -std=c++20 -O3 main.cpp -I./include -L./build -lcppgram -lssl -lcrypto -lpthread -o cppgram_app

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
#include <mutex>

#include "cppgram/client.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/mtproto/connection.h"

namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;

namespace {

// --- GLOBAL STATE FOR SESSION GENERATOR ---
std::mutex gen_mutex;
std::unique_ptr<cppgram::Client> gen_client = nullptr;
std::string gen_phone = "";
std::string gen_code_hash = "";
int gen_dc = 2;

const std::map<int, std::string> kDcAddresses = {
    {1, "149.154.175.53"}, {2, "149.154.167.51"}, {3, "149.154.175.100"},
    {4, "149.154.167.91"}, {5, "91.108.56.130"},
};

std::string env_or_throw(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') throw std::runtime_error(std::string("missing env ") + name);
    return value;
}

std::string env_or(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

std::string html_escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
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

// FIX 1: Correct migrate_target.
// Old code searched for "MIGRATE" (7 chars) then jumped pos+9, landing past the digit.
// For "USER_MIGRATE_2": pos=5, i=14 == string length — loop never ran, returned 0.
// Fixed: search for "MIGRATE_" (8 chars), atoi from pos+8 lands on the digit(s).
int migrate_target(const std::string& message) {
    const std::size_t pos = message.find("MIGRATE_");
    if (pos == std::string::npos) return 0;
    return std::atoi(message.c_str() + pos + 8);
}

bool has(const std::string& message, const char* what) { return message.find(what) != std::string::npos; }

// --- BOT LOGIN LOGIC ---
std::unique_ptr<cppgram::Client> connect_and_login(const std::string& api_id, const std::string& api_hash,
const std::string& token, const std::string& session_base) {
    std::deque<int> queue{2};
    std::map<int, bool> tried;
    while (!queue.empty()) {
        const int dc = queue.front(); queue.pop_front();
        if (tried[dc]) continue; tried[dc] = true;

        const auto addr = kDcAddresses.find(dc);
        if (addr == kDcAddresses.end()) continue;

        cppgram::ClientOptions opt;
        opt.api_id = std::stoi(api_id);
        opt.api_hash = api_hash;
        opt.dc_id = dc;
        opt.dc_ip = addr->second;
        opt.dc_port = 443;
        // Use /tmp/ on Heroku so we don't hit a read-only filesystem
        opt.session_path = "/tmp/" + session_base + "-dc" + std::to_string(dc) + ".sqlite";

        auto client = std::make_unique<cppgram::Client>(opt);
        try {
            client->connect();
            client->login_bot(token);
            std::cout << "[+] login ok on DC " << dc << "\n";
            return client;
        } catch (const cppgram::mtproto::RpcError& e) {
            std::cerr << "[-] DC " << dc << " RpcError: " << e.what() << "\n";
            const int target = migrate_target(e.what());
            if (target != 0 && !tried[target]) queue.push_front(target);
            client->disconnect();
        } catch (const std::exception& e) {
            std::cerr << "[-] DC " << dc << " Exception: " << e.what() << "\n";
            client->disconnect();
        }
    }
    throw std::runtime_error("bot could not log in on any DC");
}

} // namespace

int main() {
    try {
        const std::string api_id = env_or_throw("TG_API_ID");
        const std::string api_hash = env_or_throw("TG_API_HASH");
        const std::string token = env_or_throw("TG_BOT_TOKEN");
        const std::string session_base = env_or("TG_SESSION", "bot");

        auto client_ptr = connect_and_login(api_id, api_hash, token, session_base);
        cppgram::Client& client = *client_ptr;

        client.on_error([](cppgram::Client&, const std::string& stage, const std::string& message) {
            std::cerr << "handler error in " << stage << ": " << message << "\n";
        });

        const auto me = client.get_me();
        std::cout << "logged in as @" << me.username << "\n";

        // --- EXISTING COMMANDS ---
        client.on_message(cppgram::filters::command("start"),
            [](cppgram::Client& c, const cppgram::Message& m) {
                c.send_text(m.chat, "<b>Hi!</b> I am a cppgram bot.\nSend /help to see what I can do.", cppgram::ParseMode::Html);
            });

        client.on_message(cppgram::filters::command("help"),
            [](cppgram::Client& c, const cppgram::Message& m) {
                c.send_text(m.chat, "<b>Commands</b>\n"
                    "/start - greeting\n"
                    "/help - this list\n"
                    "/echo &lt;text&gt; - repeat text\n"
                    "/id - chat and user ids\n"
                    "/time - server time (UTC)\n"
                    "<b>Session Gen:</b>\n"
                    "/gen &lt;phone&gt; &lt;api_id&gt; &lt;api_hash&gt;\n"
                    "/otp &lt;code&gt;\n"
                    "/pass &lt;password&gt;", cppgram::ParseMode::Html);
            });

        client.on_message(cppgram::filters::command("echo"),
            [](cppgram::Client& c, const cppgram::Message& m) {
                const std::string body = args_of(m.text);
                if (body.empty()) { c.send_text(m.chat, "Usage: /echo <text>"); return; }
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

        // --- SESSION GENERATOR COMMANDS ---

        client.on_message(cppgram::filters::command("gen"),
            [](cppgram::Client& c, const cppgram::Message& m) {
                std::string args = args_of(m.text);
                std::size_t s1 = args.find(' ');
                if (s1 == std::string::npos) {
                    c.send_text(m.chat, "Usage: /gen +919876543210 <api_id> <api_hash>");
                    return;
                }
                std::string phone = args.substr(0, s1);
                std::string rest = args.substr(s1 + 1);
                std::size_t s2 = rest.find(' ');
                if (s2 == std::string::npos) {
                    c.send_text(m.chat, "Usage: /gen +919876543210 <api_id> <api_hash>");
                    return;
                }
                std::string a_id_str = rest.substr(0, s2);
                std::string a_hash = rest.substr(s2 + 1);

                if (phone.empty() || a_id_str.empty() || a_hash.empty()) {
                    c.send_text(m.chat, "Missing parameters.");
                    return;
                }
                if (phone[0] != '+') phone.insert(phone.begin(), '+');

                int a_id = 0;
                try { a_id = std::stoi(a_id_str); } catch (...) {
                    c.send_text(m.chat, "Invalid API ID."); return;
                }

                std::lock_guard<std::mutex> lock(gen_mutex);
                if (gen_client) { gen_client->disconnect(); gen_client.reset(); }

                gen_phone = phone;
                gen_dc = 2;
                gen_code_hash = "";

                // FIX 2: DC migration retry for auth_sendCode.
                // Telegram can return PHONE_MIGRATE_N if the account lives on a different DC.
                // Without this retry, /gen always fails for users on DC 1/3/4/5.
                std::deque<int> queue{2};
                std::map<int, bool> tried;
                bool sent_ok = false;

                while (!queue.empty() && !sent_ok) {
                    const int dc = queue.front(); queue.pop_front();
                    if (tried[dc]) continue; tried[dc] = true;

                    const auto addr_it = kDcAddresses.find(dc);
                    if (addr_it == kDcAddresses.end()) continue;

                    cppgram::ClientOptions opts;
                    opts.api_id = a_id;
                    opts.api_hash = a_hash;
                    opts.dc_id = dc;
                    opts.dc_ip = addr_it->second;
                    opts.dc_port = 443;
                    opts.session_path = "/tmp/gen_temp_dc" + std::to_string(dc) + ".sqlite";

                    auto candidate = std::make_unique<cppgram::Client>(opts);
                    try {
                        candidate->connect();

                        fn::auth_sendCode send;
                        send.phone_number = gen_phone;
                        send.api_id = a_id;
                        send.api_hash = a_hash;
                        send.settings = std::make_shared<tl::CodeSettings>(tl::CodeSettings{ty::codeSettings{}});

                        const tl::auth_SentCode sent = candidate->invoke(send);
                        if (const auto* s = std::get_if<ty::auth_sentCode>(&sent.value)) {
                            gen_code_hash = s->phone_code_hash;
                            gen_dc = dc;
                            gen_client = std::move(candidate);
                            c.send_text(m.chat, "✅ Code sent to Telegram app/SMS. Reply with /otp <code>");
                            sent_ok = true;
                        } else {
                            candidate->disconnect();
                            c.send_text(m.chat, "❌ Unexpected response from Telegram.");
                            sent_ok = true; // not a migrate error, don't retry
                        }
                    } catch (const cppgram::mtproto::RpcError& e) {
                        std::cerr << "[-] gen DC " << dc << " RpcError: " << e.what() << "\n";
                        const int target = migrate_target(e.what());
                        if (target != 0 && !tried[target]) {
                            queue.push_front(target);
                        } else {
                            // non-migrate RPC error (e.g. PHONE_NUMBER_INVALID)
                            c.send_text(m.chat, std::string("❌ Error: ") + e.what());
                            sent_ok = true;
                        }
                        candidate->disconnect();
                    } catch (const std::exception& e) {
                        std::cerr << "[-] gen DC " << dc << " Exception: " << e.what() << "\n";
                        c.send_text(m.chat, std::string("❌ Error: ") + e.what());
                        candidate->disconnect();
                        sent_ok = true;
                    }
                }

                if (!sent_ok) {
                    c.send_text(m.chat, "❌ Could not reach Telegram on any DC.");
                }
            });

        client.on_message(cppgram::filters::command("otp"),
            [](cppgram::Client& c, const cppgram::Message& m) {
                std::string code = args_of(m.text);
                if (code.empty()) { c.send_text(m.chat, "Usage: /otp <code>"); return; }

                std::lock_guard<std::mutex> lock(gen_mutex);
                if (!gen_client || gen_code_hash.empty()) {
                    c.send_text(m.chat, "❌ No login in progress. Start with /gen"); return;
                }

                try {
                    fn::auth_signIn sign;
                    sign.phone_number = gen_phone;
                    sign.phone_code_hash = gen_code_hash;
                    sign.phone_code = code;

                    gen_client->invoke(sign);

                    std::string session = gen_client->export_session_string();
                    c.send_text(m.chat, "✅ Success! Your Session String:\n\n<code>" + html_escape(session) + "</code>", cppgram::ParseMode::Html);

                    gen_client->disconnect();
                    gen_client.reset();
                    gen_code_hash = "";
                } catch (const cppgram::mtproto::RpcError& e) {
                    std::string msg = e.what();
                    if (has(msg, "SESSION_PASSWORD_NEEDED")) {
                        c.send_text(m.chat, "🔒 2FA is enabled. Reply with /pass <your_password>");
                    } else {
                        c.send_text(m.chat, "❌ Login failed: " + msg);
                        gen_client->disconnect();
                        gen_client.reset();
                        gen_code_hash = "";
                    }
                } catch (const std::exception& e) {
                    c.send_text(m.chat, std::string("❌ Error: ") + e.what());
                    gen_client->disconnect();
                    gen_client.reset();
                    gen_code_hash = "";
                }
            });

        client.on_message(cppgram::filters::command("pass"),
            [](cppgram::Client& c, const cppgram::Message& m) {
                std::string pw = args_of(m.text);
                if (pw.empty()) { c.send_text(m.chat, "Usage: /pass <password>"); return; }

                std::lock_guard<std::mutex> lock(gen_mutex);
                if (!gen_client) {
                    c.send_text(m.chat, "❌ No login in progress."); return;
                }

                try {
                    gen_client->check_password(pw);
                    std::string session = gen_client->export_session_string();
                    c.send_text(m.chat, "✅ 2FA Passed! Your Session String:\n\n<code>" + html_escape(session) + "</code>", cppgram::ParseMode::Html);

                    gen_client->disconnect();
                    gen_client.reset();
                    gen_code_hash = "";
                } catch (const std::exception& e) {
                    c.send_text(m.chat, std::string("❌ 2FA Failed: ") + e.what());
                }
            });

        std::cout << "listening, press Ctrl+C to stop\n";
        client.run();

    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
