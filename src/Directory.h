#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Recursive .RTI directory browser. Models a folder tree the user can expand
// or collapse, exposes a flat traversal of files for left/right navigation,
// and supports analysis-driven features: hiding duplicate instruments and a
// "group by category" view.
//
// Internal state:
//   m_nodes       - every folder + file node, flat, with parent/children
//                   indices forming the tree. Index 0 is the root folder.
//   m_files       - every file node index, depth-first scan order. The
//                   immutable source list for analysis + filters.
//   m_rows        - what the tree pane draws this frame (folder/file rows
//                   plus, in Category view, collapsible category headers).
//                   Rebuilt by RebuildViews() whenever expand/collapse,
//                   filter, view mode, or analysis flags change.
//   m_nav         - every visible (non-hidden) file in left/right navigation
//                   order. Crucially: m_nav includes files inside collapsed
//                   folders / categories, so Next/Prev can step *into* a
//                   collapsed group. EnsureCurrentVisible() then auto-
//                   expands whichever parent the new selection sits under.
//   m_current_file- index into m_nav of the currently-selected file.
//
// The "Filter" mode (SetFilter) short-circuits both views and produces a
// flat list of name-matching files; clearing the filter (SetFilter("")) goes
// back to the active ViewMode.

class Directory {
public:
    enum class NodeType { Folder, File };
    enum class ViewMode { Folder, Category, Cluster };

    struct Node {
        NodeType    type    = NodeType::Folder;
        std::string path;       // Absolute filesystem path
        std::string name;       // Display name (filename without dir)
        int         parent  = -1;
        std::vector<int> children;
        int         depth   = 0;
        bool        expanded = false;  // Folders only
        // Analysis results (files only):
        int         category = -1;          // index into category names, -1 = none
        bool        is_duplicate = false;
        unsigned    tags = 0;               // Analysis::Tag bitmask
        int         confidence = 0;         // 0=guess, higher = more agreement
        int         cluster_id = -1;        // k-means cluster index (-1 = none)
        int         manual_category = -1;   // user override (-1 = use auto)
        // Cached audio features (Analysis::Features). [0..7] are
        // rms_early, rms_mid, rms_late, zcr, peak_pos, centroid (Hz),
        // rolloff (Hz), flux. audio_valid is false when no analysis has
        // run for this file (or analysis ran without an engine).
        float       audio[8] = { 0 };
        bool        audio_valid = false;
    };

    // A row in the tree display: either a node (folder/file) or, in Category
    // view, a (collapsible) category header.
    struct Row {
        bool        is_header = false;
        std::string label;        // header text when is_header
        int         node  = -1;
        int         depth = 0;
        int         header_cat = -1;  // category index for header rows
        bool        collapsed = false; // header collapsed state
    };

    // Walk `root` recursively, collecting folders and .RTI files. Returns
    // false if `root` doesn't exist or isn't a directory.
    bool Scan(const std::string& root);

    int  NodeCount() const { return (int)m_nodes.size(); }
    const Node& At(int idx) const { return m_nodes[idx]; }

    // Rows to draw in the tree pane (folder/file nodes, or category headers).
    const std::vector<Row>& Rows() const { return m_rows; }

    // Every .RTI file node, depth-first, regardless of hide/view (analysis).
    const std::vector<int>& AllFiles() const { return m_files; }

    void ToggleExpanded(int idx);

    // View / filtering.
    void SetViewMode(ViewMode m);
    ViewMode GetViewMode() const { return m_view; }
    void SetHideDuplicates(bool h);
    bool HideDuplicates() const { return m_hide_duplicates; }

    // Case-insensitive name filter. When non-empty, the views collapse to a
    // flat list of matching files (folders/categories ignored).
    void SetFilter(const std::string& q);
    bool HasFilter() const { return !m_filter.empty(); }

    // Analysis plumbing: set a file node's category/duplicate flag, then
    // RebuildViews() once all are set. SetCategoryNames supplies the header
    // labels used in Category view.
    void SetFileAnalysis(int node, int category, bool is_duplicate);
    // Extended analysis data: tags bitmask, confidence score, k-means
    // cluster id. Call after SetFileAnalysis; no rebuild is triggered.
    void SetFileExtras(int node, unsigned tags, int confidence, int cluster_id);
    // Cached audio features (8 floats, see Node::audio). Pass `valid =
    // false` to leave audio_valid = false even with non-zero values.
    void SetFileFeatures(int node, const float audio[8], bool valid);
    // Clear every per-file manual override across the whole library.
    void ClearAllManualOverrides();
    // Persistent per-file user override. Pass -1 to clear it. Triggers a
    // RebuildViews so the new category lands in the current view.
    void SetFileManualCategory(int node, int category);
    int  GetFileManualCategory(int node) const;
    // Effective category (manual override if set, otherwise auto).
    int  EffectiveCategory(int node) const;
    void SetCategoryNames(const std::vector<std::string>& names);
    // Cluster view: how many clusters were produced (0 means no clusters).
    void SetClusterCount(int n);
    int  ClusterCount() const { return (int)m_cluster_collapsed.size(); }
    void ToggleClusterCollapsed(int cluster);
    void ClearAnalysis();
    void RebuildViews();

    // Collapse/expand a category group in Category view (cat == category
    // count means the "(unanalysed)" group).
    void ToggleCategoryCollapsed(int cat);

    // Navigation over the currently-visible files.
    //
    // m_nav contains *every* visible (non-hidden) file regardless of which
    // categories or folders are collapsed, so Next/Prev can step into a
    // collapsed group. After every navigation, EnsureCurrentVisible() is
    // called to auto-expand the containing folder (Folder view) or category
    // header (Category view) so the highlighted instrument never hides
    // under a collapsed parent.
    int  CurrentFileIndex() const { return m_current_file; }  // index into NavFiles
    int  NavCount() const { return (int)m_nav.size(); }
    int  CurrentNodeIndex() const;
    void SetCurrentFileIndex(int pos);
    void NextFile();
    void PrevFile();
    void StepFiles(int delta);
    bool SelectByNode(int node);   // place cursor on this file node if visible
    // Force the row carrying the current selection to be visible: opens any
    // collapsed ancestor folder (Folder view) or category header (Category
    // view) and triggers a single RebuildViews when something changed.
    void EnsureCurrentVisible();

private:
    void ScanRecursive(const std::string& path, int parent, int depth);

    std::vector<Node>        m_nodes;
    std::vector<int>         m_files;   // all file node indices, depth-first
    std::vector<Row>         m_rows;    // tree display rows
    std::vector<int>         m_nav;     // visible file node indices (nav order)
    std::vector<std::string> m_category_names;
    std::vector<char> m_cat_collapsed;     // per category (+1 for "(unanalysed)")
    std::vector<char> m_cluster_collapsed; // per k-means cluster
    std::string m_filter;               // lowercased name filter
    int       m_current_file = -1;      // index into m_nav
    ViewMode  m_view = ViewMode::Folder;
    bool      m_hide_duplicates = false;
};
