#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <ctime>

#define FONT_FILE "/usr/share/fonts/work-sans.ttf"
#define FONT_Y_OFFSET 1

namespace fs = std::filesystem;

struct DesktopEntry {
    std::string name;
    std::string exec;
    std::string type;
    std::string iconPath;
    std::string bgPath;
    float hue = 0.0f;
    bool skip = false;
    std::string cacheKey() const { return name + "\x1f" + iconPath + "\x1f" + bgPath; }
};

struct Row {
    std::string type;
    std::vector<DesktopEntry> entries;
    int selectedIndex = 0;
};

class LauncherModel {
public:
    std::vector<Row> rows;
    int selectedRow = 0;

    void addEntry(const DesktopEntry& entry) {
        for (auto& row : rows) {
            if (row.type == entry.type) {
                row.entries.push_back(entry);
                return;
            }
        }
        rows.push_back({entry.type, {entry}, 0});
    }

    Row* getCurrentRow() {
        if (rows.empty()) return nullptr;
        return &rows[selectedRow];
    }

    DesktopEntry* getSelectedEntry() {
        Row* row = getCurrentRow();
        if (!row || row->entries.empty()) return nullptr;
        return &row->entries[row->selectedIndex];
    }
};

class SystemInfo {
public:
    static std::string getTime() {
        std::time_t t = std::time(nullptr);
        char mbstr[16];
        if (std::strftime(mbstr, sizeof(mbstr), "%H:%M", std::localtime(&t))) {
            return std::string(mbstr);
        }
        return "--:--";
    }

    // Cached for 10s so repeated redraws don't re-scan sysfs.
    // only the periodic 15s clock tick forces a fresh read.
    // Returns -1 if no battery is present.
    static int getBatteryPercent() {
        static int cached = -1;
        static Uint32 lastRead = 0;
        Uint32 now = SDL_GetTicks();
        if (lastRead != 0 && (now - lastRead) < 10000) {
            return cached;
        }
        lastRead = now;

        cached = -1;
        if (fs::exists("/sys/class/power_supply")) {
            for (const auto& entry : fs::directory_iterator("/sys/class/power_supply")) {
                std::string path = entry.path().string();
                std::ifstream typeFile(path + "/type");
                std::string type;
                if (typeFile >> type && type == "Battery") {
                    std::ifstream capFile(path + "/capacity");
                    int cap;
                    if (capFile >> cap) {
                        cached = std::clamp(cap, 0, 100);
                        return cached;
                    }
                }
            }
        }
        return cached;
    }
};

class DesktopParser {
public:
    static float generateHue(const std::string& name) {
        size_t hash = std::hash<std::string>{}(name);
        return (float)(hash % 360);
    }

    // Desktop Entry Spec field codes (%f, %F, %u, %U, %i, %c, %k, etc) are
    // meant to be substituted by the launcher or dropped if unsupported.
    // The user has no way of sending args to entries, so we strip them
    // except `%s` - that one is still used for Archive entries.
    static std::string stripExecCodes(const std::string& exec) {
        std::string result;
        result.reserve(exec.size());
        for (size_t i = 0; i < exec.size(); ++i) {
            if (exec[i] == '%' && i + 1 < exec.size()) {
                char next = exec[i + 1];
                if (next == '%') {
                    result += '%';
                    ++i;
                } else if (next == 's') {
                    // Preserve %s so we can substitute it later for Archives
                    result += "%s";
                    ++i;
                } else {
                    ++i; // drop spec codes like %f, %F, %u, etc.
                }
            } else {
                result += exec[i];
            }
        }
        // collapse any doubled-up spaces left behind by a removed code.
        std::string collapsed;
        collapsed.reserve(result.size());
        bool lastWasSpace = false;
        for (char c : result) {
            bool isSpace = (c == ' ');
            if (isSpace && lastWasSpace) continue;
            collapsed += c;
            lastWasSpace = isSpace;
        }
        while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
        return collapsed;
    }

    static void parseDirectories(LauncherModel& model) {
        std::vector<std::string> searchPaths = {
            "/usr/share/applications",
            "/usr/local/share/applications"
        };

        if (const char* home = getenv("HOME")) {
            searchPaths.push_back(std::string(home) + "/.local/share/applications");
        }

        for (const auto& path : searchPaths) {
            if (!fs::exists(path) || !fs::is_directory(path)) continue;

            for (const auto& entry : fs::directory_iterator(path)) {
                if (entry.path().extension() == ".desktop") {
                    parseFile(entry.path().string(), model);
                }
            }
        }
    }

private:
    // Resolves patterns like /path/%s.{png;jpg} against the filesystem
    static std::string resolveAssetPattern(const std::string& pattern, const std::string& basename) {
        if (pattern.empty()) return "";

        std::string resolved = pattern;
        size_t pos = resolved.find("%s");
        if (pos != std::string::npos) {
            resolved.replace(pos, 2, basename);
        }

        size_t braceStart = resolved.find('{');
        size_t braceEnd = resolved.find('}', braceStart);
        if (braceStart != std::string::npos && braceEnd != std::string::npos && braceEnd > braceStart) {
            std::string prefix = resolved.substr(0, braceStart);
            std::string suffix = resolved.substr(braceEnd + 1);
            std::string exts = resolved.substr(braceStart + 1, braceEnd - braceStart - 1);

            size_t start = 0;
            while (start < exts.length()) {
                size_t end = exts.find(';', start);
                if (end == std::string::npos) end = exts.length();
                std::string ext = exts.substr(start, end - start);
                if (!ext.empty()) {
                    std::string testPath = prefix + ext + suffix;
                    if (fs::exists(testPath)) return testPath;
                }
                start = end + 1;
            }
            return "";
        }

        if (fs::exists(resolved)) return resolved;
        return "";
    }

    static void parseFile(const std::string& filepath, LauncherModel& model) {
        std::ifstream file(filepath);
        std::string line;
        bool inDesktopEntry = false;
        DesktopEntry entry;
        entry.type = "Applications";

        bool isArchive = false;
        std::string directory, bgDir, iconDir, fileExts, rawExec;

        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            if (line[0] == '[') {
                inDesktopEntry = (line == "[Desktop Entry]");
                continue;
            }

            if (!inDesktopEntry) continue;

            auto delim = line.find('=');
            if (delim != std::string::npos) {
                std::string key = line.substr(0, delim);
                std::string val = line.substr(delim + 1);

                if (key == "Name") entry.name = val;
                else if (key == "Exec") rawExec = val;
                else if (key == "Icon") entry.iconPath = val;
                else if (key == "Background") entry.bgPath = val;
                else if (key == "Type") {
                    if (val == "Archive") {
                        isArchive = true;
                        entry.skip = false;
                    } else if (val != "Application") {
                        entry.skip = true;
                    } else {
                        entry.skip = false;
                    }
                }
                else if (key == "NoDisplay" && val == "true") entry.skip = true;
                else if (key == "Hidden" && val == "true") entry.skip = true;
                else if (key == "Categories") {
                    // Categories is a ';'-separated list; group by the first entry.
                    // e.g. "Game;Emulator;" becomes just "Game".
                    auto firstCat = val.substr(0, val.find(';'));
                    if (!firstCat.empty()) entry.type = firstCat;
                }
                else if (key == "Directory") directory = val;
                else if (key == "BackgroundDirectory") bgDir = val;
                else if (key == "IconsDirectory") iconDir = val;
                else if (key == "FileExtensions") fileExts = val;
            }
        }

        if (entry.skip) return;

        if (isArchive) {
            if (directory.empty() || rawExec.empty()) return;
            if (!fs::exists(directory) || !fs::is_directory(directory)) return;

            // Parse valid extensions
            std::vector<std::string> exts;
            size_t start = 0;
            while (start < fileExts.length()) {
                size_t end = fileExts.find(';', start);
                if (end == std::string::npos) end = fileExts.length();
                std::string ext = fileExts.substr(start, end - start);
                if (!ext.empty()) {
                    if (ext[0] != '.') ext = "." + ext;
                    exts.push_back(ext);
                }
                start = end + 1;
            }

            std::string cleanExec = stripExecCodes(rawExec);

            for (const auto& f : fs::directory_iterator(directory)) {
                if (!f.is_regular_file()) continue;
                std::string fExt = f.path().extension().string();

                bool match = false;
                for (const auto& e : exts) {
                    if (fExt == e) { match = true; break; }
                }

                if (match || exts.empty()) {
                    DesktopEntry arcEntry;
                    arcEntry.type = entry.type;
                    arcEntry.name = f.path().stem().string();

                    // Safely insert the quoted file path for every %s
                    std::string finalExec = cleanExec;
                    size_t pos = 0;
                    std::string quotedPath = "\"" + f.path().string() + "\"";
                    while ((pos = finalExec.find("%s", pos)) != std::string::npos) {
                        finalExec.replace(pos, 2, quotedPath);
                        pos += quotedPath.length();
                    }
                    arcEntry.exec = finalExec;

                    // Resolve per-file assets, falling back to the Archive default
                    std::string resolvedBg = resolveAssetPattern(bgDir, arcEntry.name);
                    arcEntry.bgPath = resolvedBg.empty() ? entry.bgPath : resolvedBg;

                    std::string resolvedIcon = resolveAssetPattern(iconDir, arcEntry.name);
                    arcEntry.iconPath = resolvedIcon.empty() ? entry.iconPath : resolvedIcon;

                    arcEntry.hue = generateHue(arcEntry.name);
                    model.addEntry(arcEntry);
                }
            }
        } else {
            if (!entry.name.empty() && !rawExec.empty()) {
                entry.exec = stripExecCodes(rawExec);
                entry.hue = generateHue(entry.name);
                model.addEntry(entry);
            }
        }
    }
};

struct RGB { Uint8 r, g, b; };

static RGB hslToRgb(float h, float s, float l) {
    float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;
    float r = 0, g = 0, b = 0;

    if (h < 60)        { r = c; g = x; b = 0; }
    else if (h < 120)  { r = x; g = c; b = 0; }
    else if (h < 180)  { r = 0; g = c; b = x; }
    else if (h < 240)  { r = 0; g = x; b = c; }
    else if (h < 300)  { r = x; g = 0; b = c; }
    else               { r = c; g = 0; b = x; }

    return {
        (Uint8)std::lround((r + m) * 255.0f),
        (Uint8)std::lround((g + m) * 255.0f),
        (Uint8)std::lround((b + m) * 255.0f)
    };
}

class TileCache {
    SDL_Renderer* renderer;
    TTF_Font* labelFont;     // app-name strip inside each tile
    TTF_Font* monogramFont;  // big fallback letter for icon-less, art-less entries
    int tileW, tileH;
    int iconSize;

    std::map<std::string, SDL_Texture*> cache;

public:
    TileCache(SDL_Renderer* r, TTF_Font* label, TTF_Font* mono,
              int w, int h, int icon)
    : renderer(r), labelFont(label), monogramFont(mono),
    tileW(w), tileH(h), iconSize(icon) {}

    ~TileCache() {
        for (auto& [key, tex] : cache) if (tex) SDL_DestroyTexture(tex);
    }

    SDL_Texture* get(const DesktopEntry& entry) {
        std::string key = entry.cacheKey();
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        SDL_Texture* tex = build(entry);
        cache[key] = tex;
        return tex;
    }

private:
    static SDL_Surface* loadConverted(const std::string& path) {
        SDL_Surface* loaded = IMG_Load(path.c_str());
        if (!loaded) return nullptr;
        SDL_Surface* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);
        return converted;
    }

    SDL_Texture* loadCoverTexture(const std::string& path, int targetW, int targetH) {
        SDL_Surface* converted = loadConverted(path);
        if (!converted) return nullptr;

        float scale = std::max((float)targetW / converted->w, (float)targetH / converted->h);
        int scaledW = std::max(targetW, (int)std::lround(converted->w * scale));
        int scaledH = std::max(targetH, (int)std::lround(converted->h * scale));

        SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, scaledW, scaledH, 32, SDL_PIXELFORMAT_RGBA32);
        if (!scaled) { SDL_FreeSurface(converted); return nullptr; }
        SDL_SetSurfaceBlendMode(converted, SDL_BLENDMODE_NONE);
        SDL_BlitScaled(converted, nullptr, scaled, nullptr);
        SDL_FreeSurface(converted);

        SDL_Rect src = {(scaledW - targetW) / 2, (scaledH - targetH) / 2, targetW, targetH};
        SDL_Surface* cropped = SDL_CreateRGBSurfaceWithFormat(0, targetW, targetH, 32, SDL_PIXELFORMAT_RGBA32);
        if (!cropped) { SDL_FreeSurface(scaled); return nullptr; }
        SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(scaled, &src, cropped, nullptr);
        SDL_FreeSurface(scaled);

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, cropped);
        SDL_FreeSurface(cropped);
        return tex;
    }

    SDL_Texture* loadFitTexture(const std::string& path, int boxW, int boxH) {
        SDL_Surface* converted = loadConverted(path);
        if (!converted) return nullptr;

        float scale = std::min((float)boxW / converted->w, (float)boxH / converted->h);
        int scaledW = std::max(1, (int)std::lround(converted->w * scale));
        int scaledH = std::max(1, (int)std::lround(converted->h * scale));

        SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, scaledW, scaledH, 32, SDL_PIXELFORMAT_RGBA32);
        if (!scaled) { SDL_FreeSurface(converted); return nullptr; }
        SDL_SetSurfaceBlendMode(converted, SDL_BLENDMODE_NONE);
        SDL_BlitScaled(converted, nullptr, scaled, nullptr);
        SDL_FreeSurface(converted);

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, scaled);
        SDL_FreeSurface(scaled);
        return tex; // caller centers this within the icon box when drawing
    }

    // Diagonal two-tone gradient fallback background, generated once and then cached.
    void drawGradientBackground(const DesktopEntry& entry, bool includeMonogram) {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, tileW, tileH, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface) return;

        RGB c1 = hslToRgb(entry.hue, 0.55f, 0.36f);
        RGB c2 = hslToRgb(std::fmod(entry.hue + 26.0f, 360.0f), 0.55f, 0.50f);

        SDL_LockSurface(surface);
        Uint32* pixels = static_cast<Uint32*>(surface->pixels);
        for (int y = 0; y < tileH; ++y) {
            for (int x = 0; x < tileW; ++x) {
                float t = (float)(x + y) / (float)(tileW + tileH);
                Uint8 r = (Uint8)(c1.r + (c2.r - c1.r) * t);
                Uint8 g = (Uint8)(c1.g + (c2.g - c1.g) * t);
                Uint8 b = (Uint8)(c1.b + (c2.b - c1.b) * t);
                pixels[y * tileW + x] = SDL_MapRGBA(surface->format, r, g, b, 255);
            }
        }
        SDL_UnlockSurface(surface);

        if (includeMonogram && monogramFont && !entry.name.empty()) {
            std::string letter(1, (char)std::toupper((unsigned char)entry.name[0]));
            SDL_Color textColor = {255, 255, 255, 235}; // bold - this is the entry's only art
            SDL_Surface* letterSurf = TTF_RenderText_Blended(monogramFont, letter.c_str(), textColor);
            if (letterSurf) {
                SDL_Rect dest = {
                    (tileW - letterSurf->w) / 2,
                    (tileH - letterSurf->h) / 2 - 10,
                    letterSurf->w, letterSurf->h
                };
                SDL_BlitSurface(letterSurf, nullptr, surface, &dest);
                SDL_FreeSurface(letterSurf);
            }
        }

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (tex) {
            SDL_RenderCopy(renderer, tex, nullptr, nullptr);
            SDL_DestroyTexture(tex);
        }
    }

    void drawNameLabel(const std::string& name) {
        const int labelH = 40;
        int top = tileH - labelH;
        for (int dy = 0; dy < labelH; ++dy) {
            float t = (float)dy / (float)(labelH - 1);
            Uint8 alpha = (Uint8)std::lround(30 + 165 * t);
            hlineRGBA(renderer, 0, (Sint16)(tileW - 1), (Sint16)(top + dy), 0, 0, 0, alpha);
        }

        if (!labelFont || name.empty()) return;
        SDL_Color white = {245, 245, 245, 255};
        SDL_Surface* textSurf = TTF_RenderUTF8_Blended(labelFont, name.c_str(), white);
        if (!textSurf) return;

        const int sidePad = 12;
        int maxW = tileW - sidePad * 2;
        int drawW = std::min(textSurf->w, maxW);

        SDL_Texture* textTex = SDL_CreateTextureFromSurface(renderer, textSurf);
        if (textTex) {
            SDL_SetTextureBlendMode(textTex, SDL_BLENDMODE_BLEND);
            SDL_Rect src = {0, 0, drawW, textSurf->h};
            SDL_Rect dst = {sidePad, tileH - labelH / 2 - textSurf->h / 2 + 2, drawW, textSurf->h};
            SDL_RenderCopy(renderer, textTex, &src, &dst);
            SDL_DestroyTexture(textTex);
        }
        SDL_FreeSurface(textSurf);
    }

    SDL_Texture* build(const DesktopEntry& entry) {
        SDL_Texture* tile = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_TARGET, tileW, tileH);
        if (!tile) return nullptr;
        SDL_SetTextureBlendMode(tile, SDL_BLENDMODE_BLEND);

        SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, tile);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        bool haveBg = false;
        if (!entry.bgPath.empty()) {
            if (SDL_Texture* bg = loadCoverTexture(entry.bgPath, tileW, tileH)) {
                SDL_RenderCopy(renderer, bg, nullptr, nullptr);
                SDL_DestroyTexture(bg);
                haveBg = true;
            }
        }

        SDL_Texture* icon = entry.iconPath.empty() ? nullptr : loadFitTexture(entry.iconPath, iconSize, iconSize);

        if (!haveBg) {
            drawGradientBackground(entry, /*includeMonogram=*/(icon == nullptr));
        }

        if (icon) {
            SDL_Rect dst = {
                (tileW - iconSize) / 2,
                (tileH - 40) / 2 - iconSize / 2,
                iconSize, iconSize
            };
            SDL_RenderCopy(renderer, icon, nullptr, &dst);
            SDL_DestroyTexture(icon);
        }

        drawNameLabel(entry.name);

        SDL_SetRenderTarget(renderer, prevTarget);
        return tile;
    }
};

class Renderer {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* uiFont = nullptr;        // top bar / button hint
    TTF_Font* labelFont = nullptr;     // in-tile app name
    TTF_Font* monogramFont = nullptr;  // generated fallback art
    TileCache* tiles = nullptr;
    bool ok = true;

    const int screenW = 640;
    const int screenH = 480;
    const int topBarHeight = 36;

    const int gridLeftInset = 60;

    const int itemWidth = 240;
    const int itemHeight = 160;
    const int iconSize = 64;

    const SDL_Color clearColor = {30, 30, 34, 255};
    const SDL_Color accent = {255, 205, 60, 255}; // selection outline

public:
    Renderer() {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            ok = false;
            return;
        }
        IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
        TTF_Init();

        window = SDL_CreateWindow("Puppy Launcher", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  screenW, screenH, 0);
        if (!window) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
            ok = false;
            return;
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
            ok = false;
            return;
        }
        if (!SDL_RenderTargetSupported(renderer)) {
            std::cerr << "Renderer does not support render targets.\n";
            ok = false;
            return;
        }
        SDL_RenderSetLogicalSize(renderer, screenW, screenH);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        uiFont = TTF_OpenFont(FONT_FILE, 26);
        labelFont = TTF_OpenFont(FONT_FILE, 20);
        monogramFont = TTF_OpenFont(FONT_FILE, 84);
        if (!uiFont || !labelFont || !monogramFont) {
            std::cerr << "Warning: Could not load one or more fonts.\n";
        }

        tiles = new TileCache(renderer, labelFont, monogramFont, itemWidth, itemHeight, iconSize);
    }

    ~Renderer() {
        delete tiles;
        if (monogramFont) TTF_CloseFont(monogramFont);
        if (labelFont) TTF_CloseFont(labelFont);
        if (uiFont) TTF_CloseFont(uiFont);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
    }

    bool isOk() const { return ok; }

    void drawText(const std::string& text, int x, int y, SDL_Color color, int maxWidth = -1) {
        if (!uiFont || text.empty()) return;
        SDL_Surface* surface = TTF_RenderUTF8_Blended(uiFont, text.c_str(), color);
        if (!surface) return;

        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (!texture) { SDL_FreeSurface(surface); return; }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

        int drawW = surface->w;
        if (maxWidth > 0 && drawW > maxWidth) drawW = maxWidth;

        SDL_Rect src = {0, 0, drawW, surface->h};
        SDL_Rect dest = {x, y - FONT_Y_OFFSET, drawW, surface->h};
        SDL_RenderCopy(renderer, texture, &src, &dest);

        SDL_DestroyTexture(texture);
        SDL_FreeSurface(surface);
    }

    int textWidth(const std::string& text) {
        if (!uiFont || text.empty()) return 0;
        int w = 0, h = 0;
        TTF_SizeUTF8(uiFont, text.c_str(), &w, &h);
        return w;
    }

    int textHeight(const std::string& text) {
        if (!uiFont || text.empty()) return 0;
        int w = 0, h = 0;
        TTF_SizeUTF8(uiFont, text.c_str(), &w, &h);
        return h;
    }

    void drawBatteryGlyph(int x, int y, int percent) {
        const int w = 30, h = 15, nub = 3, pad = 2;
        SDL_Color grey = {200, 200, 200, 255};

        rectangleRGBA(renderer, x, y, x + w, y + h, grey.r, grey.g, grey.b, 255);
        boxRGBA(renderer, x + w, y + h / 2 - nub / 2, x + w + 3, y + h / 2 + nub / 2, grey.r, grey.g, grey.b, 255);

        if (percent >= 0) {
            int fillW = (int)std::lround((w - pad * 2) * (percent / 100.0f));
            fillW = std::clamp(fillW, 0, w - pad * 2);
            RGB fill = percent > 50 ? RGB{110, 220, 110} : (percent > 20 ? RGB{240, 190, 70} : RGB{230, 80, 80});
            if (fillW > 0) {
                boxRGBA(renderer, x + pad, y + pad, x + pad + fillW, y + h - pad, fill.r, fill.g, fill.b, 255);
            }
        }
    }

    void drawTopBar(LauncherModel& model) {
        boxRGBA(renderer, 0, 0, screenW, topBarHeight, 18, 18, 20, 255);

        std::string timeStr = SystemInfo::getTime();
        int timeW = textWidth(timeStr);
        drawText(timeStr, 15, 4, {210, 210, 210, 255});

        int percent = SystemInfo::getBatteryPercent();
        std::string battStr = percent >= 0 ? (std::to_string(percent) + "%") : "";
        int battW = textWidth(battStr);
        int battGlyphW = 33;
        int battBlockW = battGlyphW + (battW > 0 ? battW + 8 : 0);

        int battX = screenW - 15 - battBlockW;
        drawBatteryGlyph(battX, (topBarHeight - 15) / 2, percent);
        if (battW > 0) drawText(battStr, battX + battGlyphW + 8, 4, {210, 210, 210, 255});

        if (DesktopEntry* entry = model.getSelectedEntry()) {
            int leftBound = 15 + timeW + 15;
            int rightBound = battX - 15;
            int available = rightBound - leftBound;
            if (available > 20) {
                int nameW = textWidth(entry->name);
                int drawW = std::min(nameW, available);
                int x = leftBound + (available - drawW) / 2;
                drawText(entry->name, x, 4, {255, 255, 255, 255}, available);
            }
        }
    }

    void drawButtonHint() {
        const int margin = 16;
        const int btnRadius = 16;
        const int gap = 8;
        const int padX = 12;
        const int padY = 12;
        const int bgRadius = 16;
        SDL_Color bg = {12, 12, 14, 235};
        SDL_Color text_color = {200, 200, 190, 255};
        SDL_Color grey = {48, 48, 44, 255};
        SDL_Color red = {225, 70, 70, 255};

        std::string label = "Launch";
        int labelW = textWidth(label);
        int labelH = textHeight(label);

        int contentW = btnRadius * 2 + gap + labelW;
        int contentH = std::max(btnRadius * 2, labelH);

        int bgW = contentW + padX * 2;
        int bgH = contentH + padY * 2;
        int bgX = screenW - margin - bgW;
        int bgY = screenH - margin - bgH;

        roundedBoxRGBA(renderer, bgX, bgY, bgX + bgW, bgY + bgH, bgRadius, bg.r, bg.g, bg.b, bg.a);

        int cy = bgY + bgH / 2;
        int cx = bgX + padX + btnRadius;

        filledCircleRGBA(renderer, cx, cy, btnRadius - 1, grey.r, grey.g, grey.b, grey.a);
        aacircleRGBA(renderer, cx, cy, btnRadius, grey.r, grey.g, grey.b, grey.a);

        std::string letter = "A";
        int letterW = textWidth(letter);
        int letterH = textHeight(letter);
        drawText(letter, cx - letterW / 2, cy - letterH / 2, red);

        drawText(label, cx + btnRadius + gap, cy - labelH / 2, text_color);
    }

    void render(LauncherModel& model) {
        SDL_SetRenderDrawColor(renderer, clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        SDL_RenderClear(renderer);

        int padding = 20;
        int topBarOffset = topBarHeight;
        int gridLeft = padding + gridLeftInset;

        int targetY = topBarOffset + ((screenH - topBarOffset) / 2) - (itemHeight / 2);
        int currentY = targetY - (model.selectedRow * (itemHeight + padding * 3));

        for (size_t r = 0; r < model.rows.size(); ++r) {
            Row& row = model.rows[r];

            bool rowVisible = (currentY + itemHeight >= 0) && (currentY - 35 <= screenH);
            if (!rowVisible) {
                currentY += itemHeight + padding * 3;
                continue;
            }

            drawText(row.type, gridLeft, currentY - 35, {235, 235, 235, 255});

            int targetX = gridLeft;
            int currentX = targetX - (row.selectedIndex * (itemWidth + padding));

            for (size_t i = 0; i < row.entries.size(); ++i) {
                DesktopEntry& entry = row.entries[i];

                bool entryVisible = (currentX + itemWidth >= 0) && (currentX <= screenW);
                if (!entryVisible) {
                    currentX += itemWidth + padding;
                    continue;
                }

                bool selected = ((int)r == model.selectedRow && (int)i == row.selectedIndex);
                SDL_Rect rect = {currentX, currentY, itemWidth, itemHeight};

                if (SDL_Texture* tile = tiles->get(entry)) {
                    SDL_RenderCopy(renderer, tile, nullptr, &rect);
                }

                if (selected) {
                    rectangleRGBA(renderer, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h,
                                  accent.r, accent.g, accent.b, 255);
                    rectangleRGBA(renderer, rect.x - 1, rect.y - 1, rect.x + rect.w + 1, rect.y + rect.h + 1,
                                  accent.r, accent.g, accent.b, 255);
                }

                currentX += itemWidth + padding;
            }
            currentY += itemHeight + padding * 3;
        }

        drawButtonHint();
        drawTopBar(model);
        SDL_RenderPresent(renderer);
    }
};

const char* kLaunchFile = "/tmp/launch";
const char* kStateFile = "/tmp/launcher_state";

void launchAndExit(const std::string& command) {
    std::ofstream launchFile(kLaunchFile);
    if (launchFile.is_open()) {
        launchFile << command << "\n";
    }
}

// Remembers which row/entry was selected so the launcher can restore the
// cursor to the same spot after the launched app closes and it restarts.
// Identified by row type + entry name (rather than raw indices) so it still
// resolves correctly if the installed-app list has shifted slightly.
void saveState(const std::string& rowType, const std::string& entryName) {
    std::ofstream stateFile(kStateFile);
    if (stateFile.is_open()) {
        stateFile << rowType << "\n" << entryName << "\n";
    }
}

void loadState(LauncherModel& model) {
    std::ifstream stateFile(kStateFile);
    if (!stateFile.is_open()) return;

    std::string rowType, entryName;
    if (!std::getline(stateFile, rowType)) return;
    if (!std::getline(stateFile, entryName)) return;

    for (size_t r = 0; r < model.rows.size(); ++r) {
        if (model.rows[r].type == rowType) {
            model.selectedRow = (int)r;
            for (size_t i = 0; i < model.rows[r].entries.size(); ++i) {
                if (model.rows[r].entries[i].name == entryName) {
                    model.rows[r].selectedIndex = (int)i;
                    return;
                }
            }
            return; // row matched but entry didn't. Land on the row at least
        }
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    LauncherModel model;
    DesktopParser::parseDirectories(model);

    if (model.rows.empty()) {
        std::cerr << "No .desktop files found!\n";
        return 1;
    }

    loadState(model);

    Renderer renderer;
    if (!renderer.isOk()) {
        std::cerr << "Renderer failed to initialize; exiting.\n";
        return 1;
    }

    SDL_GameController* controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            break;
        }
    }

    // Event-driven main loop: SDL_WaitEventTimeout blocks until either input arrives
    // or the 15s clock/battery refresh is due.
    bool running = true;
    bool needsRender = true;
    Uint32 lastTick = SDL_GetTicks();
    SDL_Event ev;

    while (running) {
        Uint32 now = SDL_GetTicks();
        int timeout = 15000 - (now - lastTick); // 15 seconds

        if (timeout <= 0) {
            needsRender = true;
            lastTick = now;
            timeout = 15000;
        }

        if (SDL_WaitEventTimeout(&ev, timeout)) {
            do {
                if (ev.type == SDL_QUIT) {
                    running = false;
                }
                else if (ev.type == SDL_KEYDOWN || ev.type == SDL_CONTROLLERBUTTONDOWN) {

                    bool up = (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_UP) ||
                    (ev.type == SDL_CONTROLLERBUTTONDOWN && ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP);
                    bool down = (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_DOWN) ||
                    (ev.type == SDL_CONTROLLERBUTTONDOWN && ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN);
                    bool left = (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_LEFT) ||
                    (ev.type == SDL_CONTROLLERBUTTONDOWN && ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT);
                    bool right = (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_RIGHT) ||
                    (ev.type == SDL_CONTROLLERBUTTONDOWN && ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                    bool enter = (ev.type == SDL_KEYDOWN && (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_SPACE)) ||
                    (ev.type == SDL_CONTROLLERBUTTONDOWN && (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A));

                    if (down && model.selectedRow + 1 < (int)model.rows.size()) {
                        model.selectedRow++;
                    } else if (up && model.selectedRow > 0) {
                        model.selectedRow--;
                    } else if (right && model.rows[model.selectedRow].selectedIndex + 1 < (int)model.rows[model.selectedRow].entries.size()) {
                        model.rows[model.selectedRow].selectedIndex++;
                    } else if (left && model.rows[model.selectedRow].selectedIndex > 0) {
                        model.rows[model.selectedRow].selectedIndex--;
                    } else if (enter) {
                        if (DesktopEntry* entry = model.getSelectedEntry()) {
                            launchAndExit(entry->exec);
                            saveState(model.rows[model.selectedRow].type, entry->name);
                            running = false;
                        }
                    }

                    needsRender = true;
                    lastTick = SDL_GetTicks();
                }
            } while (SDL_PollEvent(&ev));
        }

        if (needsRender) {
            renderer.render(model);
            needsRender = false;
        }
    }

    if (controller) SDL_GameControllerClose(controller);
    return 0;
}
