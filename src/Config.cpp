#include "Config.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string EscapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string UnescapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                default:  out += n;    break; // covers \\ and \"
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Find the raw value text following "key": in `json`. Returns false if the
// key isn't present. `is_string` reports whether the value was quoted.
bool FindValue(const std::string& json, const std::string& key,
               std::string& out, bool& is_string)
{
    std::string needle = "\"" + key + "\"";
    size_t k = json.find(needle);
    if (k == std::string::npos) return false;
    size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return false;

    size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' ||
                               json[i] == '\n' || json[i] == '\r')) ++i;
    if (i >= json.size()) return false;

    if (json[i] == '"') {
        // Quoted string: read until an unescaped closing quote.
        ++i;
        std::string raw;
        while (i < json.size()) {
            if (json[i] == '\\' && i + 1 < json.size()) {
                raw += json[i];
                raw += json[i + 1];
                i += 2;
                continue;
            }
            if (json[i] == '"') break;
            raw += json[i++];
        }
        out = UnescapeJson(raw);
        is_string = true;
        return true;
    } else {
        // Bare token (number).
        std::string raw;
        while (i < json.size() && json[i] != ',' && json[i] != '}' &&
               json[i] != '\n' && json[i] != '\r') {
            raw += json[i++];
        }
        // Trim whitespace.
        size_t a = raw.find_first_not_of(" \t");
        size_t b = raw.find_last_not_of(" \t");
        out = (a == std::string::npos) ? "" : raw.substr(a, b - a + 1);
        is_string = false;
        return true;
    }
}

} // anonymous namespace

std::string Config::Path()
{
    const char* base = SDL_GetBasePath(); // exe directory, trailing slash
    std::string dir = base ? base : "";
    return dir + "pokeyforge.json";
}

bool Config::Load()
{
    std::ifstream in(Path(), std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string json = ss.str();

    std::string val;
    bool is_str = false;
    if (FindValue(json, "library", val, is_str))   library   = val;
    if (FindValue(json, "last_bank", val, is_str)) last_bank = val;
    if (FindValue(json, "last_file", val, is_str)) {
        last_file = std::atoi(val.c_str());
    }
    return true;
}

bool Config::Save() const
{
    std::ofstream out(Path(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "{\n";
    out << "  \"library\": \""   << EscapeJson(library)   << "\",\n";
    out << "  \"last_bank\": \"" << EscapeJson(last_bank) << "\",\n";
    out << "  \"last_file\": "   << last_file             << "\n";
    out << "}\n";
    return true;
}
