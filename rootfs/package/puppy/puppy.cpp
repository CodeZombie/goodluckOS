#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const char* kFontFile      = "/usr/share/fonts/Inter_24pt-Medium.ttf";
static const char* kAppsFiles[]   = {"/usr/share/puppy/apps.puppy", "/home/player/apps.puppy"};
static const char* kPuppyFiles[]   = {"/home/player/.local/share/applications"};
static const char* kLaunchFile    = "/tmp/launch";
static const char* kStateFile     = "/tmp/launcher_state";
static const char* kAutoStartFile = "/home/player/autostart";

constexpr int kScreenW              = 640;
constexpr int kScreenH              = 480;

constexpr int kCellWidth            = 192;
constexpr int kCellHeight           = 128;
constexpr int kGap                  = 32;
constexpr int kPitchX               = kCellWidth  + kGap;
constexpr int kPitchY               = kCellHeight + kGap;

// Screen position of the currently selected entry.
constexpr int kSelX                 = 64;
constexpr int kSelY                 = 164;

constexpr int kHeaderTextYMargin    = 9;
constexpr int kTextMargin           = 16;
constexpr int kBorder               = 3;

constexpr Uint32 kIdleCheckMs       = 60000;
constexpr size_t kMaxCachedIcons    = 64;

constexpr SDL_Color kWhite  {255, 255, 255, 255};
constexpr SDL_Color kGrey   {170, 170, 170, 255};
constexpr SDL_Color kBlack  {0, 0, 0, 255};
constexpr SDL_Color kYellow {255, 205, 60, 255};
constexpr SDL_Color kTile   {40, 40, 44, 255};
constexpr SDL_Color kClear  {24, 24, 28, 255};

// String helper functions
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Converts semicolon-separated-list to an vector:
// "a;b;c;" -> {"a","b","c"}
static std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(';', start);
        if (end == std::string::npos) end = s.size();
        std::string item = trim(s.substr(start, end - start));
        if (!item.empty()) out.push_back(item);
        start = end + 1;
    }
    return out;
}

// Single-quote a string for safe use in a shell command line.
static std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static void drawSurface(SDL_Renderer* r, SDL_Surface* s, int x, int y, int maxW = 0) {
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    if (!t) return;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    int w = (maxW > 0) ? std::min(s->w, maxW) : s->w;   // clip, don't squash
    SDL_Rect src{0, 0, w, s->h};
    SDL_Rect dst{x, y, w, s->h};
    SDL_RenderCopy(r, t, &src, &dst);
    SDL_DestroyTexture(t);
}

static void drawText(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, int maxW = 0) {
    if (!font || text.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!s) return;
    drawSurface(r, s, x, y, maxW);
    SDL_FreeSurface(s);
}

static int textWidth(TTF_Font* font, const std::string& text) {
    int w = 0, h = 0;
    if (font && !text.empty()) TTF_SizeUTF8(font, text.c_str(), &w, &h);
    return w;
}

class IconCache {
public:
    struct Icon {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
    };

    IconCache(SDL_Renderer* r, const std::string& fallbackPath) : renderer(r) {
        fallback = load(fallbackPath);
        if (!fallback.tex) std::cerr << "Warning: missing fallback icon: " << fallbackPath << "\n";
    }

    ~IconCache() {
        for (auto& [path, slot] : slots) if (slot.icon.tex) SDL_DestroyTexture(slot.icon.tex);
        if (fallback.tex) SDL_DestroyTexture(fallback.tex);
    }

    IconCache(const IconCache&) = delete;
    IconCache& operator=(const IconCache&) = delete;

    Icon get(const std::string& path) {
        if (path.empty()) return fallback;

        auto it = slots.find(path);
        if (it != slots.end()) {
            it->second.lastUsed = ++clock;
            return it->second.icon.tex ? it->second.icon : fallback;
        }

        if (slots.size() >= kMaxCachedIcons) evictOldest();

        Slot slot;
        slot.icon = load(path);
        slot.lastUsed = ++clock;
        slots[path] = slot;
        return slot.icon.tex ? slot.icon : fallback;
    }

private:
    struct Slot {
        Icon icon;
        uint64_t lastUsed = 0;
    };

    SDL_Renderer* renderer;
    Icon fallback;
    std::map<std::string, Slot> slots;
    uint64_t clock = 0;

    Icon load(const std::string& path) {
        Icon icon;
        SDL_Surface* loaded = IMG_Load(path.c_str());
        if (!loaded) return icon;
        SDL_Surface* src = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);
        if (!src) return icon;

        SDL_Surface* result = src;

        if (src->w > kCellWidth || src->h > kCellHeight) {
            // Scale and crop so icons cover the cell if they're too large.
            float scale = std::max((float)kCellWidth / src->w, (float)kCellHeight / src->h);
            SDL_Rect crop;
            crop.w = std::min(src->w, (int)std::lround(kCellWidth  / scale));
            crop.h = std::min(src->h, (int)std::lround(kCellHeight / scale));
            crop.x = (src->w - crop.w) / 2;
            crop.y = (src->h - crop.h) / 2;

            SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, kCellWidth, kCellHeight, 32, SDL_PIXELFORMAT_RGBA32);
            if (!out) { SDL_FreeSurface(src); return icon; }

            SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
            SDL_SoftStretchLinear(src, &crop, out, nullptr);
            SDL_FreeSurface(src);
            result = out;
        }

        icon.w = result->w;
        icon.h = result->h;
        icon.tex = SDL_CreateTextureFromSurface(renderer, result);
        SDL_FreeSurface(result);
        if (icon.tex) SDL_SetTextureBlendMode(icon.tex, SDL_BLENDMODE_BLEND);
        return icon;
    }

    void evictOldest() {
        auto oldest = slots.begin();
        for (auto it = slots.begin(); it != slots.end(); ++it)
            if (it->second.lastUsed < oldest->second.lastUsed) oldest = it;
        if (oldest->second.icon.tex) SDL_DestroyTexture(oldest->second.icon.tex);
        slots.erase(oldest);
    }
};

struct Entry {
    std::string category;
    std::string name;
    std::string description;
    std::string command;
    std::string iconPath;

    void render(SDL_Renderer* r, IconCache& icons, TTF_Font* badgeFont, int x, int y, bool selected, bool autostart) const {
        SDL_Rect cell{x, y, kCellWidth, kCellHeight};
        SDL_SetRenderDrawColor(r, kTile.r, kTile.g, kTile.b, kTile.a);
        SDL_RenderFillRect(r, &cell);

        IconCache::Icon icon = icons.get(iconPath);
        if (icon.tex) {
            SDL_Rect dst{x + (kCellWidth - icon.w) / 2, y + (kCellHeight - icon.h) / 2, icon.w, icon.h};
            SDL_RenderCopy(r, icon.tex, nullptr, &dst);
        }

        if (selected) {
            SDL_SetRenderDrawColor(r, kYellow.r, kYellow.g, kYellow.b, kYellow.a);
            SDL_Rect strips[4] = {
                {x, y, kCellWidth, kBorder},                                // top
                {x, y + kCellHeight - kBorder, kCellWidth, kBorder},        // bottom
                {x, y, kBorder, kCellHeight},                               // left
                {x + kCellWidth - kBorder, y, kBorder, kCellHeight},        // right
            };
            SDL_RenderFillRects(r, strips, 4);
        }

        if (autostart && badgeFont) {
            static const std::string label = "autostart";
            const int padX = 6, padY = 2;
            int tw = textWidth(badgeFont, label);
            int th = TTF_FontHeight(badgeFont);
            SDL_Rect badge{x, y, tw + padX * 2, th + padY * 2};
            SDL_SetRenderDrawColor(r, kYellow.r, kYellow.g, kYellow.b, kYellow.a);
            SDL_RenderFillRect(r, &badge);
            drawText(r, badgeFont, label, badge.x + padX, badge.y + padY, kBlack);
        }
    }
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
    int  col() const { return categories[row].col; }
    int autoRow = -1, autoCol = -1;

    const Entry* selected() const {
        if (categories.empty()) return nullptr;
        const auto& entries = categories[row].entries;
        int c = categories[row].col;
        if (c < 0 || c >= (int)entries.size()) return nullptr;
        return &entries[c];
    }

    void move(int dx, int dy) {
        if (categories.empty()) return;
        row = std::clamp(row + dy, 0, (int)categories.size() - 1);
        Category& cat = categories[row];
        int last = std::max((int)cat.entries.size() - 1, 0);
        cat.col = std::clamp(cat.col + dx, 0, last);
    }

    bool isAutoStart(int r, int c) const { return r == autoRow && c == autoCol; }

    bool find(const std::string& category, const std::string& name, int& outRow, int& outCol) const {
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
};

struct Archive {
    std::string name, description, command, defaultIcon;
    std::vector<std::string> dirs, exts, iconDirs;
};

struct Record {
    bool isArchive = false;
    Entry entry;
    Archive archive;

    std::string key() const {
        return isArchive ? "A\x1f" + archive.name
                         : "E\x1f" + entry.category + "\x1f" + entry.name;
    }
};

static void upsert(std::vector<Record>& records, const Record& rec) {
    const std::string k = rec.key();
    for (auto& existing : records) {
        if (existing.key() == k) {
            existing = rec;
            return;
        }
    }
    records.push_back(rec);
}

static void parseAppsFile(const std::string& path, std::vector<Record>& records) {
    std::ifstream in(path);
    if (!in) return;

    std::string section;
    std::map<std::string, std::string> kv;

    auto get = [&](const char* key) {
        auto it = kv.find(key);
        return it == kv.end() ? std::string() : it->second;
    };

    auto flush = [&]() {
        if (section == "ENTRY") {
            Record rec;
            rec.entry.category    = get("CATEGORY").empty() ? "Applications" : get("CATEGORY");
            rec.entry.name        = get("NAME");
            rec.entry.description = get("DESCRIPTION");
            rec.entry.command     = get("COMMAND");
            rec.entry.iconPath    = get("ICON");
            if (!rec.entry.name.empty() && !rec.entry.command.empty()) upsert(records, rec);
        } else if (section == "ARCHIVE") {
            Record rec;
            rec.isArchive = true;
            Archive& a = rec.archive;
            a.name        = get("NAME");
            a.description = get("DESCRIPTION");
            a.command     = get("COMMAND");
            a.defaultIcon = get("DEFAULT_ICON");
            a.dirs        = splitList(get("ENTRY_DIRECTORIES"));
            a.exts        = splitList(get("ENTRY_EXTENSIONS"));
            a.iconDirs    = splitList(get("ENTRY_ICONS_DIRECTORIES"));
            if (!a.name.empty() && !a.dirs.empty()) upsert(records, rec);
        }
        kv.clear();
    };

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            flush();
            section = upper(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[upper(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
    }
    flush();
}

static std::string buildArchiveCommand(const std::string& tmpl, const std::string& path) {
    const std::string quoted = shellQuote(path);
    if (tmpl.empty()) return quoted;

    std::string out = tmpl;
    bool replaced = false;
    size_t pos = 0;
    while ((pos = out.find("%s", pos)) != std::string::npos) {
        out.replace(pos, 2, quoted);
        pos += quoted.size();
        replaced = true;
    }
    if (!replaced) out += " " + quoted;
    return out;
}

static std::string findArchiveIcon(const Archive& a, const std::string& stem) {
    static const char* kExts[] = {"png", "jpg", "jpeg"};
    for (const auto& dir : a.iconDirs) {
        for (const char* ext : kExts) {
            fs::path p = fs::path(dir) / (stem + "." + ext);
            std::error_code ec;
            if (fs::is_regular_file(p, ec)) return p.string();
        }
    }
    return a.defaultIcon;
}

static std::vector<Entry> expandArchive(const Archive& a) {
    std::vector<std::string> exts;
    for (auto e : a.exts) {
        if (!e.empty() && e[0] == '.') e.erase(0, 1);
        exts.push_back(lower(e));
    }

    std::vector<Entry> out;
    for (const auto& dir : a.dirs) {
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            if (!it->is_regular_file(ec2)) continue;

            const fs::path& p = it->path();
            if (p.filename().string().empty() || p.filename().string()[0] == '.') continue;

            std::string ext = lower(p.extension().string());
            if (!ext.empty()) ext.erase(0, 1);
            if (!exts.empty() && std::find(exts.begin(), exts.end(), ext) == exts.end()) continue;

            Entry e;
            e.category    = a.name;
            e.name        = p.stem().string();
            e.description = a.description;
            e.command     = buildArchiveCommand(a.command, p.string());
            e.iconPath    = findArchiveIcon(a, e.name);
            out.push_back(std::move(e));
        }
    }

    std::sort(out.begin(), out.end(), [](const Entry& l, const Entry& r) {
        return lower(l.name) < lower(r.name);
    });
    return out;
}

static void addToCategory(std::vector<Category>& cats, const Entry& e) {
    for (auto& c : cats) {
        if (c.name != e.category) continue;
        for (auto& existing : c.entries) {
            if (existing.name == e.name) { existing = e; return; }
        }
        c.entries.push_back(e);
        return;
    }
    cats.push_back({e.category, {e}});
}

static std::vector<Category> loadCatalog() {
    std::vector<Record> records;
    for (const char* path : kAppsFiles) parseAppsFile(path, records);

    //parse indiviual .puppy files in ~/.local/share/applications
    for (const auto& path : kPuppyFiles) {
        if (!fs::exists(path) || !fs::is_directory(path)) continue;
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.path().extension() == ".puppy") {
                parseAppsFile(entry.path().string(), records);
            }
        }
    }

    std::vector<Category> cats;
    for (const auto& rec : records) {
        if (rec.isArchive) {
            for (const auto& e : expandArchive(rec.archive)) addToCategory(cats, e);
        } else {
            addToCategory(cats, rec.entry);
        }
    }
    return cats;
}

static void writeAutoStart(const Entry& e) {
    std::ofstream out(kAutoStartFile);
    if (out) out << "# " << e.category << "\t" << e.name << "\n" << e.command << "\n";
}

static void clearAutoStart() {
    std::error_code ec;
    fs::remove(kAutoStartFile, ec);
}

static bool readAutoStartId(std::string& category, std::string& name) {
    std::ifstream in(kAutoStartFile);
    std::string line;
    if (!in || !std::getline(in, line) || line.compare(0, 2, "# ") != 0) return false;
    size_t tab = line.find('\t', 2);
    if (tab == std::string::npos) return false;
    category = line.substr(2, tab - 2);
    name = line.substr(tab + 1);
    return true;
}

static void toggleAutoStart(Model& m) {
    const Entry* e = m.selected();
    if (!e) return;
    if (m.isAutoStart(m.row, m.col())) {
        clearAutoStart();
        m.autoRow = m.autoCol = -1;
    } else {
        writeAutoStart(*e);
        m.autoRow = m.row;
        m.autoCol = m.col();
    }
}

static void launch(const Model& m, const Entry& e) {
    { std::ofstream out(kLaunchFile); if (out) out << e.command << "\n"; }
    std::ofstream state(kStateFile);
    if (state) state << e.category << "\n" << e.name << "\n";
    (void)m;
}

static void restoreCursor(Model& m) {
    std::ifstream in(kStateFile);
    std::string category, name;
    if (!in || !std::getline(in, category) || !std::getline(in, name)) return;
    int r, c;
    if (m.find(category, name, r, c)) { m.row = r; m.categories[r].col = c; }
}

static int readBattery() {
    std::error_code ec;
    for (fs::directory_iterator it("/sys/class/power_supply", ec), end; !ec && it != end; it.increment(ec)) {
        std::ifstream typeFile(it->path() / "type");
        std::string type;
        if (!(typeFile >> type) || type != "Battery") continue;
        std::ifstream capFile(it->path() / "capacity");
        int cap;
        if (capFile >> cap) return std::clamp(cap, 0, 100);
    }
    return -1; // no battery
}

class Ui {
public:
    Ui() {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return;
        }
        sdlUp = true;
        IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
        TTF_Init();

        window = SDL_CreateWindow("Puppy Launcher", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kScreenW, kScreenH, 0);
        if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; return; }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) { std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n"; return; }

        SDL_RenderSetLogicalSize(renderer, kScreenW, kScreenH);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        uiFont    = TTF_OpenFont(kFontFile, 22);
        titleFont = TTF_OpenFont(kFontFile, 34);
        descFont  = TTF_OpenFont(kFontFile, 20);
        badgeFont = TTF_OpenFont(kFontFile, 14);
        if (!uiFont || !titleFont || !descFont || !badgeFont)
            std::cerr << "Warning: could not load font " << kFontFile << "\n";

        overlay = IMG_LoadTexture(renderer, "/usr/share/puppy/assets/overlay.png");
        if (overlay) SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);
        else std::cerr << "Warning: could not load /usr/share/puppy/assets/overlay.png\n";

        icons = std::make_unique<IconCache>(renderer, "/usr/share/puppy/assets/fallback.png");
        ok_ = true;
    }

    ~Ui() {
        icons.reset();
        if (overlay) SDL_DestroyTexture(overlay);
        for (TTF_Font* f : {uiFont, titleFont, descFont, badgeFont}) if (f) TTF_CloseFont(f);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        if (sdlUp) { TTF_Quit(); IMG_Quit(); SDL_Quit(); }
    }

    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    bool ok() const { return ok_; }

    void render(const Model& m, int battery) {
        SDL_SetRenderDrawColor(renderer, kClear.r, kClear.g, kClear.b, kClear.a);
        SDL_RenderClear(renderer);

        for (int r = 0; r < (int)m.categories.size(); ++r) {
            int y = kSelY + (r - m.row) * kPitchY;
            if (y >= kScreenH || y + kCellHeight <= 0) continue;

            const auto& entries = m.categories[r].entries;
            int rowCol = m.categories[r].col;
            for (int c = 0; c < (int)entries.size(); ++c) {
                int x = kSelX + (c - rowCol) * kPitchX;
                if (x >= kScreenW || x + kCellWidth <= 0) continue;
                entries[c].render(renderer, *icons, badgeFont, x, y,
                                  r == m.row && c == rowCol, m.isAutoStart(r, c));
            }
        }

        if (overlay) SDL_RenderCopy(renderer, overlay, nullptr, nullptr);
        drawText_(m, battery);
        SDL_RenderPresent(renderer);
    }

private:
    bool sdlUp = false, ok_ = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* overlay = nullptr;
    TTF_Font *uiFont = nullptr, *titleFont = nullptr, *descFont = nullptr, *badgeFont = nullptr;
    std::unique_ptr<IconCache> icons;

    void drawText_(const Model& m, int battery) {
        const int maxW = kScreenW - 2 * kTextMargin;

        if (!m.categories.empty())
            drawText(renderer, uiFont, m.categories[m.row].name, kTextMargin, kHeaderTextYMargin, kWhite, maxW);

        std::string s;
        if (battery >= 0) {
            s = std::to_string(battery);
        } else {
            s = "??";
        }
        drawText(renderer, uiFont, s, kScreenW - kTextMargin - textWidth(uiFont, s), kHeaderTextYMargin, kWhite);

        const Entry* e = m.selected();
        if (!e) return;

        const int bottom = kScreenH - kTextMargin;
        int descH = 0;
        if (descFont && !e->description.empty()) {
            SDL_Surface* s = TTF_RenderUTF8_Blended_Wrapped(descFont, e->description.c_str(), kGrey, maxW - 142);
            if (s) {
                descH = s->h;
                drawSurface(renderer, s, kTextMargin, bottom - descH);
                SDL_FreeSurface(s);
            }
        }
        if (titleFont) {
            int titleH = TTF_FontHeight(titleFont);
            drawText(renderer, titleFont, e->name, kTextMargin, bottom - descH - titleH, kWhite, maxW - 142);
        }
    }
};

enum class Action { None, Up, Down, Left, Right, Launch, ToggleAutoStart };

static Action actionFromKey(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP:     return Action::Up;
        case SDLK_DOWN:   return Action::Down;
        case SDLK_LEFT:   return Action::Left;
        case SDLK_RIGHT:  return Action::Right;
        case SDLK_RETURN: return Action::Launch;
        case SDLK_SPACE:  return Action::ToggleAutoStart;
        default:          return Action::None;
    }
}

static Action actionFromButton(Uint8 b) {
    switch (b) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return Action::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return Action::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return Action::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Action::Right;
        case SDL_CONTROLLER_BUTTON_A:          return Action::Launch;
        case SDL_CONTROLLER_BUTTON_Y:          return Action::ToggleAutoStart;
        default:                               return Action::None;
    }
}

int main() {
    Model model;
    model.categories = loadCatalog();
    if (model.categories.empty()) {
        std::cerr << "No entries found in apps.puppy files!\n";
        return 1;
    }

    restoreCursor(model);
    {
        std::string cat, name;
        if (readAutoStartId(cat, name)) model.find(cat, name, model.autoRow, model.autoCol);
    }

    Ui ui;
    if (!ui.ok()) return 1;

    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    }

    bool running = true;
    bool dirty = true;
    int shownBattery = -2;
    Uint32 lastActivity = SDL_GetTicks();

    while (running) {
        if (dirty) {
            shownBattery = readBattery();
            ui.render(model, shownBattery);
            dirty = false;
        }

        Uint32 elapsed = SDL_GetTicks() - lastActivity;
        int timeout = elapsed >= kIdleCheckMs ? 0 : (int)(kIdleCheckMs - elapsed);

        SDL_Event ev;
        if (!SDL_WaitEventTimeout(&ev, timeout)) {
            lastActivity = SDL_GetTicks();
            if (readBattery() != shownBattery) dirty = true;
            continue;
        }

        do {
            Action action = Action::None;
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (ev.window.event == SDL_WINDOWEVENT_EXPOSED) dirty = true;
                    break;
                case SDL_KEYDOWN:
                    action = actionFromKey(ev.key.keysym.sym);
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    action = actionFromButton(ev.cbutton.button);
                    break;
            }

            switch (action) {
                case Action::Up:    model.move(0, -1); break;
                case Action::Down:  model.move(0, 1);  break;
                case Action::Left:  model.move(-1, 0); break;
                case Action::Right: model.move(1, 0);  break;
                case Action::ToggleAutoStart: toggleAutoStart(model); break;
                case Action::Launch:
                    if (const Entry* e = model.selected()) {
                        launch(model, *e);
                        running = false;
                    }
                    break;
                case Action::None: break;
            }

            if (action != Action::None) {
                dirty = true;
                lastActivity = SDL_GetTicks();
            }
        } while (running && SDL_PollEvent(&ev));
    }

    if (pad) SDL_GameControllerClose(pad);
    return 0;
}
