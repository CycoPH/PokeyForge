#include "Directory.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

bool IsRtiFile(const fs::path& p)
{
    if (!p.has_extension()) return false;
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext == ".rti";
}

} // anonymous namespace

bool Directory::Scan(const std::string& root)
{
    m_nodes.clear();
    m_files.clear();
    m_rows.clear();
    m_nav.clear();
    m_current_file = -1;

    fs::path root_path(root);
    std::error_code ec;
    if (!fs::is_directory(root_path, ec)) return false;

    Node r;
    r.type = NodeType::Folder;
    r.path = fs::absolute(root_path, ec).string();
    r.name = root_path.filename().string();
    if (r.name.empty()) r.name = r.path;
    r.expanded = true;
    m_nodes.push_back(r);

    ScanRecursive(r.path, 0, 1);

    RebuildViews();
    if (!m_nav.empty()) m_current_file = 0;
    return true;
}

void Directory::ScanRecursive(const std::string& path, int parent, int depth)
{
    std::error_code ec;
    std::vector<fs::directory_entry> folders;
    std::vector<fs::directory_entry> files;

    for (fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) break;
        const auto& entry = *it;
        if (entry.is_directory(ec)) folders.push_back(entry);
        else if (entry.is_regular_file(ec) && IsRtiFile(entry.path())) files.push_back(entry);
    }

    auto sort_alpha = [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.path().filename().string() < b.path().filename().string();
    };
    std::sort(folders.begin(), folders.end(), sort_alpha);
    std::sort(files.begin(),   files.end(),   sort_alpha);

    for (const auto& f : folders) {
        Node n;
        n.type     = NodeType::Folder;
        n.path     = f.path().string();
        n.name     = f.path().filename().string();
        n.parent   = parent;
        n.depth    = depth;
        int idx = (int)m_nodes.size();
        m_nodes.push_back(n);
        m_nodes[parent].children.push_back(idx);
        ScanRecursive(n.path, idx, depth + 1);
    }

    for (const auto& f : files) {
        Node n;
        n.type   = NodeType::File;
        n.path   = f.path().string();
        n.name   = f.path().filename().string();
        n.parent = parent;
        n.depth  = depth;
        int idx = (int)m_nodes.size();
        m_nodes.push_back(n);
        m_nodes[parent].children.push_back(idx);
        m_files.push_back(idx);
    }
}

void Directory::ToggleExpanded(int idx)
{
    if (idx < 0 || idx >= (int)m_nodes.size()) return;
    if (m_nodes[idx].type != NodeType::Folder) return;
    m_nodes[idx].expanded = !m_nodes[idx].expanded;
    RebuildViews();
}

void Directory::SetCategoryNames(const std::vector<std::string>& names)
{
    m_category_names = names;
    m_cat_collapsed.assign(names.size() + 1, 0); // +1 for the "(unanalysed)" bucket
}

void Directory::SetViewMode(ViewMode m)
{
    if (m_view == m) return;
    m_view = m;
    // Entering a grouped view starts fully collapsed so all headers are
    // visible at a glance; EnsureCurrentVisible then opens whichever
    // header contains the current selection.
    if (m == ViewMode::Category)
        std::fill(m_cat_collapsed.begin(), m_cat_collapsed.end(), (char)1);
    // Cluster view is intended for browsing by similarity: open every
    // cluster so the user can scroll through sound-alike instruments
    // without having to expand each header first.
    if (m == ViewMode::Cluster)
        std::fill(m_cluster_collapsed.begin(), m_cluster_collapsed.end(), (char)0);
    RebuildViews();
}

void Directory::ToggleCategoryCollapsed(int cat)
{
    if (cat < 0 || cat >= (int)m_cat_collapsed.size()) return;
    m_cat_collapsed[cat] = m_cat_collapsed[cat] ? 0 : 1;
    RebuildViews();
}

void Directory::SetFilter(const std::string& q)
{
    std::string lc;
    lc.reserve(q.size());
    for (char c : q) lc.push_back((char)std::tolower((unsigned char)c));
    if (lc == m_filter) return;
    m_filter = lc;
    RebuildViews();
}

void Directory::SetHideDuplicates(bool h)
{
    if (m_hide_duplicates == h) return;
    m_hide_duplicates = h;
    RebuildViews();
}

void Directory::SetFileAnalysis(int node, int category, bool is_duplicate)
{
    if (node < 0 || node >= (int)m_nodes.size()) return;
    m_nodes[node].category = category;
    m_nodes[node].is_duplicate = is_duplicate;
}

void Directory::SetFileExtras(int node, unsigned tags, int confidence, int cluster_id)
{
    if (node < 0 || node >= (int)m_nodes.size()) return;
    m_nodes[node].tags       = tags;
    m_nodes[node].confidence = confidence;
    m_nodes[node].cluster_id = cluster_id;
}

void Directory::SetFileFeatures(int node, const float audio[8], bool valid)
{
    if (node < 0 || node >= (int)m_nodes.size()) return;
    for (int i = 0; i < 8; ++i) m_nodes[node].audio[i] = audio[i];
    m_nodes[node].audio_valid = valid;
}

void Directory::ClearAllManualOverrides()
{
    bool any = false;
    for (auto& n : m_nodes) {
        if (n.manual_category != -1) { n.manual_category = -1; any = true; }
    }
    if (any) RebuildViews();
}

void Directory::SetFileManualCategory(int node, int category)
{
    if (node < 0 || node >= (int)m_nodes.size()) return;
    m_nodes[node].manual_category = category;
    RebuildViews();
}

int Directory::GetFileManualCategory(int node) const
{
    if (node < 0 || node >= (int)m_nodes.size()) return -1;
    return m_nodes[node].manual_category;
}

int Directory::EffectiveCategory(int node) const
{
    if (node < 0 || node >= (int)m_nodes.size()) return -1;
    int m = m_nodes[node].manual_category;
    return (m >= 0) ? m : m_nodes[node].category;
}

void Directory::SetClusterCount(int n)
{
    m_cluster_collapsed.assign((size_t)std::max(0, n), 0);
}

void Directory::ToggleClusterCollapsed(int cluster)
{
    if (cluster < 0 || cluster >= (int)m_cluster_collapsed.size()) return;
    m_cluster_collapsed[cluster] = m_cluster_collapsed[cluster] ? 0 : 1;
    RebuildViews();
}

void Directory::ClearAnalysis()
{
    for (auto& n : m_nodes) {
        n.category = -1;
        n.is_duplicate = false;
        n.tags = 0;
        n.confidence = 0;
        n.cluster_id = -1;
        // Don't clear manual_category - it's the user's choice.
    }
    RebuildViews();
}

void Directory::RebuildViews()
{
    int cur_node = CurrentNodeIndex();   // preserve selection across rebuild

    m_rows.clear();
    m_nav.clear();
    if (m_nodes.empty()) { m_current_file = -1; return; }

    auto file_hidden = [&](int f) {
        return m_hide_duplicates && m_nodes[f].is_duplicate;
    };

    // Active filter: a flat list of matching files, regardless of view mode.
    //
    // Filter syntax:
    //   "kick"     - substring match on the filename (case insensitive)
    //   "@bright"  - filter by Analysis tag bitmask. Matches any file whose
    //                tags include the named one (see Analysis::TagsFromString).
    //                Multiple tags can be combined with commas: "@bright,loud".
    if (!m_filter.empty()) {
        std::vector<int> hits;
        if (!m_filter.empty() && m_filter[0] == '@') {
            // Tag-based filter.
            // m_filter is already lowercase from SetFilter().
            unsigned mask = 0;
            // Hand-roll the parser because Analysis::TagsFromString depends
            // on Analysis.h which we don't include here.
            auto recognise = [&](const std::string& tok) -> unsigned {
                if (tok == "vibrato")    return 1u << 0;
                if (tok == "bright")     return 1u << 1;
                if (tok == "dark")       return 1u << 2;
                if (tok == "loud")       return 1u << 3;
                if (tok == "quiet")      return 1u << 4;
                if (tok == "animated")   return 1u << 5;
                if (tok == "highfreq")   return 1u << 6;
                if (tok == "ascending")  return 1u << 7;
                if (tok == "descending") return 1u << 8;
                return 0;
            };
            size_t i = 1;   // skip '@'
            while (i < m_filter.size()) {
                size_t j = m_filter.find(',', i);
                if (j == std::string::npos) j = m_filter.size();
                mask |= recognise(m_filter.substr(i, j - i));
                i = j + 1;
            }
            if (mask) {
                for (int f : m_files) {
                    if (file_hidden(f)) continue;
                    if ((m_nodes[f].tags & mask) == mask) hits.push_back(f);
                }
            }
        } else {
            for (int f : m_files) {
                if (file_hidden(f)) continue;
                std::string lc;
                for (char c : m_nodes[f].name) lc.push_back((char)std::tolower((unsigned char)c));
                if (lc.find(m_filter) != std::string::npos) hits.push_back(f);
            }
        }
        std::sort(hits.begin(), hits.end(), [&](int a, int b) {
            return m_nodes[a].name < m_nodes[b].name;
        });
        for (int f : hits) {
            m_rows.push_back(Row{ false, "", f, 0 });
            m_nav.push_back(f);
        }
        int cur_node2 = cur_node;
        m_current_file = m_nav.empty() ? -1 : 0;
        if (cur_node2 >= 0)
            for (int i = 0; i < (int)m_nav.size(); ++i)
                if (m_nav[i] == cur_node2) { m_current_file = i; break; }
        return;
    }

    if (m_view == ViewMode::Folder) {
        // Tree display honouring expand state (depth-first).
        std::vector<int> stack;
        const Node& root = m_nodes[0];
        for (auto it = root.children.rbegin(); it != root.children.rend(); ++it)
            stack.push_back(*it);
        while (!stack.empty()) {
            int idx = stack.back();
            stack.pop_back();
            const Node& n = m_nodes[idx];
            if (n.type == NodeType::File) {
                if (file_hidden(idx)) continue;
                m_rows.push_back(Row{ false, "", idx, n.depth });
            } else {
                m_rows.push_back(Row{ false, "", idx, n.depth });
                if (n.expanded)
                    for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
                        stack.push_back(*it);
            }
        }
        // Navigation: every file depth-first, regardless of collapse state.
        for (int f : m_files) {
            if (file_hidden(f)) continue;
            m_nav.push_back(f);
        }
    } else if (m_view == ViewMode::Category) {
        // Category view: a collapsible header (with count) per non-empty
        // category, files sorted by name. Files are always added to the nav
        // order; collapsing only hides them from the tree display. Manual
        // override (per-node manual_category) takes precedence over auto.
        int ncat = (int)m_category_names.size();
        auto eff_cat = [&](int f) {
            int m = m_nodes[f].manual_category;
            return (m >= 0) ? m : m_nodes[f].category;
        };
        auto add_group = [&](const std::string& name, int cat_index, bool match_uncat) {
            std::vector<int> in;
            for (int f : m_files) {
                if (file_hidden(f)) continue;
                int c = eff_cat(f);
                bool match = match_uncat ? (c < 0 || c >= ncat) : (c == cat_index);
                if (match) in.push_back(f);
            }
            if (in.empty()) return;
            std::sort(in.begin(), in.end(), [&](int a, int b) {
                return m_nodes[a].name < m_nodes[b].name;
            });
            bool collapsed = (cat_index >= 0 && cat_index < (int)m_cat_collapsed.size())
                                 ? m_cat_collapsed[cat_index] != 0 : false;
            Row hr;
            hr.is_header  = true;
            hr.label      = name + " (" + std::to_string(in.size()) + ")";
            hr.header_cat = cat_index;
            hr.collapsed  = collapsed;
            m_rows.push_back(hr);
            for (int f : in) {
                if (!collapsed) m_rows.push_back(Row{ false, "", f, 1 });
                m_nav.push_back(f);
            }
        };
        for (int c = 0; c < ncat; ++c) add_group(m_category_names[c], c, false);
        add_group("(unanalysed)", ncat, true);  // unanalysed bucket index = ncat
    } else {
        // Cluster view: collapsible header per k-means cluster. Cluster ids
        // come from the analysis pass; clusters are numbered "Cluster N".
        // Within each cluster, sort by spectral centroid (brightness)
        // ascending so adjacent rows sound similar. Files without audio
        // analysis (e.g. failed renders or v2-era caches) fall to the end
        // of their cluster, alphabetically.
        int ncl = (int)m_cluster_collapsed.size();
        auto add_cluster = [&](int cl, bool match_uncat) {
            std::vector<int> in;
            for (int f : m_files) {
                if (file_hidden(f)) continue;
                int c = m_nodes[f].cluster_id;
                bool match = match_uncat ? (c < 0 || c >= ncl) : (c == cl);
                if (match) in.push_back(f);
            }
            if (in.empty()) return;
            std::sort(in.begin(), in.end(), [&](int a, int b) {
                const Node& na = m_nodes[a];
                const Node& nb = m_nodes[b];
                if (na.audio_valid != nb.audio_valid) return na.audio_valid;
                if (na.audio_valid && nb.audio_valid) {
                    // audio[5] is the spectral centroid in Hz.
                    if (na.audio[5] != nb.audio[5]) return na.audio[5] < nb.audio[5];
                }
                return na.name < nb.name;
            });
            bool collapsed = (cl >= 0 && cl < ncl) ? m_cluster_collapsed[cl] != 0 : false;
            Row hr;
            hr.is_header  = true;
            hr.label      = (cl >= 0 ? ("Cluster " + std::to_string(cl + 1))
                                     : std::string("(unclustered)"))
                            + " (" + std::to_string(in.size()) + ")";
            hr.header_cat = cl;
            hr.collapsed  = collapsed;
            m_rows.push_back(hr);
            for (int f : in) {
                if (!collapsed) m_rows.push_back(Row{ false, "", f, 1 });
                m_nav.push_back(f);
            }
        };
        for (int c = 0; c < ncl; ++c) add_cluster(c, false);
        add_cluster(ncl, true);   // catch-all for files with no cluster
    }

    // Restore selection to the same node if it's still visible.
    m_current_file = m_nav.empty() ? -1 : 0;
    if (cur_node >= 0) {
        for (int i = 0; i < (int)m_nav.size(); ++i) {
            if (m_nav[i] == cur_node) { m_current_file = i; break; }
        }
    }
}

void Directory::SetCurrentFileIndex(int pos)
{
    if (m_nav.empty()) { m_current_file = -1; return; }
    m_current_file = std::clamp(pos, 0, (int)m_nav.size() - 1);
    EnsureCurrentVisible();
}

void Directory::NextFile()
{
    if (m_nav.empty()) return;
    m_current_file = (m_current_file + 1) % (int)m_nav.size();
    EnsureCurrentVisible();
}

void Directory::PrevFile()
{
    if (m_nav.empty()) return;
    m_current_file = (m_current_file - 1 + (int)m_nav.size()) % (int)m_nav.size();
    EnsureCurrentVisible();
}

void Directory::StepFiles(int delta)
{
    if (m_nav.empty() || delta == 0) return;
    int pos = (m_current_file < 0 ? 0 : m_current_file) + delta;
    m_current_file = std::clamp(pos, 0, (int)m_nav.size() - 1);
    EnsureCurrentVisible();
}

bool Directory::SelectByNode(int node)
{
    for (int i = 0; i < (int)m_nav.size(); ++i) {
        if (m_nav[i] == node) {
            m_current_file = i;
            EnsureCurrentVisible();
            return true;
        }
    }
    return false;
}

void Directory::EnsureCurrentVisible()
{
    int cur = CurrentNodeIndex();
    if (cur < 0) return;
    // Filter view ignores collapse state — every match is always shown.
    if (!m_filter.empty()) return;

    bool changed = false;
    if (m_view == ViewMode::Folder) {
        // Walk the parent chain and expand every collapsed ancestor (skip
        // node 0: that's the root, which is always expanded).
        int p = m_nodes[cur].parent;
        while (p > 0) {
            if (!m_nodes[p].expanded) { m_nodes[p].expanded = true; changed = true; }
            p = m_nodes[p].parent;
        }
    } else if (m_view == ViewMode::Category) {
        // Category view: open the header that owns this file. Files with no
        // assigned category land in the "(unanalysed)" bucket at index
        // m_category_names.size(). Manual override applies.
        int ncat = (int)m_category_names.size();
        int m    = m_nodes[cur].manual_category;
        int cat  = (m >= 0) ? m : m_nodes[cur].category;
        int idx  = (cat >= 0 && cat < ncat) ? cat : ncat;
        if (idx >= 0 && idx < (int)m_cat_collapsed.size() && m_cat_collapsed[idx]) {
            m_cat_collapsed[idx] = 0;
            changed = true;
        }
    } else { // Cluster view
        int ncl = (int)m_cluster_collapsed.size();
        int cl  = m_nodes[cur].cluster_id;
        int idx = (cl >= 0 && cl < ncl) ? cl : -1;
        if (idx >= 0 && m_cluster_collapsed[idx]) {
            m_cluster_collapsed[idx] = 0;
            changed = true;
        }
    }
    // RebuildViews preserves the selected node, so m_current_file stays
    // pointed at the same file after the rebuild.
    if (changed) RebuildViews();
}

int Directory::CurrentNodeIndex() const
{
    if (m_current_file < 0 || m_current_file >= (int)m_nav.size()) return -1;
    return m_nav[m_current_file];
}
