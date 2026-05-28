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
    // Entering Category view starts fully collapsed so the whole category
    // list is visible at a glance.
    if (m == ViewMode::Category)
        std::fill(m_cat_collapsed.begin(), m_cat_collapsed.end(), (char)1);
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

void Directory::ClearAnalysis()
{
    for (auto& n : m_nodes) { n.category = -1; n.is_duplicate = false; }
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
    if (!m_filter.empty()) {
        std::vector<int> hits;
        for (int f : m_files) {
            if (file_hidden(f)) continue;
            std::string lc;
            for (char c : m_nodes[f].name) lc.push_back((char)std::tolower((unsigned char)c));
            if (lc.find(m_filter) != std::string::npos) hits.push_back(f);
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
    } else {
        // Category view: a collapsible header (with count) per non-empty
        // category, files sorted by name. Files are always added to the nav
        // order; collapsing only hides them from the tree display.
        int ncat = (int)m_category_names.size();
        auto add_group = [&](const std::string& name, int cat_index, bool match_uncat) {
            std::vector<int> in;
            for (int f : m_files) {
                if (file_hidden(f)) continue;
                int c = m_nodes[f].category;
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
}

void Directory::NextFile()
{
    if (m_nav.empty()) return;
    m_current_file = (m_current_file + 1) % (int)m_nav.size();
}

void Directory::PrevFile()
{
    if (m_nav.empty()) return;
    m_current_file = (m_current_file - 1 + (int)m_nav.size()) % (int)m_nav.size();
}

void Directory::StepFiles(int delta)
{
    if (m_nav.empty() || delta == 0) return;
    int pos = (m_current_file < 0 ? 0 : m_current_file) + delta;
    m_current_file = std::clamp(pos, 0, (int)m_nav.size() - 1);
}

bool Directory::SelectByNode(int node)
{
    for (int i = 0; i < (int)m_nav.size(); ++i) {
        if (m_nav[i] == node) { m_current_file = i; return true; }
    }
    return false;
}

int Directory::CurrentNodeIndex() const
{
    if (m_current_file < 0 || m_current_file >= (int)m_nav.size()) return -1;
    return m_nav[m_current_file];
}
