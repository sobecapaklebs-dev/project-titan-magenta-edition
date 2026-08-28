// test_app_config.cpp — F1 regression: app-prefs value escaping must neutralize
// ini-injection from a hostile filename (dropped/browsed ROM path with a newline).
#include "app_config.h"

#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using AppConfig::escapeValue;
using AppConfig::unescapeValue;

// Mirror app_config.cpp's on-disk format so the test proves the actual round-trip
// (save escapes each value; load splits on the first '=' and unescapes).
static std::string serialize(const std::vector<std::pair<std::string, std::string>> &kv) {
    std::string out = "# MGB64 app preferences\n";
    for (const auto &p : kv) out += p.first + "=" + escapeValue(p.second) + "\n";
    return out;
}

static std::map<std::string, std::string> deserialize(const std::string &text) {
    std::map<std::string, std::string> m;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        m[line.substr(0, eq)] = unescapeValue(line.substr(eq + 1));
    }
    return m;
}

int main() {
    int fails = 0;
    auto expect = [&](const char *name, bool cond) {
        if (!cond) { std::printf("FAIL: %s\n", name); ++fails; }
    };

    // --- escapeValue never emits a raw newline/CR (the injection primitive) ---
    std::string hostile = "rom.z64\nadvanced_env=GE007_EVIL=1\rmore";
    std::string esc = escapeValue(hostile);
    expect("escaped has no raw \\n", esc.find('\n') == std::string::npos);
    expect("escaped has no raw \\r", esc.find('\r') == std::string::npos);

    // --- round-trip fidelity for tricky values ---
    const char *cases[] = {
        "plain.z64",
        "with\nnewline",
        "with\r\ncrlf",
        "back\\slash",
        "literal-backslash-n\\n",           // already contains the two chars '\' 'n'
        "C:\\roms\\game.z64",               // Windows path (backslashes)
        "multi\nline\nGE007_A=1\nGE007_B=2", // advanced_env shape
        "",
    };
    for (const char *c : cases) {
        std::string s(c);
        if (unescapeValue(escapeValue(s)) != s) {
            std::printf("FAIL: round-trip mismatch for [%s]\n", c);
            ++fails;
        }
    }

    // --- THE ATTACK: a dropped filename with an embedded forged line ---
    // A macOS/Linux filename may legally contain '\n'. Persisting it as last_rom
    // must NOT create an advanced_env line that applyModeEnv() would setenv on Play.
    std::vector<std::pair<std::string, std::string>> store = {
        {"last_rom", "/tmp/evil.z64\nadvanced_env=GE007_PWNED=1"},
        {"update.dismissed_tag", "v9.9.9"},
    };
    std::string ini = serialize(store);
    // The serialized ini must have exactly: header + 2 data lines (no injected 3rd).
    size_t lines = 0;
    for (char ch : ini) if (ch == '\n') ++lines;
    expect("ini has exactly 3 lines (header + 2 keys)", lines == 3);

    std::map<std::string, std::string> loaded = deserialize(ini);
    expect("no injected advanced_env key", loaded.find("advanced_env") == loaded.end());
    expect("last_rom round-trips intact (newline preserved in value)",
           loaded["last_rom"] == "/tmp/evil.z64\nadvanced_env=GE007_PWNED=1");
    expect("dismissed_tag intact", loaded["update.dismissed_tag"] == "v9.9.9");
    expect("exactly 2 keys loaded", loaded.size() == 2);

    if (fails == 0) { std::printf("PASS: all app_config cases\n"); return 0; }
    std::printf("%d failure(s)\n", fails);
    return 1;
}
