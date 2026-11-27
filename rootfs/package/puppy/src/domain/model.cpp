#include "domain/model.h"

#include <algorithm>

const Entry* Model::selected() const {
    if (categories.empty()) return nullptr;
    const auto& entries = categories[row].entries;
    int c = categories[row].col;
    if (c < 0 || c >= (int)entries.size()) return nullptr;
    return &entries[c];
}

void Model::move(int dx, int dy) {
    if (categories.empty()) return;
    row = std::clamp(row + dy, 0, (int)categories.size() - 1);
    Category& cat = categories[row];
    int last = std::max((int)cat.entries.size() - 1, 0);
    cat.col = std::clamp(cat.col + dx, 0, last);
}

bool Model::isAutoStart(int r, int c) const { return r == autoRow && c == autoCol; }

bool Model::find(const std::string& category, const std::string& name, int& outRow, int& outCol) const {
    for (size_t r = 0; r < categories.size(); ++r) {
        if (categories[r].name != category) continue;
        for (size_t c = 0; c < categories[r].entries.size(); ++c) {
            if (categories[r].entries[c].name == name) {
                outRow = (int)r;
                outCol = (int)c;
                return true;
            }
        }
    }
    return false;
}
