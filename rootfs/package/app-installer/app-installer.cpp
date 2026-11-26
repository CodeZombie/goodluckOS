// app-installer: installs apps from .zip files in HOME/apps and uninstalls them.
//
// An installed app is a folder in HOME/apps with an app.puppy in it (docs/making-apps.md); the
// launcher shows every such folder in its Apps row. Installing unpacks a zip into such a folder,
// uninstalling deletes the folder: there is nothing else to keep in step.
//
// Zips are read here (zlib, no unzip process): every entry is checked before anything is written
// (paths that climb out, links, encryption, sizes against the free space), files are unpacked into
// a hidden folder and checked against their CRC, and only then is the folder renamed into place.
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <zlib.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs = std::filesystem;

static const char* kFont        = "/usr/share/fonts/NotoSans.ttf";
static const char* kDefaultIcon = "/usr/share/icons/apps.png";
static const char* kManifest    = "app.puppy";

constexpr float kW = 640, kH = 480;
constexpr float kTabH = 48, kPad = 12, kGridTop = kTabH + kPad, kFooterH = 36;
constexpr float kCellW = (kW - 3 * kPad) / 2;
constexpr float kCellH = (kH - kGridTop - kFooterH - kPad) / 2;
constexpr float kBorder = 3, kTextSize = 22;
constexpr Uint32 kInputPollMs = 10, kRepeatDelayMs = 350, kRepeatRateMs = 60;
constexpr int kStickPress = 16000, kStickRelease = 8000;
constexpr size_t kMaxManifestBytes = 64 << 10;
constexpr size_t kMaxIconBytes = 1 << 20;
constexpr int kMaxIconSide = 1024;
constexpr uint64_t kSpareBytes = 8 << 20;     // left free on HOME after installing

static const ImU32 kYellow  = IM_COL32(255, 205, 60, 255);   // the launcher's selection colour
static const ImU32 kTile    = IM_COL32(36, 36, 40, 255);     // the launcher's tile colour
static const ImU32 kTabOff  = IM_COL32(42, 70, 108, 255);
static const ImU32 kTabOn   = IM_COL32(66, 150, 250, 255);
static const ImU32 kText    = IM_COL32(255, 255, 255, 255);
static const ImU32 kTextDim = IM_COL32(150, 150, 150, 255);
static const ImU32 kWarn    = IM_COL32(255, 140, 120, 255);
static const ImU32 kBg      = IM_COL32(17, 17, 17, 255);

static std::string homeDir() {
    const char* h = getenv("HOME");
    return h && *h ? h : "/home/player";
}

static std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------------------------
// app.puppy: the first [ENTRY], in the launcher's own format

struct Manifest {
    std::string name, description, command, icon, version;
};

static Manifest parseManifest(const std::string& text) {
    Manifest m;
    std::string section;
    bool done = false;
    size_t pos = 0;
    while (pos <= text.size() && !done) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            if (section == "ENTRY") done = true;   // only the first entry describes the app
            section = trim(line.substr(1, line.size() - 2));
            std::transform(section.begin(), section.end(), section.begin(), ::toupper);
            continue;
        }
        const size_t eq = line.find('=');
        if (section != "ENTRY" || eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::transform(key.begin(), key.end(), key.begin(), ::toupper);
        const std::string v = trim(line.substr(eq + 1));
        if (key == "NAME") m.name = v;
        else if (key == "DESCRIPTION") m.description = v;
        else if (key == "COMMAND") m.command = v;
        else if (key == "ICON") m.icon = v;
        else if (key == "VERSION") m.version = v;
    }
    return m;
}

// A folder name the card (FAT) and the launcher can take: no separators, nothing FAT refuses,
// not hidden, not "." or ".."
static bool validName(const std::string& s) {
    if (s.empty() || s.size() > 128 || s[0] == '.' || s.back() == ' ' || s.back() == '.') return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7F || strchr("/\\:*?\"<>|", c)) return false;
    return true;
}

// For a zip with app.puppy at its root: a folder name from the app's NAME, which stays the same
// across versions (the zip's own name usually doesn't)
static std::string folderFromName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) out += (c < 0x20 || c == 0x7F || strchr("/\\:*?\"<>|", c)) ? '-' : (char)c;
    while (!out.empty() && (out[0] == '.' || out[0] == ' ')) out.erase(0, 1);
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.substr(0, 128);
}

// ---------------------------------------------------------------------------------------------
// Zip reading: stored and deflated entries, no zip64, no encryption

struct ZipEntry {
    std::string name;            // '/'-separated, as stored (backslashes turned into '/')
    uint16_t method = 0, flags = 0;
    uint32_t crc = 0, csize = 0, usize = 0, offset = 0;
    bool dir = false, link = false;
};

static uint16_t le16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const unsigned char* p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

class Zip {
public:
    std::vector<ZipEntry> entries;
    std::string error;

    ~Zip() { if (f) fclose(f); }

    bool open(const fs::path& path) {
        f = fopen(path.c_str(), "rb");
        if (!f) return fail("can't be read");
        if (fseek(f, 0, SEEK_END) != 0) return fail("can't be read");
        const long size = ftell(f);
        if (size < 22) return fail("isn't a zip file");
        // End of central directory: the last 22 bytes, or up to 64 KB earlier with a comment
        const long tail = std::min<long>(size, 22 + 0xFFFF);
        std::vector<unsigned char> buf((size_t)tail);
        if (fseek(f, size - tail, SEEK_SET) != 0 || fread(buf.data(), 1, buf.size(), f) != buf.size()) return fail("can't be read");
        long eocd = -1;
        for (long i = tail - 22; i >= 0; --i)
            if (le32(&buf[(size_t)i]) == 0x06054b50) { eocd = i; break; }
        if (eocd < 0) return fail("isn't a zip file, or is damaged");
        const unsigned char* e = &buf[(size_t)eocd];
        const uint16_t count = le16(e + 10);
        const uint32_t cdSize = le32(e + 12), cdOffset = le32(e + 16);
        if (le16(e + 4) != 0 || le16(e + 6) != 0) return fail("is split into parts");
        if (count == 0xFFFF || cdSize == 0xFFFFFFFF || cdOffset == 0xFFFFFFFF) return fail("is a zip64 file (over 4 GB), which isn't supported");
        if ((uint64_t)cdOffset + cdSize > (uint64_t)size) return fail("is damaged");
        std::vector<unsigned char> cd(cdSize);
        if (fseek(f, (long)cdOffset, SEEK_SET) != 0 || fread(cd.data(), 1, cd.size(), f) != cd.size()) return fail("is damaged");
        size_t p = 0;
        for (uint16_t i = 0; i < count; ++i) {
            if (p + 46 > cd.size() || le32(&cd[p]) != 0x02014b50) return fail("is damaged");
            const unsigned char* h = &cd[p];
            ZipEntry z;
            const uint16_t madeBy = le16(h + 4);
            z.flags = le16(h + 8);
            z.method = le16(h + 10);
            z.crc = le32(h + 16);
            z.csize = le32(h + 20);
            z.usize = le32(h + 24);
            const uint16_t nameLen = le16(h + 28), extraLen = le16(h + 30), commentLen = le16(h + 32);
            const uint32_t attrs = le32(h + 38);
            z.offset = le32(h + 42);
            if (p + 46 + nameLen > cd.size()) return fail("is damaged");
            z.name.assign((const char*)h + 46, nameLen);
            std::replace(z.name.begin(), z.name.end(), '\\', '/');   // some Windows tools
            z.dir = !z.name.empty() && z.name.back() == '/';
            z.link = (madeBy >> 8) == 3 && ((attrs >> 16) & S_IFMT) == S_IFLNK;
            if (z.csize == 0xFFFFFFFF || z.usize == 0xFFFFFFFF || z.offset == 0xFFFFFFFF)
                return fail("is a zip64 file (over 4 GB), which isn't supported");
            entries.push_back(std::move(z));
            p += 46 + nameLen + extraLen + commentLen;
        }
        return true;
    }

    // Streams the entry's bytes to sink (which returns false to stop); checks size and CRC
    bool read(const ZipEntry& z, const std::function<bool(const unsigned char*, size_t)>& sink) {
        if (z.flags & 1) return fail("is password protected");
        if (z.method != 0 && z.method != 8) return fail("uses a compression other than Deflate (save it as a plain .zip)");
        unsigned char h[30];
        if (fseek(f, (long)z.offset, SEEK_SET) != 0 || fread(h, 1, 30, f) != 30 || le32(h) != 0x04034b50) return fail("is damaged");
        if (fseek(f, (long)z.offset + 30 + le16(h + 26) + le16(h + 28), SEEK_SET) != 0) return fail("is damaged");
        uLong crc = crc32(0, nullptr, 0);
        uint64_t out = 0;
        unsigned char in[1 << 16], buf[1 << 16];
        uint32_t left = z.csize;
        if (z.method == 0) {
            if (z.csize != z.usize) return fail("is damaged");
            while (left) {
                const size_t n = fread(in, 1, std::min<uint32_t>(left, sizeof in), f);
                if (n == 0) return fail("is damaged");
                left -= (uint32_t)n; out += n;
                crc = crc32(crc, in, (uInt)n);
                if (!sink(in, n)) return false;
            }
        } else {
            z_stream s{};
            if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return fail("can't be unpacked");
            std::unique_ptr<z_stream, void (*)(z_stream*)> guard(&s, [](z_stream* x) { inflateEnd(x); });
            int ret = Z_OK;
            while (ret != Z_STREAM_END) {
                if (s.avail_in == 0) {
                    if (left == 0) return fail("is damaged");
                    const size_t n = fread(in, 1, std::min<uint32_t>(left, sizeof in), f);
                    if (n == 0) return fail("is damaged");
                    left -= (uint32_t)n;
                    s.next_in = in; s.avail_in = (uInt)n;
                }
                s.next_out = buf; s.avail_out = sizeof buf;
                ret = inflate(&s, Z_NO_FLUSH);
                if (ret != Z_OK && ret != Z_STREAM_END) return fail("is damaged");
                const size_t n = sizeof buf - s.avail_out;
                out += n;
                if (out > z.usize) return fail("is damaged");   // more than it said: a zip bomb, or broken
                crc = crc32(crc, buf, (uInt)n);
                if (n && !sink(buf, n)) return false;
            }
        }
        if (out != z.usize || crc != z.crc) return fail("is damaged (a file in it doesn't check out)");
        return true;
    }

    bool readString(const ZipEntry& z, std::string& s, size_t max) {
        if (z.usize > max) return fail("has an oversized " + z.name);
        s.clear();
        s.reserve(z.usize);
        return read(z, [&](const unsigned char* p, size_t n) { s.append((const char*)p, n); return true; });
    }

private:
    FILE* f = nullptr;
    bool fail(const std::string& why) { error = why; return false; }
};

// A path inside the app: relative, each part a name the card takes, nothing climbing out
static bool safeRelative(const std::string& rel) {
    if (rel.empty() || rel[0] == '/') return false;
    size_t start = 0;
    while (start < rel.size()) {
        size_t end = rel.find('/', start);
        if (end == std::string::npos) end = rel.size();
        const std::string part = rel.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        for (unsigned char c : part)
            if (c < 0x20 || c == 0x7F || strchr("\\:*?\"<>|", c)) return false;
        start = end + 1;
    }
    return true;
}

// Files macOS, Windows and editors add that aren't part of anyone's app
static bool junk(const std::string& name) {
    if (name.rfind("__MACOSX/", 0) == 0) return true;
    std::string path = name;
    while (!path.empty() && path.back() == '/') path.pop_back();
    const std::string base = lower(path.substr(path.find_last_of('/') + 1));
    return base.rfind("._", 0) == 0 || base == ".ds_store" || base == "thumbs.db" || base == "desktop.ini";
}

// The checks every entry of the app has to pass, at inspection and again when installing
static std::string checkEntry(const ZipEntry& e, const std::string& rel) {
    if (e.flags & 1) return "This zip is password protected";
    if (!e.dir && e.method != 0 && e.method != 8) return "This zip uses a compression other than Deflate (save it as a plain .zip)";
    if (e.link) return "It contains a link (" + rel + "): zip the real files instead";
    if (!safeRelative(e.dir ? rel.substr(0, rel.size() - 1) : rel)) return "It contains a file name the card can't take: " + rel;
    return "";
}

// ---------------------------------------------------------------------------------------------
// What the two tabs list

struct Item {
    fs::path path;               // the zip, or the installed app's folder
    std::string folder;          // the folder in HOME/apps it installs to / lives in
    std::string prefix;          // inside the zip: "" or "Folder/"
    Manifest m;
    std::string error;           // a zip that can't be installed, and why
    bool update = false;         // installing replaces an installed copy
    std::string iconData;        // zip: icon.png's bytes
    fs::path iconFile;           // installed: icon.png
    SDL_Texture* icon = nullptr;
    bool iconTried = false;
};

static std::string appsDir() { return homeDir() + "/apps"; }

static bool pngSize(const std::string& b, int& w, int& h) {
    if (b.size() < 24 || b.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) != 0 || b.compare(12, 4, "IHDR") != 0) return false;
    const unsigned char* p = (const unsigned char*)b.data();
    w = (int)((uint32_t)p[16] << 24 | p[17] << 16 | p[18] << 8 | p[19]);
    h = (int)((uint32_t)p[20] << 24 | p[21] << 16 | p[22] << 8 | p[23]);
    return w > 0 && h > 0;
}

static Item inspectZip(const fs::path& zipPath) {
    Item it;
    it.path = zipPath;
    it.m.name = zipPath.stem().string();
    Zip z;
    if (!z.open(zipPath)) { it.error = "This zip " + z.error; return it; }
    const ZipEntry* manifest = nullptr;
    int found = 0;
    for (const auto& e : z.entries) {
        if (e.dir || junk(e.name)) continue;
        if (e.name == kManifest) { manifest = &e; it.prefix = ""; found = 100; break; }   // at the root: that's it
        const size_t slash = e.name.find('/');
        if (slash != std::string::npos && e.name.compare(slash + 1, std::string::npos, kManifest) == 0) {
            manifest = &e;
            it.prefix = e.name.substr(0, slash + 1);
            ++found;
        }
    }
    if (!manifest) { it.error = "Not an app: there is no app.puppy in it"; return it; }
    if (found > 1 && found < 100) { it.error = "More than one app in one zip: give each its own zip"; return it; }
    std::string text;
    if (!z.readString(*manifest, text, kMaxManifestBytes)) { it.error = "This zip " + z.error; return it; }
    it.m = parseManifest(text);
    if (it.m.name.empty() || it.m.command.empty()) { it.error = "app.puppy needs a NAME and a COMMAND"; it.m.name = zipPath.stem().string(); return it; }
    it.folder = it.prefix.empty() ? folderFromName(it.m.name) : it.prefix.substr(0, it.prefix.size() - 1);
    if (!validName(it.folder)) { it.error = "\"" + it.folder + "\" can't be a folder name on the card"; return it; }
    for (const auto& e : z.entries) {
        if (junk(e.name) || e.name.compare(0, it.prefix.size(), it.prefix) != 0) continue;
        const std::string rel = e.name.substr(it.prefix.size());
        if (rel.empty()) continue;
        it.error = checkEntry(e, rel);
        if (!it.error.empty()) return it;
    }
    if (!it.m.icon.empty()) {
        std::string icon = it.m.icon;
        if (icon.rfind("./", 0) == 0) icon.erase(0, 2);
        const ZipEntry* e = nullptr;
        for (const auto& x : z.entries) if (!x.dir && x.name == it.prefix + icon) e = &x;
        if (!e) { it.error = "ICON=" + it.m.icon + " isn't in the zip"; return it; }
        int w = 0, h = 0;
        if (!z.readString(*e, it.iconData, kMaxIconBytes) || !pngSize(it.iconData, w, h) || w > kMaxIconSide || h > kMaxIconSide) {
            it.error = "The icon has to be a PNG, at most " + std::to_string(kMaxIconSide) + " x " + std::to_string(kMaxIconSide);
            it.iconData.clear();
            return it;
        }
    }
    std::error_code ec;
    it.update = fs::exists(fs::path(appsDir()) / it.folder / kManifest, ec);
    return it;
}

static std::vector<Item> scanZips() {
    std::vector<Item> items;
    std::error_code ec;
    for (fs::directory_iterator d(appsDir(), ec), end; !ec && d != end; d.increment(ec)) {
        const std::string n = d->path().filename().string();
        std::error_code e2;
        if (n[0] == '.' || lower(d->path().extension().string()) != ".zip" || !d->is_regular_file(e2)) continue;
        items.push_back(inspectZip(d->path()));
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return lower(a.m.name) < lower(b.m.name); });
    return items;
}

static std::vector<Item> scanInstalled() {
    std::vector<Item> items;
    std::error_code ec;
    for (fs::directory_iterator d(appsDir(), ec), end; !ec && d != end; d.increment(ec)) {
        const std::string n = d->path().filename().string();
        std::error_code e2;
        if (n[0] == '.' || !d->is_directory(e2)) continue;
        std::ifstream in(d->path() / kManifest);
        if (!in) continue;
        std::string text((std::istreambuf_iterator<char>(in)), {});
        Item it;
        it.path = d->path();
        it.folder = n;
        it.m = parseManifest(text.substr(0, kMaxManifestBytes));
        if (it.m.name.empty()) it.m.name = n;
        if (!it.m.icon.empty()) it.iconFile = it.m.icon[0] == '/' ? fs::path(it.m.icon) : d->path() / it.m.icon;
        items.push_back(std::move(it));
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return lower(a.m.name) < lower(b.m.name); });
    return items;
}

// ---------------------------------------------------------------------------------------------
// Installing and uninstalling

// Leftovers of an install the power cut: a half-unpacked folder goes, a replaced copy comes back
static void recover() {
    std::error_code ec;
    const fs::path apps = appsDir();
    std::vector<fs::path> names;
    for (fs::directory_iterator d(apps, ec), end; !ec && d != end; d.increment(ec)) names.push_back(d->path());
    for (const auto& p : names) {   // first put back any app that was set aside...
        const std::string n = p.filename().string();
        if (n.rfind(".old-", 0) != 0) continue;
        const fs::path app = apps / n.substr(5);
        if (fs::exists(app, ec)) fs::remove_all(p, ec);
        else fs::rename(p, app, ec);
    }
    for (const auto& p : names) {   // ...then drop half-unpacked ones, giving back the data they held
        const std::string n = p.filename().string();
        if (n.rfind(".install-", 0) != 0) continue;
        const fs::path app = apps / n.substr(9);
        if (fs::exists(p / "data", ec) && fs::exists(app, ec) && !fs::exists(app / "data", ec)) fs::rename(p / "data", app / "data", ec);
        fs::remove_all(p, ec);
    }
}

// "" when it worked, else why not. progress(0..1) is called now and then.
static std::string install(const Item& it, const std::function<void(float)>& progress) {
    Zip z;
    if (!z.open(it.path)) return "This zip " + z.error;
    const fs::path apps = appsDir(), tmp = apps / (".install-" + it.folder), dest = apps / it.folder;
    std::error_code ec;
    uint64_t total = 0, done = 0;
    std::vector<const ZipEntry*> files;
    for (const auto& e : z.entries) {
        if (junk(e.name) || e.name.compare(0, it.prefix.size(), it.prefix) != 0 || e.name.size() == it.prefix.size()) continue;
        const std::string why = checkEntry(e, e.name.substr(it.prefix.size()));   // the zip may have changed since
        if (!why.empty()) return why;
        files.push_back(&e);
        total += e.usize;
    }
    struct statvfs vfs;
    if (statvfs(apps.c_str(), &vfs) == 0 && (uint64_t)vfs.f_bavail * vfs.f_frsize < total + kSpareBytes)
        return "Not enough space on HOME (" + std::to_string((total + kSpareBytes) >> 20) + " MB needed)";
    fs::remove_all(tmp, ec);
    if (!fs::create_directories(tmp, ec)) return "Can't create a folder in HOME/apps";
    std::string err;
    Uint32 last = SDL_GetTicks();   // for the whole install: many small files must show progress too
    for (const ZipEntry* e : files) {
        const fs::path out = tmp / e->name.substr(it.prefix.size());
        if (e->dir) { fs::create_directories(out, ec); continue; }
        fs::create_directories(out.parent_path(), ec);
        FILE* f = fopen(out.c_str(), "wb");
        if (!f) { err = "Can't write " + out.filename().string(); break; }
        bool writeOk = true;
        const bool ok = z.read(*e, [&](const unsigned char* p, size_t n) {
            if (fwrite(p, 1, n, f) != n) return writeOk = false;
            done += n;
            if (SDL_GetTicks() - last > 200) { last = SDL_GetTicks(); progress(total ? (float)done / (float)total : 1.0f); }
            return true;
        });
        if (fclose(f) != 0) writeOk = false;
        if (!writeOk) { err = "Couldn't write to the card (is it full?)"; break; }
        if (!ok) { err = "This zip " + z.error; break; }
    }
    if (err.empty()) progress(1.0f);   // what is left (swap, and sync to the card) can take a moment
    if (err.empty()) {
        // Swap in. The installed copy is set aside first, then its data/ (the app's saves and
        // settings) moves into the new copy, then the new copy takes its place. A power cut at any
        // point leaves something recover() puts right: no step loses the app or its data.
        const fs::path old = apps / (".old-" + it.folder);
        fs::remove_all(old, ec);
        ec.clear();
        if (fs::exists(dest)) fs::rename(dest, old, ec);
        if (ec) err = "Can't replace the installed copy";
        else if (fs::exists(old / "data")) {
            fs::remove_all(tmp / "data", ec);   // a data/ in the zip is only the starting point
            ec.clear();
            fs::rename(old / "data", tmp / "data", ec);
            if (ec) { std::error_code ec2; fs::rename(old, dest, ec2); err = "Can't keep the app's data"; }
        }
        if (err.empty()) {
            fs::rename(tmp, dest, ec);
            if (ec) {
                std::error_code ec2;
                fs::rename(tmp / "data", old / "data", ec2);
                fs::rename(old, dest, ec2);
                err = "Can't move the app into place";
            } else {
                fs::remove_all(old, ec);
            }
        }
    }
    if (!err.empty()) { fs::remove_all(tmp, ec); sync(); return err; }
    sync();                       // HOME is FAT: onto the card before anyone holds the power button
    fs::remove(it.path, ec);      // installed: the zip has done its job
    sync();
    return "";
}

static std::string uninstall(const Item& it) {
    std::error_code ec;
    fs::remove_all(it.path, ec);
    // An autolaunch entry pointing into the app would now fail at every boot
    const std::string autolaunch = homeDir() + "/autolaunch";
    std::ifstream in(autolaunch);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    if (text.find(it.path.string() + "'") != std::string::npos || text.find(it.path.string() + "/") != std::string::npos)
        fs::remove(autolaunch, ec);
    sync();
    return fs::exists(it.path) ? "Couldn't remove all of " + it.folder : "";
}

// ---------------------------------------------------------------------------------------------
// Input (same as the gallery and the launcher)

enum class Action { None, Up, Down, Left, Right, A, B };

static Action fromButton(Uint8 b) {
    switch (b) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return Action::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return Action::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return Action::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Action::Right;
        case SDL_CONTROLLER_BUTTON_A:          return Action::A;
        case SDL_CONTROLLER_BUTTON_B:          return Action::B;
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
        default:             return Action::None;
    }
}

static bool isDirection(Action a) { return a == Action::Up || a == Action::Down || a == Action::Left || a == Action::Right; }

// SDL2 can't sleep until input arrives on KMSDRM (it polls every 1 ms); every 10 ms is plenty
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
// Drawing

static ImFont* uiFont = nullptr;
static ImFont* monoFont = nullptr;

static ImVec2 textSize(float size, const std::string& s) { return uiFont->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str()); }

static std::string fitText(const std::string& s, float width, float size = kTextSize) {
    if (textSize(size, s).x <= width) return s;
    std::string t = s;
    while (!t.empty() && textSize(size, t + "...").x > width) {
        t.pop_back();
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();
    }
    return t + "...";
}

static void centeredText(ImDrawList* d, float size, float cx, float y, const std::string& s, ImU32 col) {
    d->AddText(uiFont, size, ImVec2(std::floor(cx - textSize(size, s).x / 2), y), col, s.c_str());
}

// An icon is decoded only when its header says it is a sensible size
static SDL_Texture* loadIcon(SDL_Renderer* r, SDL_RWops* rw) {
    if (!rw) return nullptr;
    std::string head(24, '\0');
    const bool read = SDL_RWread(rw, &head[0], 1, 24) == 24;
    SDL_RWseek(rw, 0, RW_SEEK_SET);
    int w = 0, h = 0;
    if (!read || !pngSize(head, w, h) || w > kMaxIconSide || h > kMaxIconSide) { SDL_RWclose(rw); return nullptr; }
    return IMG_LoadTexture_RW(r, rw, 1);   // closes rw
}

static SDL_Texture* iconFor(SDL_Renderer* r, Item& it) {
    if (!it.iconTried) {
        it.iconTried = true;
        if (!it.iconData.empty()) it.icon = loadIcon(r, SDL_RWFromConstMem(it.iconData.data(), (int)it.iconData.size()));
        else if (!it.iconFile.empty()) {
            std::error_code ec;
            if (fs::file_size(it.iconFile, ec) <= kMaxIconBytes && !ec) it.icon = loadIcon(r, SDL_RWFromFile(it.iconFile.c_str(), "rb"));
        }
    }
    return it.icon;
}

static void drawIcon(ImDrawList* d, SDL_Texture* t, ImVec2 c, float maxW, float maxH) {
    int w = 1, h = 1;
    SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
    // Small icons at their own size (like the launcher), big pictures scaled down to fit
    const float s = std::min(1.0f, std::min(maxW / (float)w, maxH / (float)h));
    const float dw = std::round((float)w * s), dh = std::round((float)h * s);
    const ImVec2 a(std::floor(c.x - dw / 2), std::floor(c.y - dh / 2));
    d->AddImage((ImTextureID)(intptr_t)t, a, ImVec2(a.x + dw, a.y + dh));
}

// ---------------------------------------------------------------------------------------------

enum class Tab { Zips = 0, Installed = 1 };
enum class View { Tabs, Grid, Confirm };

struct State {
    std::vector<Item> lists[2];
    Tab tab = Tab::Zips;
    View view = View::Tabs;
    int sel = 0, firstRow = 0;
    bool yes = false;            // dialog: Cancel (false) or the action (true)
    std::string notice;
    bool noticeBad = false;

    std::vector<Item>& items() { return lists[(int)tab]; }
    int rows() { return ((int)items().size() + 1) / 2; }
    void keepVisible() {
        sel = std::max(0, std::min(sel, (int)items().size() - 1));
        const int r = sel / 2;
        if (r < firstRow) firstRow = r;
        if (r > firstRow + 1) firstRow = r - 1;
        firstRow = std::max(0, std::min(firstRow, rows() - 2));
    }
};

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return 1;
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    SDL_Window* window = SDL_CreateWindow("App Installer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          (int)kW, (int)kH, SDL_WINDOW_SHOWN);
    if (!window) return 1;
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) return 1;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i) && SDL_GameControllerOpen(i)) break;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig fontCfg;
    fontCfg.Flags |= ImFontFlags_NoLoadError;
    uiFont = io.Fonts->AddFontFromFileTTF(kFont, kTextSize, &fontCfg);
    monoFont = io.Fonts->AddFontDefault();
    if (!uiFont) uiFont = monoFont;
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    SDL_Texture* defaultIcon = IMG_LoadTexture(renderer, kDefaultIcon);

    std::error_code ec;
    fs::create_directories(appsDir(), ec);
    recover();

    State s;
    auto reload = [&] {
        for (auto& list : s.lists) for (auto& it : list) if (it.icon) SDL_DestroyTexture(it.icon);
        s.lists[0] = scanZips();
        s.lists[1] = scanInstalled();
        s.keepVisible();
        if (s.items().empty() && s.view == View::Grid) s.view = View::Tabs;
    };
    reload();

    // One frame of the current screen (also used while installing, to show progress)
    std::string busy;
    auto drawFrame = [&] {
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImDrawList* d = ImGui::GetBackgroundDrawList();
        auto& items = s.items();
        const int n = (int)items.size();

        if (!busy.empty()) {
            d->AddRectFilled(ImVec2(0, 0), ImVec2(kW, kH), kBg);
            centeredText(d, kTextSize, kW / 2, kH / 2 - kTextSize, fitText(busy, kW - 2 * kPad), kText);
        } else if (s.view == View::Confirm) {
            // Laid out like are-you-sure
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Confirm", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushFont(monoFont, 13.0f * 1.65f);
            const ImVec2 ws = ImGui::GetWindowSize();
            const Item& it = items[s.sel];
            const char* verb = s.tab == Tab::Installed ? "Uninstall" : it.update ? "Update" : "Install";
            const std::string msg = std::string(verb) + " " + it.m.name + "?";
            const std::string note = s.tab == Tab::Installed ? "Its folder and everything in it are deleted"
                                                             : "The zip is removed once it is installed";
            ImGui::SetCursorPos(ImVec2((ws.x - ImGui::CalcTextSize(msg.c_str()).x) * 0.5f, ws.y * 0.4f));
            ImGui::TextUnformatted(msg.c_str());
            ImGui::SetCursorPosX((ws.x - ImGui::CalcTextSize(note.c_str()).x) * 0.5f);
            ImGui::TextDisabled("%s", note.c_str());
            const float bw = 140, bh = 50, spacing = 40;
            ImGui::SetCursorPos(ImVec2((ws.x - (bw * 2 + spacing)) * 0.5f, ws.y * 0.6f));
            const char* labels[2] = {"Cancel", verb};
            for (int i = 0; i < 2; ++i) {
                if (i) ImGui::SameLine(0, spacing);
                const bool chosen = (i == 1) == s.yes;
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
        } else {
            d->AddRectFilled(ImVec2(0, 0), ImVec2(kW, kH), kBg);
            const char* names[2] = {"Install Files", "Installed Apps"};
            for (int i = 0; i < 2; ++i) {
                const ImVec2 a(i * kW / 2 + (i ? 1.0f : 0.0f), 0), b((i + 1) * kW / 2 - (i ? 0.0f : 1.0f), kTabH);
                d->AddRectFilled(a, b, i == (int)s.tab ? kTabOn : kTabOff);
                centeredText(d, kTextSize, (a.x + b.x) / 2, (kTabH - kTextSize) / 2 - 1, names[i], kText);
                if (s.view == View::Tabs && i == (int)s.tab)
                    d->AddRect(ImVec2(a.x + kBorder / 2, a.y + kBorder / 2), ImVec2(b.x - kBorder / 2, b.y - kBorder / 2), kYellow, 0, 0, kBorder);
            }
            if (n == 0) {
                const float cy = kGridTop + (kH - kGridTop) / 2 - 40;
                if (defaultIcon) drawIcon(d, defaultIcon, ImVec2(kW / 2, cy - 30), 96, 96);
                if (s.tab == Tab::Zips) {
                    centeredText(d, kTextSize, kW / 2, cy + 20, "No app zips to install", kText);
                    centeredText(d, kTextSize, kW / 2, cy + 52, "Copy them into the apps folder on HOME", kTextDim);
                } else {
                    centeredText(d, kTextSize, kW / 2, cy + 20, "No apps installed yet", kText);
                }
                if (!s.notice.empty())
                    d->AddText(uiFont, kTextSize, ImVec2(kPad, kH - kFooterH + (kFooterH - kTextSize) / 2 - 4),
                               s.noticeBad ? kWarn : kText, fitText(s.notice, kW - 2 * kPad).c_str());
            } else {
                for (int r = s.firstRow; r < s.firstRow + 2 && r < s.rows(); ++r) {
                    for (int c = 0; c < 2; ++c) {
                        const int i = r * 2 + c;
                        if (i >= n) break;
                        Item& it = items[i];
                        const ImVec2 a(kPad + c * (kCellW + kPad), kGridTop + (r - s.firstRow) * (kCellH + kPad));
                        const ImVec2 b(a.x + kCellW, a.y + kCellH);
                        d->AddRectFilled(a, b, kTile);
                        SDL_Texture* t = iconFor(renderer, it);
                        if (!t) t = defaultIcon;
                        if (t) drawIcon(d, t, ImVec2((a.x + b.x) / 2, a.y + 12 + (kCellH - 60) / 2), kCellW - 24, kCellH - 60);
                        const std::string label = fitText(it.m.name, kCellW - 16);
                        centeredText(d, kTextSize, (a.x + b.x) / 2, b.y - 40, label, it.error.empty() ? kText : kWarn);
                        if (s.view == View::Grid && i == s.sel)
                            d->AddRect(ImVec2(a.x - kBorder / 2, a.y - kBorder / 2), ImVec2(b.x + kBorder / 2, b.y + kBorder / 2), kYellow, 0, 0, kBorder);
                    }
                }
                const float fy = kH - kFooterH + (kFooterH - kTextSize) / 2 - 4;
                const std::string count = std::to_string(s.sel + 1) + " / " + std::to_string(n);
                const float countW = textSize(kTextSize, count).x;
                const Item& it = items[s.sel];
                std::string label;
                ImU32 col = kText;
                if (!s.notice.empty()) { label = s.notice; col = s.noticeBad ? kWarn : kText; }
                else if (s.view == View::Tabs) label = std::to_string(n) + (s.tab == Tab::Zips ? " to install" : " installed");
                else if (!it.error.empty()) { label = it.error; col = kWarn; }
                else label = (it.m.version.empty() ? "" : "v" + it.m.version + "   ") + (it.m.description.empty() ? it.folder : it.m.description);
                d->AddText(uiFont, kTextSize, ImVec2(kPad, fy), col, fitText(label, kW - 3 * kPad - countW).c_str());
                d->AddText(uiFont, kTextSize, ImVec2(kW - kPad - countW, fy), kTextDim, count.c_str());
            }
        }
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    };

    bool running = true;
    auto act = [&](Action a) {
        const int n = (int)s.items().size();
        s.notice.clear();
        switch (s.view) {
            case View::Tabs:
                if (a == Action::Left || a == Action::Right) {
                    const Tab t = a == Action::Left ? Tab::Zips : Tab::Installed;
                    if (t != s.tab) { s.tab = t; s.sel = s.firstRow = 0; }
                } else if ((a == Action::Down || a == Action::A) && n > 0) s.view = View::Grid;
                else if (a == Action::B) running = false;
                break;
            case View::Grid:
                if (a == Action::Left && s.sel % 2 == 1) --s.sel;
                else if (a == Action::Right && s.sel % 2 == 0 && s.sel + 1 < n) ++s.sel;
                else if (a == Action::Up) { if (s.sel >= 2) s.sel -= 2; else s.view = View::Tabs; }
                else if (a == Action::Down && s.sel / 2 + 1 < s.rows()) s.sel = std::min(s.sel + 2, n - 1);
                else if (a == Action::A) {
                    if (!s.items()[s.sel].error.empty()) { s.notice = s.items()[s.sel].error; s.noticeBad = true; }
                    else { s.view = View::Confirm; s.yes = false; }
                }
                else if (a == Action::B) s.view = View::Tabs;
                s.keepVisible();
                break;
            case View::Confirm:
                if (a == Action::Left || a == Action::Right) s.yes = a == Action::Right;
                else if (a == Action::A && s.yes) {
                    const Item it = s.items()[s.sel];
                    std::string err;
                    if (s.tab == Tab::Zips) {
                        busy = "Installing " + it.m.name + "...";
                        drawFrame();
                        err = install(it, [&](float f) {
                            busy = "Installing " + it.m.name + "...  " + std::to_string((int)(f * 100)) + "%";
                            drawFrame();
                        });
                        busy.clear();
                        s.notice = err.empty() ? "Installed " + it.m.name + ": it is in the launcher's Apps row" : err;
                    } else {
                        err = uninstall(it);
                        s.notice = err.empty() ? "Uninstalled " + it.m.name : err;
                    }
                    s.noticeBad = !err.empty();
                    s.view = View::Grid;
                    reload();
                } else if (a == Action::A || a == Action::B) s.view = View::Grid;
                break;
        }
    };

    Action held = Action::None, stickDir = Action::None;
    Uint32 nextRepeat = 0;
    int framesToDraw = 2;
    while (running) {
        int timeout = 60000;
        if (held != Action::None) timeout = std::max(0, (int)(nextRepeat - SDL_GetTicks()));
        if (framesToDraw > 0) timeout = 0;
        SDL_Event ev;
        if (waitEvent(&ev, timeout)) {
            do {
                ImGui_ImplSDL2_ProcessEvent(&ev);
                Action a = Action::None;
                switch (ev.type) {
                    case SDL_QUIT: running = false; break;
                    case SDL_KEYDOWN: if (!ev.key.repeat) a = fromKey(ev.key.keysym.sym); break;
                    case SDL_CONTROLLERBUTTONDOWN: a = fromButton(ev.cbutton.button); break;
                    case SDL_KEYUP: if (fromKey(ev.key.keysym.sym) == held) held = Action::None; break;
                    case SDL_CONTROLLERBUTTONUP: if (fromButton(ev.cbutton.button) == held) held = Action::None; break;
                    case SDL_CONTROLLERAXISMOTION:
                        if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                            SDL_GameController* c = SDL_GameControllerFromInstanceID(ev.caxis.which);
                            const int x = c ? SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) : 0;
                            const int y = c ? SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) : 0;
                            const int limit = stickDir == Action::None ? kStickPress : kStickRelease;
                            Action d = Action::None;
                            if (std::abs(x) >= std::abs(y) && std::abs(x) > limit) d = x < 0 ? Action::Left : Action::Right;
                            else if (std::abs(y) > limit) d = y < 0 ? Action::Up : Action::Down;
                            if (d != stickDir) { if (held == stickDir) held = Action::None; stickDir = d; a = d; }
                        }
                        break;
                    case SDL_CONTROLLERDEVICEADDED:
                        if (SDL_IsGameController(ev.cdevice.which) &&
                            !SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(ev.cdevice.which)))
                            SDL_GameControllerOpen(ev.cdevice.which);
                        break;
                }
                if (a != Action::None) {
                    act(a);
                    if (isDirection(a)) { held = a; nextRepeat = SDL_GetTicks() + kRepeatDelayMs; }
                    framesToDraw = 2;
                }
                if (ev.type == SDL_CONTROLLERBUTTONUP || ev.type == SDL_KEYUP) framesToDraw = std::max(framesToDraw, 1);
            } while (running && SDL_PollEvent(&ev));
        } else if (held != Action::None && SDL_TICKS_PASSED(SDL_GetTicks(), nextRepeat)) {
            act(held);
            nextRepeat = SDL_GetTicks() + kRepeatRateMs;
            framesToDraw = 2;
        }
        if (!running) break;
        if (framesToDraw <= 0) continue;
        drawFrame();
        --framesToDraw;
    }

    for (auto& list : s.lists) for (auto& it : list) if (it.icon) SDL_DestroyTexture(it.icon);
    if (defaultIcon) SDL_DestroyTexture(defaultIcon);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
