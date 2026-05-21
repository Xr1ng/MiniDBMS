#include "MiniDB.h"
#include "ApiServer.h"
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void setupConsoleUtf8() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}

std::string trim(const std::string& s) {
    size_t left = 0;
    while (left < s.size() && std::isspace(static_cast<unsigned char>(s[left]))) {
        ++left;
    }
    size_t right = s.size();
    while (right > left && std::isspace(static_cast<unsigned char>(s[right - 1]))) {
        --right;
    }
    return s.substr(left, right - left);
}

std::string toLower(const std::string& s) {
    std::string t = s;
    for (auto& c : t) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return t;
}

bool isExitCommand(const std::string& s) {
    std::string t = toLower(trim(s));
    return t == "exit" || t == "quit" || t == "exit;" || t == "quit;";
}

bool endsWithSemicolon(const std::string& s) {
    size_t i = s.size();
    while (i > 0 && std::isspace(static_cast<unsigned char>(s[i - 1]))) {
        --i;
    }
    return i > 0 && s[i - 1] == ';';
}

bool startsWithKeyword(const std::string& text, const std::string& keyword) {
    if (text.size() < keyword.size()) {
        return false;
    }
    for (size_t i = 0; i < keyword.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(keyword[i]))) {
            return false;
        }
    }
    return text.size() == keyword.size() ||
           std::isspace(static_cast<unsigned char>(text[keyword.size()]));
}

std::string stripOptionalQuotes(const std::string& s) {
    if (s.size() >= 2) {
        char first = s.front();
        char last = s.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

bool parseSourceCommand(const std::string& text, std::string& path) {
    std::string trimmed = trim(text);
    if (!trimmed.empty() && trimmed.back() == ';') {
        trimmed.pop_back();
        trimmed = trim(trimmed);
    }
    if (!startsWithKeyword(trimmed, "source")) {
        return false;
    }
    path = stripOptionalQuotes(trim(trimmed.substr(6)));
    return true;
}

std::vector<std::string> splitSqlStatements(const std::string& content) {
    std::vector<std::string> statements;
    std::string current;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool inBacktick = false;

    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        current += c;

        if (c == '\'' && !inDoubleQuote && !inBacktick) {
            if (inSingleQuote && i + 1 < content.size() && content[i + 1] == '\'') {
                current += content[++i];
                continue;
            }
            inSingleQuote = !inSingleQuote;
            continue;
        }
        if (c == '"' && !inSingleQuote && !inBacktick) {
            inDoubleQuote = !inDoubleQuote;
            continue;
        }
        if (c == '`' && !inSingleQuote && !inDoubleQuote) {
            inBacktick = !inBacktick;
            continue;
        }
        if (c == ';' && !inSingleQuote && !inDoubleQuote && !inBacktick) {
            std::string stmt = trim(current);
            if (!stmt.empty()) {
                statements.push_back(stmt);
            }
            current.clear();
        }
    }

    std::string tail = trim(current);
    if (!tail.empty()) {
        statements.push_back(tail);
    }
    return statements;
}

bool executeStatement(MiniDB& db, const std::string& statement, int depth = 0);

bool executeSourceFile(MiniDB& db, const std::string& path, int depth) {
    if (depth > 16) {
        std::cout << "SOURCE nesting is too deep.\n";
        return false;
    }

    std::ifstream script(path, std::ios::binary);
    if (!script) {
        std::cout << "Cannot open script file: " << path << "\n";
        return false;
    }

    std::ostringstream buffer;
    buffer << script.rdbuf();

    std::cout << "Executing script: " << path << "\n";
    for (const auto& stmt : splitSqlStatements(buffer.str())) {
        executeStatement(db, stmt, depth + 1);
    }
    std::cout << "Finished script: " << path << "\n";
    return true;
}

bool executeStatement(MiniDB& db, const std::string& statement, int depth) {
    std::string scriptPath;
    if (parseSourceCommand(statement, scriptPath)) {
        if (scriptPath.empty()) {
            std::cout << "SOURCE requires a script file path.\n";
            return false;
        }
        return executeSourceFile(db, scriptPath, depth);
    }

    db.execute(statement);
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    setupConsoleUtf8();

    bool webMode = false;
    int  port    = 3000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--web") {
            webMode = true;
        } else if (arg == "--port" && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "Invalid port number.\n";
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "MiniDB - Mini Database Management System\n"
                      << "Usage: MiniDB [options]\n"
                      << "  --web          Start web server mode\n"
                      << "  --port <num>   Port for web server (default: 3000)\n"
                      << "  --help, -h     Show this help\n";
            return 0;
        }
    }

    if (webMode) {
        std::cout << "Starting MiniDB web server on port " << port << "...\n";
        ApiServer server(port);
        server.start();
        return 0;
    }

    MiniDB db;

    std::string line;
    std::string statement;

    std::cout << "MiniDB started. Type SQL and end with ';'.\n";
    std::cout << "Use EXIT or QUIT to leave.\n";

    while (true) {
        std::cout << (statement.empty() ? "miniDB> " : ".....> ");

        if (!std::getline(std::cin, line)) {
            break;
        }

        std::string trimmed = trim(line);
        if (statement.empty() && trimmed.empty()) {
            continue;
        }
        if (statement.empty() && isExitCommand(trimmed)) {
            break;
        }

        if (!statement.empty()) {
            statement += ' ';
        }
        statement += line;

        if (endsWithSemicolon(statement)) {
            executeStatement(db, statement);
            statement.clear();
        }
    }

    return 0;
}
