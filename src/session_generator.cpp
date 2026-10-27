// cppgram session string generator (user accounts).
//
// Logs in with phone number + login code (+ 2FA password if enabled) and prints a session string
// that can be passed back as ClientOptions::session_string.
//
//   ./cppgram_session_generator
//
// Optional environment variables (otherwise you are asked on the terminal):
//   TG_API_ID, TG_API_HASH, TG_PHONE
//
// The session string gives FULL access to your account. Keep it secret, never share it, never
// commit it. To revoke it: Telegram > Settings > Devices > terminate the "cppgram" session.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <variant>

#include <termios.h>
#include <unistd.h>

#include "cppgram/client.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/session.h"

namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;

namespace {

const std::map<int, std::string> kDcAddresses = {
    {1, "149.154.175.53"}, {2, "149.154.167.51"}, {3, "149.154.175.100"},
    {4, "149.154.167.91"}, {5, "91.108.56.130"},
};

std::string env_or(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

std::string ask(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    std::getline(std::cin, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    while (!line.empty() && line.front() == ' ') line.erase(line.begin());
    return line;
}

// Reads a line without echoing it (for the 2FA password).
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

// Returns n from "..._MIGRATE_n", or 0.
int migrate_target(const std::string& message) {
    const auto pos = message.find("_MIGRATE_");
    if (pos == std::string::npos) return 0;
    std::size_t i = pos + 9;
    int dc = 0;
    while (i < message.size() && message[i] >= '0' && message[i] <= '9') dc = dc * 10 + (message[i++] - '0');
    return dc;
}

bool has(const std::string& message, const char* what) { return message.find(what) != std::string::npos; }

std::string describe_code_type(const tl::auth_SentCodeType& t) {
    if (std::holds_alternative<ty::auth_sentCodeTypeApp>(t.value)) return "the Telegram app (check your other Telegram chats, 'Telegram' official chat)";
    if (std::holds_alternative<ty::auth_sentCodeTypeSms>(t.value)) return "SMS";
    if (std::holds_alternative<ty::auth_sentCodeTypeCall>(t.value)) return "a phone call";
    return "Telegram (another delivery method)";
}

}  // namespace

int main() {
    std::string api_id_text = env_or("TG_API_ID");
    std::string api_hash = env_or("TG_API_HASH");
    std::string phone = env_or("TG_PHONE");

    std::cout << "=== cppgram session string generator ===\n"
              << "Get api_id / api_hash from https://my.telegram.org\n\n";
    if (api_id_text.empty()) api_id_text = ask("API ID: ");
    if (api_hash.empty()) api_hash = ask("API HASH: ");
    if (phone.empty()) phone = ask("Phone number with country code (e.g. +919876543210): ");
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
        cleanup();  // every DC needs a fresh auth key

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

            // ---- 1. ask Telegram to send the login code
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
                authorized = true;  // already authorised, no code needed
            } else {
                std::cerr << "Telegram asked for a payment/verification step that this tool does not support.\n";
                client.disconnect();
                cleanup();
                return 1;
            }

            // ---- 2. sign in with the code
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
                            std::cerr << "This number has no Telegram account yet. Register it in the official app first.\n";
                            client.disconnect();
                            cleanup();
                            return 1;
                        }
                        authorized = true;
                    } catch (const cppgram::mtproto::RpcError& e) {
                        const std::string msg = e.what();
                        if (has(msg, "SESSION_PASSWORD_NEEDED")) {
                            // ---- 3. two-step verification
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
                            throw std::runtime_error("the code expired; run the tool again to get a new one");
                        } else {
                            throw;
                        }
                    }
                }
                if (!authorized) throw std::runtime_error("too many wrong codes");
            }

            // ---- 4. export
            const cppgram::User me = client.get_me();
            const std::string session = client.export_session_string();
            std::cout << "\nLogged in as: " << me.first_name << (me.username.empty() ? "" : " (@" + me.username + ")")
                      << "  id=" << me.id << "\n"
                      << "\n================ SESSION STRING ================\n"
                      << session << "\n"
                      << "================================================\n"
                      << "Keep it SECRET: it gives full access to your account.\n"
                      << "Use it as ClientOptions::session_string.\n";
            // Also send it to Saved Messages (your own account's "me" chat) so it is easy to
            // copy from the phone, same as copying it from this terminal.
            try {
                const cppgram::PeerRef saved = client.resolve("me");
                client.send_text(saved,
                                  "Session string (cppgram)\n\n" + session +
                                      "\n\nKeep this secret - it gives full access to this account.");
                std::cout << "Also sent to Saved Messages.\n";
            } catch (const std::exception& e) {
                std::cerr << "Could not send it to Saved Messages: " << e.what() << "\n";
            }

            client.disconnect();
            cleanup();
            return 0;
        } catch (const cppgram::mtproto::RpcError& e) {
            const std::string msg = e.what();
            const int target = migrate_target(msg);
            client.disconnect();
            if (target != 0 && target != dc) {
                std::cout << "Your account lives on DC " << target << ", switching...\n";
                dc = target;
                continue;
            }
            if (has(msg, "PHONE_NUMBER_INVALID")) std::cerr << "Invalid phone number.\n";
            else if (has(msg, "API_ID_INVALID")) std::cerr << "API ID / API HASH is wrong.\n";
            else if (has(msg, "PHONE_NUMBER_BANNED")) std::cerr << "This phone number is banned.\n";
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
