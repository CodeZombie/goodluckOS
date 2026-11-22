#include "ui/ui.h"
#include "domain/model.h"
#include "palette/palette.h"

#include <SDL2/SDL_image.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <string>

namespace {
static const char* kFontFile      = "/usr/share/fonts/Inter_24pt-Medium.ttf";
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

constexpr size_t kMaxCachedIcons    = 64;

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

namespace {
void renderEntry(const Entry& entry, SDL_Renderer* r, IconCache& icons, TTF_Font* badgeFont, int x, int y, bool selected, bool autostart) {
    SDL_Rect cell{x, y, kCellWidth, kCellHeight};
    SDL_SetRenderDrawColor(r, palette::kTile.r, palette::kTile.g, palette::kTile.b, palette::kTile.a);
    SDL_RenderFillRect(r, &cell);

    IconCache::Icon icon = icons.get(entry.iconPath);
    if (icon.tex) {
        SDL_Rect dst{x + (kCellWidth - icon.w) / 2, y + (kCellHeight - icon.h) / 2, icon.w, icon.h};
        SDL_RenderCopy(r, icon.tex, nullptr, &dst);
    }

    if (selected) {
        SDL_SetRenderDrawColor(r, palette::kYellow.r, palette::kYellow.g, palette::kYellow.b, palette::kYellow.a);
        SDL_Rect strips[4] = {
            {x, y, kCellWidth, kBorder},                                // top
            {x, y + kCellHeight - kBorder, kCellWidth, kBorder},        // bottom
            {x, y, kBorder, kCellHeight},                               // left
            {x + kCellWidth - kBorder, y, kBorder, kCellHeight},        // right
        };
        SDL_RenderFillRects(r, strips, 4);
    }

    if (autostart && badgeFont) {
        static const std::string label = "autolaunch";
        const int padX = 6, padY = 2;
        int tw = textWidth(badgeFont, label);
        int th = TTF_FontHeight(badgeFont);
        SDL_Rect badge{x, y, tw + padX * 2, th + padY * 2};
        SDL_SetRenderDrawColor(r, palette::kYellow.r, palette::kYellow.g, palette::kYellow.b, palette::kYellow.a);
        SDL_RenderFillRect(r, &badge);
        drawText(r, badgeFont, label, badge.x + padX, badge.y + padY, palette::kBlack);
    }
}
}

Ui::Ui() {
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

Ui::~Ui() {
    icons.reset();
    if (overlay) SDL_DestroyTexture(overlay);
    for (TTF_Font* f : {uiFont, titleFont, descFont, badgeFont}) if (f) TTF_CloseFont(f);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (sdlUp) { TTF_Quit(); IMG_Quit(); SDL_Quit(); }
}

void Ui::render(const Model& m, int battery) {
    SDL_SetRenderDrawColor(renderer, palette::kClear.r, palette::kClear.g, palette::kClear.b, palette::kClear.a);
    SDL_RenderClear(renderer);

    for (int r = 0; r < (int)m.categories.size(); ++r) {
        int y = kSelY + (r - m.row) * kPitchY;
        if (y >= kScreenH || y + kCellHeight <= 0) continue;

        const auto& entries = m.categories[r].entries;
        int rowCol = m.categories[r].col;
        for (int c = 0; c < (int)entries.size(); ++c) {
            int x = kSelX + (c - rowCol) * kPitchX;
            if (x >= kScreenW || x + kCellWidth <= 0) continue;
            renderEntry(entries[c], renderer, *icons, badgeFont, x, y,
                              r == m.row && c == rowCol, m.isAutoStart(r, c));
        }
    }

    if (overlay) SDL_RenderCopy(renderer, overlay, nullptr, nullptr);
    drawText_(m, battery);
    SDL_RenderPresent(renderer);
}

void Ui::drawText_(const Model& m, int battery) {
    const int maxW = kScreenW - 2 * kTextMargin;

    if (!m.categories.empty())
        drawText(renderer, uiFont, m.categories[m.row].name, kTextMargin, kHeaderTextYMargin, palette::kWhite, maxW);

    std::string s;
    if (battery >= 0) {
        s = std::to_string(battery);
    } else {
        s = "??";
    }
    drawText(renderer, uiFont, s, kScreenW - kTextMargin - textWidth(uiFont, s), kHeaderTextYMargin, palette::kWhite);

    const Entry* e = m.selected();
    if (!e) return;

    const int bottom = kScreenH - kTextMargin;
    int descH = 0;
    if (descFont && !e->description.empty()) {
        SDL_Surface* s = TTF_RenderUTF8_Blended_Wrapped(descFont, e->description.c_str(), palette::kGrey, maxW - 142);
        if (s) {
            descH = s->h;
            drawSurface(renderer, s, kTextMargin, bottom - descH);
            SDL_FreeSurface(s);
        }
    }
    if (titleFont) {
        int titleH = TTF_FontHeight(titleFont);
        drawText(renderer, titleFont, e->name, kTextMargin, bottom - descH - titleH, palette::kWhite, maxW - 142);
    }
}
