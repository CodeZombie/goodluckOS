#pragma once

#include <string>
#include <vector>

// Catalogue data and navigation state are independent of SDL and rendering.
struct Entry {
    std::string category;
    std::string name;
    std::string description;
    bool terminal;
    std::string command;
    std::string iconPath;
};

struct Category {
    std::string name;
    std::vector<Entry> entries;
    int col = 0;
};

struct Model {
    std::vector<Category> categories;
    int row = 0;
    int& col() { return categories[row].col; }
    int col() const { return categories[row].col; }
    int autoRow = -1, autoCol = -1;

    const Entry* selected() const;
    void move(int dx, int dy);
    bool isAutoStart(int r, int c) const;
    bool find(const std::string& category, const std::string& name,
              int& outRow, int& outCol) const;
};
