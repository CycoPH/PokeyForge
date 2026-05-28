#include "Analysis.h"

#include "Directory.h"
#include "RtiFile.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Analysis {

const char* Name(Category c)
{
    switch (c) {
        case Category::Bass:       return "Bass";
        case Category::Lead:       return "Lead";
        case Category::Percussion: return "Percussion";
        case Category::NoiseFX:    return "Noise / FX";
        case Category::Pad:        return "Pad";
        default:                   return "Other";
    }
}

std::vector<std::string> Names()
{
    std::vector<std::string> v;
    for (int i = 0; i < (int)Category::COUNT; ++i) v.push_back(Name((Category)i));
    return v;
}

std::uint64_t HashAta(const std::vector<byte>& ata)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (byte b : ata) { h ^= b; h *= 1099511628211ULL; }
    return h;
}

Category Classify(const TInstrument& ins)
{
    const int* p = ins.parameters;
    int envLen = p[PAR_ENV_LENGTH];
    bool join  = p[PAR_AUDCTL_JOIN_1_2] || p[PAR_AUDCTL_JOIN_3_4];

    // Dominant distortion across the used envelope columns.
    int counts[16] = { 0 };
    for (int c = 0; c <= envLen && c < ENVELOPE_MAX_COLUMNS; ++c) {
        int d = ins.envelope[c][DISTORTION] & 0x0E;
        counts[d & 0x0F]++;
    }
    int dom = 0, best = -1;
    for (int d = 0; d < 16; ++d) if (counts[d] > best) { best = counts[d]; dom = d; }

    bool loops    = p[PAR_ENV_GOTO] < envLen;
    bool fastFade = p[PAR_VOL_FADEOUT] >= 8;
    bool noise    = (dom == 0x0 || dom == 0x8);

    if (join)                              return Category::Bass;
    if (noise && envLen <= 4 && fastFade)  return Category::Percussion;
    if (noise)                             return Category::NoiseFX;
    if (envLen <= 3 && fastFade)           return Category::Percussion;
    if (loops && !fastFade)                return Category::Pad;
    if (dom == 0x0A)                       return Category::Lead;
    return Category::Other;
}

namespace {

std::string RelPath(const std::string& abs, const std::string& root)
{
    std::error_code ec;
    fs::path r = fs::relative(fs::path(abs), fs::path(root), ec);
    if (ec || r.empty()) return abs;
    return r.generic_string();
}

std::string JsonEscape(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (c == '\\' || c == '"') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

std::string JsonUnescape(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            o += (n == 'n') ? '\n' : n;
        } else o += s[i];
    }
    return o;
}

// Pull a "key":value from one JSON object line. Strings only.
bool Field(const std::string& line, const std::string& key, std::string& out)
{
    std::string needle = "\"" + key + "\"";
    size_t k = line.find(needle);
    if (k == std::string::npos) return false;
    size_t colon = line.find(':', k + needle.size());
    if (colon == std::string::npos) return false;
    size_t q1 = line.find('"', colon + 1);
    if (q1 == std::string::npos) return false;
    std::string raw;
    for (size_t i = q1 + 1; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) { raw += line[i]; raw += line[i + 1]; i++; continue; }
        if (line[i] == '"') break;
        raw += line[i];
    }
    out = JsonUnescape(raw);
    return true;
}

int CategoryIndexFromName(const std::string& n)
{
    auto names = Names();
    for (int i = 0; i < (int)names.size(); ++i) if (names[i] == n) return i;
    return -1;
}

fs::path JsonPath(const std::string& root) { return fs::path(root) / "analysis.json"; }

} // anonymous namespace

Summary Run(Directory& dir, const std::string& libraryRoot, bool writeJson)
{
    Summary sum;

    struct Entry { int node; std::string rel; std::string name; std::uint64_t hash;
                   int len; Category cat; bool dup; std::string dup_of; };
    std::vector<Entry> entries;
    std::unordered_map<std::uint64_t, int> first_seen; // hash -> entries index

    for (int node : dir.AllFiles()) {
        const auto& n = dir.At(node);
        RtiFile rti;
        if (!rti.LoadFromFile(n.path.c_str()) || !rti.Valid()) continue;

        TInstrument ins{};
        rti.ToInstrument(ins, /*stereo=*/false);

        Entry e;
        e.node = node;
        e.rel  = RelPath(n.path, libraryRoot);
        e.name = rti.Name();
        e.hash = HashAta(rti.AtaBlob());
        e.len  = (int)rti.AtaBlob().size();
        e.cat  = Classify(ins);
        e.dup  = false;

        auto it = first_seen.find(e.hash);
        if (it == first_seen.end()) {
            first_seen[e.hash] = (int)entries.size();
        } else {
            e.dup = true;
            e.dup_of = entries[it->second].rel;
            sum.duplicates++;
        }
        entries.push_back(e);
    }

    // Apply to the directory.
    dir.SetCategoryNames(Names());
    for (const auto& e : entries) {
        dir.SetFileAnalysis(e.node, (int)e.cat, e.dup);
    }
    dir.RebuildViews();

    sum.total = (int)entries.size();
    sum.ok = true;

    if (writeJson) {
        std::ofstream out(JsonPath(libraryRoot), std::ios::trunc);
        if (out) {
            out << "{\n  \"version\": 1,\n";
            out << "  \"library\": \"" << JsonEscape(libraryRoot) << "\",\n";
            out << "  \"instruments\": [\n";
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                char hashbuf[24];
                std::snprintf(hashbuf, sizeof(hashbuf), "%016llx",
                              (unsigned long long)e.hash);
                out << "    {\"path\": \"" << JsonEscape(e.rel)
                    << "\", \"name\": \"" << JsonEscape(e.name)
                    << "\", \"hash\": \"" << hashbuf
                    << "\", \"len\": " << e.len
                    << ", \"category\": \"" << Name(e.cat)
                    << "\", \"duplicate_of\": \"" << JsonEscape(e.dup_of)
                    << "\"}" << (i + 1 < entries.size() ? "," : "") << "\n";
            }
            out << "  ]\n}\n";
        }
    }
    return sum;
}

bool LoadAndApply(Directory& dir, const std::string& libraryRoot)
{
    std::ifstream in(JsonPath(libraryRoot));
    if (!in) return false;

    // Map relative path -> file node.
    std::unordered_map<std::string, int> by_rel;
    for (int node : dir.AllFiles()) {
        by_rel[RelPath(dir.At(node).path, libraryRoot)] = node;
    }

    dir.SetCategoryNames(Names());
    bool applied = false;
    std::string line;
    while (std::getline(in, line)) {
        std::string path;
        if (!Field(line, "path", path)) continue; // not an instrument row
        std::string cat, dup_of;
        Field(line, "category", cat);
        Field(line, "duplicate_of", dup_of);

        auto it = by_rel.find(path);
        if (it == by_rel.end()) continue;
        int ci = CategoryIndexFromName(cat);
        dir.SetFileAnalysis(it->second, ci, !dup_of.empty());
        applied = true;
    }
    if (applied) dir.RebuildViews();
    return applied;
}

} // namespace Analysis
