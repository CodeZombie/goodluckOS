#include "platform/platform.h"
#include "support/shell.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {
static const char* kStateFile     = "/dev/shm/launcher_state";
static const char* kAutoStartFile = "/home/player/autolaunch";

}

static void writeAutoStart(const Entry& e) {
    std::ofstream out(kAutoStartFile);
    if (out) out << "# " << e.category << "\t" << e.name << "\n" << e.command << "\n";
}

static void clearAutoStart() {
    std::error_code ec;
    fs::remove(kAutoStartFile, ec);
}

bool readAutoStartId(std::string& category, std::string& name) {
    std::ifstream in(kAutoStartFile);
    std::string line;
    if (!in || !std::getline(in, line) || line.compare(0, 2, "# ") != 0) return false;
    size_t tab = line.find('\t', 2);
    if (tab == std::string::npos) return false;
    category = line.substr(2, tab - 2);
    name = line.substr(tab + 1);
    return true;
}

void toggleAutoStart(Model& m) {
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

void launch(const Entry& e) {
    // Save cursor position to file
    std::ofstream state(kStateFile);
    if (state) state << e.category << "\n" << e.name << "\n";

    std::ostringstream commandStream;
    commandStream << "doas appctl launch-application " << shellQuote(e.command);
    if (e.terminal) {
        commandStream << " 1";
    }
    commandStream << "\n";

    int exitCode = std::system(commandStream.str().c_str());
    (void)exitCode;
}

void restoreCursor(Model& m) {
    std::ifstream in(kStateFile);
    std::string category, name;
    if (!in || !std::getline(in, category) || !std::getline(in, name)) return;
    int r, c;
    if (m.find(category, name, r, c)) { m.row = r; m.categories[r].col = c; }
}

int readBattery() {
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
