#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Recursive .RTI directory browser. Models a folder tree the user can expand
// or collapse, exposes a flat traversal of files for left/right navigation,
// and supports analysis-driven features: hiding duplicate instruments and a
// "group by category" view.

class Directory {
public:
    enum class NodeType { Folder, File };
    enum class ViewMode { Folder, Category };

    struct Node {
        NodeType    type    = NodeType::Folder;
        std::string path;       // Absolute filesystem path
        std::string name;       // Display name (filename without dir)
        int         parent  = -1;
        std::vector<int> children;
        int         depth   = 0;
        bool        expanded = false;  // Folders only
        // Analysis results (files only):
        int         category = -1;       // index into category names, -1 = none
        bool        is_duplicate = false;
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
    void SetCategoryNames(const std::vector<std::string>& names);
    void ClearAnalysis();
    void RebuildViews();

    // Collapse/expand a category group in Category view (cat == category
    // count means the "(unanalysed)" group).
    void ToggleCategoryCollapsed(int cat);

    // Navigation over the currently-visible files.
    int  CurrentFileIndex() const { return m_current_file; }  // index into NavFiles
    int  NavCount() const { return (int)m_nav.size(); }
    int  CurrentNodeIndex() const;
    void SetCurrentFileIndex(int pos);
    void NextFile();
    void PrevFile();
    void StepFiles(int delta);
    bool SelectByNode(int node);   // place cursor on this file node if visible

private:
    void ScanRecursive(const std::string& path, int parent, int depth);

    std::vector<Node>        m_nodes;
    std::vector<int>         m_files;   // all file node indices, depth-first
    std::vector<Row>         m_rows;    // tree display rows
    std::vector<int>         m_nav;     // visible file node indices (nav order)
    std::vector<std::string> m_category_names;
    std::vector<char> m_cat_collapsed;  // per category (+1 for "(unanalysed)")
    std::string m_filter;               // lowercased name filter
    int       m_current_file = -1;      // index into m_nav
    ViewMode  m_view = ViewMode::Folder;
    bool      m_hide_duplicates = false;
};
