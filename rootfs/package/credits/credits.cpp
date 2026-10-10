#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

const float PI = 3.14159265f;
const double TWO_PI = 6.283185307179586;
const int SCREEN_W = 640, SCREEN_H = 480;
const float TUNNEL_LENGTH = 1800.f;
const int MAX_PARTICLES = 1000;
const float BOOST_MULT = 3.0f;
const float LOOK_X = 60.f, LOOK_Y = 45.f;
const float CREDIT_GAP = 0.6f;
const float SLIDE_MIN = 5.0f, SLIDE_MAX = 20.0f;

const SDL_Color NAME_COLOR = SDL_Color{220, 220, 220};
const SDL_Color TITLE_COLOR = SDL_Color{190, 190, 190};
const float NAME_FADE = 2.0f;
const float TITLE_DELAY = 0.3f;
const float TITLE_FADE = 2.5f;
const float HOLD_TIME = 2.5f;
const float FADE_OUT = 1.0f;
const float CAMERA_MOVE_AMOUNT = 1.2;

// random number generator
struct Rng {
    uint32_t s = 0x9E3779B9u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() >> 8) * (1.0f / 16777216.0f); }
} rng;

// clamp float
inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// hue-saturation-value to SDL_Color
SDL_Color hsv(float h, float s, float v) {
    h = fmodf(h, 360.f);
    if (h < 0) h += 360.f;
    float c = v * s, hp = h / 60.f;
    float x = c * (1.f - fabsf(fmodf(hp, 2.f) - 1.f)), m = v - c;
    float r = 0, g = 0, b = 0;
    switch ((int)hp) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
    }
    return SDL_Color{(Uint8)((r + m) * 255.f + 0.5f), (Uint8)((g + m) * 255.f + 0.5f), (Uint8)((b + m) * 255.f + 0.5f), 255};
}

// The parameters that define what the starfield looks like at any given moment
struct Look {
    float speed;
    float radius;
    float curveFreq;
    float curveAmp;
    float fov;
    float particleCount;
    float pSize;
    float driftSpeed;
    float driftAmp;
    float particleGlow;
    float connDist;
    float maxConn;
    float lineWidth;
    float lineAlpha;
    SDL_Color colPoint;
    SDL_Color colLine;
};

// Min/max range for every numeric parameter.
struct FRange { float Look::* m; float lo, hi; };
const FRange F_RANGES[] = {
    {&Look::speed,          4.0f,  7.5f},       // camera move speed
    {&Look::radius,         85.f, 320.f},       // tunnel radius
    {&Look::curveFreq,      0.4f,  2.2f},       // how tightly the tunnel winds
    {&Look::curveAmp,       60.f,  280.f},      // how far the tunnel swings
    {&Look::fov,            260.f, 420.f},      // camera FoV
    {&Look::particleCount,  500.f, 1000.f},     // particle count
    {&Look::pSize,          0.9f,  2.4f},
    {&Look::driftSpeed,     0.4f,  12.0f},
    {&Look::driftAmp,       4.f,   28.f},
    {&Look::particleGlow,   0.f,   10.f},       // 0 = plain dots, higher = bigger halo
    {&Look::connDist,       50.f,  120.f},      // line connection distance
    {&Look::maxConn,        2.f,   8.f},        // max lines per particle (rounded)
    {&Look::lineWidth,      0.5f,  1.3f},
    {&Look::lineAlpha,      0.20f, 0.65f},
};
const int NUM_F = (int)(sizeof(F_RANGES) / sizeof(F_RANGES[0]));

// Color sliders. Hue changes randomly, saturation/value slide within a range
struct CRange { SDL_Color Look::* m; float sLo, sHi, vLo, vHi; };
const CRange C_RANGES[] = {
    {&Look::colPoint, 0.00f, 0.50f, 0.80f, 1.00f},
    {&Look::colLine,  0.25f, 0.90f, 0.40f, 0.95f},
};
const int NUM_C = (int)(sizeof(C_RANGES) / sizeof(C_RANGES[0]));

// Allows a value to slide from one value to another within a constrained range.
struct Slider {
    float lo = 0;
    float hi = 1;
    float from = 0;
    float to = 0;
    float t = 1;
    float dur = 1;
    bool hue = false;

    void init(float lo_, float hi_, bool hue_ = false) {
        lo = lo_; hi = hi_; hue = hue_;
        from = to = hue ? rng.f() * 360.f : lo + rng.f() * (hi - lo);
        t = dur = 1.f;  // forces a new segment on the first step
    }
    float step(float dt) {
        t += dt;
        if (t >= dur) {
            from = to;
            if (hue) {  // keep numbers small, then wander up to +-150 degrees
                float w = floorf(from / 360.f) * 360.f;
                from -= w;
                to = from + (rng.f() * 2.f - 1.f) * 150.f;
            } else {
                to = lo + rng.f() * (hi - lo);
            }
            t = 0;
            dur = SLIDE_MIN + rng.f() * (SLIDE_MAX - SLIDE_MIN);
        }
        float k = clampf(t / dur, 0.f, 1.f);
        k = k * k * (3.f - 2.f * k);
        return from + (to - from) * k;
    }
};

class LookAnimator {
public:
    LookAnimator() {
        for (int i = 0; i < NUM_F; i++) fs_[i].init(F_RANGES[i].lo, F_RANGES[i].hi);
        for (int i = 0; i < NUM_C; i++) {
            cs_[i].h.init(0.f, 360.f, true);
            cs_[i].s.init(C_RANGES[i].sLo, C_RANGES[i].sHi);
            cs_[i].v.init(C_RANGES[i].vLo, C_RANGES[i].vHi);
        }
    }
    void update(float dt, Look& L) {
        for (int i = 0; i < NUM_F; i++) L.*(F_RANGES[i].m) = fs_[i].step(dt);
        for (int i = 0; i < NUM_C; i++)
            L.*(C_RANGES[i].m) = hsv(cs_[i].h.step(dt), cs_[i].s.step(dt), cs_[i].v.step(dt));
    }
private:
    struct Col { Slider h, s, v; };
    Slider fs_[NUM_F];
    Col cs_[NUM_C];
};

// ini file structure
struct Entry { std::string name, title; };
struct Ini {
    std::vector<Entry> credits;                  // [credits]  Name = Title
    std::map<std::string, std::string> settings; // [settings] key = value

    std::string str(const char* k, const std::string& d = "") const {
        auto it = settings.find(k);
        return it == settings.end() ? d : it->second;
    }
    int num(const char* k, int d) const {
        auto it = settings.find(k);
        return it == settings.end() || it->second.empty() ? d : atoi(it->second.c_str());
    }
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool loadIni(const std::string& path, Ini& ini) {
    std::ifstream f(path.c_str());
    if (!f) return false;
    std::string line, section = "credits";
    bool first = true;
    while (std::getline(f, line)) {
        if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // UTF-8 BOM
        first = false;
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t e = line.find(']');
            section = lower(trim(line.substr(1, e == std::string::npos ? std::string::npos : e - 1)));
            continue;
        }
        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        std::string val = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        if (section == "settings") ini.settings[lower(key)] = val;
        else if (section == "credits" && !key.empty()) ini.credits.push_back({key, val});
    }
    return true;
}

struct Particle {
    double z;
    float cx, cy;
    float cosA, sinA, ringDist;
    float x, y;
    float dax, day, dsx, dsy; // drift phase / speed
    float baseSize, baseAlpha;
    float sx, sy, scale, fade; // projection results
    bool visible;
};

inline Uint8 a8(float a) {
    return (Uint8)(clampf(a, 0.f, 1.f) * 255.f + 0.5f);
}

class Gfx {
public:
    explicit Gfx(SDL_Renderer* r) : r_(r) {
        lv_.reserve(4 * 4096); li_.reserve(6 * 4096);
        sv_.reserve(4 * 1024); si_.reserve(6 * 1024);
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_ADD);
    }

    void line(float x1, float y1, float x2, float y2, float w, SDL_Color c, float a1, float a2) {
        float dx = x2 - x1, dy = y2 - y1;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.5f) return;
        float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
        SDL_Color c1 = c, c2 = c;
        c1.a = a8(a1); c2.a = a8(a2);
        int base = (int)lv_.size();
        lv_.push_back({{x1 + nx, y1 + ny}, c1, {0, 0}});
        lv_.push_back({{x1 - nx, y1 - ny}, c1, {0, 0}});
        lv_.push_back({{x2 - nx, y2 - ny}, c2, {0, 0}});
        lv_.push_back({{x2 + nx, y2 + ny}, c2, {0, 0}});
        quadIdx(li_, base);
    }
    void flushLines() {
        if (!lv_.empty())
            SDL_RenderGeometry(r_, nullptr, lv_.data(), (int)lv_.size(), li_.data(), (int)li_.size());
        lv_.clear(); li_.clear();
    }

    void sprite(float cx, float cy, float h, SDL_Color c, float a) {
        c.a = a8(a);
        int base = (int)sv_.size();
        sv_.push_back({{cx - h, cy - h}, c, {0, 0}});
        sv_.push_back({{cx + h, cy - h}, c, {1, 0}});
        sv_.push_back({{cx + h, cy + h}, c, {1, 1}});
        sv_.push_back({{cx - h, cy + h}, c, {0, 1}});
        quadIdx(si_, base);
    }
    void flushSprites(SDL_Texture* tex) {
        if (!sv_.empty())
            SDL_RenderGeometry(r_, tex, sv_.data(), (int)sv_.size(), si_.data(), (int)si_.size());
        sv_.clear(); si_.clear();
    }

private:
    SDL_Renderer* r_;
    std::vector<SDL_Vertex> lv_, sv_;
    std::vector<int> li_, si_;
    static void quadIdx(std::vector<int>& v, int b) {
        v.push_back(b); v.push_back(b + 1); v.push_back(b + 2);
        v.push_back(b); v.push_back(b + 2); v.push_back(b + 3);
    }
};

SDL_Texture* makeSprite(SDL_Renderer* r, bool glow) {
    const int N = 64;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, N, N, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return nullptr;
    Uint32* px = (Uint32*)s->pixels;
    int pitch = s->pitch / 4;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float dx = (x + 0.5f - N / 2) / (N / 2), dy = (y + 0.5f - N / 2) / (N / 2);
            float d = sqrtf(dx * dx + dy * dy), a;
            if (glow) {
                float core = clampf((0.3f - d) / 0.12f, 0.f, 1.f);
                float halo = 0.5f * powf(std::max(0.f, 1.f - d), 2.5f);
                a = std::min(1.f, core + halo);
            } else {
                a = clampf((1.f - d) / 0.3f, 0.f, 1.f);
            }
            px[y * pitch + x] = ((Uint32)(a * 255.f + 0.5f) << 24) | 0x00FFFFFFu;
        }
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    return t;
}

struct TextTex {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
    void destroy() { if (tex) SDL_DestroyTexture(tex); tex = nullptr; }
};

TextTex makeText(SDL_Renderer* r, TTF_Font* font, const std::string& str, SDL_Color c) {
    TextTex t;
    if (str.empty()) return t;
    SDL_Surface* txt = TTF_RenderUTF8_Blended(font, str.c_str(), c);
    if (!txt) return t;
    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, txt->w + 2, txt->h + 2, 32, SDL_PIXELFORMAT_ARGB8888);
    if (dst) {
        SDL_FillRect(dst, nullptr, 0);
        SDL_SetSurfaceBlendMode(txt, SDL_BLENDMODE_BLEND);
        SDL_Rect d = {0, 0, 0, 0};
        SDL_BlitSurface(txt, nullptr, dst, &d);
        t.tex = SDL_CreateTextureFromSurface(r, dst);
        t.w = dst->w; t.h = dst->h;
        if (t.tex) SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(dst);
    }
    SDL_FreeSurface(txt);
    return t;
}

struct Dirs { bool l = false, r = false, u = false, d = false; };
enum Action { A_PAUSE, A_NEXT, A_PREV, A_QUIT };

const double TUN_K[4] = {0.0012, 0.0005, 0.0009, 0.0004};

struct CreditTiming { float NAME_FADE, TITLE_DELAY, TITLE_FADE, HOLD_TIME, FADE_OUT; };

struct App {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    Gfx* gfx = nullptr;
    SDL_Texture *dotTex = nullptr, *glowTex = nullptr;

    // fonts/text
    TTF_Font *font = nullptr, *osdFont = nullptr;
    std::vector<Entry> entries;
    int creditIdx = 0;
    TextTex nameTex, titleTex;
    float textX = 0, textY = 0, creditT = 0;

    // OSD
    TextTex osdTex;
    float osdTimer = 0;
    bool osdSticky = false;

    // world
    Look L{};
    LookAnimator anim;
    std::vector<Particle> P;
    std::vector<int> order, vis;
    std::vector<uint8_t> conn;
    int maxActive = 0, active = 0;
    double camZ = 0, tunPh[4] = {0, 0, 0, 0};
    float camX = 0, camY = 0, speedMult = 1.f, lookX = 0, lookY = 0;

    // state
    bool running = true, playing = true, adaptive = true;
    int fpsLimit = 60;
    Dirs kb, hat, ax;
    bool boostKey = false, boostJoy = false;
    int btnPause = 0, btnBoost = 1, btnPrev = 4, btnNext = 5, btnQuit = 7;
    std::vector<SDL_Joystick*> joys;
    int particleOverride = 0;

    bool init(const Ini& ini);
    void shutdown();
    void openJoysticks() {
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            SDL_Joystick* j = SDL_JoystickOpen(i);
            if (j) joys.push_back(j);
        }
    }

    void tunnelAt(float rel, float& x, float& y) const {
        const float a = L.curveAmp, f = L.curveFreq;
        x = sinf((float)tunPh[0] + rel * (float)TUN_K[0] * f) * a +
            sinf((float)tunPh[1] + rel * (float)TUN_K[1] * f) * (a * 0.7f);
        y = cosf((float)tunPh[2] + rel * (float)TUN_K[2] * f) * (a * 0.8f) +
            sinf((float)tunPh[3] + rel * (float)TUN_K[3] * f) * (a * 0.5f);
    }

    void resetParticle(Particle& p, double z) {
        p.z = z;
        float ang = rng.f() * 2 * PI;
        p.cosA = cosf(ang); p.sinA = sinf(ang);
        p.ringDist = L.radius * (0.65f + rng.f() * 0.5f);
        tunnelAt((float)(z - camZ), p.cx, p.cy);
        p.x = p.cx + p.cosA * p.ringDist;
        p.y = p.cy + p.sinA * p.ringDist;
        p.dax = rng.f() * 2 * PI; p.day = rng.f() * 2 * PI;
        p.dsx = 0.005f + rng.f() * 0.01f; p.dsy = 0.005f + rng.f() * 0.01f;
        p.baseSize = 0.8f + rng.f() * 1.2f;
        p.baseAlpha = 0.5f + rng.f() * 0.45f;
        p.visible = false; p.sx = p.sy = p.scale = p.fade = 0;
    }
    void rebuildOrder() {
        order.resize(active);
        for (int i = 0; i < active; i++) order[i] = i;
        sortOrder();
    }
    void sortOrder() {
        for (int i = 1; i < (int)order.size(); i++) {
            int v = order[i];
            double zv = P[v].z;
            int j = i - 1;
            while (j >= 0 && P[order[j]].z > zv) { order[j + 1] = order[j]; j--; }
            order[j + 1] = v;
        }
    }
    void setActive(int n) {
        n = std::max(std::min(n, maxActive), std::max(30, maxActive * 2 / 5));
        if (n == active) return;
        active = n;
        rebuildOrder();
    }

    void syncParticleCount() {
        int m = particleOverride > 0 ? particleOverride : (int)L.particleCount;
        m = std::max(30, std::min(m, MAX_PARTICLES));
        if (m == maxActive) return;
        bool wasFull = active >= maxActive;
        maxActive = m;
        if (wasFull || active > maxActive) setActive(maxActive);
    }

    void updateWorld(float dt) {
        const float f60 = std::min(dt * 60.f, 4.f);
        const float ease = 1.f - powf(0.95f, f60);

        float target = playing ? ((boostKey || boostJoy) ? BOOST_MULT : 1.f) : 0.f;
        speedMult += (target - speedMult) * (1.f - expf(-4.f * dt));
        double dz = (double)(L.speed * 60.f * speedMult * dt);
        camZ += dz;
        for (int i = 0; i < 4; i++) {
            tunPh[i] = fmod(tunPh[i] + dz * TUN_K[i] * L.curveFreq, TWO_PI);
        }

        Dirs d = {kb.l || hat.l || ax.l, kb.r || hat.r || ax.r, kb.u || hat.u || ax.u, kb.d || hat.d || ax.d};
        float tx = (d.r ? 1.f : 0.f) - (d.l ? 1.f : 0.f);
        float ty = (d.d ? 1.f : 0.f) - (d.u ? 1.f : 0.f);
        float lk = 1.f - powf(0.88f, f60);
        lookX += (tx - lookX) * lk;
        lookY += (ty - lookY) * lk;

        float cx, cy;
        tunnelAt(0.f, cx, cy);
        camX = cx + lookX * LOOK_X * CAMERA_MOVE_AMOUNT;
        camY = cy + lookY * LOOK_Y * CAMERA_MOVE_AMOUNT;

        const float fovPx = L.fov, halfW = SCREEN_W * 0.5f, halfH = SCREEN_H * 0.5f, margin = 120.f;
        const float radius = L.radius, invR = 1.f / std::max(1.f, radius);
        const float invLen = 1.f / (TUNNEL_LENGTH - 100.f);

        for (int n = 0; n < active; n++) {
            Particle& p = P[order[n]];
            if (p.z < camZ + 20.0) resetParticle(p, camZ + TUNNEL_LENGTH + rng.f() * 120.f);

            float relZ = (float)(p.z - camZ);
            tunnelAt(relZ, p.cx, p.cy);  // follow the tunnel as it's shape changes

            p.dax += p.dsx * L.driftSpeed * f60;
            p.day += p.dsy * L.driftSpeed * f60;
            if (p.dax > 2 * PI) p.dax -= 2 * PI;
            if (p.day > 2 * PI) p.day -= 2 * PI;
            float curR = radius * (0.65f + p.ringDist * invR * 0.35f);
            float tgx = p.cx + p.cosA * curR + sinf(p.dax) * L.driftAmp;
            float tgy = p.cy + p.sinA * curR + cosf(p.day) * L.driftAmp;
            p.x += (tgx - p.x) * ease;
            p.y += (tgy - p.y) * ease;

            if (relZ <= 10.f || relZ >= TUNNEL_LENGTH) { p.visible = false; continue; }
            p.scale = fovPx / relZ;
            p.sx = halfW + (p.x - camX) * p.scale;
            p.sy = halfH + (p.y - camY) * p.scale;
            p.visible = p.sx >= -margin && p.sx <= SCREEN_W + margin && p.sy >= -margin && p.sy <= SCREEN_H + margin;
            if (p.visible)
                p.fade = sinf(clampf((relZ - 30.f) * invLen, 0.f, 1.f) * PI);
        }
        sortOrder();
        vis.clear();
        for (int n = 0; n < active; n++)
            if (P[order[n]].visible) vis.push_back(order[n]);
    }

    void drawWorld() {
        const int nv = (int)vis.size();
        const int maxConn = std::max(1, (int)(L.maxConn + 0.5f));

        // draw lines
        if (L.connDist > 0 && L.lineAlpha > 0 && nv > 1) {
            conn.assign(nv, 0);
            const float cd = L.connDist, cd2 = cd * cd, invCd = 1.f / cd;
            for (int a = 0; a < nv; a++) {
                if (conn[a] >= maxConn) continue;
                const Particle& p = P[vis[a]];
                for (int b = a + 1; b < nv; b++) {
                    const Particle& q = P[vis[b]];
                    float dz = (float)(q.z - p.z);
                    if (dz >= cd) break;
                    if (conn[b] >= maxConn) continue;
                    float dx = p.x - q.x, dy = p.y - q.y;
                    float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 >= cd2) continue;
                    conn[a]++; conn[b]++;
                    float prox = 1.f - sqrtf(d2) * invCd;
                    float al = prox * L.lineAlpha;
                    float a1 = al * p.fade, a2 = al * q.fade;
                    if (a1 > 0.01f || a2 > 0.01f) {
                        float w = L.lineWidth * p.scale * prox;
                        if (w < 1.f) { float k = std::max(0.2f, w); a1 *= k; a2 *= k; w = 1.f; }
                        gfx->line(p.sx, p.sy, q.sx, q.sy, w, L.colLine, a1, a2);
                    }
                    if (conn[a] >= maxConn) break;
                }
            }
            gfx->flushLines();
        }

        //draw particles
        const float g = clampf(L.particleGlow / 2.f, 0.f, 1.f);
        const float glowMul = 1.2f + L.particleGlow * 0.4f;
        auto pass = [&](SDL_Texture* tex, float sizeMul, float amul) {
            for (int a = 0; a < nv; a++) {
                const Particle& p = P[vis[a]];
                float alpha = p.baseAlpha * p.fade * amul;
                if (alpha <= 0.02f) continue;
                float half = std::max(0.4f, p.baseSize * L.pSize * p.scale * 0.8f) * sizeMul;
                if (half < 1.0f) { alpha *= half; half = 1.0f; }
                gfx->sprite(p.sx, p.sy, half, L.colPoint, std::min(1.f, alpha));
            }
            gfx->flushSprites(tex);
        };
        if (g < 1.f) pass(dotTex, 1.f, 1.f - g);
        if (g > 0.f) pass(glowTex, glowMul, g);
    }

    void showCredit(int idx) {
        int n = (int)entries.size();
        creditIdx = (idx % n + n) % n;
        creditT = 0;

        nameTex.destroy(); titleTex.destroy();
        const Entry& e = entries[creditIdx];
        const SDL_Color white = {255, 255, 255, 255};
        nameTex = makeText(ren, font, e.name, white);
        titleTex = makeText(ren, font, e.title, white);

        int tw = std::max(nameTex.w - 2, titleTex.w - 2);
        int th = TTF_FontLineSkip(font) * 2 + 2;
        float mx = SCREEN_W * 0.08f, my = SCREEN_H * 0.12f;
        float rangeX = SCREEN_W - 2 * mx - tw, rangeY = SCREEN_H - 2 * my - th;
        textX = rangeX > 0 ? mx + rng.f() * rangeX : (SCREEN_W - tw) * 0.5f;
        textY = rangeY > 0 ? my + rng.f() * rangeY : (SCREEN_H - th) * 0.5f;
    }
    void updateCredits(float dt) {
        if (playing) creditT += dt;
        float tot = NAME_FADE + TITLE_DELAY + TITLE_FADE + HOLD_TIME + FADE_OUT + CREDIT_GAP;
        if (creditT >= tot) showCredit(creditIdx + 1);
    }
    void drawCredits() {
        float t0 = NAME_FADE, t1 = t0 + TITLE_DELAY, t2 = t1 + TITLE_FADE, t3 = t2 + HOLD_TIME, t4 = t3 + FADE_OUT, t = creditT, na, ta;
        if (t < t0)      { na = t / std::max(0.001f, t0); ta = 0; }
        else if (t < t1) { na = 1; ta = 0; }
        else if (t < t2) { na = 1; ta = (t - t1) / std::max(0.001f, TITLE_FADE); }
        else if (t < t3) { na = 1; ta = 1; }
        else if (t < t4) { na = ta = 1.f - (t - t3) / std::max(0.001f, FADE_OUT); }
        else             { na = ta = 0; }
        na = clampf(na, 0, 1) * 0.95f; ta = clampf(ta, 0, 1) * 0.85f;

        if (nameTex.tex && na > 0.f) {
            SDL_SetTextureColorMod(nameTex.tex, NAME_COLOR.r, NAME_COLOR.g, NAME_COLOR.b);
            SDL_SetTextureAlphaMod(nameTex.tex, a8(na));
            SDL_Rect d = {(int)textX, (int)textY, nameTex.w, nameTex.h};
            SDL_RenderCopy(ren, nameTex.tex, nullptr, &d);
        }
        if (titleTex.tex && ta > 0.f) {
            SDL_SetTextureColorMod(titleTex.tex, TITLE_COLOR.r, TITLE_COLOR.g, TITLE_COLOR.b);
            SDL_SetTextureAlphaMod(titleTex.tex, a8(ta));
            SDL_Rect d = {(int)textX, (int)textY + TTF_FontLineSkip(font) + 2, titleTex.w, titleTex.h};
            SDL_RenderCopy(ren, titleTex.tex, nullptr, &d);
        }
    }

    void osd(const std::string& s, bool sticky = false) {
        osdTex.destroy();
        osdTex = makeText(ren, osdFont, s, SDL_Color{255, 255, 255, 255});
        osdTimer = 1.2f;
        osdSticky = sticky;
    }
    void drawOsd(float dt) {
        if (!osdTex.tex) return;
        if (!osdSticky) osdTimer -= dt;
        if (osdTimer <= 0.f) { osdTex.destroy(); return; }
        SDL_SetTextureAlphaMod(osdTex.tex, a8(osdSticky ? 1.f : std::min(1.f, osdTimer / 0.3f)));
        SDL_Rect d = {SCREEN_W - osdTex.w - 12, 10, osdTex.w, osdTex.h};
        SDL_RenderCopy(ren, osdTex.tex, nullptr, &d);
    }

    void doAction(Action a) {
        switch (a) {
            case A_PAUSE:
                playing = !playing;
                osd(playing ? "PLAYING" : "PAUSED", !playing);
                break;
            case A_NEXT: showCredit(creditIdx + 1); osd("NEXT"); break;
            case A_PREV: showCredit(creditIdx - 1); osd("PREV"); break;
            case A_QUIT: running = false; break;
        }
    }
    void onKey(SDL_Keycode k, bool down, bool repeat) {
        switch (k) {
            case SDLK_LEFT: kb.l = down; return;
            case SDLK_RIGHT: kb.r = down; return;
            case SDLK_UP: kb.u = down; return;
            case SDLK_DOWN: kb.d = down; return;
            case SDLK_LALT: case SDLK_b: boostKey = down; return;
            default: break;
        }
        if (!down || repeat) return;
        switch (k) {
            case SDLK_SPACE: case SDLK_RETURN: case SDLK_LCTRL: doAction(A_PAUSE); break;
            case SDLK_TAB: case SDLK_PAGEUP: case SDLK_COMMA: case SDLK_LEFTBRACKET: doAction(A_PREV); break;
            case SDLK_BACKSPACE: case SDLK_PAGEDOWN: case SDLK_PERIOD: case SDLK_RIGHTBRACKET: doAction(A_NEXT); break;
            case SDLK_ESCAPE: case SDLK_q: doAction(A_QUIT); break;
            default: break;
        }
    }
    void onJoyButton(int b, bool down) {
        if (b == btnBoost) { boostJoy = down; return; }
        if (!down) return;
        if (b == btnPause) doAction(A_PAUSE);
        else if (b == btnNext) doAction(A_NEXT);
        else if (b == btnPrev) doAction(A_PREV);
        else if (b == btnQuit) doAction(A_QUIT);
    }
    void pollEvents() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_KEYDOWN: onKey(e.key.keysym.sym, true, e.key.repeat != 0); break;
                case SDL_KEYUP: onKey(e.key.keysym.sym, false, false); break;
                case SDL_JOYBUTTONDOWN: onJoyButton(e.jbutton.button, true); break;
                case SDL_JOYBUTTONUP: onJoyButton(e.jbutton.button, false); break;
                case SDL_JOYHATMOTION:
                    hat.u = e.jhat.value & SDL_HAT_UP;   hat.d = e.jhat.value & SDL_HAT_DOWN;
                    hat.l = e.jhat.value & SDL_HAT_LEFT; hat.r = e.jhat.value & SDL_HAT_RIGHT;
                    break;
                case SDL_JOYAXISMOTION:
                    if (e.jaxis.axis == 0) { ax.l = e.jaxis.value < -16000; ax.r = e.jaxis.value > 16000; }
                    if (e.jaxis.axis == 1) { ax.u = e.jaxis.value < -16000; ax.d = e.jaxis.value > 16000; }
                    break;
                case SDL_JOYDEVICEADDED: {
                    SDL_Joystick* j = SDL_JoystickOpen(e.jdevice.which);
                    if (j) joys.push_back(j);
                    break;
                }
                default: break;
            }
        }
    }

    void run() {
        Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
        float avgDt = 1.f / 60.f, avgWork = 0.005f, adaptTimer = 0.f, syncTimer = 0.f;

        while (running) {
            Uint64 t0 = SDL_GetPerformanceCounter();
            float dt = std::min(0.1f, (float)((t0 - last) / (double)freq));
            last = t0;
            avgDt += (dt - avgDt) * 0.1f;

            pollEvents();
            if (playing) anim.update(dt, L);
            updateWorld(dt);
            updateCredits(dt);

            syncTimer += dt;
            if (syncTimer > 0.5f) {
                syncTimer = 0; syncParticleCount();
            }

            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            drawWorld();
            drawCredits();
            drawOsd(dt);

            float work = (float)((SDL_GetPerformanceCounter() - t0) / (double)freq);
            avgWork += (work - avgWork) * 0.1f;
            SDL_RenderPresent(ren);

            // Remove particles when we miss the frame budget. Slowly bring them back when theres enough headroom.
            adaptTimer += dt;
            if (adaptive && adaptTimer > 1.0f) {
                adaptTimer = 0;
                float budget = 1.f / (fpsLimit > 0 ? fpsLimit : 60);
                if (avgDt > budget * 1.15f) setActive((int)(active * 0.85f));
                else if (avgDt < budget * 1.05f && avgWork < budget * 0.55f && active < maxActive)
                    setActive(active + std::max(4, active / 10));
            }

            if (fpsLimit > 0) {
                double ms = (SDL_GetPerformanceCounter() - t0) * 1000.0 / freq;
                double want = 1000.0 / fpsLimit;
                if (ms < want - 1.0) SDL_Delay((Uint32)(want - ms));
            }
        }
    }
};

bool App::init(const Ini& ini) {
    entries = ini.credits;
    if (entries.empty()) entries.push_back({"NO CREDITS FOUND", "check credits.ini"});

    fpsLimit = ini.num("fps_limit", 60);
    adaptive = ini.num("adaptive", 1) != 0;     // adjusts the particle-count based on framerate
    particleOverride = ini.num("particles", 0); // 0 means particle-count is determined by Look
    btnPause = ini.num("btn_pause", btnPause);
    btnBoost = ini.num("btn_boost", btnBoost);
    btnPrev = ini.num("btn_prev", btnPrev);
    btnNext = ini.num("btn_next", btnNext);
    btnQuit = ini.num("btn_quit", btnQuit);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return false;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_JoystickEventState(SDL_ENABLE);

    win = SDL_CreateWindow("goodluckOS Credits", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return false;
    }

    Uint32 vs = ini.num("vsync", 1) ? SDL_RENDERER_PRESENTVSYNC : 0;
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | vs);

    std::string fontPath = ini.str("font");
    std::vector<std::string> cands;
    if (!fontPath.empty()) cands.push_back(fontPath);
    cands.push_back("font.ttf");
    cands.push_back("/usr/share/fonts/Inter_24pt-Medium.ttf");
    cands.push_back("/usr/share/fonts/work-sans.ttf");
    int fontPx = 24;
    for (auto& c : cands) {
        font = TTF_OpenFont(c.c_str(), fontPx);
        if (font) { osdFont = TTF_OpenFont(c.c_str(), std::max(10, fontPx * 4 / 5)); break; }
    }
    if (!font || !osdFont) {
        fprintf(stderr, "No usable font found. Set 'font=' in [settings] or place font.ttf next to the binary.\n");
        return false;
    }

    gfx = new Gfx(ren);
    dotTex = makeSprite(ren, false);
    glowTex = makeSprite(ren, true);
    if (!dotTex || !glowTex) { fprintf(stderr, "sprite creation failed\n"); return false; }

    anim.update(0.f, L);
    P.resize(MAX_PARTICLES);
    for (auto& p : P) p.z = rng.f() * TUNNEL_LENGTH;
    for (auto& p : P) resetParticle(p, p.z);
    syncParticleCount();

    openJoysticks();
    showCredit(0);
    return true;
}

void App::shutdown() {
    nameTex.destroy(); titleTex.destroy(); osdTex.destroy();
    for (auto j : joys) SDL_JoystickClose(j);
    delete gfx;
    if (dotTex) SDL_DestroyTexture(dotTex);
    if (glowTex) SDL_DestroyTexture(glowTex);
    if (font) TTF_CloseFont(font);
    if (osdFont) TTF_CloseFont(osdFont);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
}

}

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "/usr/share/credits/credits.ini";
    Ini ini;
    if (!loadIni(path, ini)) fprintf(stderr, "Warning: cannot read '%s'\n", path.c_str());

    // seed rng
    rng.s ^= (uint32_t)SDL_GetPerformanceCounter() ^ ((uint32_t)time(nullptr) * 2654435761u);
    if (!rng.s) rng.s = 1;

    App app;
    if (app.init(ini)) app.run();
    app.shutdown();
    return 0;
}
