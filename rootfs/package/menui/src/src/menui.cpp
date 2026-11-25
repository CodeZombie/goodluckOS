// MenUI - The rendering backend for MenUI; A dead-simple menu application library.
// Copyright (c) 2026 Jeremy Clark (https://github.com/CodeZombie/)
// SPDX-License-Identifier: MIT
//
// TODO: This entire file is slop and needs to be cleaned up and benchmarked before release.

#include "menui.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#define ENABLE_DEBUGGING false
#define MLOG(fmt, ...) std::fprintf(stderr, "[menui] " fmt "\n", ##__VA_ARGS__)

namespace {

// =============================================================================
// Configuration
// =============================================================================

constexpr float kBaseFontSize  = 20.0f;   // at scale 1.0 (480px tall display) => ~27px line height
constexpr float kDebugFontSize = 12.0f;   // also scaled
constexpr float kReferenceHeight = 480.0f;
constexpr float kMinUiScale = 1.0f;
constexpr float kMaxUiScale = 3.0f;       // override at runtime with env MENUI_SCALE=1.5


// If true we detect the base direction of every string ourselves and call
// TTF_SetFontDirection before measuring/rasterizing. If you find SDL3_ttf
// guesses better than we do (try mixed Arabic/English), set this to false.
constexpr bool kForceTextDirection = true;

constexpr size_t kTextCacheBudgetBytes   = 16u * 1024u * 1024u;
constexpr size_t kLayoutCacheMaxEntries  = 65536;

constexpr Uint64 kFrameIntervalNs = SDL_NS_PER_SECOND / 60;   // animation pacing
constexpr float  kNominalDt = 1.0f / 60.0f;
constexpr float  kMinDt     = 0.001f;
constexpr float  kMaxDt     = 0.050f;
constexpr float  kScrollK   = 13.4f;   // exp. smoothing rate (== 0.2/frame at 60fps)
constexpr float  kScrollSnap = 0.5f;   // px

constexpr Uint64 kRepeatDelayNs    = 400 * SDL_NS_PER_MS;
constexpr Uint64 kRepeatIntervalNs = 80 * SDL_NS_PER_MS;
constexpr int    kAxisEnter = 16000;   // of 32767; hysteresis so the stick doesn't chatter
constexpr int    kAxisExit  = 9000;

constexpr float  kMaxItemFrac = 0.49f; // label / value box each get at most this share of the row

#if ENABLE_DEBUGGING
constexpr int kMaxLoggedMismatches    = 16;
#endif

constexpr uint32_t kNoStr = 0xFFFFFFFFu;

// All colors and sizes in one place. Sizes are in "base pixels" (480px tall
// display) and are multiplied by the UI scale once at init (see apply_scale).
struct Style {
    // --- Colors -------------------------------------------------------------
    SDL_Color bg{18, 19, 24, 255};
    SDL_Color barBg{12, 13, 17, 255};
    SDL_Color divider{33, 36, 45, 255};
    SDL_Color selectedRow{30, 33, 44, 255};
    SDL_Color selectedNonInteractiveRow{22, 23, 28, 255};
    SDL_Color accent{236, 99, 52, 255};
    SDL_Color onAccent{255, 255, 255, 255};
    SDL_Color text{229, 231, 238, 255};
    SDL_Color textSecondary{134, 140, 156, 255};
    SDL_Color editBoxFill{11, 12, 16, 255};
    SDL_Color editBoxBorder{50, 54, 68, 255};
    SDL_Color scrollThumb{64, 69, 85, 255};

    // --- Header / footer ----------------------------------------------------
    int headerH = 48;
    int headerBorderH = 2;
    int footerH = 40;
    int footerBorderH = 1;
    int barPadX = 16;        // header/footer text is clipped to (width - 2*barPadX)

    // --- Rows ---------------------------------------------------------------
    int rowMinHeight = 60;
    int rowPadTop = 12, rowPadBottom = 12;
    int rowPadLeft = 24, rowPadRight = 20;
    int rowHSpacing = 16;
    int rowDividerH = 1;
    int accentBarW = 4;

    // --- Value box ----------------------------------------------------------
    int editBoxPadX = 12, editBoxPadY = 4;
    int editBoxMinW = 72;
    int editBoxBorderW = 1;

    // --- Scrollbar ----------------------------------------------------------
    int scrollbarWidth = 4;
    int scrollbarMarginX = 3;
    int scrollbarMarginY = 4;
    int scrollbarMinThumb = 24;
};

Style T;

void apply_scale(float s) {
    const Style d{};
    T = d;
    auto S = [s](int v) { return std::max(1, static_cast<int>(std::lround(v * s))); };
    T.headerH = S(d.headerH);           T.headerBorderH = S(d.headerBorderH);
    T.footerH = S(d.footerH);           T.footerBorderH = S(d.footerBorderH);
    T.barPadX = S(d.barPadX);
    T.rowMinHeight = S(d.rowMinHeight);
    T.rowPadTop = S(d.rowPadTop);       T.rowPadBottom = S(d.rowPadBottom);
    T.rowPadLeft = S(d.rowPadLeft);     T.rowPadRight = S(d.rowPadRight);
    T.rowHSpacing = S(d.rowHSpacing);   T.rowDividerH = S(d.rowDividerH);
    T.accentBarW = S(d.accentBarW);
    T.editBoxPadX = S(d.editBoxPadX);   T.editBoxPadY = S(d.editBoxPadY);
    T.editBoxMinW = S(d.editBoxMinW);   T.editBoxBorderW = S(d.editBoxBorderW);
    T.scrollbarWidth = S(d.scrollbarWidth);
    T.scrollbarMarginX = S(d.scrollbarMarginX);
    T.scrollbarMarginY = S(d.scrollbarMarginY);
    T.scrollbarMinThumb = S(d.scrollbarMinThumb);
}

// =============================================================================
// Types
// =============================================================================
enum ItemKind : uint8_t { ITEM_NONE = 0, ITEM_ICON, ITEM_BOX };

// Everything about a row that depends only on its *content* (not on whether it
// is selected, and not on scroll position). Positions are derived at draw time.
struct RowLayout {
    int rowH = 0;

    int  labelW = 0, labelH = 0, labelWrapW = 0;   // wrapW 0 => no wrapping needed
    bool labelRTL = false;

    ItemKind itemKind = ITEM_NONE;
    int  itemW = 0, itemH = 0;                     // outer size (box or icon)
    int  itemTextW = 0, itemTextH = 0, itemWrapW = 0;
    bool itemRTL = false;
};

struct LayoutEntry {
    RowLayout L;
    uint64_t lastFrame = 0;
};

struct RowRec {
    int y = 0;                       // top, in content space
    int h = 0;
    uint32_t labelOff = kNoStr;      // offsets into the arena
    uint32_t itemOff  = kNoStr;
    uint64_t labelHash = 0;
    uint64_t itemHash = 0;
    RowLayout L;
    MenuiAlign align = MENUI_ALIGN_LEFT;
    bool selected = false;
    bool interactive = false;
};

// Owning wrapper around a texture.
struct Text {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;

    Text() = default;
    Text(SDL_Texture* t, int w_, int h_) : tex(t), w(w_), h(h_) {}
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;
    Text(Text&& o) noexcept : tex(o.tex), w(o.w), h(o.h) { o.tex = nullptr; }
    Text& operator=(Text&& o) noexcept {
        if (this != &o) {
            if (tex) SDL_DestroyTexture(tex);
            tex = o.tex; w = o.w; h = o.h;
            o.tex = nullptr;
        }
        return *this;
    }
    ~Text() { if (tex) SDL_DestroyTexture(tex); }

    size_t bytes() const { return tex ? static_cast<size_t>(w) * static_cast<size_t>(h) * 4u : 0u; }
};

struct TexEntry {
    Text text;                              // may hold no texture (negative cache)
    uint64_t lastFrame = 0;
    std::list<uint64_t>::iterator lru;
};

// Header / footer: single slot, replaced as soon as the key changes.
struct BarSlot {
    uint64_t key = 0;
    bool valid = false;
    Text tex;
    void reset() { valid = false; key = 0; tex = Text{}; }
};
#if ENABLE_DEBUGGING
struct Stats {
    // per frame (reset in BeginFrame)
    uint32_t rowsSubmitted = 0, rowsDrawn = 0;
    uint32_t layoutHit = 0, layoutMiss = 0;
    uint32_t texHit = 0, texMiss = 0;
    uint32_t rasterCount = 0;
    uint64_t rasterNs = 0;
    uint32_t barRaster = 0;
    // running totals
    uint64_t layoutHitTotal = 0, layoutMissTotal = 0;
    uint64_t texHitTotal = 0, texMissTotal = 0;
    uint64_t evictions = 0, evictedBytes = 0;
    uint64_t layoutSweeps = 0;
    uint32_t overBudgetFrames = 0;
    uint32_t measureMismatch = 0;
    uint32_t missingGlyphs = 0;
    uint32_t glyphWarnings = 0;
    uint32_t texFailures = 0;
    uint64_t inputsReturned = 0;
    uint64_t repeatsFired = 0;
    // timings (ms)
    double buildMs = 0, drawMs = 0, presentMs = 0, intervalMs = 0;
};
#endif

enum Dir { D_UP = 0, D_DOWN = 1, D_LEFT = 2, D_RIGHT = 3, D_COUNT = 4 };

struct Ctx {
    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font*     font      = nullptr;   // == fonts[0]
    TTF_Font*     debugFont = nullptr;
    std::vector<TTF_Font*>    fonts;     // [0] primary, rest fallbacks (successfully loaded only)
    std::vector<SDL_Gamepad*> gamepads;

    bool initialized = false;
    bool running = false;
    bool needsRedraw = true;

    int   winW = 0, winH = 0, lastW = -1, lastH = -1;
    int   maxTexSize = 4096;
    float scale = 1.0f;
    int   vsync = 0;
    int   fontsRequested = 0;
    const char* rendererName = "?";

    // frame / pacing
    uint64_t frame = 0;
    Uint64 frameStartNs = 0, lastFrameStartNs = 0, nextFrameNs = 0;
    bool animating = false, wasAnimating = false;
    float dt = kNominalDt;

    // scrolling
    int   curY = 0;
    float scrollY = 0.0f, targetScrollY = 0.0f, desiredScrollY = 0.0f;

    // per-frame submission
    std::string headerText, footerText;
    std::vector<RowRec> rows;
    std::vector<char> arena;

    // caches
    std::unordered_map<uint64_t, LayoutEntry> layoutCache;
    std::unordered_map<uint64_t, TexEntry> texCache;
    std::list<uint64_t> texLru;               // front = most recent
    size_t texBytes = 0;
    BarSlot headerSlot, footerSlot;

    // input
    bool padDown[D_COUNT] = {false, false, false, false};
    bool axisDown[D_COUNT] = {false, false, false, false};
    int  heldDir = -1;
    Uint64 nextRepeatNs = 0;
    MenuiInput lastInput = INPUT_NONE;
    #if ENABLE_DEBUGGING
    Stats stats;
    #endif
};

Ctx G;

// =============================================================================
// Small helpers
// =============================================================================
inline uint64_t fnv1a_n(const char* s, size_t n) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; ++i) { h ^= static_cast<uint8_t>(s[i]); h *= 0x100000001b3ULL; }
    return h;
}
inline uint64_t fnv1a(const char* s) { return fnv1a_n(s, std::strlen(s)); }
inline uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

int font_height() { return G.font ? TTF_GetFontHeight(G.font) : 0; }
int viewport_height() { return std::max(0, G.winH - T.headerH - T.footerH); }

void fill_rect(int x, int y, int w, int h, SDL_Color c) {
    if (w <= 0 || h <= 0) return;
    const SDL_FRect r{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    SDL_SetRenderDrawColor(G.renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(G.renderer, &r);
}

// Y to draw something `item_h` tall so it's centered in a row starting at
// `row_y` (ignoring the divider line at the bottom of the row).
int row_center(int row_y, int row_h, int item_h) {
    return row_y + (row_h - T.rowDividerH - item_h) / 2;
}

// Horizontal placement of something `tex_w` wide inside a slot [x, x+w).
// Works when tex_w > w as well (it just overhangs symmetrically / to the left).
int align_x(int x, int w, int tex_w, MenuiAlign a) {
    switch (a) {
        case MENUI_ALIGN_CENTER: return x + (w - tex_w) / 2;
        case MENUI_ALIGN_RIGHT:  return x + (w - tex_w);
        default:                 return x;
    }
}
# if ENABLE_DEBUGGING
const char* input_name(MenuiInput i) {
    switch (i) {
        case INPUT_NONE: return "NONE";   case INPUT_QUIT: return "QUIT";
        case INPUT_UP: return "UP";       case INPUT_DOWN: return "DOWN";
        case INPUT_LEFT: return "LEFT";   case INPUT_RIGHT: return "RIGHT";
        case INPUT_A: return "A";         case INPUT_B: return "B";
    }
    return "?";
}
#endif

// ---- UTF-8 / text direction -------------------------------------------------
size_t first_codepoint_len(const char* s) {
    const char* p = s;
    SDL_StepUTF8(&p, nullptr);
    return static_cast<size_t>(p - s);
}

bool is_rtl_codepoint(Uint32 cp) {
    return (cp >= 0x0590 && cp <= 0x08FF) ||      // Hebrew, Arabic, Syriac, Thaana, NKo, Arabic ext.
           (cp >= 0xFB1D && cp <= 0xFDFF) ||      // presentation forms A
           (cp >= 0xFE70 && cp <= 0xFEFF) ||      // presentation forms B
           cp == 0x200F ||                        // RLM
           (cp >= 0x10800 && cp <= 0x10FFF) ||
           (cp >= 0x1E800 && cp <= 0x1EFFF);
}

bool is_ltr_letter(Uint32 cp) {
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
    if (cp == 0xD7 || cp == 0xF7) return false;
    if (cp >= 0xC0 && cp < 0x0590) return true;
    if (cp >= 0x0900 && cp < 0x2000) return true;
    if (cp >= 0x2C00 && cp < 0xD800) return true;
    if (cp >= 0xF900 && cp < 0xFB1D) return true;
    if (cp >= 0xFF21 && cp <= 0xFF5A) return true;
    return false;
}

// Base direction = direction of the first strong character (digits, emoji and
// punctuation are neutral).
bool detect_rtl(const char* s) {
    if (!s) return false;
    const char* p = s;
    while (*p) {
        const Uint32 cp = SDL_StepUTF8(&p, nullptr);
        if (is_rtl_codepoint(cp)) return true;
        if (is_ltr_letter(cp)) return false;
    }
    return false;
}

void set_font_direction(bool rtl) {
    if (!kForceTextDirection || !G.font) return;
    TTF_SetFontDirection(G.font, rtl ? TTF_DIRECTION_RTL : TTF_DIRECTION_LTR);
}


// =============================================================================
// Text measuring & rasterizing
// =============================================================================
struct Measure {
    int w = 0, h = 0;
    int wrapW = 0;     // 0 => fits on one line (only explicit newlines break)
    bool rtl = false;
};

// Size of `s` after shaping. Figures out whether it needs to wrap at `maxW`:
// if the natural single-line width fits, it doesn't wrap.
Measure measure_block(const char* s, int maxW) {
    Measure m;
    if (!G.font || !s || !*s) return m;
    m.rtl = detect_rtl(s);
    set_font_direction(m.rtl);

    int w = 0, h = 0;
    if (!TTF_GetStringSizeWrapped(G.font, s, 0, 0, &w, &h)) {
        MLOG("WARN TTF_GetStringSizeWrapped failed: %s", SDL_GetError());
        w = 0; h = font_height();
    }
    if (maxW > 0 && w > maxW) {
        int w2 = 0, h2 = 0;
        if (TTF_GetStringSizeWrapped(G.font, s, 0, maxW, &w2, &h2)) {
            w = w2; h = h2; m.wrapW = maxW;
        }
    }
    m.w = w; m.h = h;
    return m;
}

// Takes ownership of `s`, uploads to a texture.
Text surface_to_text(SDL_Surface* s) {
    if (!s) return {};
    Text out;
    if (s->w > G.maxTexSize || s->h > G.maxTexSize) {
        #if ENABLE_DEBUGGING
        G.stats.texFailures++;
        #endif
        MLOG("WARN text surface %dx%d exceeds max texture size %d; dropped", s->w, s->h, G.maxTexSize);
    } else {
        SDL_Texture* t = SDL_CreateTextureFromSurface(G.renderer, s);
        if (!t) {
            #if ENABLE_DEBUGGING
            G.stats.texFailures++;
            #endif
            MLOG("WARN SDL_CreateTextureFromSurface(%dx%d) failed: %s", s->w, s->h, SDL_GetError());
        } else {
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
            out = Text(t, s->w, s->h);
        }
    }
    SDL_DestroySurface(s);
    return out;
}

constexpr SDL_Color kWhite{255, 255, 255, 255};

// Wrapped, aligned block of text (labels, values, icons). White; tinted at draw.
Text make_block_text(const char* s, int wrapW, MenuiAlign align, bool rtl, int expW, int expH) {
    const Uint64 t0 = SDL_GetTicksNS();

    set_font_direction(rtl);
    TTF_SetFontWrapAlignment(G.font,
        align == MENUI_ALIGN_CENTER ? TTF_HORIZONTAL_ALIGN_CENTER :
        align == MENUI_ALIGN_RIGHT  ? TTF_HORIZONTAL_ALIGN_RIGHT  : TTF_HORIZONTAL_ALIGN_LEFT);

    SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(G.font, s, 0, kWhite, wrapW);
    if (!surf) {
        MLOG("WARN TTF_RenderText_Blended_Wrapped failed for \"%.32s\": %s", s, SDL_GetError());
    } else {
        #if ENABLE_DEBUGGING
        const bool mismatch = (wrapW == 0)
            ? (std::abs(surf->w - expW) > 2 || std::abs(surf->h - expH) > 2)
            : (surf->w > wrapW + 2 || std::abs(surf->h - expH) > 2);
        if (mismatch) {
            G.stats.measureMismatch++;
            if (G.stats.measureMismatch <= static_cast<uint32_t>(kMaxLoggedMismatches))
                MLOG("WARN measure/raster mismatch \"%.24s\": measured %dx%d, rasterized %dx%d (wrapW %d)",
                     s, expW, expH, surf->w, surf->h, wrapW);
        }
        #endif
    }

    Text out = surface_to_text(surf);
    #if ENABLE_DEBUGGING
    G.stats.rasterCount++;
    G.stats.rasterNs += SDL_GetTicksNS() - t0;
    #endif
    return out;
}

// Single line. If wider than `maxW` the result is hard-cropped to exactly
// `maxW` columns: LTR keeps the right-most columns (left side cut off), RTL
// keeps the left-most (right side cut off). Narrower text is returned as-is
// and the caller centers it.
Text make_clipped_line(const char* s, int maxW) {
    if (!G.font || !s || !*s || maxW <= 0) return {};
    const Uint64 t0 = SDL_GetTicksNS();

    const bool rtl = detect_rtl(s);
    set_font_direction(rtl);
    SDL_Surface* src = TTF_RenderText_Blended(G.font, s, 0, kWhite);
    if (!src) {
        MLOG("WARN TTF_RenderText_Blended failed for \"%.32s\": %s", s, SDL_GetError());
        return {};
    }
    if (src->w > maxW) {
        SDL_Surface* cropped = SDL_CreateSurface(maxW, src->h, src->format);
        if (cropped) {
            SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);   // copy raw RGBA, don't blend onto black
            const SDL_Rect r{rtl ? 0 : src->w - maxW, 0, maxW, src->h};
            if (!SDL_BlitSurface(src, &r, cropped, nullptr))
                MLOG("WARN crop blit failed: %s", SDL_GetError());
            SDL_DestroySurface(src);
            src = cropped;
        } else {
            MLOG("WARN SDL_CreateSurface for crop failed: %s", SDL_GetError());
        }
    }
    Text out = surface_to_text(src);
    #if ENABLE_DEBUGGING
    G.stats.rasterCount++;
    G.stats.rasterNs += SDL_GetTicksNS() - t0;
    #endif
    return out;
}

void draw_text(const Text& t, int x, int y, SDL_Color c) {
    if (!t.tex) return;
    SDL_SetTextureColorMod(t.tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t.tex, c.a);
    const SDL_FRect d{static_cast<float>(x), static_cast<float>(y), static_cast<float>(t.w), static_cast<float>(t.h)};
    SDL_RenderTexture(G.renderer, t.tex, nullptr, &d);
}

// =============================================================================
// Text cache: byte-budgeted LRU
// =============================================================================
enum { MODE_BLOCK = 1 };

inline uint64_t text_key(uint64_t strHash, int wrapW, int align, int mode) {
    return mix(mix(mix(strHash, static_cast<uint64_t>(wrapW)), static_cast<uint64_t>(align)), static_cast<uint64_t>(mode));
}

// Returns a pointer valid until the next EndFrame. May point at a Text with no
// texture (rasterization failed / empty); callers check `->tex`.
template <typename MakeFn>
const Text* text_cache_get(uint64_t key, MakeFn&& make) {
    auto it = G.texCache.find(key);
    if (it != G.texCache.end()) {
        #if ENABLE_DEBUGGING
        G.stats.texHit++; G.stats.texHitTotal++;
        #endif
        it->second.lastFrame = G.frame;
        G.texLru.splice(G.texLru.begin(), G.texLru, it->second.lru);
        return &it->second.text;
    }
    #if ENABLE_DEBUGGING
    G.stats.texMiss++; G.stats.texMissTotal++;
    #endif
    Text t = make();
    G.texLru.push_front(key);
    TexEntry e;
    e.text = std::move(t);
    e.lastFrame = G.frame;
    e.lru = G.texLru.begin();
    G.texBytes += e.text.bytes();
    auto res = G.texCache.emplace(key, std::move(e));
    return &res.first->second.text;
}

void clear_text_cache() {
    G.texCache.clear();   // ~Text destroys the textures
    G.texLru.clear();
    G.texBytes = 0;
}

void evict_text_cache() {
    // Never evict anything used this frame: those textures may still be needed
    // next frame and the LRU tail is always the oldest.
    while (G.texBytes > kTextCacheBudgetBytes && !G.texLru.empty()) {
        const uint64_t k = G.texLru.back();
        auto it = G.texCache.find(k);
        if (it == G.texCache.end()) { G.texLru.pop_back(); continue; }
        if (it->second.lastFrame == G.frame) break;
        const size_t b = it->second.text.bytes();
        G.texBytes -= b;
        #if ENABLE_DEBUGGING
        G.stats.evictions++;
        G.stats.evictedBytes += b;
        #endif
        G.texCache.erase(it);
        G.texLru.pop_back();
    }
    #if ENABLE_DEBUGGING
    if (G.texBytes > kTextCacheBudgetBytes) G.stats.overBudgetFrames++;
    #endif
}

void trim_layout_cache() {
    if (G.layoutCache.size() <= kLayoutCacheMaxEntries) return;
    for (auto it = G.layoutCache.begin(); it != G.layoutCache.end();) {
        if (it->second.lastFrame != G.frame) it = G.layoutCache.erase(it);
        else ++it;
    }
    #if ENABLE_DEBUGGING
    G.stats.layoutSweeps++;
    #endif
}

// =============================================================================
// Layout
// =============================================================================
// Row layout, depending on label alignment:
//   LEFT:   [accent][pad][label] ................ [icon | value box][pad]
//   CENTER: [accent][pad] ........ [icon][label] ........ [pad]
//   RIGHT:  [accent][pad][icon | value box] ................ [label][pad]
//
// Sizing (independent, neither can push the other around):
//   * `avail` = row width minus accent bar and left/right padding.
//   * Value box: at most maxEach (~49% of avail) wide; its text wraps to fit.
//   * Label (when a value box exists): same limit.
//   * Label (icon or nothing): whatever is left after the icon.
RowLayout compute_layout(const char* label, ItemKind kind, const char* item, MenuiAlign /*align*/) {
    RowLayout L;
    L.itemKind = kind;

    const int contentX = T.accentBarW + T.rowPadLeft;
    const int avail = std::max(20, G.winW - contentX - T.rowPadRight);
    const int gap = T.rowHSpacing;
    const int maxEach = std::max(20, std::min(static_cast<int>(avail * kMaxItemFrac), (avail - gap) / 2));

    // ---- item ----
    if (kind == ITEM_ICON) {
        const Measure m = measure_block(item, avail);
        L.itemTextW = m.w; L.itemTextH = m.h; L.itemWrapW = m.wrapW; L.itemRTL = m.rtl;
        L.itemW = m.w; L.itemH = m.h;
    } else if (kind == ITEM_BOX) {
        const bool hasText = item && *item;
        const int innerMax = std::max(10, maxEach - T.editBoxPadX * 2);
        const Measure m = hasText ? measure_block(item, innerMax) : Measure{};
        L.itemTextW = m.w; L.itemTextH = m.h; L.itemWrapW = m.wrapW; L.itemRTL = m.rtl;
        L.itemW = std::min(std::max(m.w + T.editBoxPadX * 2, T.editBoxMinW), maxEach);
        L.itemH = (hasText ? m.h : font_height()) + T.editBoxPadY * 2;
    }

    // ---- label ----
    int labelMax = avail;
    if (kind == ITEM_BOX)       labelMax = maxEach;
    else if (kind == ITEM_ICON) labelMax = avail - L.itemW - gap;
    labelMax = std::max(20, labelMax);

    const Measure lm = measure_block(label, labelMax);
    L.labelW = lm.w; L.labelH = lm.h; L.labelWrapW = lm.wrapW; L.labelRTL = lm.rtl;

    const int contentH = std::max(L.labelH, L.itemH);
    L.rowH = std::max(T.rowMinHeight, T.rowPadTop + contentH + T.rowPadBottom);
    return L;
}

// If this row is selected, aim the viewport so the row is centered.
// Must be called *before* curY advances past this row.
void update_scroll_target(bool selected, int row_height) {
    if (!selected) return;
    float desired;
    if (row_height > viewport_height()) {
        desired = G.curY;
    } else {
        desired = (G.curY + row_height / 2.0f) - viewport_height() / 2.0f;
    }

    desired = std::max(desired, 0.0f);
    if (std::abs(G.desiredScrollY - desired) > 1.0f) {
        G.desiredScrollY = desired;
        G.needsRedraw = true;
    }
}

uint32_t arena_add(const char* s, size_t len) {
    const uint32_t off = static_cast<uint32_t>(G.arena.size());
    G.arena.insert(G.arena.end(), s, s + len);
    G.arena.push_back('\0');
    return off;
}

// =============================================================================
// Drawing
// =============================================================================
void draw_row(const RowRec& r, int y) {
    const RowLayout& L = r.L;
    const bool sel = r.selected;

    SDL_Color row_bg_color = T.bg;
    if (r.interactive && r.selected) {
        row_bg_color = T.selectedRow;
    }  else if (r.selected) { // selected but not interactive
        row_bg_color = T.selectedNonInteractiveRow;
    }

    // Background, divider, selection marker.
    fill_rect(0, y, G.winW, r.h, row_bg_color);
    fill_rect(0, y + r.h - T.rowDividerH, G.winW, T.rowDividerH, T.divider);
    if (r.interactive && sel) fill_rect(0, y, T.accentBarW, r.h - T.rowDividerH, T.accent);

    const int contentX = T.accentBarW + T.rowPadLeft;
    const int avail = std::max(20, G.winW - contentX - T.rowPadRight);
    const int gap = T.rowHSpacing;

    const bool hasLabel = L.labelW > 0 && r.labelOff != kNoStr;
    const bool hasItem  = L.itemKind != ITEM_NONE;

    // ---- horizontal slots ----
    int labelX = contentX, itemX = 0;
    switch (r.align) {
        case MENUI_ALIGN_LEFT:
            labelX = contentX;
            itemX  = G.winW - T.rowPadRight - L.itemW;
            break;
        case MENUI_ALIGN_RIGHT:
            labelX = G.winW - T.rowPadRight - L.labelW;
            itemX  = contentX;
            break;
        case MENUI_ALIGN_CENTER: {
            const int iconPart = (hasItem ? L.itemW : 0) + ((hasItem && hasLabel) ? gap : 0);
            const int groupW = iconPart + (hasLabel ? L.labelW : 0);
            const int startX = contentX + (avail - groupW) / 2;
            itemX  = startX;
            labelX = startX + iconPart;
            break;
        }
    }

    // ---- label ----
    if (hasLabel) {
        const char* s = G.arena.data() + r.labelOff;
        const uint64_t key = text_key(r.labelHash, L.labelWrapW, r.align, MODE_BLOCK);
        const Text* t = text_cache_get(key, [&] {
            return make_block_text(s, L.labelWrapW, r.align, L.labelRTL, L.labelW, L.labelH);
        });
        if (t && t->tex) draw_text(*t, align_x(labelX, L.labelW, t->w, r.align), row_center(y, r.h, t->h), T.text);
    }

    // ---- icon / value box ----
    if (L.itemKind == ITEM_ICON && r.itemOff != kNoStr) {
        const char* s = G.arena.data() + r.itemOff;
        const uint64_t key = text_key(r.itemHash, L.itemWrapW, MENUI_ALIGN_CENTER, MODE_BLOCK);
        const Text* t = text_cache_get(key, [&] {
            return make_block_text(s, L.itemWrapW, MENUI_ALIGN_CENTER, L.itemRTL, L.itemTextW, L.itemTextH);
        });
        if (t && t->tex)
            draw_text(*t, align_x(itemX, L.itemW, t->w, MENUI_ALIGN_CENTER), row_center(y, r.h, t->h),
                      sel ? T.text : T.textSecondary);
    } else if (L.itemKind == ITEM_BOX) {
        const int boxH = L.itemH;
        const int boxY = row_center(y, r.h, boxH);
        const int bw = T.editBoxBorderW;
        fill_rect(itemX, boxY, L.itemW, boxH, sel ? T.accent : T.editBoxBorder);
        fill_rect(itemX + bw, boxY + bw, L.itemW - bw * 2, boxH - bw * 2, sel ? T.accent : T.editBoxFill);

        if (r.itemOff != kNoStr) {
            const char* s = G.arena.data() + r.itemOff;
            const uint64_t key = text_key(r.itemHash, L.itemWrapW, MENUI_ALIGN_CENTER, MODE_BLOCK);
            const Text* t = text_cache_get(key, [&] {
                return make_block_text(s, L.itemWrapW, MENUI_ALIGN_CENTER, L.itemRTL, L.itemTextW, L.itemTextH);
            });
            if (t && t->tex)
                draw_text(*t, itemX + (L.itemW - t->w) / 2, boxY + (boxH - t->h) / 2,
                          sel ? T.onAccent : T.text);
        }
    }
}

void draw_bar(BarSlot& slot, const std::string& text, bool top, int height, int border_h, SDL_Color bg, SDL_Color border, SDL_Color text_color, MenuiAlign align = MENUI_ALIGN_CENTER) {
    const int y = top ? 0 : G.winH - height;
    fill_rect(0, y, G.winW, height, bg);
    fill_rect(0, top ? y + height - border_h : y, G.winW, border_h, border);

    if (text.empty()) { if (slot.valid) slot.reset(); return; }

    const int maxW = std::max(1, G.winW - T.barPadX * 2);
    const uint64_t key = mix(fnv1a(text.c_str()), static_cast<uint64_t>(maxW));
    if (!slot.valid || slot.key != key) {
        slot.tex = make_clipped_line(text.c_str(), maxW);   // old texture destroyed by move-assign
        slot.key = key;
        slot.valid = true;
        #if ENABLE_DEBUGGING
        G.stats.barRaster++;
        #endif
    }
    if (!slot.tex.tex) return;

    const int content_y = top ? y : y + border_h;
    const int content_h = height - border_h;
    // Centered while it fits; once clipped its width == maxW so this is the left margin.
    int x_offset;
    if (align == MENUI_ALIGN_LEFT) {
        x_offset = 0 + T.barPadX;
    } else if (align == MENUI_ALIGN_CENTER) {
        x_offset = T.barPadX + (maxW - slot.tex.w) / 2;
    } else if(align == MENUI_ALIGN_RIGHT) {
        x_offset = G.winW - slot.tex.w - T.barPadX;
    }
    draw_text(slot.tex, x_offset, content_y + (content_h - slot.tex.h) / 2, text_color);
}

void draw_scrollbar(int viewport_h, float max_scroll, int scrollInt) {
    if (max_scroll <= 0.0f) return;
    const int track_x = G.winW - T.scrollbarWidth - T.scrollbarMarginX;
    const int track_y = T.headerH + T.scrollbarMarginY;
    const int track_h = std::max(1, viewport_h - T.scrollbarMarginY * 2);

    const float visible_ratio = static_cast<float>(viewport_h) / static_cast<float>(std::max(1, G.curY));
    const int thumb_h = std::min(track_h, std::max(T.scrollbarMinThumb, static_cast<int>(track_h * visible_ratio)));
    const float progress = std::clamp(static_cast<float>(scrollInt) / max_scroll, 0.0f, 1.0f);
    const int thumb_y = track_y + static_cast<int>((track_h - thumb_h) * progress);
    fill_rect(track_x, thumb_y, T.scrollbarWidth, thumb_h, T.scrollThumb);
}

// Not cached on purpose (see requirements): rebuilt and destroyed every frame.
# if ENABLE_DEBUGGING
void draw_debug_overlay(int scrollInt, float max_scroll) {
    TTF_Font* f = G.debugFont ? G.debugFont : G.font;
    if (!f) return;

    const Stats& s = G.stats;
    const double lTot = static_cast<double>(s.layoutHitTotal + s.layoutMissTotal);
    const double tTot = static_cast<double>(s.texHitTotal + s.texMissTotal);
    const double lPct = lTot > 0 ? 100.0 * static_cast<double>(s.layoutHitTotal) / lTot : 0.0;
    const double tPct = tTot > 0 ? 100.0 * static_cast<double>(s.texHitTotal) / tTot : 0.0;
    int hb[3] = {0, 0, 0};
    TTF_GetHarfBuzzVersion(&hb[0], &hb[1], &hb[2]);

    char buf[1600];
    std::snprintf(buf, sizeof buf,
        "%dx%d  scale %.2f  %s  vsync %d\n"
        "frame %llu  interval %.1fms  dt %.1fms  %s\n"
        "build %.2f  draw %.2f  present %.2f ms\n"
        "rows %u submitted / %u drawn  contentH %d\n"
        "scroll %d (target %.0f / max %.0f)\n"
        "layout$ %zu  hit %u miss %u  (all %.1f%%)\n"
        "text$ %zu  %.2f / %.1f MB  hit %u miss %u  (all %.1f%%)\n"
        "raster %u this frame (%.2fms)  bar %u  evict %llu (%.1f MB)\n"
        "arena %zu B  rows cap %zu\n"
        "fonts %zu/%d  HarfBuzz %d.%d.%d\n"
        "WARN mismatch %u  tofu %u  texfail %u  overbudget %u\n"
        "pads %zu  last input %s  total %llu  repeats %llu",
        G.winW, G.winH, G.scale, G.rendererName, G.vsync,
        static_cast<unsigned long long>(G.frame), s.intervalMs, G.dt * 1000.0f, G.animating ? "ANIM" : "idle",
        s.buildMs, s.drawMs, s.presentMs,
        s.rowsSubmitted, s.rowsDrawn, G.curY,
        scrollInt, G.targetScrollY, max_scroll,
        G.layoutCache.size(), s.layoutHit, s.layoutMiss, lPct,
        G.texCache.size(), G.texBytes / 1048576.0, kTextCacheBudgetBytes / 1048576.0, s.texHit, s.texMiss, tPct,
        s.rasterCount, s.rasterNs / 1e6, s.barRaster,
        static_cast<unsigned long long>(s.evictions), s.evictedBytes / 1048576.0,
        G.arena.size(), G.rows.capacity(),
        G.fonts.size(), G.fontsRequested, hb[0], hb[1], hb[2],
        s.measureMismatch, s.missingGlyphs, s.texFailures, s.overBudgetFrames,
        G.gamepads.size(), input_name(G.lastInput),
        static_cast<unsigned long long>(s.inputsReturned), static_cast<unsigned long long>(s.repeatsFired));

    SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(f, buf, 0, SDL_Color{130, 255, 150, 255}, 0);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(G.renderer, surf);
    if (tex) {
        const int pad = std::max(2, static_cast<int>(4 * G.scale));
        const int x = pad, y = T.headerH + pad;
        fill_rect(x - pad / 2, y - pad / 2, surf->w + pad, surf->h + pad, SDL_Color{0, 0, 0, 185});
        const SDL_FRect d{static_cast<float>(x), static_cast<float>(y), static_cast<float>(surf->w), static_cast<float>(surf->h)};
        SDL_RenderTexture(G.renderer, tex, nullptr, &d);
        SDL_DestroyTexture(tex);
    }
    SDL_DestroySurface(surf);
}
#endif

// =============================================================================
// Input
// =============================================================================
MenuiInput dir_input(int d) {
    switch (d) {
        case D_UP: return INPUT_UP;
        case D_DOWN: return INPUT_DOWN;
        case D_LEFT: return INPUT_LEFT;
        default: return INPUT_RIGHT;
    }
}

bool dir_is_down(int d) { return G.padDown[d] || G.axisDown[d]; }

void reset_held_input() {
    for (int i = 0; i < D_COUNT; ++i) { G.padDown[i] = false; G.axisDown[i] = false; }
    G.heldDir = -1;
}

// Updates one direction source (d-pad or stick). Returns the input to emit on
// a fresh press, and arms/disarms auto-repeat.
MenuiInput set_dir(bool* arr, int d, bool down) {
    const bool before = dir_is_down(d);
    arr[d] = down;
    const bool after = dir_is_down(d);
    const Uint64 now = SDL_GetTicksNS();
    if (!before && after) {
        G.heldDir = d;
        G.nextRepeatNs = now + kRepeatDelayNs;
        return dir_input(d);
    }
    if (before && !after && G.heldDir == d) {
        G.heldDir = -1;
        for (int e = 0; e < D_COUNT; ++e) {
            if (dir_is_down(e)) { G.heldDir = e; G.nextRepeatNs = now + kRepeatDelayNs; break; }
        }
    }
    return INPUT_NONE;
}

MenuiInput axis_update(int dNeg, int dPos, int value) {
    bool neg = G.axisDown[dNeg], pos = G.axisDown[dPos];
    if (value <= -kAxisEnter)      { neg = true;  pos = false; }
    else if (value >= kAxisEnter)  { pos = true;  neg = false; }
    else if (std::abs(value) < kAxisExit) { neg = false; pos = false; }
    const MenuiInput a = set_dir(G.axisDown, dNeg, neg);
    const MenuiInput b = set_dir(G.axisDown, dPos, pos);
    return a != INPUT_NONE ? a : b;
}

void open_gamepad(SDL_JoystickID id) {
    if (!SDL_IsGamepad(id)) return;
    if (SDL_GetGamepadFromID(id)) return;   // already open
    SDL_Gamepad* gp = SDL_OpenGamepad(id);
    if (!gp) { MLOG("WARN SDL_OpenGamepad(%u) failed: %s", static_cast<unsigned>(id), SDL_GetError()); return; }
    G.gamepads.push_back(gp);
    const char* name = SDL_GetGamepadName(gp);
    MLOG("gamepad opened: \"%s\" (instance %u), %zu total", name ? name : "?", static_cast<unsigned>(id), G.gamepads.size());
}

void close_gamepad(SDL_JoystickID id) {
    for (auto it = G.gamepads.begin(); it != G.gamepads.end(); ++it) {
        if (SDL_GetGamepadID(*it) == id) {
            SDL_CloseGamepad(*it);
            G.gamepads.erase(it);
            MLOG("gamepad removed (instance %u), %zu left", static_cast<unsigned>(id), G.gamepads.size());
            break;
        }
    }
    reset_held_input();
}

MenuiInput handle_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT:
            return INPUT_QUIT;

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            G.needsRedraw = true;
            return INPUT_NONE;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            reset_held_input();
            return INPUT_NONE;

        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_TARGETS_RESET:
            MLOG("render device/targets reset: dropping all cached textures");
            clear_text_cache();
            G.headerSlot.reset();
            G.footerSlot.reset();
            G.needsRedraw = true;
            return INPUT_NONE;

        case SDL_EVENT_GAMEPAD_ADDED:
            open_gamepad(e.gdevice.which);
            return INPUT_NONE;
        case SDL_EVENT_GAMEPAD_REMOVED:
            close_gamepad(e.gdevice.which);
            return INPUT_NONE;

        case SDL_EVENT_KEY_DOWN:   // includes OS key-repeat events
            switch (e.key.key) {
                case SDLK_UP:     return INPUT_UP;
                case SDLK_DOWN:   return INPUT_DOWN;
                case SDLK_LEFT:   return INPUT_LEFT;
                case SDLK_RIGHT:  return INPUT_RIGHT;
                case SDLK_RETURN: return INPUT_A;
                case SDLK_ESCAPE: return INPUT_B;
                case SDLK_X:      return INPUT_X;
                case SDLK_Y:      return INPUT_Y;
                default: return INPUT_NONE;
            }

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            MLOG("Button press: %d", e.gbutton.button);
            switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP:    return set_dir(G.padDown, D_UP, true);
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  return set_dir(G.padDown, D_DOWN, true);
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  return set_dir(G.padDown, D_LEFT, true);
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return set_dir(G.padDown, D_RIGHT, true);
                case SDL_GAMEPAD_BUTTON_NORTH:      return INPUT_X;
                case SDL_GAMEPAD_BUTTON_WEST:       return INPUT_Y;
                case SDL_GAMEPAD_BUTTON_SOUTH:      return INPUT_B;
                case SDL_GAMEPAD_BUTTON_EAST:       return INPUT_A;

                default: return INPUT_NONE;
            }

        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP:    set_dir(G.padDown, D_UP, false); break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  set_dir(G.padDown, D_DOWN, false); break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  set_dir(G.padDown, D_LEFT, false); break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: set_dir(G.padDown, D_RIGHT, false); break;
                default: break;
            }
            return INPUT_NONE;

        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) return axis_update(D_LEFT, D_RIGHT, e.gaxis.value);
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) return axis_update(D_UP, D_DOWN, e.gaxis.value);
            return INPUT_NONE;

        default:
            return INPUT_NONE;
    }
}

Sint32 ns_to_wait_ms(Uint64 ns) {
    const Uint64 ms = (ns + SDL_NS_PER_MS - 1) / SDL_NS_PER_MS;
    return static_cast<Sint32>(std::min<Uint64>(ms, 1000000));
}

// =============================================================================
// Shutdown
// =============================================================================
void shutdown() {
    // Textures must die before the renderer does.
    clear_text_cache();
    G.layoutCache.clear();
    G.headerSlot.reset();
    G.footerSlot.reset();

    for (SDL_Gamepad* gp : G.gamepads) SDL_CloseGamepad(gp);
    G.gamepads.clear();

    if (G.font) TTF_ClearFallbackFonts(G.font);
    for (TTF_Font* f : G.fonts) TTF_CloseFont(f);
    G.fonts.clear();
    G.font = nullptr;
    #if ENABLE_DEBUGGING
    if (G.debugFont) { TTF_CloseFont(G.debugFont); G.debugFont = nullptr; }
    #endif
    if (G.renderer) { SDL_DestroyRenderer(G.renderer); G.renderer = nullptr; }
    if (G.window)   { SDL_DestroyWindow(G.window);     G.window = nullptr; }
    if (G.initialized || TTF_WasInit()) TTF_Quit();
    SDL_Quit();
    G.initialized = false;
    G.running = false;
}

}  // namespace

// =============================================================================
// Public API
// =============================================================================
bool Menui_Init(const char* window_title, const char* font_path) {
    if (!font_path) {
        MLOG("ERROR Menui must be initialized with a font path");
        return false;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        MLOG("ERROR SDL_Init: %s", SDL_GetError());
        return false;
    }
    if (!TTF_Init()) {
        MLOG("ERROR TTF_Init: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }
    G.initialized = true;   // from here on shutdown() does a full teardown

    {
        const int sv = SDL_GetVersion(), tv = TTF_Version();
        int hb[3] = {0, 0, 0}, ft[3] = {0, 0, 0};
        TTF_GetHarfBuzzVersion(&hb[0], &hb[1], &hb[2]);
        TTF_GetFreeTypeVersion(&ft[0], &ft[1], &ft[2]);
        MLOG("SDL %d.%d.%d  SDL_ttf %d.%d.%d  FreeType %d.%d.%d  HarfBuzz %d.%d.%d  video driver: %s",
             SDL_VERSIONNUM_MAJOR(sv), SDL_VERSIONNUM_MINOR(sv), SDL_VERSIONNUM_MICRO(sv),
             SDL_VERSIONNUM_MAJOR(tv), SDL_VERSIONNUM_MINOR(tv), SDL_VERSIONNUM_MICRO(tv),
             ft[0], ft[1], ft[2], hb[0], hb[1], hb[2], SDL_GetCurrentVideoDriver());
    }

    // ---- window: fullscreen at whatever the display is already doing ----
    int reqW = 640, reqH = 480;
    {
        const SDL_DisplayID did = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode* dm = did ? SDL_GetDesktopDisplayMode(did) : nullptr;
        if (dm) {
            reqW = dm->w; reqH = dm->h;
            MLOG("desktop mode: %dx%d @ %.2fHz", dm->w, dm->h, dm->refresh_rate);
        } else {
            MLOG("WARN no desktop display mode (%s); asking for %dx%d", SDL_GetError(), reqW, reqH);
        }
    }
    G.window = SDL_CreateWindow(window_title, reqW, reqH, SDL_WINDOW_FULLSCREEN);
    if (!G.window) {
        MLOG("ERROR SDL_CreateWindow: %s", SDL_GetError());
        shutdown();
        return false;
    }
    SDL_SyncWindow(G.window);
    SDL_HideCursor();

    // ---- renderer (override with env SDL_RENDER_DRIVER=software|opengles2|...) ----
    G.renderer = SDL_CreateRenderer(G.window, nullptr);
    if (!G.renderer) {
        MLOG("ERROR SDL_CreateRenderer: %s", SDL_GetError());
        shutdown();
        return false;
    }
    G.rendererName = SDL_GetRendererName(G.renderer);
    if (!G.rendererName) G.rendererName = "?";
    SDL_SetRenderDrawBlendMode(G.renderer, SDL_BLENDMODE_BLEND);

    if (!SDL_SetRenderVSync(G.renderer, 1))
        MLOG("WARN vsync unavailable (%s); pacing relies on the frame deadline only", SDL_GetError());
    if (!SDL_GetRenderVSync(G.renderer, &G.vsync)) G.vsync = 0;

    const SDL_PropertiesID rprops = SDL_GetRendererProperties(G.renderer);
    const int maxTex = static_cast<int>(SDL_GetNumberProperty(rprops, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0));
    if (maxTex > 0) G.maxTexSize = maxTex;

    if (!SDL_GetCurrentRenderOutputSize(G.renderer, &G.winW, &G.winH)) {
        MLOG("WARN SDL_GetCurrentRenderOutputSize failed: %s", SDL_GetError());
        SDL_GetWindowSizeInPixels(G.window, &G.winW, &G.winH);
    }

    // ---- UI scale from the output size (env MENUI_SCALE overrides) ----
    float scale = static_cast<float>(std::min(G.winW, G.winH)) / kReferenceHeight;
    scale = std::round(scale * 4.0f) / 4.0f;
    scale = std::clamp(scale, kMinUiScale, kMaxUiScale);
    if (const char* env = SDL_getenv("MENUI_SCALE")) {
        const float v = static_cast<float>(std::atof(env));
        if (v > 0.1f) { scale = v; MLOG("MENUI_SCALE override: %.2f", v); }
    }
    G.scale = scale;
    apply_scale(scale);
    MLOG("renderer: %s  output %dx%d  vsync %d  max texture %d  UI scale %.2f",
         G.rendererName, G.winW, G.winH, G.vsync, G.maxTexSize, G.scale);

    // Load fonts
    TTF_Font* f = TTF_OpenFont(font_path, kBaseFontSize * scale);
    if (!f) {
        MLOG("ERROR failed to load PRIMARY font \"%s\": %s", font_path ? font_path : "(null)", SDL_GetError());
        shutdown();
        return false;
    }
    G.font = f;
    G.fonts.push_back(f);

    #if ENABLE_DEBUGGING
    // Load debug font
    TTF_Font* debugf = TTF_OpenFont(font_path, kDebugFontSize * scale);
    if (!f) {
        MLOG("ERROR failed to load DEBUG font \"%s\": %s", font_path ? font_path : "(null)", SDL_GetError());
        shutdown();
        return false;
    }
    G.fonts.push_back(debugf);
    G.debugFont = debugf;
    #endif

    // ---- gamepads (hotplug is handled via events) ----
    {
        int n = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&n);
        for (int i = 0; ids && i < n; ++i) open_gamepad(ids[i]);
        SDL_free(ids);
        if (G.gamepads.empty()) MLOG("no gamepad connected yet (keyboard works; hotplug is handled)");
    }

    G.rows.reserve(256);
    G.arena.reserve(16 * 1024);
    G.running = true;
    G.needsRedraw = true;
    return true;
}

void Menui_Quit() {
    if (!G.window && !G.renderer && !G.initialized) return;
    shutdown();
}

bool Menui_IsRunning() { return G.running; }

bool Menui_LoadFallbackFont(const char* font_path, const float scale = 1.0) {
    TTF_Font* f = TTF_OpenFont(font_path, kBaseFontSize * G.scale * scale);
    if (!f) {
        MLOG("WARN failed to load fallback font \"%s\": %s (skipping)", font_path ? font_path : "(null)", SDL_GetError());
        return false;
    }
    if (TTF_AddFallbackFont(G.font, f)) {
        G.fonts.push_back(f);
    } else {
        MLOG("WARN TTF_AddFallbackFont failed for \"%s\": %s (skipping)", font_path, SDL_GetError());
        TTF_CloseFont(f);
    }
    return true;
}


void Menui_SetHeaderText(const char* text) { G.headerText = text ? text : ""; }
void Menui_SetFooterText(const char* text) { G.footerText = text ? text : ""; }

MenuiInput Menui_WaitForInput() {
    if (!G.initialized) return INPUT_QUIT;
    for (;;) {
        const Uint64 now = SDL_GetTicksNS();

        // D-pad / stick auto-repeat.
        if (G.heldDir >= 0 && now >= G.nextRepeatNs) {
            G.nextRepeatNs = now + kRepeatIntervalNs;
            #if ENABLE_DEBUGGING
            G.stats.repeatsFired++;
            G.stats.inputsReturned++;
            #endif
            G.lastInput = dir_input(G.heldDir);
            return G.lastInput;
        }

        // Sleep until the earliest of: next frame deadline (only if a redraw
        // is pending) and next repeat. Otherwise sleep until an event arrives.
        Uint64 wake = UINT64_MAX;
        if (G.needsRedraw) wake = std::min(wake, G.nextFrameNs);
        if (G.heldDir >= 0) wake = std::min(wake, G.nextRepeatNs);

        SDL_Event e;
        bool got;
        if (wake == UINT64_MAX) got = SDL_WaitEvent(&e);
        else got = SDL_WaitEventTimeout(&e, wake <= now ? 0 : ns_to_wait_ms(wake - now));

        if (got) {
            const MenuiInput in = handle_event(e);
            if (in != INPUT_NONE) {
                #if ENABLE_DEBUGGING
                G.stats.inputsReturned++;
                #endif
                G.lastInput = in;
                return in;
            }
            // Don't let a flood of ignorable events (stick jitter...) starve rendering.
            if (G.needsRedraw && SDL_GetTicksNS() >= G.nextFrameNs) return INPUT_NONE;
            continue;
        }

        if (G.needsRedraw && SDL_GetTicksNS() >= G.nextFrameNs) return INPUT_NONE;
    }
}

void Menui_BeginFrame(const char* headerText) {
    if (!G.initialized) return;
    const Uint64 now = SDL_GetTicksNS();
    G.frame++;
    G.frameStartNs = now;

    // Delta time. After an idle gap use one nominal frame instead of a huge dt.
    float dt = kNominalDt;
    if (G.wasAnimating && G.lastFrameStartNs != 0)
        dt = std::clamp(static_cast<float>(now - G.lastFrameStartNs) / 1e9f, kMinDt, kMaxDt);
    G.dt = dt;
    #if ENABLE_DEBUGGING
    G.stats.intervalMs = G.lastFrameStartNs ? static_cast<double>(now - G.lastFrameStartNs) / 1e6 : 0.0;
    #endif
    G.lastFrameStartNs = now;
    G.needsRedraw = false;

    if (!SDL_GetCurrentRenderOutputSize(G.renderer, &G.winW, &G.winH))
        SDL_GetWindowSizeInPixels(G.window, &G.winW, &G.winH);
    if (G.winW != G.lastW || G.winH != G.lastH) {
        MLOG("output size now %dx%d: layout cache invalidated (%zu entries)", G.winW, G.winH, G.layoutCache.size());
        G.layoutCache.clear();
        G.lastW = G.winW; G.lastH = G.winH;
    }

    G.rows.clear();
    G.arena.clear();
    G.curY = 0;
    G.headerText = headerText ? headerText : "";
    G.footerText.clear();
    #if ENABLE_DEBUGGING
    Stats& s = G.stats;
    s.rowsSubmitted = s.rowsDrawn = 0;
    s.layoutHit = s.layoutMiss = 0;
    s.texHit = s.texMiss = 0;
    s.rasterCount = 0; s.rasterNs = 0; s.barRaster = 0;
    #endif
}

void Menui_Row(const MenuiRow* in) {
    if (!in || !G.initialized) return;

    MenuiAlign align = in->label_align;
    if (align != MENUI_ALIGN_LEFT && align != MENUI_ALIGN_CENTER && align != MENUI_ALIGN_RIGHT) align = MENUI_ALIGN_LEFT;

    // ---- decide what's actually shown ----
    const char* label = (in->label && *in->label) ? in->label : nullptr;

    ItemKind kind = ITEM_NONE;
    const char* itemSrc = nullptr;
    size_t itemLen = 0;
    if (in->item_outline) {
        if (align == MENUI_ALIGN_CENTER) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                MLOG("WARN a value box (item_outline=true) can't be used with MENUI_ALIGN_CENTER; item dropped");
            }
        } else {
            kind = ITEM_BOX;
            if (in->item && *in->item) { itemSrc = in->item; itemLen = std::strlen(in->item); }
        }
    } else if (in->item && *in->item) {
        kind = ITEM_ICON;
        itemSrc = in->item;
        itemLen = first_codepoint_len(in->item);   // icons: first codepoint only
    }

    // ---- hashes + arena copies ----
    RowRec rec;
    rec.align = align;
    rec.selected = in->selected;
    rec.interactive = in->interactive;
    if (label) {
        rec.labelHash = fnv1a(label);
        rec.labelOff = arena_add(label, std::strlen(label));
    }
    if (itemSrc) {
        rec.itemHash = fnv1a_n(itemSrc, itemLen);
        rec.itemOff = arena_add(itemSrc, itemLen);
    }

    // ---- layout (cached) ----
    const uint64_t lk = mix(mix(mix(rec.labelHash, rec.itemHash),
                                static_cast<uint64_t>(align) | (static_cast<uint64_t>(kind) << 8)),
                            (label ? 1u : 0u) | (itemSrc ? 2u : 0u));
    auto it = G.layoutCache.find(lk);
    if (it == G.layoutCache.end()) {
        #if ENABLE_DEBUGGING
        G.stats.layoutMiss++; G.stats.layoutMissTotal++;
        #endif
        // Take pointers only now: the arena won't grow again during this call.
        const char* labelS = rec.labelOff != kNoStr ? G.arena.data() + rec.labelOff : nullptr;
        const char* itemS  = rec.itemOff  != kNoStr ? G.arena.data() + rec.itemOff  : nullptr;
        LayoutEntry le;
        le.L = compute_layout(labelS, kind, itemS, align);
        it = G.layoutCache.emplace(lk, le).first;
    } else {
        #if ENABLE_DEBUGGING
        G.stats.layoutHit++; G.stats.layoutHitTotal++;
        #endif
    }
    it->second.lastFrame = G.frame;
    rec.L = it->second.L;
    rec.h = rec.L.rowH;

    update_scroll_target(in->selected, rec.h);
    rec.y = G.curY;
    G.curY += rec.h;
    G.rows.push_back(rec);
    #if ENABLE_DEBUGGING
    G.stats.rowsSubmitted++;
    #endif
}

void Menui_EndFrame() {
    if (!G.initialized) return;
    const Uint64 tBuildEnd = SDL_GetTicksNS();
    #if ENABLE_DEBUGGING
    G.stats.buildMs = static_cast<double>(tBuildEnd - G.frameStartNs) / 1e6;
    #endif

    // ---- scroll: clamp, then animate BEFORE drawing ----
    const int   viewport_h = viewport_height();
    const float max_scroll = std::max(0.0f, static_cast<float>(G.curY - viewport_h));
    // set desireScrollY to max_scroll if we've selected the last row.
    if (G.rows.back().selected) {
        G.desiredScrollY = max_scroll;
    }

    G.targetScrollY = std::clamp(G.desiredScrollY, 0.0f, max_scroll);
    G.scrollY = std::clamp(G.scrollY, 0.0f, max_scroll);

    const float diff = G.targetScrollY - G.scrollY;
    if (std::fabs(diff) <= kScrollSnap) {
        G.scrollY = G.targetScrollY;
    } else {
        G.scrollY += diff * (1.0f - std::exp(-kScrollK * G.dt));
    }
    G.animating = (G.scrollY != G.targetScrollY);
    const int scrollInt = static_cast<int>(std::lround(G.scrollY));

    // ---- draw ----
    SDL_SetRenderDrawColor(G.renderer, T.bg.r, T.bg.g, T.bg.b, T.bg.a);
    SDL_RenderClear(G.renderer);

    {
        const SDL_Rect clip{0, T.headerH, G.winW, viewport_h};
        SDL_SetRenderClipRect(G.renderer, &clip);

        const int viewTop = scrollInt, viewBot = scrollInt + viewport_h;
        auto first = std::partition_point(G.rows.begin(), G.rows.end(),
            [&](const RowRec& r) { return r.y + r.h <= viewTop; });
        for (auto r = first; r != G.rows.end() && r->y < viewBot; ++r) {
            draw_row(*r, T.headerH + r->y - scrollInt);
            #if ENABLE_DEBUGGING
            G.stats.rowsDrawn++;
            #endif
        }
        SDL_SetRenderClipRect(G.renderer, nullptr);
    }

    draw_scrollbar(viewport_h, max_scroll, scrollInt);
    draw_bar(G.headerSlot, G.headerText, /*top=*/true,  T.headerH, T.headerBorderH, T.barBg, T.divider, T.textSecondary, MENUI_ALIGN_LEFT);
    draw_bar(G.footerSlot, G.footerText, /*top=*/false, T.footerH, T.footerBorderH, T.barBg, T.divider, T.textSecondary, MENUI_ALIGN_RIGHT);
    #if ENABLE_DEBUGGING
    G.stats.drawMs = static_cast<double>(SDL_GetTicksNS() - tBuildEnd) / 1e6;
    draw_debug_overlay(scrollInt, max_scroll);   // shows last frame's present time
    #endif
    const Uint64 tPresent = SDL_GetTicksNS();
    SDL_RenderPresent(G.renderer);
    #if ENABLE_DEBUGGING
    G.stats.presentMs = static_cast<double>(SDL_GetTicksNS() - tPresent) / 1e6;
    #endif
    // ---- housekeeping + pacing ----
    evict_text_cache();
    trim_layout_cache();

    G.wasAnimating = G.animating;
    G.needsRedraw = G.animating;
    G.nextFrameNs = G.frameStartNs + kFrameIntervalNs;   // deadline-based, not a fixed sleep
}
