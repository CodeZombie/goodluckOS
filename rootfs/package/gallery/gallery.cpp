// gallery: browse the screenshots and recordings RetroArch saved.
//
// Two tabs (Screenshots, Recordings) on top, a 2x2 grid of the files below, newest first.
// D-pad / left stick move, A opens full screen (and plays audio recordings), A or B closes it,
// X asks to delete the file, B goes back to the tabs and B on the tabs quits.
//
// Images are loaded only when they come into view and kept in a small texture cache, and a frame
// is drawn only when something changed, so the gallery costs nothing while it sits idle.
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

static const char* kHome          = "/home/player";
static const char* kRetroArchCfg  = "/home/player/.config/retroarch/retroarch.cfg";
static const char* kFont          = "/usr/share/fonts/NotoSans.ttf";
static const char* kAudioIcon     = "/usr/share/icons/gallery-audio.png";
static const char* kVideoIcon     = "/usr/share/icons/gallery-video.png";

constexpr float kW = 640, kH = 480;
constexpr float kTabH = 48;                  // tab buttons, like the other apps' 48 px buttons
constexpr float kPad = 12;                   // around and between the grid cells
constexpr float kGridTop = kTabH + kPad;
constexpr float kFooterH = 36;
constexpr float kCellW = (kW - 3 * kPad) / 2;
constexpr float kCellH = (kH - kGridTop - kFooterH - kPad) / 2;
constexpr float kBorder = 3;
constexpr float kTextSize = 22;
constexpr Uint32 kInputPollMs = 10;          // see waitEvent()
constexpr Uint32 kRepeatDelayMs = 350;       // holding a direction repeats it after this...
constexpr Uint32 kRepeatRateMs = 60;         // ...and then this often (like the launcher)
constexpr int kStickPress = 16000, kStickRelease = 8000;
constexpr size_t kMaxTextures = 12;          // 3 pages of the grid

static const ImU32 kYellow   = IM_COL32(255, 205, 60, 255);   // the launcher's selection colour
static const ImU32 kTile     = IM_COL32(36, 36, 40, 255);     // the launcher's tile colour
static const ImU32 kTabOff   = IM_COL32(42, 70, 108, 255);    // ImGui's button colour on the dark background
static const ImU32 kTabOn    = IM_COL32(66, 150, 250, 255);   // ImGui's hovered button colour
static const ImU32 kText     = IM_COL32(255, 255, 255, 255);
static const ImU32 kTextDim  = IM_COL32(150, 150, 150, 255);
static const SDL_Color kBackground = {17, 17, 17, 255};       // ImGui's window background over the clear colour

enum class Kind { Image, Audio, Video };

struct Item {
    fs::path path;
    time_t mtime = 0;
    Kind kind = Kind::Image;
    int seconds = -1;          // audio length, when known
    std::string title, when;   // "Super Mario World", "2026-10-16 12:34"
};

// ---------------------------------------------------------------------------------------------
// Files

// Value of "key = "value"" in retroarch.cfg, "~" expanded; empty when unset or "default".
static std::string retroArchDir(const char* key) {
    std::ifstream in(kRetroArchCfg);
    const std::string prefix = std::string(key) + " = \"";
    for (std::string line; std::getline(in, line);) {
        if (line.compare(0, prefix.size(), prefix) != 0) continue;
        std::string v = line.substr(prefix.size());
        if (!v.empty() && v.back() == '"') v.pop_back();
        if (v.empty() || v == "default") return "";
        if (v[0] == '~') v = kHome + v.substr(1);
        return v;
    }
    return "";
}

static bool hasExtension(const fs::path& p, std::initializer_list<const char*> exts) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
    for (const char* x : exts) if (e == x) return true;
    return false;
}

static bool isImageFile(const fs::path& p) { return hasExtension(p, {".png", ".jpg", ".jpeg", ".bmp", ".gif"}); }

// Length of a WAV file (RetroArch's "wav" recording driver writes them, even into .mkv names).
static bool wavSeconds(const fs::path& p, int& seconds) {
    std::ifstream f(p, std::ios::binary);
    unsigned char h[12];
    if (!f.read((char*)h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
    uint32_t byteRate = 0, dataSize = 0;
    std::streamoff dataStart = 0;
    unsigned char c[8];
    while (f.read((char*)c, 8)) {
        const uint32_t size = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char fmt[16];
            if (size < 16 || !f.read((char*)fmt, 16)) return false;
            byteRate = fmt[8] | fmt[9] << 8 | fmt[10] << 16 | (uint32_t)fmt[11] << 24;
            f.seekg(size - 16 + (size & 1), std::ios::cur);
        } else if (!memcmp(c, "data", 4)) {
            dataSize = size;
            dataStart = f.tellg();
            break;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);
        }
    }
    if (!byteRate || !dataStart) return false;
    std::error_code ec;
    const auto fileSize = fs::file_size(p, ec);
    // A recording that was cut short keeps the size it was created with (often 0)
    if (!ec && (dataSize == 0 || dataStart + (std::streamoff)dataSize > (std::streamoff)fileSize))
        dataSize = (uint32_t)(fileSize - dataStart);
    seconds = (int)(dataSize / byteRate);
    return true;
}

// RetroArch names its files "<content>-YYMMDD-HHMMSS.<ext>"
static void describe(Item& it) {
    const std::string stem = it.path.stem().string();
    const size_t n = stem.size();
    auto digits = [&](size_t from, size_t len) {
        for (size_t i = from; i < from + len; ++i) if (!isdigit((unsigned char)stem[i])) return false;
        return true;
    };
    if (n > 14 && stem[n - 14] == '-' && stem[n - 7] == '-' && digits(n - 13, 6) && digits(n - 6, 6)) {
        it.title = stem.substr(0, n - 14);
        it.when = "20" + stem.substr(n - 13, 2) + "-" + stem.substr(n - 11, 2) + "-" + stem.substr(n - 9, 2) +
                  " " + stem.substr(n - 6, 2) + ":" + stem.substr(n - 4, 2);
    } else {
        it.title = stem;
        char buf[32];
        strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", localtime(&it.mtime));
        it.when = buf;
    }
}

static std::vector<Item> scan(const std::string& dir, bool recordings) {
    std::vector<Item> items;
    std::error_code ec;
    if (dir.empty()) return items;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().filename().string()[0] == '.') continue;
        Item item;
        item.path = it->path();
        if (isImageFile(item.path)) item.kind = Kind::Image;
        else if (!recordings) continue;
        else item.kind = wavSeconds(item.path, item.seconds) ? Kind::Audio : Kind::Video;
        struct stat st;
        if (stat(item.path.c_str(), &st) == 0) item.mtime = st.st_mtime;
        describe(item);
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.mtime != b.mtime ? a.mtime > b.mtime : a.path > b.path;   // newest first
    });
    return items;
}

// ---------------------------------------------------------------------------------------------
// Textures: loaded when they come into view, least recently used dropped first.

class TextureCache {
public:
    explicit TextureCache(SDL_Renderer* r) : renderer(r) {}
    ~TextureCache() { clear(); }

    bool has(const fs::path& p) const { return map.count(p.string()) != 0; }

    // nullptr when the file can't be loaded (remembered, so it isn't retried every frame)
    SDL_Texture* get(const fs::path& p) {
        const std::string key = p.string();
        auto it = map.find(key);
        if (it != map.end()) {
            order.splice(order.begin(), order, it->second.second);
            return it->second.first;
        }
        SDL_Texture* t = IMG_LoadTexture(renderer, key.c_str());
        if (t) {
            // Set once, here: SDL's GL/GLES2 renderers bind the texture to change it without telling
            // their own state cache, so changing it between draws makes later draws use the wrong
            // texture. Screen-sized images stay smooth; smaller pixel art stays sharp when blown up.
            int w = 0, h = 0;
            SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
            SDL_SetTextureScaleMode(t, w >= kW || h >= kH ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        }
        order.push_front(key);
        map[key] = {t, order.begin()};
        while (map.size() > kMaxTextures) {
            auto last = map.find(order.back());
            if (last->second.first) SDL_DestroyTexture(last->second.first);
            map.erase(last);
            order.pop_back();
        }
        return t;
    }

    void drop(const fs::path& p) {
        auto it = map.find(p.string());
        if (it == map.end()) return;
        if (it->second.first) SDL_DestroyTexture(it->second.first);
        order.erase(it->second.second);
        map.erase(it);
    }

    void clear() {
        for (auto& kv : map) if (kv.second.first) SDL_DestroyTexture(kv.second.first);
        map.clear();
        order.clear();
    }

private:
    SDL_Renderer* renderer;
    std::list<std::string> order;
    std::unordered_map<std::string, std::pair<SDL_Texture*, std::list<std::string>::iterator>> map;
};

// ---------------------------------------------------------------------------------------------
// Audio recordings play through aplay

class Player {
public:
    ~Player() { stop(); }
    bool start(const fs::path& p) {
        stop();
        pid = fork();
        if (pid == 0) {
            execlp("aplay", "aplay", "-q", p.c_str(), (char*)nullptr);
            _exit(127);
        }
        return pid > 0;
    }
    void stop() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
        pid = -1;
    }
    // true while it plays; reaps aplay when it is done
    bool playing() {
        if (pid > 0 && waitpid(pid, nullptr, WNOHANG) == pid) pid = -1;
        return pid > 0;
    }
private:
    pid_t pid = -1;
};

// ---------------------------------------------------------------------------------------------
// Input

enum class Action { None, Up, Down, Left, Right, A, B, X };

static Action fromButton(Uint8 b) {
    switch (b) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return Action::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return Action::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return Action::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Action::Right;
        case SDL_CONTROLLER_BUTTON_A:          return Action::A;
        case SDL_CONTROLLER_BUTTON_B:          return Action::B;
        case SDL_CONTROLLER_BUTTON_X:          return Action::X;
        default:                               return Action::None;
    }
}

static Action fromKey(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP:        return Action::Up;
        case SDLK_DOWN:      return Action::Down;
        case SDLK_LEFT:      return Action::Left;
        case SDLK_RIGHT:     return Action::Right;
        case SDLK_RETURN:    return Action::A;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE: return Action::B;
        case SDLK_DELETE:
        case SDLK_x:         return Action::X;
        default:             return Action::None;
    }
}

static bool isDirection(Action a) { return a == Action::Up || a == Action::Down || a == Action::Left || a == Action::Right; }

// SDL2 can't sleep until input arrives on KMSDRM: SDL_WaitEventTimeout() polls every 1 ms there.
// Polling every kInputPollMs keeps it instant and wakes the CPU 10x less.
static bool waitEvent(SDL_Event* ev, int timeoutMs) {
    const Uint32 end = SDL_GetTicks() + (Uint32)timeoutMs;
    for (;;) {
        if (SDL_PollEvent(ev)) return true;
        const Sint32 left = (Sint32)(end - SDL_GetTicks());
        if (left <= 0) return false;
        SDL_Delay(std::min<Uint32>((Uint32)left, kInputPollMs));
    }
}

// ---------------------------------------------------------------------------------------------
// Drawing helpers

static ImFont* uiFont = nullptr;
static ImFont* monoFont = nullptr;

static ImVec2 textSize(ImFont* f, float size, const std::string& s) {
    return f->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str());
}

// Cuts s to fit in width, ending in "..."
static std::string fitText(const std::string& s, float width) {
    if (textSize(uiFont, kTextSize, s).x <= width) return s;
    std::string t = s;
    while (!t.empty() && textSize(uiFont, kTextSize, t + "...").x > width) {
        t.pop_back();
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();   // whole UTF-8 characters
    }
    return t + "...";
}

static void centeredText(ImDrawList* d, ImFont* f, float size, float cx, float y, const std::string& s, ImU32 col) {
    d->AddText(f, size, ImVec2(cx - textSize(f, size, s).x / 2, y), col, s.c_str());
}

// Largest rectangle with the texture's aspect ratio inside the box, centred
static void fitRect(SDL_Texture* t, ImVec2 p0, ImVec2 p1, ImVec2& a, ImVec2& b, float& scale) {
    int w = 1, h = 1;
    SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
    scale = std::min((p1.x - p0.x) / w, (p1.y - p0.y) / h);
    const float dw = w * scale, dh = h * scale;
    a = ImVec2(std::floor((p0.x + p1.x - dw) / 2), std::floor((p0.y + p1.y - dh) / 2));
    b = ImVec2(a.x + std::round(dw), a.y + std::round(dh));
}

static std::string duration(int seconds) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d:%02d", seconds / 60, seconds % 60);
    return buf;
}

static const char* kCat[] = {   // a cat batting a ball of yarn
    "    /\\_/\\",
    "   ( o.o )              .-~~-.",
    "    > ^ <             .' @@@@ '.",
    "   /     \\~~~~~~~~~~~( @@@@@@@@ )",
    "  (_|   |_)           '. @@@@ .'",
    "                        '-..-'",
};

// ---------------------------------------------------------------------------------------------

enum class Tab { Screenshots = 0, Recordings = 1 };
enum class View { Tabs, Grid, Full, Confirm };

struct Gallery {
    std::string dirs[2];
    std::vector<Item> items;
    Tab tab = Tab::Screenshots;
    View view = View::Tabs;
    int sel = 0, firstRow = 0;
    bool deleteChosen = false;   // in the dialog: Cancel (false) or Delete (true)

    void load() {
        items = scan(dirs[(int)tab], tab == Tab::Recordings);
        sel = firstRow = 0;
    }
    int rows() const { return ((int)items.size() + 1) / 2; }
    void keepVisible() {
        const int r = sel / 2;
        if (r < firstRow) firstRow = r;
        if (r > firstRow + 1) firstRow = r - 1;
        firstRow = std::max(0, std::min(firstRow, rows() - 2));
    }
};

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return 1;
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    SDL_Window* window = SDL_CreateWindow("Gallery", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          (int)kW, (int)kH, SDL_WINDOW_SHOWN);
    if (!window) return 1;
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);   // whatever the video driver offers
    if (!renderer) return 1;

    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i) && SDL_GameControllerOpen(i)) break;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // nothing to remember, and no imgui.ini in HOME
    // No ImGui navigation: all input goes through act(), so the dialog can't get two answers at once
    uiFont = io.Fonts->AddFontFromFileTTF(kFont, kTextSize);
    monoFont = io.Fonts->AddFontDefault();   // ProggyClean: monospaced, for the cat
    if (!uiFont) uiFont = monoFont;
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    TextureCache textures(renderer);
    SDL_Texture* audioIcon = IMG_LoadTexture(renderer, kAudioIcon);
    SDL_Texture* videoIcon = IMG_LoadTexture(renderer, kVideoIcon);
    Player player;

    Gallery g;
    g.dirs[0] = retroArchDir("screenshot_directory");
    if (g.dirs[0].empty()) g.dirs[0] = std::string(kHome) + "/.config/retroarch/screenshots";
    g.dirs[1] = retroArchDir("recording_output_directory");
    if (g.dirs[1].empty()) g.dirs[1] = std::string(kHome) + "/.config/retroarch/records";
    g.load();

    Action held = Action::None, stickDir = Action::None;
    Uint32 nextRepeat = 0;
    int framesToDraw = 2;        // ImGui settles focus a frame later, so draw changes twice
    bool shownPlaying = false;   // what the full screen view last said about aplay
    bool running = true;

    auto closeFull = [&] { player.stop(); g.view = View::Grid; };
    auto openFull = [&] {
        g.view = View::Full;
        const Item& it = g.items[g.sel];
        shownPlaying = it.kind == Kind::Audio && player.start(it.path);
    };

    auto deleteSelected = [&] {
        const fs::path p = g.items[g.sel].path;
        const fs::path root = g.dirs[(int)g.tab];
        std::error_code ec;
        textures.drop(p);
        fs::remove(p, ec);
        // RetroArch keeps screenshots in a folder per game: drop the folder once it is empty
        if (p.parent_path() != fs::path(root) && fs::is_empty(p.parent_path(), ec)) fs::remove(p.parent_path(), ec);
        g.items.erase(g.items.begin() + g.sel);
        g.sel = std::min(g.sel, (int)g.items.size() - 1);
        if (g.sel < 0) g.sel = 0;
        g.view = g.items.empty() ? View::Tabs : View::Grid;
        g.keepVisible();
    };

    auto act = [&](Action a) {
        const int n = (int)g.items.size();
        switch (g.view) {
            case View::Tabs:
                if (a == Action::Left || a == Action::Right) {
                    const Tab t = a == Action::Left ? Tab::Screenshots : Tab::Recordings;
                    if (t != g.tab) { g.tab = t; textures.clear(); g.load(); }
                } else if ((a == Action::Down || a == Action::A) && n > 0) {
                    g.view = View::Grid;
                } else if (a == Action::B) {
                    running = false;
                }
                break;
            case View::Grid:
                if (a == Action::Left && g.sel % 2 == 1) --g.sel;
                else if (a == Action::Right && g.sel % 2 == 0 && g.sel + 1 < n) ++g.sel;
                else if (a == Action::Up) { if (g.sel >= 2) g.sel -= 2; else g.view = View::Tabs; }
                else if (a == Action::Down && g.sel / 2 + 1 < g.rows()) g.sel = std::min(g.sel + 2, n - 1);
                else if (a == Action::A) openFull();
                else if (a == Action::X) { g.view = View::Confirm; g.deleteChosen = false; }
                else if (a == Action::B) g.view = View::Tabs;
                g.keepVisible();
                break;
            case View::Full:
                if (a == Action::A || a == Action::B) closeFull();
                else if (a == Action::X) { player.stop(); g.view = View::Confirm; g.deleteChosen = false; }
                else if ((a == Action::Left && g.sel > 0) || (a == Action::Right && g.sel + 1 < n)) {
                    g.sel += a == Action::Left ? -1 : 1;
                    g.keepVisible();
                    openFull();
                }
                break;
            case View::Confirm:
                if (a == Action::Left || a == Action::Right) g.deleteChosen = a == Action::Right;
                else if (a == Action::A && g.deleteChosen) deleteSelected();
                else if (a == Action::A || a == Action::B) g.view = View::Grid;
                break;
        }
    };


    while (running) {
        // Wait for input, a held direction's next repeat, or aplay finishing
        int timeout = framesToDraw > 0 ? 0 : 60000;
        if (held != Action::None) timeout = std::max(0, (int)(nextRepeat - SDL_GetTicks()));
        if (g.view == View::Full && player.playing()) timeout = std::min(timeout, 250);

        SDL_Event ev;
        if (waitEvent(&ev, timeout)) {
            do {
                ImGui_ImplSDL2_ProcessEvent(&ev);
                Action a = Action::None;
                switch (ev.type) {
                    case SDL_QUIT: running = false; break;
                    case SDL_KEYDOWN:
                        if (!ev.key.repeat) a = fromKey(ev.key.keysym.sym);
                        break;
                    case SDL_CONTROLLERBUTTONDOWN:
                        a = fromButton(ev.cbutton.button);
                        break;
                    case SDL_KEYUP:
                        if (fromKey(ev.key.keysym.sym) == held) held = Action::None;
                        break;
                    case SDL_CONTROLLERBUTTONUP:
                        if (fromButton(ev.cbutton.button) == held) held = Action::None;
                        break;
                    case SDL_CONTROLLERAXISMOTION:
                        if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                            SDL_GameController* c = SDL_GameControllerFromInstanceID(ev.caxis.which);
                            const int x = c ? SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) : 0;
                            const int y = c ? SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) : 0;
                            const int limit = stickDir == Action::None ? kStickPress : kStickRelease;
                            Action d = Action::None;
                            if (std::abs(x) >= std::abs(y) && std::abs(x) > limit) d = x < 0 ? Action::Left : Action::Right;
                            else if (std::abs(y) > limit) d = y < 0 ? Action::Up : Action::Down;
                            if (d != stickDir) {
                                if (held == stickDir) held = Action::None;
                                stickDir = d;
                                a = d;
                            }
                        }
                        break;
                    case SDL_CONTROLLERDEVICEADDED:
                        if (SDL_IsGameController(ev.cdevice.which)) SDL_GameControllerOpen(ev.cdevice.which);
                        break;
                }
                if (a != Action::None) {
                    act(a);
                    if (isDirection(a)) { held = a; nextRepeat = SDL_GetTicks() + kRepeatDelayMs; }
                    framesToDraw = 2;
                }
                if (ev.type == SDL_CONTROLLERBUTTONUP || ev.type == SDL_KEYUP) framesToDraw = std::max(framesToDraw, 1);
            } while (SDL_PollEvent(&ev));
        } else if (held != Action::None && SDL_TICKS_PASSED(SDL_GetTicks(), nextRepeat)) {
            act(held);
            nextRepeat = SDL_GetTicks() + kRepeatRateMs;
            framesToDraw = 2;
        }
        if (g.view == View::Full && player.playing() != shownPlaying) {
            shownPlaying = !shownPlaying;                  // aplay finished: say so
            framesToDraw = std::max(framesToDraw, 1);
        }
        if (!running) break;
        if (framesToDraw <= 0) continue;

        // ---- frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImDrawList* d = ImGui::GetBackgroundDrawList();
        const int n = (int)g.items.size();

        if (g.view == View::Confirm) {
            // Same layout as are-you-sure
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Delete", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushFont(monoFont, 13.0f * 1.65f);
            const ImVec2 ws = ImGui::GetWindowSize();
            const char* msg = g.tab == Tab::Screenshots ? "Delete this screenshot?" : "Delete this recording?";
            const ImVec2 ts = ImGui::CalcTextSize(msg);
            ImGui::SetCursorPos(ImVec2((ws.x - ts.x) * 0.5f, ws.y * 0.4f));
            ImGui::Text("%s", msg);
            const float bw = 140, bh = 50, spacing = 40;
            ImGui::SetCursorPos(ImVec2((ws.x - (bw * 2 + spacing)) * 0.5f, ws.y * 0.6f));
            // Drawn like ImGui draws a button that has the gamepad's focus (Cancel first)
            const char* labels[2] = {"Cancel", "Delete"};
            for (int i = 0; i < 2; ++i) {
                if (i) ImGui::SameLine(0, spacing);
                const bool chosen = (i == 1) == g.deleteChosen;
                if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
                ImGui::Button(labels[i], ImVec2(bw, bh));
                if (chosen) {
                    ImGui::PopStyleColor();
                    ImGui::GetWindowDrawList()->AddRect(ImVec2(ImGui::GetItemRectMin().x - 2, ImGui::GetItemRectMin().y - 2),
                                                        ImVec2(ImGui::GetItemRectMax().x + 2, ImGui::GetItemRectMax().y + 2),
                                                        ImGui::GetColorU32(ImGuiCol_NavCursor), 0.0f, 0, 2.0f);
                }
            }
            ImGui::PopFont();
            ImGui::End();
        } else if (g.view == View::Full && n > 0) {
            const Item& it = g.items[g.sel];
            d->AddRectFilled(ImVec2(0, 0), ImVec2(kW, kH), IM_COL32(0, 0, 0, 255));
            if (it.kind == Kind::Image) {
                if (SDL_Texture* t = textures.get(it.path)) {
                    ImVec2 a, b; float scale;
                    fitRect(t, ImVec2(0, 0), ImVec2(kW, kH), a, b, scale);
                    d->AddImage((ImTextureID)(intptr_t)t, a, b);
                } else {
                    centeredText(d, uiFont, kTextSize, kW / 2, kH / 2 - 12, "This image can't be opened", kTextDim);
                }
            } else {
                SDL_Texture* icon = it.kind == Kind::Audio ? audioIcon : videoIcon;
                if (icon) d->AddImage((ImTextureID)(intptr_t)icon, ImVec2(kW / 2 - 48, 120), ImVec2(kW / 2 + 48, 216));
                centeredText(d, uiFont, kTextSize, kW / 2, 240, fitText(it.title, kW - 2 * kPad), kText);
                centeredText(d, uiFont, kTextSize, kW / 2, 272, it.when, kTextDim);
                std::string status;
                if (it.kind == Kind::Video) status = "This recording can't be played here";
                else status = (player.playing() ? "Playing" : "Finished") +
                              (it.seconds >= 0 ? std::string("  ") + duration(it.seconds) : std::string());
                centeredText(d, uiFont, kTextSize, kW / 2, 320, status, kText);
            }
        } else {
            if (g.view == View::Full) g.view = View::Tabs;   // the list emptied under it
            d->AddRectFilled(ImVec2(0, 0), ImVec2(kW, kH), IM_COL32(kBackground.r, kBackground.g, kBackground.b, 255));

            // Tabs: two halves of the top edge
            const char* names[2] = {"Screenshots", "Recordings"};
            for (int i = 0; i < 2; ++i) {
                const ImVec2 a(i * kW / 2 + (i ? 1.0f : 0.0f), 0), b((i + 1) * kW / 2 - (i ? 0.0f : 1.0f), kTabH);
                d->AddRectFilled(a, b, i == (int)g.tab ? kTabOn : kTabOff);
                centeredText(d, uiFont, kTextSize, (a.x + b.x) / 2, (kTabH - kTextSize) / 2 - 1, names[i], kText);
                if (g.view == View::Tabs && i == (int)g.tab)
                    d->AddRect(a, b, kYellow, 0, 0, kBorder * 2);   // half falls off the screen edge
            }

            if (n == 0) {
                const float lineH = 13.0f * 2;   // ProggyClean at twice its size stays crisp
                const size_t lines = sizeof kCat / sizeof kCat[0];
                float artW = 0;
                for (size_t i = 0; i < lines; ++i) artW = std::max(artW, textSize(monoFont, lineH, kCat[i]).x);
                const float top = std::floor(kGridTop + (kH - kGridTop - lines * lineH - 60) / 2);
                for (size_t i = 0; i < lines; ++i)
                    d->AddText(monoFont, lineH, ImVec2(std::floor((kW - artW) / 2), top + i * lineH), kText, kCat[i]);
                centeredText(d, uiFont, kTextSize, kW / 2, top + lines * lineH + 30, "Nothing to see here", kTextDim);
            } else {
                for (int r = g.firstRow; r < g.firstRow + 2 && r < g.rows(); ++r) {
                    for (int c = 0; c < 2; ++c) {
                        const int i = r * 2 + c;
                        if (i >= n) break;
                        const Item& it = g.items[i];
                        const ImVec2 a(kPad + c * (kCellW + kPad), kGridTop + (r - g.firstRow) * (kCellH + kPad));
                        const ImVec2 b(a.x + kCellW, a.y + kCellH);
                        d->AddRectFilled(a, b, kTile);
                        const ImVec2 in0(a.x + 6, a.y + 6), in1(b.x - 6, b.y - 6);
                        if (it.kind == Kind::Image) {
                            if (SDL_Texture* t = textures.get(it.path)) {
                                ImVec2 p, q; float scale;
                                fitRect(t, in0, in1, p, q, scale);
                                d->AddImage((ImTextureID)(intptr_t)t, p, q);
                            } else {
                                centeredText(d, uiFont, kTextSize, (a.x + b.x) / 2, (a.y + b.y) / 2 - 12, "?", kTextDim);
                            }
                        } else {
                            SDL_Texture* icon = it.kind == Kind::Audio ? audioIcon : videoIcon;
                            const float cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2;
                            if (icon) d->AddImage((ImTextureID)(intptr_t)icon, ImVec2(cx - 24, cy - 40), ImVec2(cx + 24, cy + 8));
                            if (it.seconds >= 0) centeredText(d, uiFont, kTextSize, cx, cy + 16, duration(it.seconds), kTextDim);
                        }
                        if (g.view == View::Grid && i == g.sel)
                            d->AddRect(ImVec2(a.x - kBorder / 2, a.y - kBorder / 2), ImVec2(b.x + kBorder / 2, b.y + kBorder / 2),
                                       kYellow, 0, 0, kBorder);
                    }
                }
                // Footer: what is selected, and where in the list
                const float fy = kH - kFooterH + (kFooterH - kTextSize) / 2 - 4;
                const std::string count = std::to_string(g.sel + 1) + " / " + std::to_string(n);
                const float countW = textSize(uiFont, kTextSize, count).x;
                const Item& it = g.items[g.sel];
                const std::string label = g.view == View::Grid ? it.title + "   " + it.when
                                        : std::to_string(n) + (g.tab == Tab::Screenshots ? " screenshots" : " recordings");
                d->AddText(uiFont, kTextSize, ImVec2(kPad, fy), kText, fitText(label, kW - 3 * kPad - countW).c_str());
                d->AddText(uiFont, kTextSize, ImVec2(kW - kPad - countW, fy), kTextDim, count.c_str());
            }
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        if (framesToDraw > 0) --framesToDraw;

        // Idle: load the row after the visible ones, so scrolling down doesn't wait for the disk
        if (framesToDraw == 0 && g.view == View::Grid && !SDL_HasEvents(SDL_FIRSTEVENT, SDL_LASTEVENT)) {
            for (int i = (g.firstRow + 2) * 2; i < std::min(n, (g.firstRow + 3) * 2); ++i)
                if (g.items[i].kind == Kind::Image && !textures.has(g.items[i].path)) textures.get(g.items[i].path);
        }
    }

    player.stop();
    textures.clear();
    if (audioIcon) SDL_DestroyTexture(audioIcon);
    if (videoIcon) SDL_DestroyTexture(videoIcon);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
