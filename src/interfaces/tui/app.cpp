#include "disktree/interfaces/tui/app.hpp"
#include "disktree/utils/humanize.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>


#include <string>
#include <vector>
#include <algorithm>
#include <stack>
#include<fmt/format.h>
#include <sstream>

using namespace ftxui;
namespace hu = disktree::utils;

namespace disktree::tui {

// ── Color scheme ─────────────────────────────────────────────────
static Color col_header  = Color::RGB(80,  160, 255);
static Color col_size    = Color::RGB(100, 220, 100);
static Color col_pct     = Color::RGB(220, 180, 80);
static Color col_footer  = Color::RGB(120, 120, 120);
static Color col_sel     = Color::RGB(40,  80,  160);
static Color col_dir     = Color::RGB(100, 180, 255);
static Color col_file    = Color::RGB(200, 200, 200);
static Color col_bar_bg  = Color::RGB(40,  40,  40);

// ── Treemap block renderer ────────────────────────────────────────
// Renders a proportional bar chart of top children
static Element render_treemap(const DirNode* node, int width) {
    if (!node || node->size == 0) return text("");

    // Take top 8 children by size
    std::vector<std::pair<std::string, double>> items;

    for (const auto* d : node->child_dirs) {
        double pct = double(d->size) / double(node->size);
        items.push_back({std::string(d->name), pct});
        if (items.size() >= 8) break;
    }

    if (items.empty()) return text(" (no subdirectories)") | dim;

    // Build bar elements
    Elements bars;
    for (auto& [name, pct] : items) {
        int bar_w = std::max(1, int(pct * (width - 2)));
        std::string label = name;
        if (int(label.size()) > bar_w - 2)
            label = label.substr(0, std::max(0, bar_w - 2));

        std::string bar_str(bar_w, ' ');
        if (int(label.size()) <= bar_w)
            bar_str.replace(1, label.size(), label);

        bars.push_back(
            hbox({
                text(bar_str)
                    | bgcolor(Color::RGB(
                        40 + int(pct * 80),
                        80 + int(pct * 60),
                        160 + int(pct * 40)))
                    | color(Color::White),
                text(" ") | bgcolor(col_bar_bg),
            })
        );
    }

    return vbox({
        text(" Treemap") | bold | color(col_header),
        separator(),
        hbox(bars) | flex,
        separator(),
    });
}

// ── Row builder ───────────────────────────────────────────────────
struct TableRow {
    std::string name;
    std::string size_str;
    std::string count_str;
    std::string pct_str;
    bool        is_dir;
    int         depth;
    const DirNode*  dir_node  = nullptr;
    const FileNode* file_node = nullptr;
};

static std::vector<TableRow> build_rows(const DirNode* node, int depth_limit) {
    std::vector<TableRow> rows;

    struct Frame {
        const DirNode* node;
        int depth;
    };

    std::stack<Frame> stk;
    // Push children in reverse so first child renders first
    std::vector<const DirNode*> dirs(node->child_dirs.begin(),
                                      node->child_dirs.end());
    for (int i = int(dirs.size()) - 1; i >= 0; --i)
        stk.push({dirs[i], 1});

    while (!stk.empty()) {
        auto [n, d] = stk.top();
        stk.pop();

        TableRow row;
        row.depth    = d;
        row.is_dir   = true;
        row.dir_node = n;
        row.name     = std::string(d * 2, ' ') + "▶ " + std::string(n->name);
        row.size_str = hu::format_bytes(n->size);
        row.count_str= hu::format_count(n->file_count);
        row.pct_str  = hu::format_pct(n->pct_of_parent);
        rows.push_back(row);

        // Expand children if within depth limit
        if (depth_limit == 0 || d < depth_limit) {
            std::vector<const DirNode*> children(n->child_dirs.begin(),
                                                   n->child_dirs.end());
            for (int i = int(children.size()) - 1; i >= 0; --i)
                stk.push({children[i], d + 1});
        }
    }

    // Add direct files of root
    for (const auto* f : node->child_files) {
        TableRow row;
        row.depth     = 1;
        row.is_dir    = false;
        row.file_node = f;
        row.name      = "   " + std::string(f->name);
        row.size_str  = hu::format_bytes(f->size);
        row.count_str = "";
        row.pct_str   = hu::format_pct(f->pct_of_parent);
        rows.push_back(row);
    }

    return rows;
}

// ── Main TUI app ──────────────────────────────────────────────────
int run(const ScanResult& result, const Index& index) {
    auto screen = ScreenInteractive::Fullscreen();

    // Navigation state
    const DirNode* current = result.root;
    std::vector<const DirNode*> nav_stack; // breadcrumb
    int selected   = 0;
    int scroll_off = 0;
    std::string filter;
    bool filter_mode = false;

    // Build rows for current directory
    auto get_rows = [&]() {
        return build_rows(current, 2);
    };

    auto rows = get_rows();

    // ── Renderer ──────────────────────────────────────────────────
    auto renderer = Renderer([&] {
        int w = screen.dimx();
        int h = screen.dimy();
        int table_h = h - 12; // rows visible

        // Clamp selected
        if (!rows.empty()) {
            selected = std::clamp(selected, 0, int(rows.size()) - 1);
            // Auto-scroll
            if (selected < scroll_off) scroll_off = selected;
            if (selected >= scroll_off + table_h)
                scroll_off = selected - table_h + 1;
        }

        // ── Header ────────────────────────────────────────────────
        auto header = hbox({
            text(" disktree ") | bold | color(col_header),
            text("▸ ") | color(col_footer),
            text(current->path) | color(Color::White),
            text("  ") ,
            text(hu::format_bytes(current->size)) | color(col_size) | bold,
            text("  "),
            text(hu::format_count(current->file_count) + " files")
                | color(col_footer),
            text("  "),
            text(hu::format_count(current->dir_count) + " dirs")
                | color(col_footer),
        }) | bgcolor(Color::RGB(20, 20, 35));

        // ── Treemap ───────────────────────────────────────────────
        auto treemap = render_treemap(current, w);

        // ── Table header ──────────────────────────────────────────
        auto col_hdr = hbox({
            text(" Name")
                | size(WIDTH, EQUAL, w - 38)
                | bold | color(col_header),
            text("│"),
            text("       Size")
                | size(WIDTH, EQUAL, 13)
                | bold | color(col_header),
            text("│"),
            text("     Files")
                | size(WIDTH, EQUAL, 11)
                | bold | color(col_header),
            text("│"),
            text("      %")
                | size(WIDTH, EQUAL, 8)
                | bold | color(col_header),
        }) | bgcolor(Color::RGB(30, 30, 50));

        // ── Table rows ────────────────────────────────────────────
        Elements table_rows;
        int end = std::min(int(rows.size()), scroll_off + table_h);
        for (int i = scroll_off; i < end; ++i) {
            const auto& row = rows[i];
            bool is_sel = (i == selected);

            // Apply filter
            if (!filter.empty()) {
                std::string lname = row.name;
                std::transform(lname.begin(), lname.end(),
                               lname.begin(), ::tolower);
                std::string lfilt = filter;
                std::transform(lfilt.begin(), lfilt.end(),
                               lfilt.begin(), ::tolower);
                if (lname.find(lfilt) == std::string::npos) continue;
            }

            Color name_col = row.is_dir ? col_dir : col_file;

            auto cell = hbox({
                text(" " + row.name)
                    | size(WIDTH, EQUAL, w - 38)
                    | color(name_col),
                text("│"),
                text(" " + row.size_str)
                    | size(WIDTH, EQUAL, 13)
                    | color(col_size),
                text("│"),
                text(" " + row.count_str)
                    | size(WIDTH, EQUAL, 11)
                    | color(col_footer),
                text("│"),
                text(" " + row.pct_str)
                    | size(WIDTH, EQUAL, 8)
                    | color(col_pct),
            });

            if (is_sel)
                table_rows.push_back(cell | bgcolor(col_sel));
            else
                table_rows.push_back(cell
                    | bgcolor(i % 2 == 0
                        ? Color::RGB(18,18,28)
                        : Color::RGB(22,22,34)));
        }

        // ── Filter bar ────────────────────────────────────────────
        Element filter_bar = text("");
        if (filter_mode) {
            filter_bar = hbox({
                text(" Filter: ") | color(col_header),
                text(filter + "█") | color(Color::White),
            }) | bgcolor(Color::RGB(30, 30, 60));
        }

        // ── Footer ────────────────────────────────────────────────
        auto footer = hbox({
            text(" [↑↓]") | color(col_header), text(" Navigate "),
            text("[Enter]") | color(col_header), text(" Open "),
            text("[Bksp]") | color(col_header), text(" Back "),
            text("[/]") | color(col_header), text(" Filter "),
            text("[Esc]") | color(col_header), text(" Clear "),
            text("[Q]") | color(col_header), text(" Quit"),
        }) | bgcolor(Color::RGB(20, 20, 35)) | color(col_footer);

        // ── Scrollbar indicator ───────────────────────────────────
        std::string scroll_info = "";
        if (!rows.empty()) {
            scroll_info = fmt::format(" {}/{} ",
                selected + 1, rows.size());
        }

        return vbox({
            header,
            separator(),
            treemap | size(HEIGHT, EQUAL, 4),
            separator(),
            col_hdr,
            separator(),
            vbox(table_rows) | flex,
            separator(),
            filter_bar,
            hbox({
                footer | flex,
                text(scroll_info) | color(col_footer),
            }),
        });
    });

    // ── Event handler ─────────────────────────────────────────────
    auto handler = CatchEvent(renderer, [&](Event e) -> bool {
        // Filter mode: type characters
        if (filter_mode) {
            if (e == Event::Escape) {
                filter_mode = false;
                filter.clear();
                return true;
            }
            if (e == Event::Return) {
                filter_mode = false;
                return true;
            }
            if (e == Event::Backspace) {
                if (!filter.empty()) filter.pop_back();
                return true;
            }
            if (e.is_character()) {
                filter += e.character();
                return true;
            }
            return false;
        }

        // Normal mode
        if (e == Event::Character('q') || e == Event::Character('Q')) {
            screen.ExitLoopClosure()();
            return true;
        }

        if (e == Event::Character('/')) {
            filter_mode = true;
            filter.clear();
            return true;
        }

        if (e == Event::Escape) {
            filter.clear();
            return true;
        }

        if (e == Event::ArrowUp || e == Event::Character('k')) {
            if (selected > 0) --selected;
            return true;
        }

        if (e == Event::ArrowDown || e == Event::Character('j')) {
            if (selected < int(rows.size()) - 1) ++selected;
            return true;
        }

        if (e == Event::PageUp) {
            selected = std::max(0, selected - 10);
            return true;
        }

        if (e == Event::PageDown) {
            selected = std::min(int(rows.size()) - 1, selected + 10);
            return true;
        }

        if (e == Event::Home) { selected = 0; return true; }
        if (e == Event::End)  {
            selected = int(rows.size()) - 1;
            return true;
        }

        // Enter: drill into selected dir
        if (e == Event::Return) {
            if (!rows.empty() && rows[selected].is_dir
                && rows[selected].dir_node) {
                nav_stack.push_back(current);
                current  = rows[selected].dir_node;
                rows     = get_rows();
                selected = 0;
                scroll_off = 0;
                filter.clear();
            }
            return true;
        }

        // Backspace: go up
        if (e == Event::Backspace) {
            if (!nav_stack.empty()) {
                current = nav_stack.back();
                nav_stack.pop_back();
                rows     = get_rows();
                selected = 0;
                scroll_off = 0;
                filter.clear();
            }
            return true;
        }

        return false;
    });

    screen.Loop(handler);
    return 0;
}

} // namespace disktree::tui