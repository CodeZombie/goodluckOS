// Vitrine: a streaming-style game launcher for goodluckOS (GA36-MB / R36S clones).
//
// It reads the same apps.puppy files as Puppy, shows one screen per system with the
// selected game's picture beside the list, keeps favourites and recently played games,
// and launches games itself so it can come back to the same spot afterwards.
// The interface is in English or Brazilian Portuguese, chosen in Settings.

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <regex>
#include <tuple>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Paths. VITRINE_ROOT prefixes every filesystem access so the launcher can run
// against a copy of the card on a desktop machine. Commands keep device paths.
// ---------------------------------------------------------------------------

static std::string gRoot;
static bool gPreview = false;
static bool gScreenshot = false;  // draws pictures at once instead of waiting for scrolling to stop

static std::string hostPath(const std::string& devicePath) { return gRoot + devicePath; }

static const char* kSystemAppsFile = "/usr/share/puppy/apps.puppy";
static const char* kHomeAppsFile   = "/home/player/apps.puppy";
static const char* kFontFile       = "/usr/share/fonts/work-sans.ttf";
static const char* kDataDir        = "/home/player/vitrine";
static const char* kPidFile        = "/dev/shm/puppy-active-process-id";
static const char* kPowerFifo      = "/run/power-request";
// Where RetroArch looks for a game's cheats when it loads it (cheat_database_path, set by the
// installer together with apply_cheats_after_load = true): <dir>/<core library name>/<rom stem>.cht
static const char* kCheatDir       = "/home/player/.config/retroarch/cheats";
// One ready infinite-lives .cht per game, made on the desktop: <dir>/<games folder>/<rom stem>.cht
// Cheats each game's core applies, made on the desktop from the libretro database:
// <dir>/<games folder name>/<rom stem>.txt, one "kind<TAB>description<TAB>code|code" per line,
// kind M (master code), L (infinite lives) or C (any other cheat).
static const char* kCheatsDir      = "/home/player/vitrine/trapacas";
static const char* kRetroArchCfg   = "/home/player/.config/retroarch/retroarch.cfg";

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;
constexpr int kMaxRecents = 30;
constexpr Uint32 kBatteryCheckMs = 60000;
constexpr int kBatteryLow = 15;        // warning on the home screen
constexpr int kBatteryCritical = 4;    // during a game: save and close it
constexpr int kRetroArchCmdPort = 55355;
constexpr Uint32 kRepeatDelayMs = 300;
constexpr Uint32 kRepeatRateMs = 70;
constexpr Uint32 kRepeatFastMs = 30;
constexpr Uint32 kRepeatFastAfterMs = 1500;
constexpr Uint32 kHeroSettleMs = 140;

// ---------------------------------------------------------------------------
// Palette (contrast checked against kBg: text 19.4:1, muted 10.2:1, teal 10.4:1).
// ---------------------------------------------------------------------------

constexpr SDL_Color kBg      {11, 13, 18, 255};
constexpr SDL_Color kBar     {7, 8, 12, 255};
constexpr SDL_Color kCard    {30, 34, 43, 255};
constexpr SDL_Color kText    {255, 255, 255, 255};
constexpr SDL_Color kMuted   {184, 188, 198, 255};
constexpr SDL_Color kTeal    {79, 209, 197, 255};
constexpr SDL_Color kStar    {245, 197, 66, 255};
constexpr SDL_Color kStarInk {201, 154, 14, 255};
constexpr SDL_Color kBtnA    {229, 72, 77, 255};
constexpr SDL_Color kBtnB    {245, 197, 66, 255};
constexpr SDL_Color kBtnX    {62, 142, 247, 255};
constexpr SDL_Color kBtnY    {70, 167, 88, 255};

static SDL_Color hexColor(unsigned v) {
    return SDL_Color{(Uint8)((v >> 16) & 0xff), (Uint8)((v >> 8) & 0xff), (Uint8)(v & 0xff), 255};
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Interface language: Portuguese or English. Chosen in Settings (idioma= in ajustes.txt); when
// unset, it follows RetroArch's user_language (7 = Brazilian Portuguese). Set before the catalog
// loads, because region names and app names are resolved while scanning.
static bool gEnglish = false;
static char** gArgv = nullptr;  // to start again after a language change

// The text in the current language. Call sites keep both versions side by side.
static const char* L(const char* pt, const char* en) { return gEnglish ? en : pt; }

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Lowercase without accents, for searching: "Pokémon Ação" -> "pokemon acao".
static std::string fold(const std::string& s) {
    static const std::map<std::string, char> kAccents = {
        {"á", 'a'}, {"à", 'a'}, {"â", 'a'}, {"ã", 'a'}, {"ä", 'a'}, {"é", 'e'}, {"ê", 'e'}, {"è", 'e'},
        {"ë", 'e'}, {"í", 'i'}, {"î", 'i'}, {"ï", 'i'}, {"ó", 'o'}, {"ô", 'o'}, {"õ", 'o'}, {"ö", 'o'},
        {"ú", 'u'}, {"ü", 'u'}, {"û", 'u'}, {"ç", 'c'}, {"ñ", 'n'}, {"Á", 'a'}, {"À", 'a'}, {"Â", 'a'},
        {"Ã", 'a'}, {"É", 'e'}, {"Ê", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ô", 'o'}, {"Õ", 'o'}, {"Ú", 'u'},
        {"Ü", 'u'}, {"Ç", 'c'}, {"Ñ", 'n'}};
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0xC0 && i + 1 < s.size()) {
            auto it = kAccents.find(s.substr(i, 2));
            if (it != kAccents.end()) {
                out += it->second;
                ++i;
                continue;
            }
        }
        out += (char)std::tolower(c);
    }
    return out;
}

static std::string upper(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xC3 && i + 1 < s.size()) {  // á é ç ã... -> Á É Ç Ã (Latin-1 range of UTF-8)
            unsigned char n = (unsigned char)s[i + 1];
            if (n >= 0xA0 && n <= 0xBE && n != 0xB7) s[i + 1] = (char)(n - 0x20);
            ++i;
            continue;
        }
        s[i] = (char)std::toupper(c);
    }
    return s;
}

static std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ';')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

static long monotonicSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec;
}

// "3h 20min", "45 min", "menos de 1 min" / "under 1 min".
static std::string formatDuration(long s) {
    long m = s / 60;
    if (m < 1) return L("menos de 1 min", "under 1 min");
    if (m < 60) return std::to_string(m) + " min";
    long h = m / 60, rest = m % 60;
    return std::to_string(h) + "h" + (rest ? " " + std::to_string(rest) + "min" : "");
}

// "há 2 dias" / "2 days ago". Relative, because the console has no network to set the date: the
// clock keeps running but its calendar date may be wrong. Empty when the clock looks inconsistent.
static std::string formatAgo(long last, long now) {
    long d = now - last;
    if (last <= 0 || d < 0 || d > 3L * 365 * 86400) return "";
    if (d < 3600) return L("agora há pouco", "just now");
    if (d < 86400) {
        if (d < 7200) return L("há 1 hora", "1 hour ago");
        std::string n = std::to_string(d / 3600);
        return gEnglish ? n + " hours ago" : "há " + n + " horas";
    }
    if (d < 2 * 86400) return L("há 1 dia", "1 day ago");
    std::string n = std::to_string(d / 86400);
    return gEnglish ? n + " days ago" : "há " + n + " dias";
}

static std::string upperFirst(std::string s) {
    if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}

// Groups thousands: "13.995" in Portuguese, "13,995" in English.
static std::string formatCount(size_t n) {
    std::string digits = std::to_string(n), out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count && count % 3 == 0) out.insert(out.begin(), gEnglish ? ',' : '.');
        out.insert(out.begin(), *it);
        ++count;
    }
    return out;
}

static std::string plural(size_t n, const char* one, const char* many) {
    return formatCount(n) + " " + (n == 1 ? one : many);
}

// Drops the last UTF-8 code point.
static void popCodePoint(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
    s.erase(i);
}

// ---------------------------------------------------------------------------
// Catalog: systems and games from apps.puppy
// ---------------------------------------------------------------------------

struct Game {
    std::string path;    // device path, used in the launch command
    std::string stem;    // file name without extension
    std::string title;   // cleaned title shown on screen
    std::string region;  // "EUA", "Europa", ... (Portuguese)
    std::string details; // region plus the other tags, told apart when titles repeat
    std::string sortKey;
    bool ptbr = false;
    bool duplicateTitle = false;
    int systemIndex = -1;
    int iconState = 0;   // 0 unknown, 1 found, 2 none
    std::string iconPath;
    int coverState = 0;  // same, for the box art in the "capas" folder next to the games
    std::string coverPath;
    int cheatsState = 0; // same, for the cheat catalog
    std::string cheatsPath;
    std::string year, genre, developer;  // game sheet from the libretro database, when known
    int players = 0;
};

struct System {
    std::string archiveName;
    std::string display;
    SDL_Color color{};
    std::string command;
    std::vector<std::string> dirs, exts, iconDirs;
    std::vector<Game> games;
    size_t ptbrCount = 0;
    int order = 1000;
};

struct App {
    std::string name, command;
    std::string key;  // CATEGORY and NAME from apps.puppy, as Puppy tells two entries apart
};

struct SystemStyle {
    const char* archiveName;
    const char* display;
    unsigned color;
};

// Display order and colours. Archives not listed here are shown after these.
static const SystemStyle kStyles[] = {
    {"Nintendo Entertainment System", "NES", 0xFF6B6B},
    {"Super Nintendo", "Super Nintendo", 0xA99BFF},
    {"Sega Genesis", "Mega Drive", 0x5AA2FF},
    {"Sega Master System", "Master System", 0x4FD1C5},
    {"Sega Game Gear", "Game Gear", 0xF59E0B},
    {"Game Boy", "Game Boy", 0xB4D23C},
    {"Game Boy (Color)", "Game Boy Color", 0xF472B6},
    {"Game Boy Advance", "Game Boy Advance", 0x818CF8},
    {"PC Engine", "PC Engine", 0xFB923C},
    {"Atari 2600", "Atari 2600", 0xF87171},
    {"Arcade (MAME 2003 Plus)", "Arcade", 0xFACC15},
    {"Arcade CPS (FBNeo)", "Arcade CPS", 0x38BDF8},
    {"Neo Geo", "Neo Geo", 0xE11D48},
    {"Sega 32X", "32X", 0xF43F5E},
    {"Sega CD", "Sega CD", 0x60A5FA},
    {"Playstation", "PlayStation", 0x94A3B8},
    {"PSP", "PSP", 0xCBD5E1},
    {"WonderSwan", "WonderSwan", 0xA3E635},
    {"Neo Geo Pocket", "Neo Geo Pocket", 0xFDBA74},
    {"Atari Lynx", "Atari Lynx", 0xFDE047},
    {"MSX", "MSX", 0x93C5FD},
    {"ColecoVision", "ColecoVision", 0xFCA5A5},
    {"Game & Watch", "Game & Watch", 0xD4D4D8},
    {"Pokemon Mini", "Pokémon Mini", 0xFBBF24},
    {"Ports", "Ports", 0x34D399},
    {"DOSBox", "DOS", 0x9CA3AF},
    {"Doom", "Doom", 0xEF4444},
};

// Files a games folder needs before its games can open. Any one of the paths is enough.
struct Requirement {
    const char* folder;
    std::vector<const char*> anyOf;
    const char* title;
    const char* body;
    const char* titleEn;
    const char* bodyEn;
};
static const std::vector<Requirement> kRequirements = {
    {"neogeo",
     {"/home/player/roms/neogeo/neogeo.zip", "/home/player/.config/retroarch/system/neogeo.zip",
      "/home/player/.config/retroarch/system/fbneo/neogeo.zip"},
     L("Falta o BIOS do Neo Geo", "Neo Geo BIOS missing"),
     "Copie o arquivo neogeo.zip para a pasta roms/neogeo do cartão, na partição HOME. Os jogos já estão lá e "
     "passam a abrir sem mais nada.",
     "Neo Geo BIOS missing",
     "Copy neogeo.zip to the roms/neogeo folder on the card's HOME partition. The games are already there "
     "and will run with no further setup."},
    {"msx", {"/home/player/.config/retroarch/system/Machines/MSX2+ - C-BIOS/config.ini"},
     L("Faltam os arquivos do MSX", "MSX files missing"),
     "As pastas Machines e Databases do blueMSX vão em .config/retroarch/system, na partição HOME. "
     "Coloque o cartão no Mac e rode a instalação do Vitrine de novo.",
     "MSX files missing",
     "blueMSX's Machines and Databases folders go in .config/retroarch/system on the HOME partition. "
     "Put the card in a computer and run the Vitrine installer again."},
    {"coleco", {"/home/player/.config/retroarch/system/Machines/COL - ColecoVision/config.ini"},
     L("Faltam os arquivos do ColecoVision", "ColecoVision files missing"),
     "As pastas Machines e Databases do blueMSX vão em .config/retroarch/system, na partição HOME. "
     "Coloque o cartão no Mac e rode a instalação do Vitrine de novo.",
     "ColecoVision files missing",
     "blueMSX's Machines and Databases folders go in .config/retroarch/system on the HOME partition. "
     "Put the card in a computer and run the Vitrine installer again."},
};

// BIOS files that sit next to the games but are not games.
static const std::set<std::string> kBiosStems = {"neogeo"};

// [ENTRY] apps that Vitrine replaces with its own screens, and its own entry in Puppy.
static const std::set<std::string> kReplacedEntries = {"Reboot", "Power Off", "Vitrine"};

// Friendlier names for apps from apps.puppy: Portuguese (the screens behind them are in English,
// so the label says so) and English.
static const std::map<std::string, std::pair<const char*, const char*>> kAppNames = {
    {"System Settings", {L("Ajustes do sistema (em inglês)", "System settings"), "System settings"}},
    {"Rezize Home", {L("Expandir armazenamento (em inglês)", "Expand storage"), "Expand storage"}},
    {"Resize Home", {L("Expandir armazenamento (em inglês)", "Expand storage"), "Expand storage"}},
    {"RetroArch", {L("Abrir o RetroArch", "Open RetroArch"), "Open RetroArch"}},
    {"Chocolate Doom", {"Doom", "Doom"}},
};

struct ParsedArchive {
    std::string name, command;
    std::vector<std::string> dirs, exts, iconDirs;
};

static void parseAppsFile(const std::string& devicePath,
                          std::vector<ParsedArchive>& archives, std::vector<App>& apps) {
    std::ifstream in(hostPath(devicePath));
    if (!in) return;
    std::string section, line;
    std::map<std::string, std::string> kv;
    auto get = [&](const char* key) {
        auto it = kv.find(key);
        return it == kv.end() ? std::string() : it->second;
    };
    auto flush = [&]() {
        if (section == "ARCHIVE" && !get("NAME").empty()) {
            ParsedArchive a{get("NAME"), get("COMMAND"), splitList(get("ENTRY_DIRECTORIES")),
                            splitList(get("ENTRY_EXTENSIONS")), splitList(get("ENTRY_ICONS_DIRECTORIES"))};
            auto same = std::find_if(archives.begin(), archives.end(),
                                     [&](const ParsedArchive& x) { return x.name == a.name; });
            if (same != archives.end()) *same = a;
            else archives.push_back(a);
        } else if (section == "ENTRY" && !get("NAME").empty() && !get("COMMAND").empty()) {
            std::string name = get("NAME");
            if (!kReplacedEntries.count(name)) {
                auto it = kAppNames.find(name);
                std::string category = get("CATEGORY").empty() ? "Applications" : get("CATEGORY");
                App app{it != kAppNames.end() ? L(it->second.first, it->second.second) : name, get("COMMAND"),
                        category + "\x1f" + name};
                auto same = std::find_if(apps.begin(), apps.end(),
                                         [&](const App& x) { return x.key == app.key; });
                if (same != apps.end()) *same = app;
                else apps.push_back(app);
            }
        }
        kv.clear();
    };
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            flush();
            section = upper(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq != std::string::npos) kv[upper(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
    }
    flush();
}

static std::string regionFromTag(const std::string& tag) {
    static const std::vector<std::pair<std::string, std::string>> kRegions = {
        {"USA, Europe", "EUA e Europa"}, {"Japan, USA", "Japão e EUA"}, {"USA", "EUA"},
        {"Europe", "Europa"}, {"Japan", "Japão"}, {"Brazil", "Brasil"}, {"World", "Mundo"},
        {"Korea", "Coreia"}, {"China", "China"}, {"Taiwan", "Taiwan"}, {"Asia", "Ásia"},
        {"France", "França"}, {"Germany", "Alemanha"}, {"Spain", "Espanha"}, {"Italy", "Itália"},
    };
    static const std::map<std::string, std::string> kEnglish = {
        {"USA, Europe", "USA & Europe"}, {"Japan, USA", "Japan & USA"}};
    for (const auto& [en, pt] : kRegions) {
        if (tag.compare(0, en.size(), en) != 0) continue;
        if (!gEnglish) return pt;
        auto it = kEnglish.find(en);
        return it != kEnglish.end() ? it->second : en;
    }
    return "";
}

// "Flashback - The Quest for Identity (USA) (En,Fr) [PT-BR]" ->
// title "Flashback - The Quest for Identity", region "EUA", ptbr = true.
static void describeGame(Game& g) {
    std::string s = g.stem;
    g.ptbr = s.find("[PT-BR]") != std::string::npos;
    std::string title;
    int depth = 0;
    std::string group;
    std::vector<std::string> tags;
    for (char c : s) {
        if (c == '(' || c == '[') {
            if (depth == 0) group.clear();
            ++depth;
            continue;
        }
        if ((c == ')' || c == ']') && depth > 0) {
            if (--depth == 0) {
                std::string region = g.region.empty() ? regionFromTag(group) : "";
                if (!region.empty()) g.region = region;
                else if (group != "PT-BR") tags.push_back(trim(group));
            }
            continue;
        }
        if (depth == 0) title += c;
        else group += c;
    }
    g.details = g.region;
    for (const auto& t : tags) g.details += (g.details.empty() ? "" : " · ") + t;
    title = trim(title);
    g.title = title.empty() ? s : title;
    g.sortKey = lower(g.title);
}

// Length of a pack number such as "001 " at the start of a title, or 0.
static size_t packPrefixLen(const std::string& t) {
    size_t d = 0;
    while (d < t.size() && std::isdigit((unsigned char)t[d])) ++d;
    return d >= 3 && d < t.size() && t[d] == ' ' ? d + 1 : 0;
}

// "fichas/<folder>.txt": file name without extension, year, genre, developer, players, title.
// Arcade zips are short names ("mslug"), so their sheet also brings the real title.
static void loadSheets(System& sys) {
    std::unordered_map<std::string, Game*> byStem;
    for (auto& g : sys.games) byStem[g.stem] = &g;
    for (const auto& dir : sys.dirs) {
        std::string folder = dir.substr(dir.find_last_of('/') + 1);
        std::ifstream in(hostPath(std::string(kDataDir) + "/fichas/" + folder + ".txt"));
        std::string line;
        while (std::getline(in, line)) {
            std::vector<std::string> f;
            std::stringstream ss(line);
            std::string field;
            while (std::getline(ss, field, '\t')) f.push_back(field);
            if (f.size() < 5) continue;
            auto it = byStem.find(f[0]);
            if (it == byStem.end()) continue;
            Game& g = *it->second;
            g.year = f[1];
            g.genre = f[2];
            g.developer = f[3];
            g.players = std::atoi(f[4].c_str());
            if (f.size() > 5 && !f[5].empty()) {
                Game named;
                named.stem = f[5];
                describeGame(named);
                g.title = named.title;
                g.region = named.region;
                g.details = named.details;
                g.sortKey = named.sortKey;
            }
        }
    }
}

static void scanSystem(System& sys) {
    std::vector<std::string> exts;
    for (auto e : sys.exts) {
        if (!e.empty() && e[0] == '.') e.erase(0, 1);
        exts.push_back(lower(e));
    }
    std::set<std::string> seen;
    for (const auto& dir : sys.dirs) {
        std::string hostDir = hostPath(dir);
        DIR* d = opendir(hostDir.c_str());
        if (!d) continue;
        while (dirent* de = readdir(d)) {
            if (de->d_name[0] == '.') continue;
            if (de->d_type != DT_REG && de->d_type != DT_LNK && de->d_type != DT_UNKNOWN) continue;
            std::string name = de->d_name;
            size_t dot = name.rfind('.');
            if (dot == std::string::npos) continue;
            std::string ext = lower(name.substr(dot + 1));
            if (!exts.empty() && std::find(exts.begin(), exts.end(), ext) == exts.end()) continue;
            if (de->d_type != DT_REG) {  // a symlink or an unknown type: follow it, as Puppy does
                struct stat st;
                if (stat((hostDir + "/" + name).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            }
            Game g;
            g.path = dir + "/" + name;
            if (!seen.insert(g.path).second) continue;
            g.stem = name.substr(0, dot);
            if (kBiosStems.count(lower(g.stem))) continue;
            describeGame(g);
            sys.games.push_back(std::move(g));
        }
        closedir(d);
    }
    loadSheets(sys);
    // Factory packs number every game ("001 Super Mario Bros"); strip that only when most of
    // the folder is numbered, so real titles like "1942" or "007 ..." keep their digits.
    size_t numbered = std::count_if(sys.games.begin(), sys.games.end(),
                                    [](const Game& g) { return packPrefixLen(g.title) > 0; });
    if (numbered * 2 > sys.games.size()) {
        for (auto& g : sys.games) {
            size_t n = packPrefixLen(g.title);
            if (n) {
                g.title = trim(g.title.substr(n));
                g.sortKey = lower(g.title);
            }
        }
    }
    std::sort(sys.games.begin(), sys.games.end(), [](const Game& a, const Game& b) {
        return a.sortKey != b.sortKey ? a.sortKey < b.sortKey : a.stem < b.stem;
    });
    for (size_t i = 0; i < sys.games.size(); ++i) {
        bool dupPrev = i > 0 && sys.games[i - 1].sortKey == sys.games[i].sortKey;
        bool dupNext = i + 1 < sys.games.size() && sys.games[i + 1].sortKey == sys.games[i].sortKey;
        sys.games[i].duplicateTitle = dupPrev || dupNext;
        if (sys.games[i].ptbr) ++sys.ptbrCount;
    }
}

// RetroArch keeps a game's cheats under the core's library name, read from each core binary.
static std::string coreLibraryName(const std::string& command) {
    static const std::map<std::string, std::string> kNames = {
        {"fceumm", "FCEUmm"}, {"quicknes", "QuickNES"}, {"snes9x2005", "Snes9x 2005"},
        {"gambatte", "Gambatte"}, {"mgba", "mGBA"}, {"genesis_plus_gx", "Genesis Plus GX"},
        {"picodrive", "PicoDrive"}, {"pcsx_rearmed", "PCSX-ReARMed"}, {"ppsspp", "PPSSPP"},
        {"mednafen_pce_fast", "Beetle PCE Fast"}, {"stella2014", "Stella 2014"},
        {"fbneo", "FinalBurn Neo"}, {"mame2003_plus", "MAME 2003-Plus"},
    };
    size_t at = command.find("-L ");
    if (at == std::string::npos) return "";
    std::string core = command.substr(at + 3);
    core = core.substr(0, core.find(' '));
    core = core.substr(core.find_last_of('/') + 1);
    size_t suffix = core.find("_libretro");
    if (suffix != std::string::npos) core.erase(suffix);
    auto it = kNames.find(core);
    return it == kNames.end() ? "" : it->second;
}

static void makeDirs(const std::string& devicePath) {
    std::string cur;
    std::stringstream ss(devicePath);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        cur += "/" + part;
        mkdir(hostPath(cur).c_str(), 0755);
    }
}

static std::string buildCommand(const std::string& tmpl, const std::string& path) {
    const std::string quoted = shellQuote(path);
    if (tmpl.empty()) return quoted;
    std::string out = tmpl;
    bool replaced = false;
    for (size_t pos = 0; (pos = out.find("%s", pos)) != std::string::npos; pos += quoted.size()) {
        out.replace(pos, 2, quoted);
        replaced = true;
    }
    return replaced ? out : out + " " + quoted;
}

struct CheatEntry {
    char kind = 'C';         // 'M' master code, 'L' infinite lives, 'C' other
    std::string desc, shown; // database text and what the screen shows (Portuguese when known)
    std::vector<std::string> codes;  // one .cht entry each
};

// The most common descriptions in the database, in Portuguese; anything else stays as it is.
// Portuguese names for the most common cheat descriptions of the libretro database; anything
// else keeps its original English text. "(P1)", "Player 2" and the like become "(jogador N)".
static std::string translateCheat(const std::string& desc) {
    static const std::vector<std::pair<std::regex, std::string>> kRules = [] {
        std::vector<std::pair<std::regex, std::string>> r;
        auto add = [&](const char* pattern, const char* text) {
            r.emplace_back(std::regex(std::string("^\\s*") + pattern + "\\s*$", std::regex::icase), text);
        };
        add("(infinite|unlimited|inf\\.?) (lives|lifes|men)", "Vida infinita");
        add("(invincibility|invincible|invulnerability|untouchable)", "Invencível");
        add("(infinite|unlimited) (energy|health|hp)", "Energia infinita");
        add("(infinite|unlimited) time", "Tempo infinito");
        add("freeze (the )?(time|timer|clock)", "Congelar o tempo");
        add("(infinite|unlimited) continues", "Continues infinitos");
        add("(infinite|unlimited) credits", "Créditos infinitos");
        add("(infinite|unlimited) (ammo|ammunition)", "Munição infinita");
        add("(infinite|unlimited|max) (money|cash|gold|coins)", "Dinheiro infinito");
        add("(infinite|unlimited) bombs", "Bombas infinitas");
        add("(infinite|unlimited) fuel", "Combustível infinito");
        add("start with 1 (life|man)", "Começar com 1 vida");
        add("start with ([0-9]+) (lives|men)", "Começar com $1 vidas");
        add("start (on|at) (level|stage|round|world) ([0-9-]+)", "Começar na fase $3");
        add("hit anywhere", "Acerta de qualquer distância");
        add("one hit kills?", "Inimigos caem com um golpe");
        add("jump in mid-?air", "Pular no ar");
        add("(super|moon|high) jump|jump higher", "Pulo alto");
        add("walk through walls", "Atravessar paredes");
        add("(level|stage) (select|modifier)", "Escolher fase");
        add("character (select|modifier)", "Escolher personagem");
        add("weapon (select|modifier)", "Escolher arma");
        add("skip intro screens?", "Pular a abertura");
        add("max(imum)? (power|weapon|weapons)", "Poder máximo");
        add("regional lockout bypass", "Ignorar trava de região");
        return r;
    }();
    static const std::regex kPlayerAfter("^(.*?)\\s*\\(?(?:p|player ?)([0-9])\\)?\\s*$", std::regex::icase);
    static const std::regex kPlayerBefore("^\\s*(?:p|player ?)([0-9])\\s+(.*)$", std::regex::icase);
    std::string core = desc, player;
    std::smatch m;
    if (std::regex_match(desc, m, kPlayerBefore)) { player = m[1]; core = m[2]; }
    else if (std::regex_match(desc, m, kPlayerAfter)) { core = m[1]; player = m[2]; }
    for (const auto& [re, text] : kRules)
        if (std::regex_match(core, re))
            return std::regex_replace(core, re, text) + (player.empty() ? "" : " (jogador " + player + ")");
    return desc;
}

struct Catalog {
    std::vector<System> systems;  // only systems with games
    std::vector<App> apps;
    std::unordered_map<std::string, std::pair<int, int>> byPath;  // path -> (system, game)
    size_t totalGames = 0;

    void load() {
        std::vector<ParsedArchive> archives;
        parseAppsFile(kSystemAppsFile, archives, apps);
        parseAppsFile(kHomeAppsFile, archives, apps);
        // Puppy also reads one .puppy file per app from here.
        std::vector<std::string> extra;
        if (DIR* d = opendir(hostPath("/home/player/.local/share/applications").c_str())) {
            while (dirent* de = readdir(d)) {
                std::string n = de->d_name;
                if (n.size() > 6 && n[0] != '.' && n.compare(n.size() - 6, 6, ".puppy") == 0) extra.push_back(n);
            }
            closedir(d);
        }
        std::sort(extra.begin(), extra.end());
        for (const auto& n : extra) parseAppsFile("/home/player/.local/share/applications/" + n, archives, apps);
        for (const auto& a : archives) {
            System sys;
            sys.archiveName = a.name;
            sys.display = a.name;
            sys.color = hexColor(0x9CA3AF);
            for (size_t i = 0; i < sizeof(kStyles) / sizeof(kStyles[0]); ++i) {
                if (a.name == kStyles[i].archiveName) {
                    sys.display = kStyles[i].display;
                    sys.color = hexColor(kStyles[i].color);
                    sys.order = (int)i;
                }
            }
            sys.command = a.command;
            sys.dirs = a.dirs;
            sys.exts = a.exts;
            sys.iconDirs = a.iconDirs;
            scanSystem(sys);
            if (!sys.games.empty()) systems.push_back(std::move(sys));
        }
        std::stable_sort(systems.begin(), systems.end(),
                         [](const System& a, const System& b) { return a.order < b.order; });
        for (int s = 0; s < (int)systems.size(); ++s) {
            for (int g = 0; g < (int)systems[s].games.size(); ++g) {
                systems[s].games[g].systemIndex = s;
                byPath[systems[s].games[g].path] = {s, g};
            }
            totalGames += systems[s].games.size();
        }
    }

    Game* find(const std::string& path) {
        auto it = byPath.find(path);
        return it == byPath.end() ? nullptr : &systems[it->second.first].games[it->second.second];
    }

    const std::string& iconFor(Game& g) {
        if (g.iconState == 0) {
            g.iconState = 2;
            static const char* kExts[] = {".png", ".jpg", ".jpeg"};
            for (const auto& dir : systems[g.systemIndex].iconDirs) {
                for (const char* ext : kExts) {
                    std::string p = dir + "/" + g.stem + ext;
                    if (access(hostPath(p).c_str(), R_OK) == 0) {
                        g.iconPath = p;
                        g.iconState = 1;
                        break;
                    }
                }
                if (g.iconState == 1) break;
            }
        }
        return g.iconPath;
    }

    const std::string& cheatsFileFor(Game& g) {
        if (g.cheatsState == 0) {
            g.cheatsState = 2;
            for (const auto& dir : systems[g.systemIndex].dirs) {
                std::string folder = dir.substr(dir.find_last_of('/') + 1);
                std::string p = std::string(kCheatsDir) + "/" + folder + "/" + g.stem + ".txt";
                if (access(hostPath(p).c_str(), R_OK) == 0) {
                    g.cheatsPath = p;
                    g.cheatsState = 1;
                    break;
                }
            }
        }
        return g.cheatsPath;
    }

    // The game's cheats, read once and kept while the launcher runs.
    const std::vector<CheatEntry>& cheatsFor(Game& g) {
        static const std::vector<CheatEntry> kNone;
        const std::string& file = cheatsFileFor(g);
        if (file.empty()) return kNone;
        auto it = cheatCache.find(file);
        if (it != cheatCache.end()) return it->second;
        if (cheatCache.size() > 300) cheatCache.clear();
        std::vector<CheatEntry> entries;
        std::map<std::string, int> seen;
        std::ifstream in(hostPath(file));
        std::string line;
        while (std::getline(in, line)) {
            size_t t1 = line.find('\t'), t2 = line.find('\t', t1 + 1);
            if (t1 != 1 || t2 == std::string::npos) continue;
            CheatEntry e;
            e.kind = line[0];
            e.desc = line.substr(t1 + 1, t2 - t1 - 1);
            std::stringstream codes(line.substr(t2 + 1));
            std::string code;
            while (std::getline(codes, code, '|')) if (!code.empty()) e.codes.push_back(code);
            if (e.codes.empty()) continue;
            // Two cheats with the same text would share one switch: number the repeats.
            int same = seen[e.desc]++;
            if (same) e.desc += " (" + std::to_string(same + 1) + ")";
            entries.push_back(std::move(e));
        }
        return cheatCache[file] = std::move(entries);
    }

    bool hasLives(Game& g) {
        for (const auto& e : cheatsFor(g)) if (e.kind == 'L') return true;
        return false;
    }

    // The panel lists infinite lives and other cheats: a catalog with only master codes, or none
    // that can be read, has nothing to switch. Read once per catalog file, then cached.
    bool hasCheats(Game& g) {
        if (cheatsFileFor(g).empty()) return false;
        for (const auto& e : cheatsFor(g)) if (e.kind == 'L' || e.kind == 'C') return true;
        return false;
    }

    std::unordered_map<std::string, std::vector<CheatEntry>> cheatCache;

    // Box art lives in a "capas" folder next to each games folder.
    const std::string& coverFor(Game& g) {
        if (g.coverState == 0) {
            g.coverState = 2;
            for (const auto& dir : systems[g.systemIndex].dirs) {
                std::string p = dir + "/capas/" + g.stem + ".jpg";
                if (access(hostPath(p).c_str(), R_OK) == 0) {
                    g.coverPath = p;
                    g.coverState = 1;
                    break;
                }
            }
        }
        return g.coverPath;
    }
};

// ---------------------------------------------------------------------------
// Favourites, recents and last position, kept as plain text in the HOME partition.
// ---------------------------------------------------------------------------

struct PlayStat {
    long seconds = 0;  // total time played
    long last = 0;     // wall clock at the end of the last session (the console may never set it)
    int count = 0;     // sessions
};

struct Store {
    std::vector<std::string> favorites;  // device paths, newest first
    std::vector<std::string> recents;    // device paths, most recent first
    std::string lastScreen, lastSystem, lastGame;
    bool livesByDefault = true;          // infinite lives for every game that has the cheat
    std::string theme = "streaming";     // "streaming", "estante" or "tubo"
    bool resume = true;                  // RetroArch saves the game when it closes and loads it back
    bool filters = true;                 // CRT and LCD shaders installed per core
    bool confirmBottom = false;          // true: PlayStation style, B (bottom) confirms and A (right) goes back
    std::string language;                // "pt" or "en"; empty: follow RetroArch's user_language
    std::map<std::string, bool> livesOverride;  // per game, when it differs from the default
    std::set<std::string> cheatFilesMade;       // cheat files Vitrine wrote, the only ones it deletes
    std::map<std::string, std::set<std::string>> cheatChoices;  // per game, descriptions switched on
    std::map<std::string, PlayStat> playStats;  // per game

    static std::vector<std::string> readLines(const std::string& file) {
        std::vector<std::string> out;
        std::ifstream in(hostPath(file));
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty()) out.push_back(line);
        }
        return out;
    }

    // Replaces the file only after the new content is fully on the card: a full partition or a
    // forced power-off leaves the previous file intact instead of an empty one.
    static void writeLines(const std::string& file, const std::vector<std::string>& lines) {
        writeLinesAt(file, lines);
    }

    static bool writeLinesAt(const std::string& file, const std::vector<std::string>& lines) {
        const std::string dst = hostPath(file), tmp = dst + ".tmp";
        std::string data;
        for (const auto& l : lines) data += l + "\n";
        int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0) return false;
        bool ok = true;
        for (size_t off = 0; ok && off < data.size();) {
            ssize_t n = write(fd, data.data() + off, data.size() - off);
            if (n > 0) off += (size_t)n;
            else if (!(n < 0 && errno == EINTR)) ok = false;
        }
        if (ok && fsync(fd) != 0) ok = false;
        if (close(fd) != 0) ok = false;
        if (!ok || std::rename(tmp.c_str(), dst.c_str()) != 0) {
            unlink(tmp.c_str());
            return false;
        }
        std::string parent = file.substr(0, file.find_last_of('/'));
        int dir = open(hostPath(parent).c_str(), O_RDONLY | O_CLOEXEC);
        if (dir >= 0) {
            fsync(dir);
            close(dir);
        }
        return true;
    }

    void load() {
        mkdir(hostPath(kDataDir).c_str(), 0755);
        favorites = readLines(std::string(kDataDir) + "/favoritos.txt");
        recents = readLines(std::string(kDataDir) + "/recentes.txt");
        for (const auto& line : readLines(std::string(kDataDir) + "/ajustes.txt")) {
            if (line == "vida_infinita=nao") livesByDefault = false;
            if (line.compare(0, 10, "aparencia=") == 0) theme = line.substr(10);
            if (line == "continuar=nao") resume = false;
            if (line == "filtros=nao") filters = false;
            if (line == "confirmar=b") confirmBottom = true;
            if (line.compare(0, 7, "idioma=") == 0) language = line.substr(7);
        }
        for (const auto& line : readLines(std::string(kDataDir) + "/vida-infinita-por-jogo.txt")) {
            size_t tab = line.rfind('\t');
            if (tab != std::string::npos) livesOverride[line.substr(0, tab)] = line.substr(tab + 1) == "sim";
        }
        for (const auto& line : readLines(std::string(kDataDir) + "/trapacas-criadas.txt")) cheatFilesMade.insert(line);
        for (const auto& line : readLines(std::string(kDataDir) + "/tempo-jogado.txt")) {
            std::stringstream ss(line);
            std::string path, secs, last, count;
            if (std::getline(ss, path, '\t') && std::getline(ss, secs, '\t') && std::getline(ss, last, '\t') &&
                std::getline(ss, count, '\t'))
                playStats[path] = {std::atol(secs.c_str()), std::atol(last.c_str()), std::atoi(count.c_str())};
        }
        // "path<TAB>description<TAB>..." by description, not position: survives a catalog update.
        for (const auto& line : readLines(std::string(kDataDir) + "/trapacas-escolhidas.txt")) {
            std::stringstream ss(line);
            std::string path, desc;
            if (!std::getline(ss, path, '\t')) continue;
            while (std::getline(ss, desc, '\t'))
                if (!desc.empty()) cheatChoices[path].insert(desc);
        }
        auto state = readLines(std::string(kDataDir) + "/posicao.txt");
        if (state.size() >= 3) {
            lastScreen = state[0];
            lastSystem = state[1];
            lastGame = state[2];
        }
    }

    bool isFavorite(const std::string& path) const {
        return std::find(favorites.begin(), favorites.end(), path) != favorites.end();
    }

    void toggleFavorite(const std::string& path) {
        auto it = std::find(favorites.begin(), favorites.end(), path);
        if (it != favorites.end()) favorites.erase(it);
        else favorites.insert(favorites.begin(), path);
        writeLines(std::string(kDataDir) + "/favoritos.txt", favorites);
    }

    void addRecent(const std::string& path) {
        recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());
        recents.insert(recents.begin(), path);
        if ((int)recents.size() > kMaxRecents) recents.resize(kMaxRecents);
        writeLines(std::string(kDataDir) + "/recentes.txt", recents);
    }

    void savePosition(const std::string& screen, const std::string& system, const std::string& game) {
        writeLines(std::string(kDataDir) + "/posicao.txt", {screen, system, game.empty() ? "-" : game});
    }

    bool livesOn(const std::string& path) const {
        auto it = livesOverride.find(path);
        return it == livesOverride.end() ? livesByDefault : it->second;
    }

    void setLives(const std::string& path, bool on) {
        if (on == livesByDefault) livesOverride.erase(path);
        else livesOverride[path] = on;
        std::vector<std::string> lines;
        for (const auto& [p, v] : livesOverride) lines.push_back(p + "\t" + (v ? "sim" : "nao"));
        writeLines(std::string(kDataDir) + "/vida-infinita-por-jogo.txt", lines);
    }

    void savePrefs() {
        writeLines(std::string(kDataDir) + "/ajustes.txt",
                   {livesByDefault ? "vida_infinita=sim" : "vida_infinita=nao", "aparencia=" + theme,
                    resume ? "continuar=sim" : "continuar=nao", filters ? "filtros=sim" : "filtros=nao",
                    confirmBottom ? "confirmar=b" : "confirmar=a", "idioma=" + language});
    }

    void setTheme(const std::string& t) {
        theme = t;
        savePrefs();
    }

    void setLivesDefault(bool on) {
        livesByDefault = on;
        savePrefs();
        for (auto it = livesOverride.begin(); it != livesOverride.end();)
            it = it->second == on ? livesOverride.erase(it) : std::next(it);
        std::vector<std::string> lines;
        for (const auto& [p, v] : livesOverride) lines.push_back(p + "\t" + (v ? "sim" : "nao"));
        writeLines(std::string(kDataDir) + "/vida-infinita-por-jogo.txt", lines);
    }

    bool cheatOn(const std::string& path, const std::string& desc) const {
        auto it = cheatChoices.find(path);
        return it != cheatChoices.end() && it->second.count(desc) > 0;
    }

    int cheatsChosen(const std::string& path) const {
        auto it = cheatChoices.find(path);
        return it == cheatChoices.end() ? 0 : (int)it->second.size();
    }

    void toggleCheat(const std::string& path, const std::string& desc) {
        auto& chosen = cheatChoices[path];
        if (!chosen.erase(desc)) chosen.insert(desc);
        if (chosen.empty()) cheatChoices.erase(path);
        std::vector<std::string> lines;
        for (const auto& [p, descs] : cheatChoices) {
            std::string line = p;
            for (const auto& d : descs) line += "\t" + d;
            lines.push_back(line);
        }
        writeLines(std::string(kDataDir) + "/trapacas-escolhidas.txt", lines);
    }

    // A session shorter than a few seconds is a game that failed to open, not a game played.
    void addPlay(const std::string& path, long seconds, long now) {
        if (seconds < 5) return;
        PlayStat& p = playStats[path];
        p.seconds += seconds;
        p.last = now;
        p.count += 1;
        std::vector<std::string> lines;
        for (const auto& [game, st] : playStats)
            lines.push_back(game + "\t" + std::to_string(st.seconds) + "\t" + std::to_string(st.last) + "\t" +
                            std::to_string(st.count));
        writeLines(std::string(kDataDir) + "/tempo-jogado.txt", lines);
    }

    void forgetCheatFile(const std::string& file) {
        if (!cheatFilesMade.erase(file)) return;
        writeLines(std::string(kDataDir) + "/trapacas-criadas.txt",
                   std::vector<std::string>(cheatFilesMade.begin(), cheatFilesMade.end()));
    }

    void rememberCheatFile(const std::string& file) {
        if (!cheatFilesMade.insert(file).second) return;
        writeLines(std::string(kDataDir) + "/trapacas-criadas.txt",
                   std::vector<std::string>(cheatFilesMade.begin(), cheatFilesMade.end()));
    }
};

// ---------------------------------------------------------------------------
// Rendering helpers: cached text, rounded shapes, pictures.
// ---------------------------------------------------------------------------

enum FontId { kFont9, kFont11, kFont12, kFont13, kFont14, kFont15, kFont16, kFont18, kFont21, kFont24,
              kFont26, kFont28, kFont34, kFont40, kFont46, kFontCount };
static const int kFontSizes[kFontCount] = {9, 11, 12, 13, 14, 15, 16, 18, 21, 24, 26, 28, 34, 40, 46};

// Font families: the device's Work Sans, plus the two the Estante and TV de tubo layouts use
// (OFL). They are looked up where the goodluckOS package installs them, then in
// /home/player/vitrine/fontes. A missing file falls back to Work Sans.
enum Family { kSans, kReadable, kRetro };
static const char* kFontDirs[] = {"/usr/share/vitrine/fonts", "/home/player/vitrine/fontes"};

static TTF_Font* openBundledFont(const std::string& file, int size) {
    for (const char* dir : kFontDirs)
        if (TTF_Font* f = TTF_OpenFont(hostPath(std::string(dir) + "/" + file).c_str(), size)) return f;
    return nullptr;
}

struct TextTexture {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};

class Painter {
public:
    SDL_Renderer* r = nullptr;

    Family family = kSans;  // family used by text(), width(), fit() and height()

    bool open(SDL_Renderer* renderer) {
        r = renderer;
        if (!getFont(kSans, 13, false)) return false;
        circle = makeCircle(64);
        fadeLeft = makeFade(256, 1, true);
        fadeDown = makeFade(1, 256, false);
        return circle && fadeLeft && fadeDown;
    }

    void close() {
        clearTextCache();
        for (auto& [path, pic] : pictures) if (pic.tex) SDL_DestroyTexture(pic.tex);
        pictures.clear();
        pictureOrder.clear();
        for (SDL_Texture* t : {circle, fadeLeft, fadeDown}) if (t) SDL_DestroyTexture(t);
        circle = fadeLeft = fadeDown = nullptr;
        for (auto& [key, t] : sized) if (t) SDL_DestroyTexture(t);
        sized.clear();
        for (auto& [key, f] : fonts) if (f) TTF_CloseFont(f);
        fonts.clear();
        r = nullptr;
    }

    TTF_Font* font(FontId id, bool bold) { return getFont(family, kFontSizes[id], bold); }

    TTF_Font* getFont(Family fam, int size, bool bold) {
        auto key = std::make_tuple((int)fam, size, bold);
        auto it = fonts.find(key);
        if (it != fonts.end()) return it->second;
        TTF_Font* f = nullptr;
        bool synthBold = bold;
        if (fam == kReadable) {
            f = openBundledFont(bold ? "AtkinsonHyperlegible-Bold.ttf" : "AtkinsonHyperlegible-Regular.ttf", size);
            if (f) synthBold = false;
        } else if (fam == kRetro) {
            f = openBundledFont("VT323-Regular.ttf", size);
            if (f) synthBold = false;  // pixel font: bold would smear it
        }
        if (!f) f = TTF_OpenFont(hostPath(kFontFile).c_str(), size);
        if (f) {
            if (synthBold) TTF_SetFontStyle(f, TTF_STYLE_BOLD);
            TTF_SetFontHinting(f, fam == kRetro ? TTF_HINTING_MONO : TTF_HINTING_LIGHT);
        }
        return fonts[key] = f;
    }

    int width(FontId id, bool bold, const std::string& s) {
        int w = 0, h = 0;
        if (!s.empty()) TTF_SizeUTF8(font(id, bold), s.c_str(), &w, &h);
        return w;
    }

    int height(FontId id) { return TTF_FontHeight(font(id, false)); }

    // Cuts the text with an ellipsis so it fits in maxW pixels.
    std::string fit(FontId id, bool bold, std::string s, int maxW) {
        if (maxW <= 0 || width(id, bold, s) <= maxW) return s;
        const std::string ellipsis = TTF_GlyphIsProvided32(font(id, bold), 0x2026) ? "…" : "...";
        while (!s.empty() && width(id, bold, s + ellipsis) > maxW) popCodePoint(s);
        return trim(s) + ellipsis;
    }

    // Draws text with its top-left corner at (x, y); returns the drawn width.
    int text(FontId id, bool bold, const std::string& s, int x, int y, SDL_Color c, int maxW = 0) {
        if (s.empty()) return 0;
        std::string shown = fit(id, bold, s, maxW);
        TextTexture t = cachedText(id, bold, shown, c);
        if (!t.tex) return 0;
        SDL_Rect dst{x, y, t.w, t.h};
        SDL_RenderCopy(r, t.tex, nullptr, &dst);
        return t.w;
    }

    // Draws text vertically centred on cy.
    int textMid(FontId id, bool bold, const std::string& s, int x, int cy, SDL_Color c, int maxW = 0) {
        return text(id, bold, s, x, cy - height(id) / 2, c, maxW);
    }

    void fill(int x, int y, int w, int h, SDL_Color c) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_Rect rect{x, y, w, h};
        SDL_RenderFillRect(r, &rect);
    }

    void roundRect(int x, int y, int w, int h, int radius, SDL_Color c) {
        radius = std::min({radius, w / 2, h / 2});
        if (radius <= 0) return fill(x, y, w, h, c);
        SDL_SetTextureColorMod(circle, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(circle, c.a);
        const int half = 32;  // circle texture is 64px wide
        SDL_Rect src[4] = {{0, 0, half, half}, {half, 0, half, half}, {0, half, half, half}, {half, half, half, half}};
        SDL_Rect dst[4] = {{x, y, radius, radius},
                           {x + w - radius, y, radius, radius},
                           {x, y + h - radius, radius, radius},
                           {x + w - radius, y + h - radius, radius, radius}};
        for (int i = 0; i < 4; ++i) SDL_RenderCopy(r, circle, &src[i], &dst[i]);
        fill(x + radius, y, w - 2 * radius, h, c);
        fill(x, y + radius, radius, h - 2 * radius, c);
        fill(x + w - radius, y + radius, radius, h - 2 * radius, c);
    }

    void roundOutline(int x, int y, int w, int h, int radius, int thickness, SDL_Color line, SDL_Color inside) {
        roundRect(x, y, w, h, radius, line);
        roundRect(x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness,
                  std::max(0, radius - thickness), inside);
    }

    // Round button glyph in the colour of the physical button.
    void buttonGlyph(char letter, SDL_Color c, int x, int cy, int size = 20) {
        roundRect(x, cy - size / 2, size, size, size / 2, c);
        std::string s(1, letter);
        int w = width(kFont11, true, s);
        textMid(kFont11, true, s, x + (size - w) / 2, cy, kBg);
    }

    // Outlined key name such as "START" or "L1 R1"; "<>" and "^v" draw arrows.
    int keyGlyph(const std::string& label, int x, int cy, SDL_Color c, SDL_Color inside = kBar) {
        bool arrows = label == "<>" || label == "^v";
        int w = arrows ? 34 : width(kFont11, true, label) + 12;
        roundRect(x, cy - 9, w, 18, 4, c);
        roundRect(x + 1, cy - 8, w - 2, 16, 3, inside);
        if (label == "<>") {
            triangle(x + 8, cy, 6, true, c);
            triangle(x + w - 8, cy, 6, false, c);
        } else if (label == "^v") {
            triangleV(x + 11, cy, 6, true, c);
            triangleV(x + w - 11, cy, 6, false, c);
        } else {
            textMid(kFont11, true, label, x + 6, cy, c);
        }
        return w;
    }

    // Small solid triangle pointing left or right, centred on (cx, cy).
    void triangle(int cx, int cy, int size, bool pointsLeft, SDL_Color c) {
        float h = size / 2.0f, dir = pointsLeft ? -1.0f : 1.0f;
        SDL_Vertex v[3];
        v[0] = {{cx + dir * h, (float)cy}, c, {0, 0}};
        v[1] = {{cx - dir * h, cy - h}, c, {0, 0}};
        v[2] = {{cx - dir * h, cy + h}, c, {0, 0}};
        SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
    }

    void triangleV(int cx, int cy, int size, bool pointsUp, SDL_Color c) {
        float h = size / 2.0f, dir = pointsUp ? -1.0f : 1.0f;
        SDL_Vertex v[3];
        v[0] = {{(float)cx, cy + dir * h}, c, {0, 0}};
        v[1] = {{cx - h, cy - dir * h}, c, {0, 0}};
        v[2] = {{cx + h, cy - dir * h}, c, {0, 0}};
        SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
    }

    // Text in the retro TV font, whatever family is current.
    int retro(FontId id, const std::string& s, int x, int y, SDL_Color c, int maxW = 0) {
        Family before = family;
        family = kRetro;
        int w = text(id, false, s, x, y, c, maxW);
        family = before;
        return w;
    }

    int retroWidth(FontId id, const std::string& s) {
        Family before = family;
        family = kRetro;
        int w = width(id, false, s);
        family = before;
        return w;
    }

    // Five-pointed star drawn as triangles, so it never depends on a font glyph.
    void star(float cx, float cy, float radius, SDL_Color c) {
        SDL_Vertex v[11];
        v[0] = {{cx, cy}, c, {0, 0}};
        for (int i = 0; i < 10; ++i) {
            float a = -M_PI / 2 + i * M_PI / 5;
            float rad = i % 2 == 0 ? radius : radius * 0.45f;
            v[i + 1] = {{cx + rad * std::cos(a), cy + rad * std::sin(a)}, c, {0, 0}};
        }
        int idx[30];
        for (int i = 0; i < 10; ++i) {
            idx[i * 3] = 0;
            idx[i * 3 + 1] = i + 1;
            idx[i * 3 + 2] = (i + 1) % 10 + 1;
        }
        SDL_RenderGeometry(r, nullptr, v, 11, idx, 30);
    }

    // Covers the destination box with the picture, cropping what does not fit.
    bool picture(const std::string& devicePath, int x, int y, int w, int h) {
        TextTexture* pic = loadPicture(devicePath);
        if (!pic || !pic->tex) return false;
        float scale = std::max((float)w / pic->w, (float)h / pic->h);
        SDL_Rect src;
        src.w = std::min(pic->w, (int)std::lround(w / scale));
        src.h = std::min(pic->h, (int)std::lround(h / scale));
        src.x = (pic->w - src.w) / 2;
        src.y = (pic->h - src.h) / 2;
        SDL_Rect dst{x, y, w, h};
        SDL_RenderCopy(r, pic->tex, &src, &dst);
        return true;
    }

    bool pictureSize(const std::string& devicePath, int& w, int& h) {
        TextTexture* pic = loadPicture(devicePath);
        if (!pic || !pic->tex) return false;
        w = pic->w;
        h = pic->h;
        return true;
    }

    bool hasPicture(const std::string& devicePath) {
        TextTexture* pic = loadPicture(devicePath);
        return pic && pic->tex;
    }

    // Fades a picture into the background towards its left and bottom edges.
    void heroFades(int x, int y, int w, int h, float leftPortion) {
        SDL_SetTextureColorMod(fadeLeft, kBg.r, kBg.g, kBg.b);
        SDL_SetTextureColorMod(fadeDown, kBg.r, kBg.g, kBg.b);
        SDL_Rect left{x, y, (int)(w * leftPortion), h};
        SDL_RenderCopy(r, fadeLeft, nullptr, &left);
        SDL_Rect down{x, y + h / 2, w, h - h / 2};
        SDL_RenderCopy(r, fadeDown, nullptr, &down);
    }

    void battery(int percent, int right, int cy) {
        std::string label = percent >= 0 ? std::to_string(percent) + "%" : "";
        int tw = width(kFont12, false, label);
        // Dark backing so it stays readable over a bright picture.
        roundRect(right - tw - 38, cy - 12, tw + 48, 24, 12, SDL_Color{11, 13, 18, 200});
        int x = right - tw;
        text(kFont12, false, label, x, cy - height(kFont12) / 2, kText);
        int bx = x - 28;
        roundRect(bx, cy - 6, 19, 12, 3, kText);
        roundRect(bx + 1, cy - 5, 17, 10, 2, kBg);
        int level = percent < 0 ? 13 : std::max(1, 13 * percent / 100);
        fill(bx + 3, cy - 3, level, 6, percent >= 0 && percent < 15 ? kBtnA : kText);
        fill(bx + 20, cy - 2, 2, 4, kText);
    }

    // Dark line on every third row, like a CRT screen.
    void scanlines(int x, int y, int w, int h, Uint8 alpha) {
        SDL_Texture* t = generated("scan", w, h, [alpha](int, int py, int, int) -> Uint8 {
            return py % 3 == 2 ? alpha : 0;
        });
        SDL_Rect dst{x, y, w, h};
        if (t) SDL_RenderCopy(r, t, nullptr, &dst);
    }

    // Darker edges, like the curved glass of an old TV.
    void vignette(int x, int y, int w, int h, Uint8 strength) {
        SDL_Texture* t = generated("vig" + std::to_string(strength), 128, 128,
                                   [strength](int px, int py, int tw, int th) -> Uint8 {
            float dx = (px + 0.5f) / tw * 2 - 1, dy = (py + 0.5f) / th * 2 - 1;
            float d = std::clamp((std::sqrt(dx * dx + dy * dy) - 0.55f) / 0.6f, 0.0f, 1.0f);
            return (Uint8)(d * d * strength);
        }, true);
        SDL_Rect dst{x, y, w, h};
        if (t) SDL_RenderCopy(r, t, nullptr, &dst);
    }

    // Paints what lies outside rounded corners, so a picture under it looks rounded.
    void roundCorners(int x, int y, int w, int h, int radius, SDL_Color outside) {
        SDL_Texture* t = generated("corner", 64, 64, [](int px, int py, int, int) -> Uint8 {
            // quarter of a ring: opaque outside a circle of radius 64 centred at (64, 64)
            float dx = 64 - (px + 0.5f), dy = 64 - (py + 0.5f);
            return (Uint8)(std::clamp(std::sqrt(dx * dx + dy * dy) - 63.5f, 0.0f, 1.0f) * 255);
        }, true);
        if (!t) return;
        SDL_SetTextureColorMod(t, outside.r, outside.g, outside.b);
        SDL_Rect dst{x, y, radius, radius};
        SDL_RenderCopyEx(r, t, nullptr, &dst, 0, nullptr, SDL_FLIP_NONE);
        dst = {x + w - radius, y, radius, radius};
        SDL_RenderCopyEx(r, t, nullptr, &dst, 0, nullptr, SDL_FLIP_HORIZONTAL);
        dst = {x, y + h - radius, radius, radius};
        SDL_RenderCopyEx(r, t, nullptr, &dst, 0, nullptr, SDL_FLIP_VERTICAL);
        dst = {x + w - radius, y + h - radius, radius, radius};
        SDL_RenderCopyEx(r, t, nullptr, &dst, 0, nullptr, (SDL_RendererFlip)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
    }

    // Vertical shade from clear to dark over the lower part of a box.
    void shadeBottom(int x, int y, int w, int h, float from, Uint8 alpha) {
        SDL_SetTextureColorMod(fadeDown, 0, 0, 0);
        SDL_SetTextureAlphaMod(fadeDown, alpha);
        SDL_Rect dst{x, y + (int)(h * from), w, h - (int)(h * from)};
        SDL_RenderCopy(r, fadeDown, nullptr, &dst);
        SDL_SetTextureAlphaMod(fadeDown, 255);
    }

    void clearTextCache() {
        for (auto& [key, t] : textCache) if (t.tex) SDL_DestroyTexture(t.tex);
        textCache.clear();
        textOrder.clear();
    }

private:
    std::map<std::tuple<int, int, bool>, TTF_Font*> fonts;
    std::map<std::string, SDL_Texture*> sized;  // generated overlays per size (scanlines, corners...)
    SDL_Texture* circle = nullptr;
    SDL_Texture* fadeLeft = nullptr;
    SDL_Texture* fadeDown = nullptr;
    std::unordered_map<std::string, TextTexture> textCache;
    std::list<std::string> textOrder;
    std::unordered_map<std::string, TextTexture> pictures;
    std::list<std::string> pictureOrder;

    TextTexture cachedText(FontId id, bool bold, const std::string& s, SDL_Color c) {
        std::string key = std::to_string((int)family) + "/" + std::to_string(id) + (bold ? "b" : "r") + std::to_string(c.r) + "," +
                          std::to_string(c.g) + "," + std::to_string(c.b) + "|" + s;
        auto it = textCache.find(key);
        if (it != textCache.end()) return it->second;
        TextTexture t;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font(id, bold), s.c_str(), c);
        if (surf) {
            t.tex = SDL_CreateTextureFromSurface(r, surf);
            t.w = surf->w;
            t.h = surf->h;
            SDL_FreeSurface(surf);
        }
        if (textCache.size() >= 400) {
            auto old = textCache.find(textOrder.front());
            if (old != textCache.end()) {
                if (old->second.tex) SDL_DestroyTexture(old->second.tex);
                textCache.erase(old);
            }
            textOrder.pop_front();
        }
        textCache[key] = t;
        textOrder.push_back(key);
        return t;
    }

    TextTexture* loadPicture(const std::string& devicePath) {
        if (devicePath.empty()) return nullptr;
        auto it = pictures.find(devicePath);
        if (it != pictures.end()) return &it->second;
        TextTexture t;
        if (SDL_Surface* s = IMG_Load(hostPath(devicePath).c_str())) {
            t.tex = SDL_CreateTextureFromSurface(r, s);
            t.w = s->w;
            t.h = s->h;
            SDL_FreeSurface(s);
        }
        if (pictures.size() >= 16) {
            auto old = pictures.find(pictureOrder.front());
            if (old != pictures.end()) {
                if (old->second.tex) SDL_DestroyTexture(old->second.tex);
                pictures.erase(old);
            }
            pictureOrder.pop_front();
        }
        pictureOrder.push_back(devicePath);
        return &(pictures[devicePath] = t);
    }

    // White texture whose alpha comes from a function of the pixel, made once per name and size.
    SDL_Texture* generated(const std::string& name, int w, int h,
                           const std::function<Uint8(int, int, int, int)>& alpha, bool linear = false) {
        std::string key = name + ":" + std::to_string(w) + "x" + std::to_string(h);
        auto it = sized.find(key);
        if (it != sized.end()) return it->second;
        SDL_Texture* t = nullptr;
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32)) {
            Uint32* px = (Uint32*)s->pixels;
            for (int py = 0; py < h; ++py)
                for (int pxi = 0; pxi < w; ++pxi)
                    px[py * (s->pitch / 4) + pxi] = SDL_MapRGBA(s->format, 255, 255, 255, alpha(pxi, py, w, h));
            t = SDL_CreateTextureFromSurface(r, s);
            SDL_FreeSurface(s);
            if (t) {
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(t, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
                SDL_SetTextureColorMod(t, 0, 0, 0);  // dark by default; callers may tint it
            }
        }
        return sized[key] = t;
    }

    SDL_Texture* makeCircle(int size) {
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
        if (!s) return nullptr;
        Uint32* px = (Uint32*)s->pixels;
        const float rad = size / 2.0f;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float dx = x + 0.5f - rad, dy = y + 0.5f - rad;
                float a = std::clamp(rad - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
                px[y * (s->pitch / 4) + x] = SDL_MapRGBA(s->format, 255, 255, 255, (Uint8)(a * 255));
            }
        }
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        if (t) {
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        }
        return t;
    }

    // White texture whose alpha goes from opaque to clear along one axis.
    SDL_Texture* makeFade(int w, int h, bool horizontal) {
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
        if (!s) return nullptr;
        Uint32* px = (Uint32*)s->pixels;
        int n = horizontal ? w : h;
        for (int i = 0; i < n; ++i) {
            float t = (float)i / (n - 1);
            float a = horizontal ? 1.0f - t : t;
            a = a * a * (3 - 2 * a);  // smoothstep
            Uint32 v = SDL_MapRGBA(s->format, 255, 255, 255, (Uint8)(a * 255));
            if (horizontal) px[i] = v;
            else px[i * (s->pitch / 4)] = v;
        }
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        if (t) {
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        }
        return t;
    }
};

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------

enum class Screen { Home, List, Settings, Confirm, Power, Message, Cheats, Search, Stats, Notice };
enum class Filter { All, Favorites, Portuguese };

// Home cards: two virtual collections followed by one card per system.
constexpr int kCardFavorites = -2;
constexpr int kCardRecents = -1;
constexpr int kCardMostPlayed = -3;
constexpr int kCardClassics = -12;

// Automatic collections, filled from the game sheet (F7) and the time played (F11).
struct Collection {
    int card;
    const char* key;   // saved position
    const char* name;
    const char* nameEn;
    uint32_t color;
};
static const std::vector<Collection> kCollections = {
    {kCardClassics, "@classicos", "Clássicos", "Classics", 0xFFD166},
    {kCardMostPlayed, "@mais-jogados", "Mais jogados", "Most played", 0xF472B6},
    {-4, "@portugues", "Em português", "In Portuguese", 0x22C55E},
    {-5, "@2-jogadores", "Para 2 jogadores", "2 Players", 0x60A5FA},
    {-6, "@luta", "Luta", "Fighting", 0xEF4444},
    {-7, "@plataforma", "Plataforma", "Platform", 0xF59E0B},
    {-8, "@rpg", "RPG", "RPG", 0xA78BFA},
    {-9, "@corrida", "Corrida", "Racing", 0x38BDF8},
    {-10, "@esporte", "Esporte", "Sports", 0x84CC16},
    {-11, "@tiro", "Tiro", "Shooter", 0xFB923C},
};

static const Collection* collectionOf(int card) {
    for (const auto& c : kCollections)
        if (c.card == card) return &c;
    return nullptr;
}

struct ListView {
    int card = 0;                    // kCardFavorites, kCardRecents or a system index
    Filter filter = Filter::All;
    std::vector<Game*> items;
    int cursor = 0;
};

struct SettingsItem {
    std::string label;
    std::function<void()> run;
};

class Vitrine {
public:
    Catalog catalog;
    Store store;
    Painter painter;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_GameController* pad = nullptr;

    Screen screen = Screen::Home;
    Screen beforeOverlay = Screen::Home;
    int homeCard = 0;  // index into cards()
    ListView list;
    std::vector<SettingsItem> settings;
    int settingsCursor = 0;
    std::string confirmQuestion;
    std::function<void()> confirmAction;
    bool confirmYes = false;
    std::string powerTitle;
    Game* cheatsGame = nullptr;           // game whose cheat panel is open
    std::vector<CheatEntry> cheatRows;    // lives first, then the other cheats
    int cheatsCursor = 0;
    int statsScroll = 0;
    std::string noticeTitle, noticeBody;  // a warning shown instead of opening a game
    std::string query;                    // search: what was typed
    std::vector<Game*> found;             // search results, all systems
    int keyRow = 0, keyCol = 0, foundCursor = 0;
    bool resultsFocus = false;            // keyboard or results list
    std::vector<std::pair<std::string, Game*>> searchIndex;  // folded title of every game, built once
    int battery = -1;
    bool plugged = false;                       // charger connected (battery not discharging)
    std::deque<std::pair<long, int>> drain;     // (seconds, percent) while discharging, last hour
    bool closedForBattery = false;              // the last game was closed to save it before the battery ran out
    bool running = true;
    bool dirty = true;
    Uint32 lastMoveAt = 0;
    std::map<int, std::string> systemHero;  // cached picture per system
    std::map<int, std::vector<std::string>> systemPictures;  // cached picture grid per system

    bool startVideo() {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
            return false;
        }
        Uint32 flags = gPreview ? SDL_WINDOW_HIDDEN : 0;
        window = SDL_CreateWindow("Vitrine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kScreenW, kScreenH, flags);
        if (!window) return false;
        Uint32 rflags = gPreview ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
        renderer = SDL_CreateRenderer(window, -1, rflags);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) return false;
        SDL_RenderSetLogicalSize(renderer, kScreenW, kScreenH);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_ShowCursor(SDL_DISABLE);
        if (!painter.open(renderer)) {
            std::cerr << L("Fonte não encontrada: ", "Font not found: ") << hostPath(kFontFile) << "\n";
            return false;
        }
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i)) {
                pad = SDL_GameControllerOpen(i);
                break;
            }
        }
        dirty = true;
        return true;
    }

    void stopVideo() {
        painter.close();
        if (pad) SDL_GameControllerClose(pad);
        pad = nullptr;
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        renderer = nullptr;
        window = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    }

    // -- cards and lists ------------------------------------------------------

    // Favourites, recents and the most played come first, then the consoles, then the other
    // collections: the row wraps around, so those are one press left of Favourites.
    // An automatic collection only shows up when it has games.
    std::vector<int> cards() const {
        std::vector<int> out = {kCardFavorites, kCardRecents};
        auto add = [&](int card) {
            auto it = autoCollections.find(card);
            if (it != autoCollections.end() && !it->second.empty()) out.push_back(card);
        };
        add(kCardClassics);
        add(kCardMostPlayed);
        for (int s = 0; s < (int)catalog.systems.size(); ++s) out.push_back(s);
        for (const auto& c : kCollections)
            if (c.card != kCardMostPlayed && c.card != kCardClassics) add(c.card);
        return out;
    }

    std::map<int, std::vector<Game*>> autoCollections;

    void buildCollections() {
        autoCollections.clear();
        auto genreCard = [](const std::string& genre) {  // English keys; older sheets had Portuguese
            if (genre == "Fighting" || genre == "Beat'em up" || genre == "Luta" || genre == "Briga de rua") return -6;
            if (genre == "Platform" || genre == "Plataforma") return -7;
            if (genre == "RPG") return -8;
            if (genre == "Racing" || genre == "Corrida") return -9;
            if (genre == "Sports" || genre == "Esporte") return -10;
            if (genre == "Shooter" || genre == "Shoot'em up" || genre == "Tiro" || genre == "Tiro de nave") return -11;
            return 0;
        };
        std::vector<std::pair<long, Game*>> played;
        for (auto& sys : catalog.systems) {
            for (auto& g : sys.games) {
                if (g.ptbr) autoCollections[-4].push_back(&g);
                if (g.players >= 2) autoCollections[-5].push_back(&g);
                if (int c = genreCard(g.genre)) autoCollections[c].push_back(&g);
                auto it = store.playStats.find(g.path);
                if (it != store.playStats.end()) played.push_back({it->second.seconds, &g});
            }
        }
        auto byTitle = [](const Game* a, const Game* b) {
            return a->sortKey != b->sortKey ? a->sortKey < b->sortKey : a->systemIndex < b->systemIndex;
        };
        for (auto& [card, games] : autoCollections) std::sort(games.begin(), games.end(), byTitle);
        std::stable_sort(played.begin(), played.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        auto& most = autoCollections[kCardMostPlayed];
        for (size_t i = 0; i < played.size() && i < 50; ++i) most.push_back(played[i].second);
        // Clássicos: lista curada no Mac (tools/classicos.py), um caminho por linha, na ordem do menu.
        auto& classics = autoCollections[kCardClassics];
        for (const auto& path : Store::readLines(std::string(kDataDir) + "/classicos.txt"))
            if (Game* g = catalog.find(path)) classics.push_back(g);
    }

    std::string cardName(int card) const {
        if (card == kCardFavorites) return L("Favoritos", "Favorites");
        if (card == kCardRecents) return L("Recentes", "Recent");
        if (const Collection* c = collectionOf(card)) return L(c->name, c->nameEn);
        return catalog.systems[card].display;
    }

    std::vector<Game*> gamesOf(int card) {
        std::vector<Game*> out;
        if (auto it = autoCollections.find(card); it != autoCollections.end()) return it->second;
        if (card == kCardFavorites || card == kCardRecents) {
            const auto& paths = card == kCardFavorites ? store.favorites : store.recents;
            for (const auto& p : paths)
                if (Game* g = catalog.find(p)) out.push_back(g);
        } else {
            for (auto& g : catalog.systems[card].games) out.push_back(&g);
        }
        return out;
    }

    void rebuildList(const std::string& keepPath = "") {
        std::string keep = keepPath;
        if (keep.empty() && !list.items.empty() && list.cursor < (int)list.items.size())
            keep = list.items[list.cursor]->path;
        list.items.clear();
        for (Game* g : gamesOf(list.card)) {
            if (list.filter == Filter::Favorites && !store.isFavorite(g->path)) continue;
            if (list.filter == Filter::Portuguese && !g->ptbr) continue;
            list.items.push_back(g);
        }
        list.cursor = 0;
        for (int i = 0; i < (int)list.items.size(); ++i)
            if (list.items[i]->path == keep) list.cursor = i;
    }

    void openList(int card, const std::string& keepPath = "") {
        list.card = card;
        list.filter = Filter::All;
        list.items.clear();
        rebuildList(keepPath);
        screen = Screen::List;
        dirty = true;
    }

    Game* selectedGame() {
        bool overlay = screen == Screen::Settings || screen == Screen::Cheats || screen == Screen::Notice ||
                       screen == Screen::Stats || screen == Screen::Message || screen == Screen::Confirm;
        bool overList = overlay && beforeOverlay == Screen::List;
        if (screen != Screen::List && !overList) return nullptr;
        if (list.items.empty()) return nullptr;
        return list.items[std::clamp(list.cursor, 0, (int)list.items.size() - 1)];
    }

    Game* lastPlayed(int card) {
        for (const auto& p : store.recents) {
            Game* g = catalog.find(p);
            if (g && (card < 0 || g->systemIndex == card)) return g;
        }
        return nullptr;
    }

    // Picture for a home card: for a collection, its first game with a picture; for a
    // system, the last game played there, else the first game in its list with one.
    std::string heroForCard(int card) {
        if (card < 0) {
            auto games = gamesOf(card);  // look a little into the list, as for systems
            for (size_t i = 0; i < games.size() && i < 400; ++i)
                if (!catalog.iconFor(*games[i]).empty()) return games[i]->iconPath;
            return "";
        }
        if (Game* g = lastPlayed(card); g && !catalog.iconFor(*g).empty()) return g->iconPath;
        auto it = systemHero.find(card);
        if (it != systemHero.end()) return it->second;
        std::string found;
        auto& games = catalog.systems[card].games;
        // Look a little into the list so the home screen stays fast on big systems.
        for (size_t i = 0; i < games.size() && i < 400 && found.empty(); ++i)
            if (!catalog.iconFor(games[i]).empty()) found = games[i].iconPath;
        return systemHero[card] = found;
    }

    // -- actions --------------------------------------------------------------

    // The requirement the game's folder misses, or nullptr when it can open.
    static const Requirement* missingRequirement(const Game& g) {
        std::string dir = g.path.substr(0, g.path.find_last_of('/'));
        std::string folder = dir.substr(dir.find_last_of('/') + 1);
        for (const auto& r : kRequirements) {
            if (folder != r.folder) continue;
            bool ok = std::any_of(r.anyOf.begin(), r.anyOf.end(),
                                  [](const char* p) { return access(hostPath(p).c_str(), R_OK) == 0; });
            return ok ? nullptr : &r;
        }
        return nullptr;
    }

    void launch(Game* g) {
        if (!g) return;
        if (const Requirement* r = missingRequirement(*g)) {
            noticeTitle = L(r->title, r->titleEn);
            noticeBody = L(r->body, r->bodyEn);
            beforeOverlay = screen;
            screen = Screen::Notice;
            dirty = true;
            return;
        }
        const System& sys = catalog.systems[g->systemIndex];
        store.addRecent(g->path);
        // From Home ("Continuar") the game comes back in its own system list.
        int card = screen == Screen::List ? list.card : g->systemIndex;
        store.savePosition("lista", cardKey(card), g->path);
        ensureRetroArchSettings();
        prepareCheats(*g, sys);
        long started = monotonicSeconds();
        closedForBattery = false;
        bool retroarch = sys.command.find("retroarch") != std::string::npos;
        runCommand(buildCommand(sys.command, g->path), L("Abrindo ", "Opening ") + g->title + "…",
                   retroarch ? returnHint() : portHint(), retroarch);
        long played = monotonicSeconds() - started;
        if (gPreview)  // nothing really runs in the preview: VITRINE_SIMULAR_SEGUNDOS stands for the session
            if (const char* sim = std::getenv("VITRINE_SIMULAR_SEGUNDOS")) played = std::atol(sim);
        store.addPlay(g->path, played, (long)time(nullptr));
        {
            auto before = cards();  // "Mais jogados" may appear now: keep Home on the same card
            int homeId = before[std::clamp(homeCard, 0, (int)before.size() - 1)];
            buildCollections();
            auto after = cards();
            auto it = std::find(after.begin(), after.end(), homeId);
            if (it != after.end()) homeCard = (int)(it - after.begin());
        }
        if (closedForBattery) {
            closedForBattery = false;
            noticeTitle = L("Bateria acabando", "Battery running out");
            noticeBody = L("O jogo foi fechado e salvo para não perder o progresso. Ligue o carregador; "
                           "ao abrir de novo, ele continua de onde parou.",
                           "The game was saved and closed so no progress is lost. Plug in the charger; "
                           "it will continue where you left off.");
            if (!store.resume)  // the command has no reply: say what was asked, not what happened
                noticeBody = L("O jogo foi fechado depois do pedido para salvar no espaço atual. Ligue o carregador; "
                               "para continuar, abra o jogo e carregue o ponto salvo (SELECT+L1).",
                               "The game was closed after being asked to save to the current slot. Plug in the "
                               "charger; to continue, open the game and load the saved point (SELECT+L1).");
            beforeOverlay = screen;
            screen = Screen::Notice;
        }
        systemHero.erase(g->systemIndex);
        rebuildList(g->path);
    }

    // Writes the cheats switched on for this game (infinite lives and the ones chosen in the
    // panel) where RetroArch loads them, or takes the file away when none is on.
    // RetroArch rewrites that file when the game closes, so it is set again on every launch.
    void prepareCheats(Game& g, const System& sys) {
        std::string lib = coreLibraryName(sys.command);
        if (lib.empty()) return;
        std::string dir = std::string(kCheatDir) + "/" + lib;
        // RetroArch cuts the content name at the last dot twice before adding ".cht"
        // (runloop_path_set_basename, then fill_pathname), so "Super Mario Bros. 3 (USA).nes"
        // is looked up as "Super Mario Bros.cht". The stem already lost the extension once.
        std::string name = g.stem;
        size_t dot = name.find_last_of('.');
        if (dot != std::string::npos && dot > 0) name.erase(dot);
        std::string target = dir + "/" + name + ".cht";
        bool ours = store.cheatFilesMade.count(target) > 0;
        bool exists = access(hostPath(target).c_str(), F_OK) == 0;
        const std::vector<CheatEntry>& all = catalog.cheatsFor(g);
        std::vector<std::pair<std::string, std::string>> items;  // description, code
        bool genie = false;
        for (const auto& e : all) {
            bool on = e.kind == 'L' ? store.livesOn(g.path) : e.kind == 'C' && store.cheatOn(g.path, e.desc);
            if (!on) continue;
            for (const auto& c : e.codes) {
                items.push_back({e.desc, c});
                if (c.size() >= 9 && c[4] == '-') genie = true;  // Mega Drive Game Genie: patches the ROM
            }
        }
        if (items.empty()) {
            if (ours) {
                std::remove(hostPath(target).c_str());
                store.forgetCheatFile(target);  // a file the player saves there later is theirs
            }
            return;
        }
        if (exists && !ours) return;  // a cheat file the player made in RetroArch: leave it alone
        // A patched ROM fails its own checksum and stops on a red screen; the master code skips it.
        if (genie)
            for (auto it = all.rbegin(); it != all.rend(); ++it)
                if (it->kind == 'M')
                    for (auto c = it->codes.rbegin(); c != it->codes.rend(); ++c) items.insert(items.begin(), {it->desc, *c});
        std::vector<std::string> lines = {"cheats = " + std::to_string(items.size()), ""};
        for (size_t i = 0; i < items.size(); ++i) {
            std::string desc = items[i].first;
            std::replace(desc.begin(), desc.end(), '"', '\'');
            std::string n = "cheat" + std::to_string(i);
            lines.push_back(n + "_desc = \"" + desc + "\"");
            lines.push_back(n + "_code = \"" + items[i].second + "\"");
            lines.push_back(n + "_enable = true");
            lines.push_back("");
        }
        makeDirs(dir);
        if (Store::writeLinesAt(target, lines)) store.rememberCheatFile(target);
    }

    // RetroArch saves its config on exit, so an option switched off in its menu would stay off.
    // Before every game, put back the keys that Vitrine's own settings control: cheats applied
    // on load (infinite lives) and the automatic save state ("Continuar de onde parou").
    void ensureRetroArchSettings() {
        const std::string resume = store.resume ? "true" : "false";
        const std::vector<std::pair<std::string, std::string>> kKeys = {
            {"apply_cheats_after_load", "true"}, {"cheat_database_path", kCheatDir},
            {"savestate_auto_save", resume}, {"savestate_auto_load", resume},
            {"video_shader_enable", store.filters ? "true" : "false"},
            {"network_cmd_enable", "true"}, {"network_cmd_port", std::to_string(kRetroArchCmdPort)},
            // Save and load on the first screen of the FN menu, back to the game right after.
            {"quick_menu_show_savestate_submenu", "false"}, {"quick_menu_show_save_load_state", "true"},
            {"menu_savestate_resume", "true"},
            {"user_language", gEnglish ? "0" : "7"}};  // RetroArch menus in the same language (7 = pt_BR)
        std::vector<std::string> lines;
        {
            std::ifstream in(hostPath(kRetroArchCfg));
            if (!in) return;
            std::string line;
            while (std::getline(in, line)) lines.push_back(line);
        }
        bool changed = false;
        for (const auto& [key, value] : kKeys) {
            std::string wanted = key + " = \"" + value + "\"";
            bool found = false;
            for (auto& line : lines) {
                std::string t = trim(line);
                if (t.compare(0, key.size(), key) != 0) continue;
                std::string rest = trim(t.substr(key.size()));
                if (rest.empty() || rest[0] != '=') continue;
                found = true;
                if (t != wanted) {
                    line = wanted;
                    changed = true;
                }
                break;  // RetroArch keeps the first occurrence of a key
            }
            if (!found) {
                lines.push_back(wanted);
                changed = true;
            }
        }
        if (changed) Store::writeLinesAt(kRetroArchCfg, lines);
    }

    void runCommand(const std::string& command, const std::string& waiting, const std::string& hint = "",
                    bool game = false) {
        renderWaiting(waiting, hint);
        std::string run = command;
        if (gPreview) {
            std::cout << "[preview] " << command << "\n";
            // VITRINE_SIMULAR_COMANDO stands for the game, to exercise the wait and battery watch.
            const char* sim = std::getenv("VITRINE_SIMULAR_COMANDO");
            if (!sim) return;
            run = sim;
        } else {
            SDL_Delay(1200);  // releasing the display brings the text console back, so let them read this first
            stopVideo();
        }
        const std::string shellCommand = "exec " + run;
        pid_t child = fork();
        if (child == 0) {
            setpgid(0, 0);             // its own process group, so everything it starts can be closed with it
            signal(SIGPIPE, SIG_DFL);  // games and apps get the default, not Vitrine's SIG_IGN
            execl("/bin/sh", "sh", "-c", shellCommand.c_str(), (char*)nullptr);
            _exit(127);
        }
        if (child > 0) {
            // The MODE+START+SELECT shortcut kills whatever pid this file names.
            std::ofstream(kPidFile) << child << "\n";
            watchGame(child, game);
            std::ofstream(kPidFile) << getpid() << "\n";
            // The FN+START+SELECT shortcut kills only the pid in the file. A PortMaster game runs
            // under a shell script and would survive it, holding the screen: close the whole group.
            if (kill(-child, 0) == 0) {
                kill(-child, SIGTERM);
                usleep(300000);
                kill(-child, SIGKILL);
            }
        }
        if (gPreview) return;
        if (!startVideo()) running = false;
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        // Releases that happened while the game ran never reached us.
        held = Key::None;
        triggerDown[0] = triggerDown[1] = false;
        dirty = true;
    }

    // Waits for the game to close. Every 20 seconds it reads the battery; below 4% without the
    // charger it asks RetroArch to save and close, so the game is not lost when the console dies.
    // With "Continuar de onde parou" on, closing already saves the game (savestate_auto_save);
    // otherwise SAVE_STATE writes it to the current slot first.
    void watchGame(pid_t child, bool game) {
        long lastCheck = monotonicSeconds(), askedAt = 0, startedAt = monotonicSeconds();
        bool reminded = !game;  // shortcut reminder: RetroArch games only
        const long checkEvery = gPreview ? 1 : 20;
        for (;;) {
            int status = 0;
            pid_t r = waitpid(child, &status, WNOHANG);
            if (r == child || (r < 0 && errno != EINTR)) break;
            long now = monotonicSeconds();
            // A few seconds in, once the game is on screen, RetroArch shows the shortcuts
            // (SHOW_MSG: about 3 s each line); the hint on the opening screen goes by too fast.
            if (!reminded && now - startedAt >= 8) {
                reminded = true;
                retroArchCommand(L("SHOW_MSG FN: menu (salvar, carregar, sair)\n"
                                   "SHOW_MSG SELECT+R1 salva  |  SELECT+L1 carrega\n"
                                   "SHOW_MSG SELECT+R2 acelera  |  SELECT+L2 (segure) volta no tempo",
                                   "SHOW_MSG FN: menu (save, load, quit)\n"
                                   "SHOW_MSG SELECT+R1 save  |  SELECT+L1 load\n"
                                   "SHOW_MSG SELECT+R2 fast-forward  |  SELECT+L2 (hold) rewind"));
            }
            if (now - lastCheck >= checkEvery) {
                lastCheck = now;
                updateBattery();
                if (game && !askedAt && battery >= 0 && battery < kBatteryCritical && !plugged) {
                    if (!store.resume) {
                        retroArchCommand("SAVE_STATE");
                        usleep(2000000);  // writing the state takes a moment on the SD card
                    }
                    retroArchCommand("QUIT");
                    askedAt = now;
                    closedForBattery = true;
                }
            }
            // RetroArch did not answer the command: SIGTERM also makes it close normally.
            if (askedAt && now - askedAt > 20) {
                kill(child, SIGTERM);
                askedAt = now;
            }
            usleep(250000);
        }
    }

    void requestPower(const std::string& what) {
        powerTitle = what == "reboot" ? L("Reiniciando…", "Restarting…") : L("Desligando…", "Shutting down…");
        saveCurrentPosition();
        screen = Screen::Power;
        render();
        if (gPreview) return;
        std::ofstream fifo(kPowerFifo);
        if (fifo) fifo << what << "\n";
    }

    void openSettings() {
        beforeOverlay = screen;
        settings.clear();
        auto confirmLabel = [this] {
            return std::string(L("Confirmar com: ", "Confirm with: ")) + (store.confirmBottom ? L("B, embaixo (PlayStation)", "B (bottom, PlayStation style)") : L("A, à direita (Nintendo)", "A (right, Nintendo style)"));
        };
        auto livesLabel = [this] {
            return std::string(L("Vida infinita nos jogos: ", "Infinite lives: ")) + (store.livesByDefault ? L("ligada", "on") : L("desligada", "off"));
        };
        auto themeLabel = [this] { return L("Aparência: ", "Layout: ") + themeName(theme()); };
        settings.push_back({L("Idioma: Português", "Language: English"), [this] { switchLanguage(); }});
        settings.push_back({themeLabel(), [this, themeLabel] {
            cycleTheme();
            settings[settingsCursor].label = themeLabel();
        }});
        auto resumeLabel = [this] {
            return std::string(L("Continuar de onde parou: ", "Resume where you left off: ")) + (store.resume ? L("ligado", "on") : L("desligado", "off"));
        };
        settings.push_back({resumeLabel(), [this, resumeLabel] {
            store.resume = !store.resume;
            store.savePrefs();
            settings[settingsCursor].label = resumeLabel();
        }});
        auto filtersLabel = [this] {
            return std::string(L("Filtros de tela (TV e LCD): ", "Screen filters (CRT & LCD): ")) + (store.filters ? L("ligados", "on") : L("desligados", "off"));
        };
        settings.push_back({filtersLabel(), [this, filtersLabel] {
            store.filters = !store.filters;
            store.savePrefs();
            settings[settingsCursor].label = filtersLabel();
        }});
        settings.push_back({confirmLabel(), [this, confirmLabel] {
            store.confirmBottom = !store.confirmBottom;
            store.savePrefs();
            settings[settingsCursor].label = confirmLabel();
        }});
        settings.push_back({livesLabel(), [this, livesLabel] {
            store.setLivesDefault(!store.livesByDefault);
            settings[settingsCursor].label = livesLabel();
        }});
        settings.push_back({L("Tempo de jogo", "Play time"), [this] {
            statsScroll = 0;
            screen = Screen::Stats;
            dirty = true;
        }});
        settings.push_back({L("Atalhos dos jogos", "In-game shortcuts"), [this] {
            screen = Screen::Message;
            dirty = true;
        }});
        settings.push_back({L("Desligar o console", "Power off"), [this] {
            ask(L("Desligar o console?", "Power off the console?"), [this] { requestPower("poweroff"); });
        }});
        settings.push_back({L("Reiniciar", "Restart"), [this] {
            ask(L("Reiniciar o console?", "Restart the console?"), [this] { requestPower("reboot"); });
        }});
        for (const auto& app : catalog.apps) {
            settings.push_back({app.name, [this, app] {
                screen = beforeOverlay;
                runCommand(app.command, L("Abrindo ", "Opening ") + app.name + "…",
                           app.command.find("retroarch") == 0 ? returnHint() : "");
            }});
        }
        settings.push_back({L("Voltar ao menu clássico", "Back to classic menu"), [this] { running = false; }});
        settingsCursor = 0;
        screen = Screen::Settings;
        dirty = true;
    }

    // The cheat panel of the selected game: infinite lives first, then every other cheat.
    void openCheats(Game* g) {
        cheatsGame = g;
        cheatRows.clear();
        for (const auto& e : catalog.cheatsFor(*g)) if (e.kind == 'L') cheatRows.push_back(e);
        for (const auto& e : catalog.cheatsFor(*g)) if (e.kind == 'C') cheatRows.push_back(e);
        if (cheatRows.empty()) return;
        for (auto& e : cheatRows) {
            // The number added to a repeated description stays after the translated text.
            size_t cut = e.desc.size();
            if (cut > 4 && e.desc.back() == ')' && e.desc[cut - 4] == ' ' && e.desc[cut - 3] == '(') cut -= 4;
            // The libretro database is in English already: translate only for Portuguese.
            e.shown = gEnglish ? e.desc : translateCheat(e.desc.substr(0, cut)) + e.desc.substr(cut);
        }
        cheatsCursor = 0;
        beforeOverlay = screen;
        screen = Screen::Cheats;
        dirty = true;
    }

    bool cheatRowOn(const CheatEntry& e) const {
        return e.kind == 'L' ? store.livesOn(cheatsGame->path) : store.cheatOn(cheatsGame->path, e.desc);
    }

    // Genre keys come in English from the game sheets (tools/fichas.py); older sheets have the
    // Portuguese labels. Shown in the current language either way.
    static std::string genreLabel(const std::string& genre) {
        static const std::vector<std::pair<const char*, const char*>> kGenres = {
            {"Platform", "Plataforma"}, {"Action", "Ação"}, {"Sports", "Esporte"}, {"RPG", "RPG"},
            {"Adventure", "Aventura"}, {"Racing", "Corrida"}, {"Strategy", "Estratégia"},
            {"Puzzle", "Quebra-cabeça"}, {"Shooter", "Tiro"}, {"Shoot'em up", "Tiro de nave"},
            {"Fighting", "Luta"}, {"Beat'em up", "Briga de rua"}, {"Board & cards", "Tabuleiro e cartas"},
            {"Simulation", "Simulação"}, {"Compilation", "Coletânea"}, {"Educational", "Educativo"},
            {"Pinball", "Pinball"}, {"Quiz", "Quiz"}, {"Music", "Música"}, {"Casual", "Casual"},
            {"Maze", "Labirinto"}, {"Sandbox", "Construção"}};
        for (const auto& [en, pt] : kGenres)
            if (genre == en || genre == pt) return gEnglish ? en : pt;
        return genre;
    }

    // "1991 · Plataforma · até 2 jogadores": the game sheet in one line, empty when unknown.
    static std::string sheetLine(const Game& g) {
        std::string out;
        auto add = [&](const std::string& s) {
            if (!s.empty()) out += (out.empty() ? "" : " · ") + s;
        };
        add(g.year);
        add(genreLabel(g.genre));
        add(g.players == 1 ? L("1 jogador", "1 player") : g.players > 1 ? L("até ", "up to ") + std::to_string(g.players) + L(" jogadores", " players") : "");
        return out;
    }

    // "Jogado 3h 20min · há 2 dias", empty for a game never played.
    std::string playLine(const Game& g) const {
        auto it = store.playStats.find(g.path);
        if (it == store.playStats.end()) return "";
        std::string ago = formatAgo(it->second.last, (long)time(nullptr));
        return L("Jogado ", "Played ") + formatDuration(it->second.seconds) + (ago.empty() ? "" : " · " + ago);
    }

    // One line about the game's cheats for the detail area; empty when it has none.
    std::string cheatsStatus(Game& g, bool& on) {
        on = false;
        if (!catalog.hasCheats(g)) return "";
        std::string out;
        if (catalog.hasLives(g)) {
            on = store.livesOn(g.path);
            out = on ? L("vida infinita ligada", "infinite lives on") : L("vida infinita desligada", "infinite lives off");
        }
        int chosen = store.cheatsChosen(g.path);
        if (chosen > 0) {
            std::string n = std::to_string(chosen) + (chosen == 1 ? L(" trapaça", " cheat") : L(" trapaças", " cheats"));
            out = on ? L("vida infinita + ", "infinite lives + ") + n : n + (chosen == 1 ? L(" ligada", " on") : L(" ligadas", " on"));
            on = true;
        } else if (out.empty()) {
            out = L("trapaças disponíveis", "cheats available");
        }
        return out;
    }

    // -- search -----------------------------------------------------------------

    // On-screen keyboard: ten columns; a cell wider than one column repeats its label.
    static const std::vector<std::vector<std::string>>& keyboard() {
        static const std::vector<std::vector<std::string>> kRows = {
            {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"},
            {"K", "L", "M", "N", "O", "P", "Q", "R", "S", "T"},
            {"U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3"},
            {"4", "5", "6", "7", "8", "9", "Espaço", "Espaço", "Apagar", "Apagar"}};
        return kRows;
    }

    void openSearch() {
        screen = Screen::Search;
        resultsFocus = false;
        runSearch();
        dirty = true;
    }

    // Every word typed must appear in the title; titles that start with the text come first.
    void runSearch() {
        found.clear();
        foundCursor = 0;
        std::vector<std::string> words;
        std::stringstream ss(fold(query));
        for (std::string w; ss >> w;) words.push_back(w);
        if (words.empty()) return;
        std::string whole = fold(trim(query));
        if (searchIndex.empty())
            for (auto& sys : catalog.systems)
                for (auto& g : sys.games) searchIndex.push_back({fold(g.title), &g});
        std::vector<Game*> starts, contains;
        for (const auto& entry : searchIndex) {
            const std::string& t = entry.first;
            bool all = std::all_of(words.begin(), words.end(),
                                   [&](const std::string& w) { return t.find(w) != std::string::npos; });
            if (all) (t.compare(0, whole.size(), whole) == 0 ? starts : contains).push_back(entry.second);
        }
        auto byTitle = [](const Game* a, const Game* b) {
            return a->sortKey != b->sortKey ? a->sortKey < b->sortKey : a->systemIndex < b->systemIndex;
        };
        std::sort(starts.begin(), starts.end(), byTitle);
        std::sort(contains.begin(), contains.end(), byTitle);
        found = std::move(starts);
        found.insert(found.end(), contains.begin(), contains.end());
    }

    void type(const std::string& key) {
        if (key == "Apagar") {
            if (!query.empty()) query.pop_back();
        } else if (key == "Espaço") {
            if (!query.empty() && query.back() != ' ') query += ' ';
        } else if (query.size() < 30) {
            query += key;
        }
        runSearch();
    }

    // Region and app names are resolved while the catalog loads, so the new language takes effect
    // by starting Vitrine again in place (a few seconds), back on the same screen as before.
    void switchLanguage() {
        store.language = gEnglish ? "pt" : "en";
        store.savePrefs();
        saveCurrentPosition();
        if (gPreview) {
            gEnglish = !gEnglish;
            openSettings();
            return;
        }
        renderWaiting(L("Trocando o idioma…", "Switching language…"), "");
        SDL_Quit();
        execv("/proc/self/exe", gArgv);
        running = false;  // exec failed: leave, Puppy comes back
    }

    void ask(const std::string& question, std::function<void()> action) {
        confirmQuestion = question;
        confirmAction = std::move(action);
        confirmYes = false;
        screen = Screen::Confirm;
        dirty = true;
    }

    // -- input ----------------------------------------------------------------

    enum class Key { None, Up, Down, Left, Right, A, B, X, Y, L1, R1, L2, R2, Start, Select };

    Key held = Key::None;  // direction being held, for auto-repeat
    Uint32 heldSince = 0, nextRepeat = 0;
    bool triggerDown[2] = {false, false};  // L2/R2 come as trigger axes
    static const char* portHint() {
        return L("Para sair: o menu do próprio jogo, ou SELECT+START\n"
                 "Se travar: FN+START+SELECT fecha o jogo e volta para cá",
                 "To quit: the game's own menu, or SELECT+START\n"
                 "If it freezes: FN+START+SELECT closes it and comes back here");
    }
    static const char* returnHint() {
        return L("FN abre o menu do jogo: Sair volta para cá\n"
                 "SELECT+R1 salva  ·  SELECT+L1 carrega  ·  SELECT+esquerda/direita troca o espaço\n"
                 "SELECT+L2 (segurando) volta no tempo  ·  SELECT+R2 acelera",
                 "FN opens the game menu: Quit comes back here\n"
                 "SELECT+R1 save  ·  SELECT+L1 load  ·  SELECT+left/right change slot\n"
                 "SELECT+L2 (hold) rewind  ·  SELECT+R2 fast-forward");
    }

    // Lines of the "Atalhos dos jogos" screen.
    std::vector<std::pair<std::string, std::string>> shortcutLines() const {
        return {{"FN", L("Menu do jogo (Sair, Reiniciar, opções)", "Game menu (quit, restart, options)")},
                {"SELECT + R1", L("Salvar o jogo neste ponto", "Save state")},
                {"SELECT + L1", L("Carregar o ponto salvo", "Load state")},
                {L("SELECT + esq./dir.", "SELECT + left/right"), L("Trocar o espaço de salvamento", "Change save slot")},
                {"SELECT + L2", L("Voltar no tempo (segure)  ·  NES, GB, MS, GG, PCE, Atari", "Rewind (hold)  ·  NES, GB, MS, GG, PCE, Atari")},
                {"SELECT + R2", L("Acelerar (liga e desliga)", "Fast-forward (toggle)")},
                {"FN + START + SELECT", L("Fechar o jogo na hora (sem salvar)", "Quit immediately (without saving)")}};
    }

    // Stable names for cards in the saved position (indexes change when systems come and go).
    std::string cardKey(int card) const {
        if (card == kCardFavorites) return "@favoritos";
        if (card == kCardRecents) return "@recentes";
        if (const Collection* c = collectionOf(card)) return c->key;
        return catalog.systems[card].archiveName;
    }

    bool cardFromKey(const std::string& key, int& card) const {
        if (key == "@favoritos") { card = kCardFavorites; return true; }
        if (key == "@recentes") { card = kCardRecents; return true; }
        for (const auto& c : kCollections)
            if (key == c.key) { card = c.card; return true; }
        for (int i = 0; i < (int)catalog.systems.size(); ++i)
            if (catalog.systems[i].archiveName == key) { card = i; return true; }
        return false;
    }

    void saveCurrentPosition() {
        Screen base = screen == Screen::List ? Screen::List : beforeOverlay;
        if (base == Screen::List && !list.items.empty()) {
            Game* g = list.items[std::clamp(list.cursor, 0, (int)list.items.size() - 1)];
            store.savePosition("lista", cardKey(list.card), g->path);
        } else {
            auto cs = cards();
            store.savePosition("inicio", cardKey(cs[std::clamp(homeCard, 0, (int)cs.size() - 1)]), "");
        }
    }

    void handle(Key k) {
        dirty = true;
        switch (screen) {
            case Screen::Home: return homeKey(k);
            case Screen::List: return listKey(k);
            case Screen::Settings: return settingsKey(k);
            case Screen::Confirm: return confirmKey(k);
            case Screen::Message:
                if (k == Key::A || k == Key::B || k == Key::Start) screen = Screen::Settings;
                return;
            case Screen::Cheats: return cheatsKey(k);
            case Screen::Search: return searchKey(k);
            case Screen::Notice:
                if (k == Key::A || k == Key::B) screen = beforeOverlay;
                return;
            case Screen::Stats:
                if (k == Key::Down) ++statsScroll;
                else if (k == Key::Up) statsScroll = std::max(0, statsScroll - 1);
                else if (k == Key::A || k == Key::B || k == Key::Start) screen = Screen::Settings;
                return;
            case Screen::Power: return;
        }
    }

    void homeKey(Key k) {
        auto cs = cards();
        int n = (int)cs.size();
        // Streaming moves sideways and opens with Down; Estante is a vertical list that opens
        // with Right; the TV changes channel with any direction.
        bool estante = theme() == "estante", tubo = theme() == "tubo";
        bool prev = k == (estante ? Key::Up : Key::Left) || (tubo && k == Key::Up);
        bool next = k == (estante ? Key::Down : Key::Right) || (tubo && k == Key::Down);
        bool open = k == Key::A || (!estante && !tubo && k == Key::Down) || (estante && k == Key::Right);
        if (prev || next) {
            homeCard = (homeCard + (next ? 1 : n - 1)) % n;
            lastMoveAt = SDL_GetTicks();
            return;
        }
        if (open) return openList(cs[homeCard]);
        switch (k) {
            case Key::Select: cycleTheme(); break;
            case Key::X: openSearch(); break;
            case Key::Y:
                if (Game* g = lastPlayed(-1)) launch(g);
                break;
            case Key::Start: openSettings(); break;
            default: break;
        }
    }

    void moveCursor(int delta) {
        int n = (int)list.items.size();
        if (n == 0) return;
        list.cursor = std::clamp(list.cursor + delta, 0, n - 1);
        lastMoveAt = SDL_GetTicks();
    }

    static char letterOf(const Game* g) {
        char c = g->sortKey.empty() ? '#' : (char)std::toupper((unsigned char)g->sortKey[0]);
        return std::isalpha((unsigned char)c) ? c : '#';
    }

    void jumpLetter(int dir) {
        int n = (int)list.items.size();
        if (n == 0) return;
        char current = letterOf(list.items[list.cursor]);
        int i = list.cursor;
        if (dir > 0) {
            while (i < n && letterOf(list.items[i]) == current) ++i;
            list.cursor = std::min(i, n - 1);
        } else {
            while (i > 0 && letterOf(list.items[i - 1]) == current) --i;  // start of this letter
            if (i == list.cursor && i > 0) {                                // already there: previous letter
                char prev = letterOf(list.items[i - 1]);
                while (i > 0 && letterOf(list.items[i - 1]) == prev) --i;
            }
            list.cursor = i;
        }
        lastMoveAt = SDL_GetTicks();
    }

    void listKey(Key k) {
        switch (k) {
            case Key::Up: moveCursor(-1); break;
            case Key::Down: moveCursor(1); break;
            case Key::Left: moveCursor(-10); break;
            case Key::Right: moveCursor(10); break;
            case Key::L2: case Key::R2:
                if (list.card >= 0) jumpLetter(k == Key::R2 ? 1 : -1);  // collections are not alphabetical
                break;
            case Key::L1: case Key::R1: {
                if (theme() == "estante") {  // the tab strip: previous or next console
                    auto cs = cards();
                    int idx = (int)(std::find(cs.begin(), cs.end(), list.card) - cs.begin());
                    int n = (int)cs.size();
                    homeCard = (idx + (k == Key::R1 ? 1 : n - 1)) % n;
                    openList(cs[homeCard]);
                    break;
                }
                if (theme() == "tubo") {
                    if (list.card >= 0) jumpLetter(k == Key::R1 ? 1 : -1);
                    break;
                }
                if (list.card < 0) break;  // collections have no filters
                int f = ((int)list.filter + (k == Key::R1 ? 1 : 2)) % 3;
                list.filter = (Filter)f;
                rebuildList();
                break;
            }
            case Key::A: launch(selectedGame()); break;
            case Key::X:
                if (Game* g = selectedGame()) {
                    int at = list.cursor;
                    store.toggleFavorite(g->path);
                    if (list.card == kCardFavorites || list.filter == Filter::Favorites) {
                        rebuildList();
                        // The game left this list: stay on the one that took its place.
                        list.cursor = std::clamp(at, 0, std::max(0, (int)list.items.size() - 1));
                    }
                }
                break;
            case Key::Select:
                if (Game* g = selectedGame(); g && catalog.hasCheats(*g)) openCheats(g);
                break;
            case Key::Y: {
                if (theme() != "streaming") {  // filters live on Y in the other layouts
                    if (list.card >= 0) {
                        list.filter = (Filter)(((int)list.filter + 1) % 3);
                        rebuildList();
                    }
                    break;
                }
                auto cs = cards();
                int idx = (int)(std::find(cs.begin(), cs.end(), list.card) - cs.begin());
                homeCard = (idx + 1) % (int)cs.size();
                openList(cs[homeCard]);
                break;
            }
            case Key::B: {
                auto cs = cards();
                homeCard = (int)(std::find(cs.begin(), cs.end(), list.card) - cs.begin());
                screen = Screen::Home;
                break;
            }
            case Key::Start: openSettings(); break;
            default: break;
        }
    }

    void settingsKey(Key k) {
        int n = (int)settings.size();
        switch (k) {
            case Key::Up: settingsCursor = (settingsCursor + n - 1) % n; break;
            case Key::Down: settingsCursor = (settingsCursor + 1) % n; break;
            case Key::A: settings[settingsCursor].run(); break;
            case Key::B: case Key::Start: screen = beforeOverlay; break;
            default: break;
        }
    }

    void confirmKey(Key k) {
        switch (k) {
            case Key::Left: confirmYes = false; break;
            case Key::Right: confirmYes = true; break;
            case Key::A:
                if (confirmYes) confirmAction();
                else screen = Screen::Settings;
                break;
            case Key::B: screen = Screen::Settings; break;
            default: break;
        }
    }

    void cheatsKey(Key k) {
        int n = (int)cheatRows.size();
        if (n == 0 && k != Key::B && k != Key::Select) return;
        switch (k) {
            case Key::Up: cheatsCursor = (cheatsCursor + n - 1) % n; break;
            case Key::Down: cheatsCursor = (cheatsCursor + 1) % n; break;
            case Key::Left: cheatsCursor = std::max(0, cheatsCursor - 10); break;
            case Key::Right: cheatsCursor = std::min(n - 1, cheatsCursor + 10); break;
            case Key::A: {
                const CheatEntry& e = cheatRows[cheatsCursor];
                if (e.kind == 'L') store.setLives(cheatsGame->path, !store.livesOn(cheatsGame->path));
                else store.toggleCheat(cheatsGame->path, e.desc);
                break;
            }
            case Key::B: case Key::Select: screen = beforeOverlay; break;
            default: break;
        }
    }

    void searchKey(Key k) {
        const auto& rows = keyboard();
        if (resultsFocus) {
            int n = (int)found.size();
            switch (k) {
                case Key::Up: foundCursor = std::max(0, foundCursor - 1); break;
                case Key::Down:  // the keyboard sits under the list
                    if (foundCursor >= n - 1) { resultsFocus = false; keyRow = 0; }
                    else ++foundCursor;
                    break;
                case Key::Left: foundCursor = std::max(0, foundCursor - 6); break;
                case Key::Right: foundCursor = std::min(n - 1, foundCursor + 6); break;
                case Key::A:
                    if (n > 0) launch(found[foundCursor]);
                    break;
                case Key::B: resultsFocus = false; break;
                default: break;
            }
            return;
        }
        auto cell = [&] { return rows[keyRow][keyCol]; };
        switch (k) {
            case Key::Up:
                if (keyRow == 0 && !found.empty()) { resultsFocus = true; foundCursor = 0; }
                else keyRow = (keyRow + (int)rows.size() - 1) % (int)rows.size();
                break;
            case Key::Down: keyRow = (keyRow + 1) % (int)rows.size(); break;
            case Key::Left: {
                std::string here = cell();  // a wide key counts as one stop
                do keyCol = (keyCol + 9) % 10; while (rows[keyRow][keyCol] == here && here.size() > 1);
                while (keyCol > 0 && rows[keyRow][keyCol - 1] == rows[keyRow][keyCol]) --keyCol;
                break;
            }
            case Key::Right: {
                std::string here = cell();
                do keyCol = (keyCol + 1) % 10; while (rows[keyRow][keyCol] == here && here.size() > 1);
                break;
            }
            case Key::A: type(cell()); break;
            case Key::X: type("Apagar"); break;
            case Key::Y: type("Espaço"); break;
            case Key::Start: case Key::R1:
                if (!found.empty()) resultsFocus = true;
                break;
            case Key::B:
                if (!query.empty()) type("Apagar");
                else screen = Screen::Home;
                break;
            case Key::Select: screen = Screen::Home; break;
            default: break;
        }
    }

    // Key::A means "confirm" and Key::B "back"; which physical button does each follows the setting.
    Key keyFromButton(Uint8 b) const {
        if (store.confirmBottom && b == SDL_CONTROLLER_BUTTON_A) return Key::B;
        if (store.confirmBottom && b == SDL_CONTROLLER_BUTTON_B) return Key::A;
        switch (b) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP: return Key::Up;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Key::Down;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Key::Left;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Key::Right;
            case SDL_CONTROLLER_BUTTON_A: return Key::A;
            case SDL_CONTROLLER_BUTTON_B: return Key::B;
            case SDL_CONTROLLER_BUTTON_X: return Key::X;
            case SDL_CONTROLLER_BUTTON_Y: return Key::Y;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Key::L1;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Key::R1;
            case SDL_CONTROLLER_BUTTON_START: return Key::Start;
            case SDL_CONTROLLER_BUTTON_BACK: return Key::Select;
            default: return Key::None;
        }
    }

    static Key keyFromKeyboard(SDL_Keycode k) {
        switch (k) {
            case SDLK_UP: return Key::Up;
            case SDLK_DOWN: return Key::Down;
            case SDLK_LEFT: return Key::Left;
            case SDLK_RIGHT: return Key::Right;
            case SDLK_RETURN: return Key::A;
            case SDLK_BACKSPACE: case SDLK_ESCAPE: return Key::B;
            case SDLK_x: return Key::X;
            case SDLK_y: return Key::Y;
            case SDLK_q: return Key::L1;
            case SDLK_e: return Key::R1;
            case SDLK_1: return Key::L2;
            case SDLK_3: return Key::R2;
            case SDLK_s: return Key::Start;
            case SDLK_TAB: return Key::Select;
            default: return Key::None;
        }
    }

    // -- drawing --------------------------------------------------------------

    // The letter printed on the physical button that does what Key::A / Key::B do, with its colour.
    char shownLetter(char key) const {
        if (store.confirmBottom && key == 'A') return 'B';
        if (store.confirmBottom && key == 'B') return 'A';
        return key;
    }

    static SDL_Color letterColor(char letter) {
        return letter == 'A' ? kBtnA : letter == 'B' ? kBtnB : letter == 'X' ? kBtnX : kBtnY;
    }

    void hintBar(const std::vector<std::pair<std::string, std::string>>& hints, SDL_Color bg = kBar,
                 SDL_Color fg = kMuted) {
        const int y = kScreenH - 34;
        painter.fill(0, y, kScreenW, 34, bg);
        int x = 22;
        const int cy = y + 17;
        for (const auto& [key, label] : hints) {
            if (key.size() == 1 && std::string("ABXY").find(key) != std::string::npos) {
                char letter = shownLetter(key[0]);
                painter.buttonGlyph(letter, letterColor(letter), x, cy);
                x += 26;
            } else {
                x += painter.keyGlyph(key, x, cy, fg, bg) + 6;
            }
            x += painter.textMid(kFont12, false, label, x, cy, fg) + 18;
        }
    }

    void pill(const std::string& label, char button, SDL_Color buttonColor, bool solid, int x, int y, int maxW = 0) {
        const int h = 32;
        int textW = painter.width(kFont13, true, label);
        if (maxW > 0) textW = std::min(textW, maxW - 54);
        int w = textW + (button ? 54 : 28);
        if (solid) painter.roundRect(x, y, w, h, h / 2, kText);
        else painter.roundOutline(x, y, w, h, h / 2, 1, hexColor(0x5A6070), kBg);
        int tx = x + 14;
        if (button) {
            char letter = shownLetter(button);
            painter.buttonGlyph(letter, letter == button ? buttonColor : letterColor(letter), tx, y + h / 2, 18);
            tx += 26;
        }
        painter.textMid(kFont13, true, label, tx, y + h / 2, solid ? kBg : kText, textW);
    }

    void drawHero(const std::string& picture, int x, int y, int w, int h, float leftFade) {
        if (picture.empty() || !painter.picture(picture, x, y, w, h)) return;
        painter.heroFades(x, y, w, h, leftFade);
    }

    const std::string& theme() const { return store.theme; }

    static std::string themeName(const std::string& t) {
        return t == "estante" ? L("Estante", "Shelf") : t == "tubo" ? L("TV de tubo", "CRT TV") : "Streaming";
    }

    void cycleTheme() {
        static const char* kOrder[] = {"streaming", "estante", "tubo"};
        int i = 0;
        while (i < 3 && theme() != kOrder[i]) ++i;
        store.setTheme(kOrder[(i + 1) % 3]);
        dirty = true;
    }

    void drawHome() {
        if (theme() == "estante") return drawHomeEstante();
        if (theme() == "tubo") return drawHomeTubo();
        drawHomeStreaming();
    }

    void drawList() {
        if (theme() == "estante") return drawListEstante();
        if (theme() == "tubo") return drawListTubo();
        drawListStreaming();
    }

    // Up to n pictures for a card: collections in their order, systems from the top of the list.
    std::vector<std::string> picturesForCard(int card, size_t n) {
        std::vector<std::string> out;
        if (card < 0) {
            auto games = gamesOf(card);
            for (size_t i = 0; i < games.size() && i < 400 && out.size() < n; ++i)
                if (!catalog.iconFor(*games[i]).empty()) out.push_back(games[i]->iconPath);
            return out;
        }
        auto it = systemPictures.find(card);
        if (it != systemPictures.end()) return it->second;
        auto& games = catalog.systems[card].games;
        for (size_t i = 0; i < games.size() && i < 400 && out.size() < n; ++i)
            if (!catalog.iconFor(games[i]).empty()) out.push_back(games[i].iconPath);
        return systemPictures[card] = out;
    }

    SDL_Color cardColor(int card) const {
        if (card == kCardFavorites) return kStar;
        if (card == kCardRecents) return hexColor(0xA9AFB8);
        if (const Collection* c = collectionOf(card)) return hexColor(c->color);
        return catalog.systems[card].color;
    }

    static std::string twoDigits(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

    static std::string fileName(const std::string& path) { return path.substr(path.find_last_of('/') + 1); }

    // -- Estante: a shelf of consoles on the left, a preview panel on the right ----

    void drawHomeEstante() {
        const SDL_Color bg = hexColor(0x121417), panel = hexColor(0x1C2026), text = hexColor(0xF2F2F0),
                        muted = hexColor(0xA9AFB8), line = hexColor(0x262B33), ink = hexColor(0x121417);
        painter.fill(0, 0, kScreenW, kScreenH, bg);
        auto cs = cards();
        int n = (int)cs.size(), card = cs[homeCard];
        int tx = 20 + painter.text(kFont18, true, "Consoles", 20, 9, text);
        painter.text(kFont13, false, formatCount(catalog.systems.size()) + L(" sistemas · ", " systems · ") +
                     formatCount(catalog.totalGames) + L(" jogos", " games"), tx + 10, 13, muted);
        painter.battery(battery, kScreenW - 18, 20);

        const int top = 44, rowH = 29, visible = 13, lx = 12, lw = 240;
        int first = std::clamp(homeCard - 6, 0, std::max(0, n - visible));
        for (int i = first; i < n && i < first + visible; ++i) {
            int c = cs[i], y = top + (i - first) * (rowH + 1);
            bool sel = i == homeCard;
            if (sel) painter.roundRect(lx, y, lw, rowH, 8, cardColor(c));
            SDL_Color fg = sel ? ink : text;
            int nx = lx + 10;
            if (c == kCardFavorites) {
                painter.star(nx + 7, y + rowH / 2, 7, sel ? ink : kStar);
                nx += 20;
            } else if (c == kCardRecents) {
                painter.roundOutline(nx + 1, y + rowH / 2 - 7, 14, 14, 7, 2, sel ? ink : muted, sel ? cardColor(c) : bg);
                painter.fill(nx + 7, y + rowH / 2 - 4, 2, 5, sel ? ink : muted);
                nx += 20;
            }
            std::string count = c == kCardFavorites ? "" : formatCount(gamesOf(c).size());
            int cw = painter.width(kFont13, false, count);
            painter.textMid(kFont15, sel || c < 0, cardName(c), nx, y + rowH / 2, fg, lx + lw - nx - cw - 18);
            if (!count.empty()) painter.textMid(kFont13, false, count, lx + lw - 10 - cw, y + rowH / 2, sel ? ink : muted);
            if (c == kCardRecents || (c < 0 && i + 1 < n && cs[i + 1] >= 0) || (c >= 0 && i + 1 < n && cs[i + 1] < 0))
                painter.fill(lx, y + rowH, lw, 1, line);  // separates collections from consoles
        }

        const int px = 260, py = 44, pw = kScreenW - px - 16, ph = kScreenH - py - 36 - 10;
        painter.roundRect(px, py, pw, ph, 14, panel);
        painter.text(kFont28, true, cardName(card), px + 16, py + 12, text, pw - 32);
        std::string sub;
        if (card == kCardFavorites) sub = store.favorites.empty() ? L("Aperte X num jogo para guardar aqui", "Press X on a game to keep it here")
                                                                 : plural(gamesOf(card).size(), L("favorito", "favorite"), L("favoritos", "favorites"));
        else if (card == kCardRecents) sub = store.recents.empty() ? L("Os jogos que você abrir aparecem aqui", "Games you open will show up here")
                                                                  : plural(gamesOf(card).size(), L("jogo recente", "recent game"), L("jogos recentes", "recent games"));
        else if (card < 0) sub = plural(gamesOf(card).size(), L("jogo", "game"), L("jogos", "games")) + L(" de todos os consoles", " from all consoles");
        else {
            const System& sys = catalog.systems[card];
            sub = plural(sys.games.size(), L("jogo", "game"), L("jogos", "games"));
            if (sys.ptbrCount) sub += " · " + formatCount(sys.ptbrCount) + L(" em português", " in Portuguese");
        }
        painter.text(kFont14, false, sub, px + 16, py + 50, muted, pw - 32);
        bool settled = gScreenshot || SDL_GetTicks() - lastMoveAt >= kHeroSettleMs;
        const int gw = (pw - 32 - 8) / 2, gh = gw * 3 / 4;
        auto pics = settled ? picturesForCard(card, 4) : std::vector<std::string>{};
        for (int k = 0; k < 4; ++k) {
            int gx = px + 16 + (k % 2) * (gw + 8), gy = py + 80 + (k / 2) * (gh + 8);
            if (k < (int)pics.size() && painter.picture(pics[k], gx, gy, gw, gh)) painter.roundCorners(gx, gy, gw, gh, 8, panel);
            else painter.roundRect(gx, gy, gw, gh, 8, line);
        }
        if (Game* g = lastPlayed(card < 0 ? -1 : card)) {
            int lx2 = px + 16 + painter.text(kFont13, false, L("Último jogado: ", "Last played: "), px + 16, py + ph - 28, muted);
            painter.text(kFont13, true, g->title, lx2, py + ph - 28, text, px + pw - 16 - lx2);
        }
        hintBar({{"^v", L("Escolher", "Move")}, {"A", L("Abrir", "Open")}, {"X", L("Buscar", "Search")}, {"SELECT", L("Aparência", "Layout")}, {"START", L("Ajustes", "Settings")}},
                hexColor(0x0C0E10), muted);
    }

    void drawListEstante() {
        const SDL_Color bg = hexColor(0x121417), panel = hexColor(0x1C2026), text = hexColor(0xF2F2F0),
                        muted = hexColor(0xA9AFB8), line = hexColor(0x262B33), ink = hexColor(0x121417),
                        chipBg = hexColor(0x2A3A2E), chipFg = hexColor(0x9FE0A8);
        painter.fill(0, 0, kScreenW, kScreenH, bg);
        auto cs = cards();
        int n = (int)cs.size();
        int idx = (int)(std::find(cs.begin(), cs.end(), list.card) - cs.begin());
        SDL_Color accent = cardColor(list.card);

        // Tab strip: L1, previous, current (underlined), next, R1.
        int x = 14;
        x += painter.keyGlyph("L1", x, 22, muted, bg) + 10;
        x += painter.textMid(kFont14, false, cardName(cs[(idx + n - 1) % n]), x, 22, muted, 110) + 14;
        int cw = painter.textMid(kFont16, true, cardName(list.card), x, 22, text, 170);
        painter.fill(x, 38, cw, 3, accent);
        x += cw + 14;
        x += painter.textMid(kFont14, false, cardName(cs[(idx + 1) % n]), x, 22, muted, 110) + 10;
        painter.keyGlyph("R1", x, 22, muted, bg);
        std::string count = plural(list.items.size(), L("jogo", "game"), L("jogos", "games"));
        if (list.filter != Filter::All) count += " · " + filterLabel(list.filter);
        painter.text(kFont13, false, count, kScreenW - 18 - painter.width(kFont13, false, count), 14, muted);
        painter.fill(0, 44, kScreenW, 1, line);

        Game* sel = selectedGame();
        const int rx = 10, top = 52, rowH = 28, visible = 13, rw = 318;
        int total = (int)list.items.size();
        if (total == 0) {
            painter.text(kFont15, true, list.filter == Filter::Favorites || list.card == kCardFavorites
                                            ? L("Nenhum favorito ainda.", "No favorites yet.") : L("Nenhum jogo aqui.", "No games here."), rx + 10, top + 16, text);
        }
        int first = std::clamp(list.cursor - 6, 0, std::max(0, total - visible));
        for (int i = first; i < total && i < first + visible; ++i) {
            Game* g = list.items[i];
            int y = top + (i - first) * (rowH + 1);
            bool isSel = i == list.cursor, fav = store.isFavorite(g->path);
            if (isSel) painter.roundRect(rx, y, rw, rowH, 8, accent);
            std::string suffix = list.card < 0 ? catalog.systems[g->systemIndex].display
                                               : g->duplicateTitle ? g->details : "";
            int tagW = g->ptbr ? painter.width(kFont9, true, "PT-BR") + 14 : 0;
            int avail = rw - 20 - tagW - (fav ? 18 : 0);
            if (!suffix.empty()) suffix = painter.fit(kFont12, false, suffix, avail * 45 / 100);
            int sw = suffix.empty() ? 0 : painter.width(kFont12, false, suffix) + 8;
            int ax = rx + 10 + painter.textMid(kFont15, isSel, g->title, rx + 10, y + rowH / 2, isSel ? ink : text,
                                               std::max(1, avail - sw)) + 8;
            if (sw) ax += painter.textMid(kFont12, false, suffix, ax, y + rowH / 2, isSel ? ink : muted) + 8;
            if (tagW) {
                painter.roundRect(ax, y + rowH / 2 - 8, tagW - 4, 16, 4, isSel ? ink : chipBg);
                painter.textMid(kFont9, true, "PT-BR", ax + 5, y + rowH / 2, isSel ? accent : chipFg);
                ax += tagW;
            }
            if (fav) painter.star(ax + 6, y + rowH / 2, 6, isSel ? ink : kStar);
        }

        const int px = 338, pw = 272;
        bool settled = gScreenshot || SDL_GetTicks() - lastMoveAt >= kHeroSettleMs;
        if (sel) {
            const System& sys = catalog.systems[sel->systemIndex];
            const std::string& pic = settled ? catalog.iconFor(*sel) : std::string();
            int ph = pw * 3 / 4;
            if (!pic.empty() && painter.picture(pic, px, top + 2, pw, ph)) {
                painter.roundCorners(px, top + 2, pw, ph, 10, bg);
            } else {
                painter.roundRect(px, top + 2, pw, ph, 10, panel);
                painter.textMid(kFont16, true, sys.display, px + 18, top + 2 + ph / 2, muted, pw - 36);
            }
            int ty = top + ph + 14;
            painter.text(kFont24, true, sel->title, px, ty, text, pw);
            std::string meta = sys.display + (sel->details.empty() ? "" : " · " + sel->details);
            painter.text(kFont14, false, meta, px, ty + 34, muted, pw);
            int ly = ty + 58;
            std::string sheet = sheetLine(*sel);
            if (!sheet.empty()) { painter.text(kFont13, false, sheet, px, ly, text, pw); ly += 20; }
            if (!sel->developer.empty()) { painter.text(kFont12, false, sel->developer, px, ly, muted, pw); ly += 20; }
            std::string played = playLine(*sel);
            if (!played.empty()) { painter.text(kFont12, false, played, px, ly, text, pw); ly += 20; }
            bool on = false;
            std::string cheats = settled ? cheatsStatus(*sel, on) : "";
            if (!cheats.empty()) { painter.text(kFont12, on, upperFirst(cheats), px, ly, on ? chipFg : muted, pw); ly += 20; }
            painter.text(kFont11, false, fileName(sel->path), px, ly, hexColor(0x6B727C), pw);
        }

        // Alphabet rail for system lists.
        if (list.card >= 0 && sel) {
            const char* kLetters = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            char current = letterOf(sel);
            const int railTop = top + 2, railH = kScreenH - 36 - 8 - railTop, step = railH / 27;
            for (int i = 0; i < 27; ++i) {
                std::string l(1, kLetters[i]);
                int cy = railTop + i * step + step / 2;
                int lw = painter.width(kFont9, true, l);
                if (kLetters[i] == current) {
                    painter.roundRect(kScreenW - 22, cy - 6, 14, 12, 3, accent);
                    painter.textMid(kFont9, true, l, kScreenW - 15 - lw / 2, cy, ink);
                } else {
                    painter.textMid(kFont9, true, l, kScreenW - 15 - lw / 2, cy, hexColor(0x6B727C));
                }
            }
        }

        bool hasCheats = sel && catalog.hasCheats(*sel);
        if (hasCheats)
            hintBar({{"A", L("Jogar", "Play")}, {"B", L("Voltar", "Back")}, {"X", L("Favorito", "Favorite")}, {"L2 R2", L("Letra", "Jump A-Z")}, {"SELECT", L("Trapaças", "Cheats")}},
                    hexColor(0x0C0E10), muted);
        else
            hintBar({{"A", L("Jogar", "Play")}, {"B", L("Voltar", "Back")}, {"X", L("Favorito", "Favorite")}, {"L1 R1", "Console"}, {"Y", L("Filtro", "Filter")}},
                    hexColor(0x0C0E10), muted);
    }

    // -- TV de tubo: every console is a channel on an old TV --------------------

    void drawTvSet(int x, int y, int w, int h, int bezel, int radius, const std::string& picture, bool knobs) {
        const SDL_Color body = hexColor(0x2E2721);
        painter.roundRect(x, y + 10, w, h, radius + 4, hexColor(0x120D0A));
        painter.roundRect(x, y, w, h, radius + 4, body);
        int sw = w - 2 * bezel - (knobs ? 62 + 16 : 0), sh = h - 2 * bezel;
        int sx = x + bezel, sy = y + bezel;
        painter.fill(sx, sy, sw, sh, hexColor(0x000000));
        if (!picture.empty()) painter.picture(picture, sx, sy, sw, sh);
        painter.scanlines(sx, sy, sw, sh, 70);
        painter.vignette(sx, sy, sw, sh, 170);
        tvScreen = {sx, sy, sw, sh};
        if (knobs) {
            int kx = sx + sw + 16;
            painter.roundRect(kx + 9, sy + 10, 44, 44, 22, hexColor(0x5C5047));
            painter.roundRect(kx + 12, sy + 13, 38, 38, 19, hexColor(0x4A4038));
            painter.fill(kx + 29, sy + 16, 4, 16, hexColor(0xC4B596));
            painter.roundRect(kx + 13, sy + 80, 36, 36, 18, hexColor(0x5C5047));
            painter.roundRect(kx + 16, sy + 83, 30, 30, 15, hexColor(0x4A4038));
            for (int i = 0; i < 5; ++i) painter.roundRect(kx + 11, sy + sh - 52 + i * 9, 40, 3, 1, hexColor(0x120D0A));
        }
    }

    // Rounds the screen corners last, over whatever was drawn on the screen.
    void finishTvScreen(int radius) {
        painter.roundCorners(tvScreen.x, tvScreen.y, tvScreen.w, tvScreen.h, radius, hexColor(0x2E2721));
    }

    void drawHomeTubo() {
        const SDL_Color bg = hexColor(0x1A1410), cream = hexColor(0xF3E9D2), muted = hexColor(0xC4B596),
                        green = hexColor(0x7CFF8A), orange = hexColor(0xFF9F1C), chip = hexColor(0x2E2721);
        painter.fill(0, 0, kScreenW, kScreenH, bg);
        auto cs = cards();
        int n = (int)cs.size(), card = cs[homeCard];
        painter.retro(kFont24, L("CANAIS", "CHANNELS"), 20, 6, muted);
        painter.battery(battery, kScreenW - 18, 20);

        bool settled = gScreenshot || SDL_GetTicks() - lastMoveAt >= kHeroSettleMs;
        drawTvSet(70, 44, 500, 300, 18, 26, settled ? heroForCard(card) : "", true);
        const SDL_Rect sc = tvScreen;
        painter.shadeBottom(sc.x, sc.y, sc.w, sc.h, 0.4f, 230);
        std::string ch = "CH " + twoDigits(homeCard);
        painter.retro(kFont34, ch, sc.x + sc.w - 16 - painter.retroWidth(kFont34, ch), sc.y + 8, green);
        painter.retro(kFont46, upper(cardName(card)), sc.x + 18, sc.y + sc.h - 82, cream, sc.w - 36);
        std::string sub = card < 0 ? plural(gamesOf(card).size(), L("jogo", "game"), L("jogos", "games"))
                                   : plural(catalog.systems[card].games.size(), L("jogo", "game"), L("jogos", "games")) +
                                         (catalog.systems[card].ptbrCount
                                              ? " · " + formatCount(catalog.systems[card].ptbrCount) + L(" em português", " in Portuguese") : "");
        painter.text(kFont14, false, sub, sc.x + 18, sc.y + sc.h - 30, cream, sc.w - 36);
        finishTvScreen(22);

        // Channel strip.
        const int cy = 386;
        painter.triangle(18, cy, 10, true, muted);
        int x = 32, firstChip = std::max(0, homeCard - 1);
        for (int i = firstChip; i < n; ++i) {
            int c = cs[i];
            bool sel = i == homeCard;
            std::string label = twoDigits(i) + " " + upper(cardName(c));
            int starW = c == kCardFavorites ? 16 : 0;
            int w = painter.retroWidth(kFont21, label) + 20 + starW;
            if (x + w > kScreenW - 30) break;
            painter.roundRect(x, cy - 14, w, 28, 6, sel ? orange : chip);
            if (starW) painter.star(x + 16, cy, 6, sel ? bg : kStar);
            painter.retro(kFont21, label, x + 10 + starW, cy - 12, sel ? bg : muted);
            x += w + 8;
        }
        painter.triangle(kScreenW - 18, cy, 10, false, muted);
        hintBar({{"<>", L("Mudar canal", "Change channel")}, {"A", L("Ligar", "Open")}, {"X", L("Buscar", "Search")}, {"SELECT", L("Aparência", "Layout")}, {"START", L("Ajustes", "Settings")}},
                hexColor(0x110D0A), muted);
    }

    void drawListTubo() {
        const SDL_Color bg = hexColor(0x1A1410), cream = hexColor(0xF3E9D2), muted = hexColor(0xC4B596),
                        green = hexColor(0x7CFF8A), orange = hexColor(0xFF9F1C), panel = hexColor(0x221B16),
                        marker = hexColor(0x8C7E66);
        painter.fill(0, 0, kScreenW, kScreenH, bg);
        auto cs = cards();
        int idx = (int)(std::find(cs.begin(), cs.end(), list.card) - cs.begin());
        int hx = 18 + painter.retro(kFont26, "CH " + twoDigits(idx), 18, 4, green) + 12;
        painter.retro(kFont26, upper(cardName(list.card)), hx, 4, cream, 360);
        std::string count = plural(list.items.size(), L("jogo", "game"), L("jogos", "games"));
        if (list.filter != Filter::All) count += " · " + filterLabel(list.filter);
        painter.text(kFont13, false, count, kScreenW - 18 - painter.width(kFont13, false, count), 13, muted);

        Game* sel = selectedGame();
        bool settled = gScreenshot || SDL_GetTicks() - lastMoveAt >= kHeroSettleMs;
        drawTvSet(14, 44, 286, 232, 12, 20, sel && settled ? catalog.iconFor(*sel) : "", false);
        if (sel && settled && catalog.iconFor(*sel).empty()) {
            int w = painter.retroWidth(kFont28, L("SEM IMAGEM", "NO PICTURE"));
            painter.retro(kFont28, L("SEM IMAGEM", "NO PICTURE"), tvScreen.x + (tvScreen.w - w) / 2, tvScreen.y + tvScreen.h / 2 - 14, muted);
        }
        finishTvScreen(18);
        if (sel) {
            const System& sys = catalog.systems[sel->systemIndex];
            painter.retro(kFont40, upper(sel->title), 18, 286, cream, 282);
            std::string meta = (list.card < 0 ? sys.display + " · " : "") + (sel->details.empty() ? fileName(sel->path) : sel->details);
            painter.text(kFont14, false, meta, 18, 326, muted, 282);
            int ly = 348;
            std::string sheet = sheetLine(*sel);
            if (!sheet.empty()) { painter.text(kFont13, false, sheet, 18, ly, cream, 282); ly += 19; }
            if (!sel->developer.empty()) { painter.text(kFont13, false, sel->developer, 18, ly, muted, 282); ly += 19; }
            std::string played = playLine(*sel);
            if (!played.empty()) { painter.text(kFont13, false, played, 18, ly, cream, 282); ly += 19; }
            bool on = false;
            std::string cheats = settled ? cheatsStatus(*sel, on) : "";
            if (!cheats.empty()) painter.text(kFont13, on, upperFirst(cheats), 18, ly, on ? green : muted, 282);
        }

        const int lx = 314, ly = 44, lw = kScreenW - lx - 14, lh = kScreenH - 36 - 6 - ly;
        painter.roundRect(lx, ly, lw, lh, 12, panel);
        const int rowH = 27, visible = (lh - 16) / (rowH + 1);
        int total = (int)list.items.size();
        if (total == 0)
            painter.text(kFont15, true, list.filter == Filter::Favorites || list.card == kCardFavorites
                                            ? L("Nenhum favorito ainda.", "No favorites yet.") : L("Nenhum jogo aqui.", "No games here."), lx + 16, ly + 18, cream);
        int first = std::clamp(list.cursor - visible / 2, 0, std::max(0, total - visible));
        for (int i = first; i < total && i < first + visible; ++i) {
            Game* g = list.items[i];
            int y = ly + 8 + (i - first) * (rowH + 1), cy = y + rowH / 2;
            bool isSel = i == list.cursor, fav = store.isFavorite(g->path);
            if (isSel) painter.roundRect(lx + 6, y, lw - 12, rowH, 7, orange);
            int tx = lx + 6 + 36;
            if (isSel) painter.triangle(lx + 24, cy, 10, false, bg);
            else if (list.card >= 0 && (i == first || letterOf(list.items[i - 1]) != letterOf(g)))
                painter.retro(kFont18, std::string(1, letterOf(g)), lx + 16, y + 3, marker);
            std::string suffix = list.card < 0 ? catalog.systems[g->systemIndex].display
                                               : g->duplicateTitle ? g->details : "";
            int tagW = g->ptbr ? painter.width(kFont9, true, "PT") + 14 : 0;
            int avail = lx + lw - 14 - tx - tagW - (fav ? 16 : 0);
            if (!suffix.empty()) suffix = painter.fit(kFont12, false, suffix, avail * 45 / 100);
            int sw = suffix.empty() ? 0 : painter.width(kFont12, false, suffix) + 8;
            int ax = tx + painter.textMid(kFont15, isSel, g->title, tx, cy, isSel ? bg : cream, std::max(1, avail - sw)) + 8;
            if (sw) ax += painter.textMid(kFont12, false, suffix, ax, cy, isSel ? bg : muted) + 8;
            if (tagW) {
                painter.roundOutline(ax, cy - 8, tagW - 4, 16, 4, 1, isSel ? bg : orange, isSel ? orange : panel);
                painter.textMid(kFont9, true, "PT", ax + 5, cy, isSel ? bg : orange);
                ax += tagW;
            }
            if (fav) painter.star(ax + 6, cy, 6, isSel ? bg : kStar);
        }

        bool hasCheats = sel && catalog.hasCheats(*sel);
        std::vector<std::pair<std::string, std::string>> hints = {{"A", L("Jogar", "Play")}, {"B", L("Canais", "Channels")}, {"X", L("Favorito", "Favorite")}};
        if (hasCheats) hints.push_back({"SELECT", L("Trapaças", "Cheats")});
        else hints.push_back({"L1 R1", L("Pular letra", "Skip letter")});
        hints.push_back({"Y", L("Filtro", "Filter")});
        hintBar(hints, hexColor(0x110D0A), muted);
    }

    SDL_Rect tvScreen{0, 0, 0, 0};  // screen area of the last TV drawn

    void drawHomeStreaming() {
        auto cs = cards();
        homeCard = std::clamp(homeCard, 0, (int)cs.size() - 1);
        int card = cs[homeCard];
        bool settled = gScreenshot || SDL_GetTicks() - lastMoveAt >= kHeroSettleMs;
        if (settled) drawHero(heroForCard(card), 200, 0, 440, 330, 0.7f);

        painter.battery(battery, kScreenW - 18, 20);

        const int left = 28;
        painter.text(kFont40, true, cardName(card), left, 66, kText, 360);
        std::string sub;
        if (card == kCardFavorites) {
            sub = store.favorites.empty() ? L("Aperte X num jogo para guardar aqui", "Press X on a game to keep it here")
                                          : plural(gamesOf(card).size(), L("jogo favorito", "favorite game"), L("jogos favoritos", "favorite games"));
        } else if (card == kCardRecents) {
            sub = store.recents.empty() ? L("Os jogos que você abrir aparecem aqui", "Games you open will show up here")
                                        : plural(gamesOf(card).size(), L("jogo recente", "recent game"), L("jogos recentes", "recent games"));
        } else if (card < 0) {
            sub = plural(gamesOf(card).size(), L("jogo", "game"), L("jogos", "games")) + L(" de todos os consoles", " from all consoles");
        } else {
            const System& sys = catalog.systems[card];
            sub = plural(sys.games.size(), L("jogo", "game"), L("jogos", "games"));
            if (sys.ptbrCount) sub += " · " + formatCount(sys.ptbrCount) + L(" em português", " in Portuguese");
        }
        painter.text(kFont14, false, sub, left, 120, kMuted, 380);

        int px = left;
        pill(L("Ver jogos", "View games"), 'A', kBtnA, true, px, 156);
        px += painter.width(kFont13, true, L("Ver jogos", "View games")) + 54 + 10;
        if (Game* g = lastPlayed(-1)) pill(L("Continuar ", "Continue ") + g->title, 'Y', kBtnY, false, px, 156, 300);

        painter.text(kFont13, true, L("Consoles e coleções", "Consoles & collections"), left, 312, kMuted);
        drawCards(cs, card);

        std::vector<std::pair<std::string, std::string>> hints = {{"<>", L("Escolher", "Move")}, {"A", L("Ver jogos", "View games")}};
        if (lastPlayed(-1)) hints.push_back({"Y", L("Continuar", "Continue")});
        hints.push_back({"X", L("Buscar", "Search")});
        hints.push_back({"START", L("Ajustes", "Settings")});
        hintBar(hints);
    }

    void drawCards(const std::vector<int>& cs, int selectedCard) {
        const int cardW = 112, cardH = 64, selW = 140, selH = 80, gap = 10, top = 338;
        // Keep the selected card in the third slot once the row has scrolled.
        int first = std::max(0, homeCard - 2);
        int x = 28;
        for (int i = first; i < (int)cs.size() && x < kScreenW; ++i) {
            int card = cs[i];
            bool sel = card == selectedCard;
            int w = sel ? selW : cardW, h = sel ? selH : cardH;
            int y = top + (selH - h) / 2;
            if (sel) painter.roundOutline(x, y, w, h, 12, 3, kText, kCard);
            else painter.roundRect(x, y, w, h, 10, kCard);
            int pad = sel ? 13 : 10;
            if (card == kCardFavorites) {
                painter.star(x + pad + 8, y + pad + 8, sel ? 9 : 8, kStar);
            } else if (card == kCardRecents) {
                painter.text(kFont11, false, store.recents.empty() ? L("vazio", "empty") : formatCount(gamesOf(card).size()),
                             x + pad, y + pad - 2, kMuted);
            } else {
                painter.roundRect(x + 2, y + (sel ? 3 : 0), w - 4, 4, 2, cardColor(card));
                painter.text(kFont11, false, formatCount(gamesOf(card).size()), x + pad, y + pad, kMuted);
            }
            FontId f = sel ? kFont15 : kFont13;
            painter.text(f, true, cardName(card), x + pad, y + h - pad - painter.height(f), kText, w - 2 * pad);
            x += w + gap;
        }
    }

    std::string filterLabel(Filter f) const {
        return f == Filter::All ? L("Todos", "All") : f == Filter::Favorites ? L("Favoritos", "Favorites") : "PT-BR";
    }

    void drawListStreaming() {
        Game* sel = selectedGame();
        bool settled = gScreenshot || SDL_GetTicks() - lastMoveAt >= kHeroSettleMs;
        std::string hero = sel && settled ? catalog.iconFor(*sel) : "";
        const int heroX = 240, heroW = 400, heroH = 300;
        if (!hero.empty()) {
            drawHero(hero, heroX, 0, heroW, heroH, 0.62f);
        } else if (sel && settled) {
            // No picture: a quiet block in the system colour with its name.
            const System& sys = catalog.systems[sel->systemIndex];
            painter.roundRect(heroX + 120, 40, 240, 180, 16, kCard);
            painter.roundRect(heroX + 120, 40, 240, 6, 3, sys.color);
            painter.textMid(kFont16, true, sys.display, heroX + 140, 130, kMuted, 200);
        }

        painter.battery(battery, kScreenW - 18, 20);
        std::string header = cardName(list.card);
        int hx = 22;
        hx += painter.text(kFont13, true, header, hx, 12, kMuted);
        painter.text(kFont12, false, "  ·  " + formatCount(gamesOf(list.card).size()), hx, 13, kMuted);

        if (list.card >= 0) {
            int cx = 22;
            for (Filter f : {Filter::All, Filter::Favorites, Filter::Portuguese}) {
                std::string label = filterLabel(f);
                bool active = f == list.filter;
                int starW = f == Filter::Favorites ? 15 : 0;
                int w = painter.width(kFont12, active, label) + 20 + starW;
                if (active) painter.roundRect(cx, 34, w, 24, 12, kText);
                else painter.roundOutline(cx, 34, w, 24, 12, 1, hexColor(0x3A3F4B), kBg);
                if (starW) painter.star(cx + 16, 46, 6, active ? kStarInk : kStar);
                painter.textMid(kFont12, active, label, cx + 10 + starW, 46, active ? kBg : kMuted);
                cx += w + 6;
            }
        }

        drawRows();

        if (sel) {
            const System& sys = catalog.systems[sel->systemIndex];
            const int dx = 300, maxW = kScreenW - dx - 22;
            FontId titleFont = painter.width(kFont34, true, sel->title) <= maxW ? kFont34 : kFont26;
            if (settled) drawCover(*sel, dx, 280);
            painter.text(titleFont, true, sel->title, dx, 292, kText, maxW);
            std::string meta;
            if (list.card < 0) meta = sys.display;
            if (!sel->details.empty()) meta += (meta.empty() ? "" : " · ") + sel->details;
            if (sel->ptbr) meta += (meta.empty() ? "" : " · ") + std::string(L("em português", "in Portuguese"));
            // Region and tags, the sheet, then developer and cheats: one line each, as many as exist.
            std::string sheet = sheetLine(*sel);
            if (!sheet.empty()) meta += (meta.empty() ? "" : " · ") + sheet;
            int ly = 334;
            if (!meta.empty()) { painter.text(kFont13, false, meta, dx, ly, kMuted, maxW); ly += 18; }
            bool on = false;
            std::string cheats = settled ? cheatsStatus(*sel, on) : "";
            int lx = dx;
            if (!sel->developer.empty()) lx += painter.text(kFont13, false, sel->developer, lx, ly, kMuted, maxW / 2);
            if (!cheats.empty()) {
                if (lx > dx) lx += painter.text(kFont13, false, " · ", lx, ly, kMuted);
                painter.text(kFont13, on, cheats, lx, ly, on ? kTeal : kMuted, dx + maxW - lx);
            }
            if (lx > dx || !cheats.empty()) ly += 18;
            std::string played = playLine(*sel);
            if (!played.empty()) { painter.text(kFont13, false, played, dx, ly, kText, maxW); ly += 18; }
            int py = std::max(362, ly + 8);
            pill(L("Jogar", "Play"), 'A', kBtnA, true, dx, py);
            bool fav = store.isFavorite(sel->path);
            pill(fav ? L("Nos favoritos", "Favorited") : L("Favoritar", "Favorite"), 'X', kBtnX, false,
                 dx + painter.width(kFont13, true, L("Jogar", "Play")) + 54 + 8, py);
        }

        bool hasCheats = sel && catalog.hasCheats(*sel);
        if (list.card >= 0 && hasCheats)
            hintBar({{"B", L("Voltar", "Back")}, {"L1 R1", L("Filtro", "Filter")}, {"L2 R2", L("Letra", "Jump A-Z")}, {"SELECT", L("Trapaças", "Cheats")}});
        else if (list.card >= 0)
            hintBar({{"B", L("Voltar", "Back")}, {"L1 R1", L("Filtro", "Filter")}, {"L2 R2", L("Letra", "Jump A-Z")}, {"Y", L("Próximo console", "Next console")}});
        else
            hintBar({{"B", L("Voltar", "Back")}, {"X", L("Favorito", "Favorite")}, {"Y", L("Próximo", "Next list")}, {"START", L("Ajustes", "Settings")}});
    }

    // Box art standing on the picture, just above the title.
    void drawCover(Game& g, int x, int bottom) {
        const std::string& cover = catalog.coverFor(g);
        if (cover.empty()) return;
        int w = 0, h = 0;
        if (!painter.pictureSize(cover, w, h) || w <= 0 || h <= 0) return;
        const int maxW = 96, maxH = 120;
        float scale = std::min((float)maxW / w, (float)maxH / h);
        int dw = (int)(w * scale), dh = (int)(h * scale);
        painter.fill(x - 2, bottom - dh - 2, dw + 4, dh + 4, kBg);
        painter.picture(cover, x, bottom - dh, dw, dh);
    }

    void drawRows() {
        const int x = 14, top = 70, rowH = 27, selH = 32, visible = 12, width = 272;
        int n = (int)list.items.size();
        if (n == 0) {
            std::string msg = list.filter == Filter::Favorites || list.card == kCardFavorites
                                  ? L("Nenhum favorito ainda.", "No favorites yet.")
                                  : list.filter == Filter::Portuguese ? L("Nenhum jogo em português aqui.", "No Portuguese games here.")
                                                                      : L("Nenhum jogo ainda.", "No games yet.");
            painter.text(kFont15, true, msg, x + 10, top + 20, kText, width);
            if (list.filter == Filter::Favorites || list.card == kCardFavorites)
                painter.text(kFont13, false, L("Num jogo, aperte X para favoritar.", "Press X on a game to favorite it."), x + 10, top + 46, kMuted, width);
            return;
        }
        const int anchor = 5;  // the selected row sits in the sixth slot when it can
        int first = std::clamp(list.cursor - anchor, 0, std::max(0, n - visible));
        int y = top;
        for (int i = first; i < n && i < first + visible; ++i) {
            Game* g = list.items[i];
            bool isSel = i == list.cursor;
            bool fav = store.isFavorite(g->path);
            std::string suffix;
            if (list.card < 0) suffix = catalog.systems[g->systemIndex].display;
            else if (g->duplicateTitle) suffix = g->details;
            if (isSel) {
                painter.roundRect(x, y, width, selH, selH / 2, kText);
                int right = x + width - 12;
                int starW = fav ? 16 : 0;
                if (fav) painter.star(right - 6, y + selH / 2, 7, kStarInk);
                painter.textMid(kFont15, true, g->title, x + 12, y + selH / 2, kBg, width - 24 - starW);
                y += selH + 1;
            } else {
                bool edge = (i == first && first > 0) || (i == first + visible - 1 && i < n - 1);
                SDL_Color c = edge ? kMuted : kText;
                int tx = x + 10, maxW = width - 20;
                int tagW = g->ptbr ? painter.width(kFont11, true, "PT") + 8 : 0;
                int favW = fav ? 16 : 0;
                int avail = maxW - tagW - favW;
                // The suffix gets at most 45% of the row, so the title always keeps a limit.
                if (!suffix.empty()) suffix = painter.fit(kFont12, false, suffix, std::max(1, avail * 45 / 100));
                int suffixW = suffix.empty() ? 0 : painter.width(kFont12, false, suffix) + 8;
                int drawn = painter.textMid(kFont14, false, g->title, tx, y + rowH / 2, c,
                                            std::max(1, avail - suffixW));
                int ax = tx + drawn + 6;
                if (!suffix.empty()) ax += painter.textMid(kFont12, false, suffix, ax, y + rowH / 2, kMuted) + 6;
                if (tagW) ax += painter.textMid(kFont11, true, "PT", ax, y + rowH / 2, kTeal) + 6;
                if (fav) painter.star(ax + 6, y + rowH / 2, 6, kStar);
                y += rowH + 1;
            }
        }
        // Position counter under the list.
        painter.text(kFont11, false, formatCount(list.cursor + 1) + L(" de ", " of ") + formatCount(n), x + 10,
                     kScreenH - 34 - 22, kMuted);
    }

    void drawSettings() {
        painter.fill(0, 0, kScreenW, kScreenH, SDL_Color{0, 0, 0, 170});
        const int w = 400, rowH = 40;
        const int n = (int)settings.size();
        const int visible = std::min(n, (kScreenH - 34 - 16 - 86) / rowH);  // stays above the hint bar
        const int first = std::clamp(settingsCursor - visible / 2, 0, std::max(0, n - visible));
        int h = 70 + visible * rowH + 16;
        int x = (kScreenW - w) / 2, y = std::max(16, (kScreenH - 34 - h) / 2);
        painter.roundRect(x, y, w, h, 18, kCard);
        painter.text(kFont26, true, L("Ajustes", "Settings"), x + 24, y + 20, kText);
        int ry = y + 70;
        for (int i = first; i < first + visible; ++i) {
            bool sel = i == settingsCursor;
            if (sel) painter.roundRect(x + 12, ry, w - 24, rowH - 4, (rowH - 4) / 2, kText);
            painter.textMid(kFont15, sel, settings[i].label, x + 28, ry + (rowH - 4) / 2, sel ? kBg : kText, w - 56);
            ry += rowH;
        }
        hintBar({{"A", L("Escolher", "Choose")}, {"B", L("Fechar", "Close")}});
    }

    void drawShortcuts() {
        painter.fill(0, 0, kScreenW, kScreenH, SDL_Color{0, 0, 0, 200});
        const int w = 580, h = 330, x = (kScreenW - w) / 2, y = (kScreenH - 34 - h) / 2;
        painter.roundRect(x, y, w, h, 18, kCard);
        painter.text(kFont26, true, L("Atalhos dos jogos", "In-game shortcuts"), x + 24, y + 18, kText);
        int ry = y + 72;
        for (const auto& [keys, what] : shortcutLines()) {
            painter.text(kFont13, true, keys, x + 24, ry, kTeal, 170);
            painter.text(kFont13, false, what, x + 200, ry, kText, w - 224);
            ry += 32;
        }
        hintBar({{"B", L("Fechar", "Close")}});
    }

    void drawCheats() {
        painter.fill(0, 0, kScreenW, kScreenH, SDL_Color{0, 0, 0, 200});
        const int w = 600, rowH = 30, x = (kScreenW - w) / 2, y = 12;
        const int n = (int)cheatRows.size();
        const int visible = std::min(n, 10);
        const int h = 74 + visible * rowH + 34;
        const int first = std::clamp(cheatsCursor - visible / 2, 0, std::max(0, n - visible));
        painter.roundRect(x, y, w, h, 18, kCard);
        painter.text(kFont24, true, L("Trapaças", "Cheats"), x + 22, y + 14, kText);
        int tw = painter.width(kFont24, true, L("Trapaças", "Cheats")) + 14;
        painter.text(kFont13, false, cheatsGame->title, x + 22 + tw, y + 22, kMuted, w - 44 - tw - 70);
        std::string count = formatCount(cheatsCursor + 1) + L(" de ", " of ") + formatCount(n);
        painter.text(kFont11, false, count, x + w - 22 - painter.width(kFont11, false, count), y + 24, kMuted);
        int ry = y + 56;
        for (int i = first; i < first + visible; ++i) {
            const CheatEntry& e = cheatRows[i];
            bool sel = i == cheatsCursor, on = cheatRowOn(e);
            if (sel) painter.roundRect(x + 10, ry, w - 20, rowH - 2, (rowH - 2) / 2, kText);
            // Switch: filled and pushed right when on.
            const int sw = 34, sh = 16, sx = x + w - 24 - sw, sy = ry + (rowH - 2 - sh) / 2;
            SDL_Color track = on ? kTeal : (sel ? hexColor(0xB8BCC6) : hexColor(0x3A404C));
            painter.roundRect(sx, sy, sw, sh, sh / 2, track);
            painter.roundRect(on ? sx + sw - sh + 2 : sx + 2, sy + 2, sh - 4, sh - 4, (sh - 4) / 2,
                              on ? kBg : (sel ? kBg : kMuted));
            std::string label = e.kind == 'L' ? L("Vida infinita", "Infinite lives") : e.shown;
            painter.textMid(kFont14, sel || e.kind == 'L', label, x + 26, ry + (rowH - 2) / 2,
                            sel ? kBg : kText, sx - 12 - (x + 26));
            ry += rowH;
        }
        painter.text(kFont12, false, L("Alguns códigos podem travar o jogo. Se travar, desligue a trapaça aqui.", "Some codes can freeze the game. If that happens, turn the cheat off here."),
                     x + 22, y + h - 26, kMuted, w - 44);
        hintBar({{"A", L("Liga/desliga", "On/off")}, {"^v", L("Escolher", "Move")}, {"<>", L("Pular 10", "Skip 10")}, {"B", L("Fechar", "Close")}});
    }

    void drawSearch() {
        painter.fill(0, 0, kScreenW, kScreenH, kBg);
        // The text typed, as a field with a caret.
        painter.roundRect(16, 12, kScreenW - 32, 40, 12, kCard);
        int tx = 32;
        if (!query.empty()) tx += painter.textMid(kFont18, true, query, tx, 32, kText, kScreenW - 200) + 2;
        if (!resultsFocus) painter.fill(tx, 22, 2, 20, kTeal);
        if (query.empty()) painter.textMid(kFont16, false, L("Digite o nome do jogo", "Type a game name"), tx + 8, 32, kMuted, kScreenW - 200);
        std::string count = query.empty() ? "" : found.empty() ? L("nada encontrado", "nothing found") : plural(found.size(), L("jogo", "game"), L("jogos", "games"));
        painter.text(kFont12, false, count, kScreenW - 32 - painter.width(kFont12, false, count), 26, kMuted);

        // Results from every system.
        const int ry = 62, rowH = 26, visible = 7, rw = kScreenW - 32;
        int n = (int)found.size();
        int first = std::clamp(foundCursor - visible / 2, 0, std::max(0, n - visible));
        if (n == 0)
            painter.text(kFont14, false, query.empty() ? L("Os jogos de todos os consoles aparecem aqui enquanto você digita.", "Games from every console show up here as you type.")
                                                       : L("Nenhum jogo com esse nome. Tente só uma palavra.", "No game with that name. Try just one word."),
                         30, ry + 8, kMuted, rw - 28);
        for (int i = first; i < n && i < first + visible; ++i) {
            Game* g = found[i];
            int y = ry + (i - first) * rowH;
            bool sel = resultsFocus && i == foundCursor;
            if (sel) painter.roundRect(16, y, rw, rowH - 2, (rowH - 2) / 2, kText);
            std::string sys = catalog.systems[g->systemIndex].display;
            if (g->duplicateTitle && !g->details.empty()) sys = g->details + " · " + sys;
            int sw = std::min(painter.width(kFont12, false, sys), rw / 2);
            painter.textMid(kFont12, false, sys, 16 + rw - 14 - sw, y + (rowH - 2) / 2, sel ? kBg : kMuted, sw);
            painter.textMid(kFont14, sel, g->title, 30, y + (rowH - 2) / 2, sel ? kBg : kText, rw - sw - 40);
        }

        // Keyboard.
        const auto& rows = keyboard();
        const int kx = 16, ky = 254, kw = (kScreenW - 32) / 10, kh = 40;
        for (int r = 0; r < (int)rows.size(); ++r) {
            for (int c = 0; c < 10;) {
                int span = 1;
                while (c + span < 10 && rows[r][c + span] == rows[r][c]) ++span;
                bool sel = !resultsFocus && r == keyRow && c <= keyCol && keyCol < c + span;
                int x = kx + c * kw, y = ky + r * (kh + 6), w = span * kw - 6;
                painter.roundRect(x, y, w, kh, 10, sel ? kText : kCard);
                const std::string& id = rows[r][c];  // "Espaço" and "Apagar" are ids, not labels
                std::string label = id == "Espaço" ? L("Espaço", "Space") : id == "Apagar" ? L("Apagar", "Delete") : id;
                FontId f = label.size() > 1 ? kFont14 : kFont18;
                int lw = painter.width(f, true, label);
                painter.textMid(f, true, label, x + (w - lw) / 2, y + kh / 2, sel ? kBg : kText);
                c += span;
            }
        }
        if (resultsFocus)
            hintBar({{"A", L("Jogar", "Play")}, {"^v", L("Escolher", "Move")}, {"B", L("Teclado", "Keyboard")}});
        else
            hintBar({{"A", L("Digitar", "Type")}, {"X", L("Apagar", "Backspace")}, {"Y", L("Espaço", "Space")}, {"START", L("Resultados", "Results")}, {"B", query.empty() ? L("Sair", "Exit") : L("Apagar", "Backspace")}});
    }

    // Breaks text into lines that fit the width, at spaces.
    std::vector<std::string> wrapText(FontId f, const std::string& text, int maxW) {
        std::vector<std::string> lines;
        std::stringstream ss(text);
        std::string word, line;
        while (ss >> word) {
            std::string tryLine = line.empty() ? word : line + " " + word;
            if (!line.empty() && painter.width(f, false, tryLine) > maxW) {
                lines.push_back(line);
                line = word;
            } else {
                line = tryLine;
            }
        }
        if (!line.empty()) lines.push_back(line);
        return lines;
    }

    // A strip above the hint bar while the battery is low and the charger is out.
    void drawLowBattery() {
        if (battery < 0 || battery >= kBatteryLow || plugged) return;
        int mins = minutesLeft();
        std::string text = L("Bateria em ", "Battery at ") + std::to_string(battery) + "%";
        if (mins >= 0) text += mins < 1 ? L(" · acabando", " · almost empty") : L(" · cerca de ", " · about ") + formatDuration(mins * 60L);
        text += L(" · ligue o carregador", " · plug in the charger");
        const int h = 26, y = kScreenH - 34 - h;
        painter.fill(0, y, kScreenW, h, hexColor(0xB91C1C));
        int tw = painter.width(kFont13, true, text);
        painter.textMid(kFont13, true, text, (kScreenW - tw) / 2, y + h / 2, hexColor(0xFFFFFF));
    }

    void drawNotice() {
        painter.fill(0, 0, kScreenW, kScreenH, SDL_Color{0, 0, 0, 200});
        const int w = 520, x = (kScreenW - w) / 2;
        auto lines = wrapText(kFont15, noticeBody, w - 56);
        const int h = 84 + (int)lines.size() * 22 + 20, y = (kScreenH - 34 - h) / 2;
        painter.roundRect(x, y, w, h, 18, kCard);
        painter.roundRect(x, y + 18, 6, h - 36, 3, kStar);
        painter.text(kFont21, true, noticeTitle, x + 28, y + 22, kText, w - 56);
        int ly = y + 66;
        for (const auto& l : lines) {
            painter.text(kFont15, false, l, x + 28, ly, kMuted, w - 56);
            ly += 22;
        }
        hintBar({{"B", L("Fechar", "Close")}});
    }

    // Most played games by time, and the total.
    void drawStats() {
        painter.fill(0, 0, kScreenW, kScreenH, SDL_Color{0, 0, 0, 200});
        const int w = 600, x = (kScreenW - w) / 2, y = 12, rowH = 30, visible = 10;
        std::vector<std::pair<const PlayStat*, Game*>> rows;
        long total = 0;
        for (const auto& [path, st] : store.playStats) {
            total += st.seconds;
            if (Game* g = catalog.find(path)) rows.push_back({&st, g});
        }
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first->seconds > b.first->seconds; });
        int n = (int)rows.size();
        statsScroll = std::clamp(statsScroll, 0, std::max(0, n - visible));
        const int h = 96 + std::max(1, std::min(n, visible)) * rowH + 12;
        painter.roundRect(x, y, w, h, 18, kCard);
        painter.text(kFont24, true, L("Tempo de jogo", "Play time"), x + 22, y + 14, kText);
        std::string summary = n == 0 ? L("Nenhum jogo jogado ainda.", "No games played yet.")
                                     : formatDuration(total) + L(" no total, em ", " in total, across ") + plural(store.playStats.size(), L("jogo", "game"), L("jogos", "games"));
        painter.text(kFont14, false, summary, x + 22, y + 52, kMuted, w - 44);
        int ry = y + 86;
        for (int i = statsScroll; i < n && i < statsScroll + visible; ++i) {
            const auto& [st, g] = rows[i];
            std::string rank = std::to_string(i + 1);
            painter.textMid(kFont14, true, rank, x + 22, ry + rowH / 2, kTeal);
            std::string time = formatDuration(st->seconds);
            int tw = painter.width(kFont14, true, time);
            painter.textMid(kFont14, true, time, x + w - 22 - tw, ry + rowH / 2, kText);
            std::string sys = catalog.systems[g->systemIndex].display;
            int sw = std::min(painter.width(kFont12, false, sys), 150);
            painter.textMid(kFont12, false, sys, x + w - 36 - tw - sw, ry + rowH / 2, kMuted, sw);
            painter.textMid(kFont14, false, g->title, x + 52, ry + rowH / 2, kText, w - 52 - 36 - tw - sw - 30);
            ry += rowH;
        }
        if (n > visible) hintBar({{"^v", L("Rolar", "Scroll")}, {"B", L("Fechar", "Close")}});
        else hintBar({{"B", L("Fechar", "Close")}});
    }

    void drawConfirm() {
        painter.fill(0, 0, kScreenW, kScreenH, SDL_Color{0, 0, 0, 190});
        const int w = 420, h = 170;
        int x = (kScreenW - w) / 2, y = (kScreenH - 34 - h) / 2;
        painter.roundRect(x, y, w, h, 18, kCard);
        painter.text(kFont26, true, confirmQuestion, x + 28, y + 30, kText, w - 56);
        int bx = x + 28;
        for (bool yes : {false, true}) {
            std::string label = yes ? L("Sim", "Yes") : L("Não", "No");
            int bw = 110;
            if (confirmYes == yes) painter.roundRect(bx, y + 100, bw, 40, 20, kText);
            else painter.roundOutline(bx, y + 100, bw, 40, 20, 1, hexColor(0x5A6070), kCard);
            int tw = painter.width(kFont16, true, label);
            painter.textMid(kFont16, true, label, bx + (bw - tw) / 2, y + 120, confirmYes == yes ? kBg : kText);
            bx += bw + 12;
        }
        hintBar({{"<>", L("Escolher", "Move")}, {"A", L("Confirmar", "Confirm")}, {"B", L("Cancelar", "Cancel")}});
    }

    void drawPower() {
        painter.fill(0, 0, kScreenW, kScreenH, kBg);
        painter.text(kFont40, true, powerTitle, 40, 120, kText);
        painter.text(kFont16, false, L("Pode levar até 1 minuto.", "This can take up to 1 minute."), 40, 182, kMuted);
        painter.roundRect(40, 236, 560, 112, 14, kCard);
        painter.roundRect(40, 236, 6, 112, 3, kStar);
        painter.text(kFont15, true, L("Se a tela ficar parada por mais de 5 minutos,", "If the screen stays frozen for over 5 minutes,"), 64, 256, kText, 520);
        painter.text(kFont15, true, L("segure o botão POWER até o console desligar.", "hold the POWER button until the console turns off."), 64, 280, kText, 520);
        painter.text(kFont13, false, L("Seus jogos e favoritos já estão salvos.", "Your games and favorites are already saved."), 64, 314, kMuted, 520);
    }

    void renderWaiting(const std::string& message, const std::string& hint) {
        if (!renderer) return;
        SDL_SetRenderDrawColor(renderer, kBg.r, kBg.g, kBg.b, 255);
        SDL_RenderClear(renderer);
        painter.textMid(kFont26, true, message, 40, kScreenH / 2 - 40, kText, kScreenW - 80);
        std::stringstream lines(hint);
        std::string line;
        for (int y = kScreenH / 2; std::getline(lines, line); y += 22)
            painter.textMid(kFont13, false, line, 40, y, kMuted, kScreenW - 80);
        SDL_RenderPresent(renderer);
    }

    void render() {
        SDL_SetRenderDrawColor(renderer, kBg.r, kBg.g, kBg.b, 255);
        SDL_RenderClear(renderer);
        painter.family = theme() == "streaming" ? kSans : kReadable;
        auto under = [this] {
            if (beforeOverlay == Screen::List) drawList();
            else drawHome();
            painter.family = kSans;  // overlays look the same in every layout
        };
        switch (screen) {
            case Screen::Home:
                drawHome();
                drawLowBattery();
                break;
            case Screen::List: drawList(); break;
            case Screen::Settings:
                under();
                drawSettings();
                break;
            case Screen::Confirm:
                under();
                drawConfirm();
                break;
            case Screen::Power: drawPower(); break;
            case Screen::Message:
                under();
                drawShortcuts();
                break;
            case Screen::Cheats:
                under();
                drawCheats();
                break;
            case Screen::Search:
                painter.family = kSans;
                drawSearch();
                break;
            case Screen::Stats:
                under();
                drawStats();
                break;
            case Screen::Notice:
                if (beforeOverlay == Screen::Search) drawSearch();
                else under();
                painter.family = kSans;
                drawNotice();
                break;
        }
        SDL_RenderPresent(renderer);
    }

    void renderLoading() {
        SDL_SetRenderDrawColor(renderer, kBg.r, kBg.g, kBg.b, 255);
        SDL_RenderClear(renderer);
        painter.text(kFont26, true, L("Carregando seus jogos…", "Loading your games…"), 40, kScreenH / 2 - 20, kText);
        SDL_RenderPresent(renderer);
    }

    // Brings back the screen and game shown before the last launch or reboot.
    void restorePosition() {
        int card;
        if (!cardFromKey(store.lastSystem, card)) return;
        auto cs = cards();
        auto it = std::find(cs.begin(), cs.end(), card);
        if (it == cs.end()) return;
        homeCard = (int)(it - cs.begin());
        if (store.lastScreen == "lista") openList(card, store.lastGame);
    }

    // -- main loop ------------------------------------------------------------

    int run() {
        std::ofstream(kPidFile) << getpid() << "\n";
        Uint32 lastBattery = 0;
        bool waitingForPicture = false;
        while (running) {
            Uint32 now = SDL_GetTicks();
            if (now - lastBattery >= kBatteryCheckMs || lastBattery == 0) {
                updateBattery();
                lastBattery = now;
            }
            // Timers run on every pass, so a stream of other events cannot starve them.
            if (held != Key::None && now >= nextRepeat) {
                handle(held);
                nextRepeat = now + (now - heldSince > kRepeatFastAfterMs ? kRepeatFastMs : kRepeatRateMs);
            }
            if (lastMoveAt && now - lastMoveAt < kHeroSettleMs) {
                waitingForPicture = true;
            } else if (waitingForPicture) {
                waitingForPicture = false;
                dirty = true;  // scrolling stopped: show the picture
            }
            if (dirty) {
                render();
                dirty = false;
            }
            int timeout = 1000;
            if (held != Key::None) timeout = (int)std::max<Sint64>(1, (Sint64)nextRepeat - now);
            if (waitingForPicture) timeout = std::min(timeout, (int)(lastMoveAt + kHeroSettleMs - now) + 1);
            SDL_Event ev;
            if (!SDL_WaitEventTimeout(&ev, std::max(timeout, 1))) continue;
            do {
                Key k = Key::None;
                bool down = false, up = false;
                switch (ev.type) {
                    case SDL_QUIT: running = false; break;
                    case SDL_WINDOWEVENT: dirty = true; break;
                    case SDL_CONTROLLERDEVICEADDED:
                        if (!pad) pad = SDL_GameControllerOpen(ev.cdevice.which);
                        break;
                    case SDL_CONTROLLERBUTTONDOWN: k = keyFromButton(ev.cbutton.button); down = true; break;
                    case SDL_CONTROLLERBUTTONUP: k = keyFromButton(ev.cbutton.button); up = true; break;
                    case SDL_CONTROLLERAXISMOTION:
                        // L2/R2 are mapped as trigger axes on this pad.
                        if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                            ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                            int side = ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 0 : 1;
                            bool pressed = ev.caxis.value > 16000;
                            if (pressed && !triggerDown[side]) handle(side == 0 ? Key::L2 : Key::R2);
                            triggerDown[side] = pressed;
                        }
                        break;
                    case SDL_KEYDOWN:
                        if (!ev.key.repeat) { k = keyFromKeyboard(ev.key.keysym.sym); down = true; }
                        break;
                    case SDL_KEYUP: k = keyFromKeyboard(ev.key.keysym.sym); up = true; break;
                }
                if (down && k != Key::None) {
                    Screen before = screen;
                    handle(k);
                    bool direction = k == Key::Up || k == Key::Down || k == Key::Left || k == Key::Right;
                    // Repeat only while the same screen stays up (Down on Home opens the list).
                    if (direction && screen == before) {
                        held = k;
                        heldSince = SDL_GetTicks();
                        nextRepeat = heldSince + kRepeatDelayMs;
                    } else if (screen != before) {
                        held = Key::None;
                    }
                }
                if (up && k == held) held = Key::None;
                if (!running || screen == Screen::Power) break;
            } while (SDL_PollEvent(&ev));
            if (screen == Screen::Power && !gPreview) {
                // Keep the warning on screen until the system turns off.
                for (;;) SDL_Delay(1000);
            }
        }
        saveCurrentPosition();
        return 0;
    }

    // Charge of the battery and whether it is discharging ("Charging", "Full" and "Not charging"
    // all mean the charger is in).
    static int readBattery(bool* pluggedIn = nullptr) {
        DIR* d = opendir(hostPath("/sys/class/power_supply").c_str());
        if (!d) return -1;
        int result = -1;
        bool charger = false;
        while (dirent* de = readdir(d)) {
            if (de->d_name[0] == '.') continue;
            std::string base = hostPath("/sys/class/power_supply/") + de->d_name;
            std::ifstream type(base + "/type");
            std::string t;
            if (!(type >> t) || t != "Battery") continue;
            std::ifstream cap(base + "/capacity");
            int c;
            if (cap >> c) result = std::clamp(c, 0, 100);
            std::ifstream status(base + "/status");
            std::string st;
            charger = (status >> st) && st != "Discharging";
        }
        closedir(d);
        if (pluggedIn) *pluggedIn = charger;
        return result;
    }

    void updateBattery() {
        bool wasPlugged = plugged;
        int b = readBattery(&plugged);
        if (b != battery || plugged != wasPlugged) dirty = true;
        battery = b;
        long now = monotonicSeconds();
        if (plugged || b < 0 || (!drain.empty() && b > drain.back().second)) drain.clear();
        if (!plugged && b >= 0 && (drain.empty() || drain.back().second != b)) drain.push_back({now, b});
        while (drain.size() > 2 && now - drain.front().first > 3600) drain.pop_front();
    }

    // Minutes left at the discharge rate measured over the last hour; -1 until there is enough
    // (two points of drop over five minutes or more).
    int minutesLeft() const {
        if (drain.size() < 2 || battery < 0) return -1;
        int dropped = drain.front().second - drain.back().second;
        long secs = drain.back().first - drain.front().first;
        if (dropped < 2 || secs < 300) return -1;
        return (int)(battery * (double)secs / dropped / 60.0);
    }

    // Sends one command to RetroArch's local command port (network_cmd_enable).
    static void retroArchCommand(const char* cmd) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return;
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(kRetroArchCmdPort);
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sendto(fd, cmd, strlen(cmd), 0, (sockaddr*)&to, sizeof(to));
        close(fd);
    }
};

// ---------------------------------------------------------------------------
// Screenshot mode for checking layouts on a desktop:
//   vitrine --shot out.png --screen home|list|settings|power [--card N] [--game PATH] [--filter N]
// ---------------------------------------------------------------------------

static int screenshot(Vitrine& v, int argc, char** argv) {
    std::string out, screenName = "home", gamePath;
    int card = 2, filter = 0;
    for (int i = 1; i + 1 < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shot") out = argv[++i];
        else if (a == "--screen") screenName = argv[++i];
        else if (a == "--card") card = std::atoi(argv[++i]);
        else if (a == "--game") gamePath = argv[++i];
        else if (a == "--filter") filter = std::atoi(argv[++i]);
    }
    gScreenshot = true;
    auto cs = v.cards();
    for (int i = 1; i + 1 < argc; ++i)  // --cartao "@luta" or "Sega Genesis": the card by its saved-position key
        if (std::string(argv[i]) == "--cartao") {
            int c;
            if (v.cardFromKey(argv[i + 1], c)) card = (int)(std::find(cs.begin(), cs.end(), c) - cs.begin());
        }
    v.homeCard = std::clamp(card, 0, (int)cs.size() - 1);
    v.updateBattery();  // a fake /sys/class/power_supply under VITRINE_ROOT, if there is one
    if (v.battery < 0) v.battery = 99;
    for (int i = 1; i + 1 < argc; ++i)  // --descarga "0:20,900:14": seconds and percent seen before
        if (std::string(argv[i]) == "--descarga") {
            v.drain.clear();
            std::stringstream ss(argv[i + 1]);
            for (std::string pt; std::getline(ss, pt, ',');)
                v.drain.push_back({std::atol(pt.c_str()), std::atoi(pt.substr(pt.find(':') + 1).c_str())});
        }
    v.lastMoveAt = 0;
    if (screenName != "home") {
        v.openList(cs[v.homeCard], gamePath);
        v.list.filter = (Filter)filter;
        v.rebuildList(gamePath);
    }
    if (screenName == "launch") {  // simulates pressing A on the game, for checking cheat files
        v.launch(v.selectedGame());
        if (v.screen != Screen::Notice) return 0;  // a warning instead of the game: capture it
    }
    if (screenName == "settings") v.openSettings();
    if (screenName == "estatisticas") {
        v.openSettings();
        v.screen = Screen::Stats;
    }
    if (screenName == "busca") {  // --query "mario" --results N: typed text and cursor in the results
        v.screen = Screen::Home;
        v.openSearch();
        for (int i = 1; i + 1 < argc; ++i) {
            std::string a = argv[i];
            if (a == "--query") for (char c : std::string(argv[i + 1])) v.type(c == ' ' ? "Espaço" : std::string(1, (char)std::toupper((unsigned char)c)));
            if (a == "--results") { v.resultsFocus = true; v.foundCursor = std::atoi(argv[i + 1]); }
            if (a == "--key") { v.keyRow = std::atoi(argv[i + 1]) / 10; v.keyCol = std::atoi(argv[i + 1]) % 10; }
        }
        if (std::find(argv, argv + argc, std::string("--play")) != argv + argc) {
            v.launch(v.found.empty() ? nullptr : v.found[v.foundCursor]);
            return 0;
        }
    }
    if (screenName == "trapacas") {
        Game* g = v.selectedGame();
        if (!g || !v.catalog.hasCheats(*g)) return 2;
        v.openCheats(g);
        for (int i = 1; i + 1 < argc; ++i)  // --toggle N switches row N, as pressing A on it
            if (std::string(argv[i]) == "--toggle") {
                v.cheatsCursor = std::atoi(argv[i + 1]);
                v.cheatsKey(Vitrine::Key::A);
            }
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--cursor") v.cheatsCursor = std::atoi(argv[i + 1]);
    }
    if (screenName == "atalhos") {
        v.openSettings();
        v.screen = Screen::Message;
    }
    if (screenName == "power") {
        v.powerTitle = L("Desligando…", "Shutting down…");
        v.screen = Screen::Power;
    }
    SDL_Texture* target = SDL_CreateTexture(v.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, kScreenW, kScreenH);
    SDL_SetRenderTarget(v.renderer, target);
    v.render();
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, kScreenW, kScreenH, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_RenderReadPixels(v.renderer, nullptr, SDL_PIXELFORMAT_RGBA32, s->pixels, s->pitch);
    int ok = IMG_SavePNG(s, out.c_str());
    SDL_FreeSurface(s);
    SDL_DestroyTexture(target);
    return ok == 0 ? 0 : 1;
}

// The language follows Settings; unset, it follows RetroArch's user_language (7 = Portuguese).
// VITRINE_LANG=en|pt overrides it (screenshots).
static bool englishByDefault(const Store& store) {
    if (const char* env = std::getenv("VITRINE_LANG")) return std::string(env) == "en";
    if (!store.language.empty()) return store.language == "en";
    std::ifstream in(hostPath(kRetroArchCfg));
    for (std::string line; std::getline(in, line);)
        if (line.compare(0, 13, "user_language") == 0) return line.find("\"7\"") == std::string::npos;
    return true;
}

int main(int argc, char** argv) {
    gArgv = argv;
    if (const char* root = std::getenv("VITRINE_ROOT")) {
        gRoot = root;
        gPreview = true;
    }
    signal(SIGPIPE, SIG_IGN);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    if (SDL_Init(0) != 0 || TTF_Init() != 0) return 1;
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    Vitrine v;
    v.store.load();
    gEnglish = englishByDefault(v.store);
    if (!v.startVideo()) return 1;
    // FN+UP sends Mesa's HUD signal to the pid in the pid file; keep the GL libraries that hold
    // its handler loaded for the whole run, including while a game owns the display.
    if (!gPreview) (void)SDL_LoadObject("libEGL.so.1");
    v.renderLoading();
    v.catalog.load();
    if (v.catalog.systems.empty()) {
        std::cerr << L("Nenhum jogo encontrado nos arquivos apps.puppy\n", "No games found in the apps.puppy files\n");
        return 1;
    }
    v.buildCollections();
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--shot") return screenshot(v, argc, argv);
    v.restorePosition();
    int rc = v.run();
    v.stopVideo();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return rc;
}
