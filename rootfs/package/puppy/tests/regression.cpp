#include "catalog/catalog.h"
#include "input/input.h"
#include "domain/model.h"
#include "support/shell.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path dir = argv[1];
    fs::create_directories(dir / "roms");
    fs::create_directories(dir / "icons");
    fs::create_directories(dir / "applications");
    std::ofstream(dir / "roms/Zelda.GBA");
    std::ofstream(dir / "roms/Alice's game.gba");
    std::ofstream(dir / "roms/.hidden.gba");
    std::ofstream(dir / "roms/notes.txt");
    std::ofstream(dir / "icons/Zelda.png");
    std::ofstream(dir / "system.puppy") <<
        "[ENTRY]\nCATEGORY=Tools\nNAME=Settings\nCOMMAND=old-command\n"
        "[ENTRY]\nNAME=Incomplete\n"
        "[ARCHIVE]\nNAME=GBA\nCOMMAND=retroarch -L mgba %s\n"
        "ENTRY_DIRECTORIES=" << (dir / "roms").string() << ";\n"
        "ENTRY_EXTENSIONS=.gba;\nENTRY_ICONS_DIRECTORIES=" << (dir / "icons").string() << ";\n"
        "DEFAULT_ICON=fallback.png\n";
    std::ofstream(dir / "user.puppy") <<
        "[ENTRY]\nCATEGORY=Tools\nNAME=Settings\nCOMMAND=user-command\n";
    std::ofstream(dir / "applications/override.puppy") <<
        "[ENTRY]\nCATEGORY=Tools\nNAME=Settings\nCOMMAND=module-command\nTERMINAL=1\n";
    std::ofstream(dir / "applications/ignored.txt") <<
        "[ENTRY]\nNAME=Ignored\nCOMMAND=false\n";

    Model m;
    m.categories = loadCatalog({(dir / "system.puppy").string(), (dir / "user.puppy").string()},
                               {(dir / "applications").string()});
    assert(m.categories.size() == 2);
    assert(m.selected()->name == "Settings");
    assert(m.selected()->command == "module-command");
    assert(m.selected()->terminal);
    const auto& games = m.categories[1].entries;
    assert(games.size() == 2);
    assert(games[0].name == "Alice's game");
    assert(games[0].command == "retroarch -L mgba " + shellQuote((dir / "roms/Alice's game.gba").string()));
    assert(games[0].iconPath == "fallback.png");
    assert(games[1].iconPath == (dir / "icons/Zelda.png").string());
    assert(shellQuote("a'b") == "'a'\\''b'");

    m.move(0, 1);
    m.move(100, 0);
    assert(m.selected()->name == "Zelda");
    m.move(0, -1);
    assert(m.selected()->name == "Settings");
    m.move(0, 1);
    assert(m.selected()->name == "Zelda"); // each category remembers its column
    m.move(-100, 0);
    assert(m.col() == 0);
    int row = -1, col = -1;
    assert(m.find("GBA", "Zelda", row, col) && row == 1 && col == 1);
    assert(!m.find("GBA", "missing", row, col));
    m.autoRow = 1; m.autoCol = 1;
    assert(m.isAutoStart(1, 1) && !m.isAutoStart(1, 0));
    Model empty;
    empty.move(1, 1);
    assert(empty.selected() == nullptr);

    assert(actionFromKey(SDLK_RETURN) == Action::Launch);
    assert(actionFromKey(SDLK_SPACE) == Action::ToggleAutoStart);
    assert(actionFromKey(SDLK_ESCAPE) == Action::None);
    assert(actionFromButton(SDL_CONTROLLER_BUTTON_A) == Action::Launch);
    assert(actionFromButton(SDL_CONTROLLER_BUTTON_DPAD_UP) == Action::Up);
    assert(actionFromStick(15999, 0, Action::None) == Action::None);
    assert(actionFromStick(16000, 0, Action::None) == Action::Right);
    assert(actionFromStick(9000, 10000, Action::Right) == Action::Right);
    assert(actionFromStick(7999, 0, Action::Right) == Action::None);
    assert(actionFromStick(9000, -17000, Action::Right) == Action::Up);
    std::cout << "Catalogue, navigation, quoting, and input checks passed.\n";
}
