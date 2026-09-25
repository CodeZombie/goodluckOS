#include <SDL2/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

constexpr const char* kMountPoint = "/home/player";
constexpr const char* kRequestFlag = "/etc/player-flags/.home_resize_requested";
constexpr const char* kStatusFile = "/etc/player-flags/.home_resize_status";
constexpr const char* kDiskSizePath = "/sys/class/block/mmcblk0/size";
constexpr const char* kPartStartPath = "/sys/class/block/mmcblk0p2/start";
constexpr const char* kPartSizePath = "/sys/class/block/mmcblk0p2/size";
constexpr const char* kSectorSizePath = "/sys/block/mmcblk0/queue/hw_sector_size";

constexpr uint64_t kDefaultSectorSize = 512;

constexpr float kSidePadding = 24.0f;
constexpr float kButtonHeight = 55.0f;
constexpr float kRowSpacing = 16.0f;
constexpr float kBottomMargin = 20.0f;

std::optional<uint64_t> read_sysfs_u64(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) return std::nullopt;
    uint64_t value = 0;
    f >> value;
    if (f.fail()) return std::nullopt;
    return value;
}

bool path_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

std::map<std::string, std::string> read_key_value_file(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

std::string human_size_kb(uint64_t kb) {
    char buf[64];
    double val = static_cast<double>(kb);
    const char* unit = "KB";
    if (val >= 1024.0 * 1024.0) {
        val /= 1024.0 * 1024.0;
        unit = "GB";
    } else if (val >= 1024.0) {
        val /= 1024.0;
        unit = "MB";
    }
    snprintf(buf, sizeof(buf), "%.2f %s", val, unit);
    return std::string(buf);
}

bool write_request_flag(std::string& err) {
    int fd = open(kRequestFlag, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        err = std::string("Could not create resize-home flag file: ") + strerror(errno);
        return false;
    }

    sync();
    return true;
}

struct DiskStats {
    bool valid = false;
    bool pending_reboot = false;
    std::string error;

    uint64_t disk_sectors = 0;
    uint64_t part_start = 0;
    uint64_t part_size_sectors = 0;
    uint64_t sector_size = kDefaultSectorSize;

    uint64_t part_size_kb = 0;
    uint64_t free_unallocated_kb = 0;

    bool fs_mounted = false;
    uint64_t fs_total_kb = 0;
    uint64_t fs_used_kb = 0;
    uint64_t fs_free_kb = 0;
};

DiskStats gather_stats() {
    DiskStats s;
    s.pending_reboot = path_exists(kRequestFlag);

    auto disk_sectors = read_sysfs_u64(kDiskSizePath);
    auto part_start = read_sysfs_u64(kPartStartPath);
    auto part_size = read_sysfs_u64(kPartSizePath);

    if (!disk_sectors || !part_start || !part_size) {
        s.error = "Could not read partition table information from sysfs.";
        return s;
    }

    s.disk_sectors = *disk_sectors;
    s.part_start = *part_start;
    s.part_size_sectors = *part_size;
    s.sector_size = read_sysfs_u64(kSectorSizePath).value_or(kDefaultSectorSize);

    uint64_t part_end = s.part_start + s.part_size_sectors - 1;
    uint64_t free_sectors = (s.disk_sectors > part_end + 1) ? (s.disk_sectors - part_end - 1) : 0;

    s.part_size_kb = s.part_size_sectors * s.sector_size / 1024;
    s.free_unallocated_kb = free_sectors * s.sector_size / 1024;

    struct statvfs vfs;
    if (statvfs(kMountPoint, &vfs) == 0) {
        s.fs_mounted = true;
        s.fs_total_kb = (static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize) / 1024;
        uint64_t free_kb = (static_cast<uint64_t>(vfs.f_bfree) * vfs.f_frsize) / 1024;
        s.fs_free_kb = free_kb;
        s.fs_used_kb = (s.fs_total_kb > free_kb) ? (s.fs_total_kb - free_kb) : 0;
    }

    s.valid = true;
    return s;
}

float content_width() {
    return ImGui::GetWindowWidth() - kSidePadding * 2.0f;
}

// centers a single line of text
void center_text(const std::string& text) {
    float x = (ImGui::GetWindowWidth() - ImGui::CalcTextSize(text.c_str()).x) * 0.5f;
    ImGui::SetCursorPosX(x > kSidePadding ? x : kSidePadding);
    ImGui::TextUnformatted(text.c_str());
}

void center_text_colored(const ImVec4& color, const std::string& text) {
    float x = (ImGui::GetWindowWidth() - ImGui::CalcTextSize(text.c_str()).x) * 0.5f;
    ImGui::SetCursorPosX(x > kSidePadding ? x : kSidePadding);
    ImGui::TextColored(color, "%s", text.c_str());
}

// centers a multi-line wrapped string
void center_wrapped(const std::string& text, float width_fraction = 0.85f) {
    float width = ImGui::GetWindowWidth() * width_fraction;
    float x = (ImGui::GetWindowWidth() - width) * 0.5f;
    ImGui::SetCursorPosX(x);
    ImGui::PushTextWrapPos(x + width);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopTextWrapPos();
}

// A single button spanning the full width of the screen
bool full_width_button(const char* label, float height = kButtonHeight) {
    ImGui::SetCursorPosX(kSidePadding);
    return ImGui::Button(label, ImVec2(content_width(), height));
}

// Two buttons sharing a row, each filling half the content width.
void two_button_row(const char* left_label, const char* right_label, bool& left_clicked, bool& right_clicked, float height = kButtonHeight) {
    float button_width = (content_width() - kRowSpacing) / 2.0f;
    ImGui::SetCursorPosX(kSidePadding);
    left_clicked = ImGui::Button(left_label, ImVec2(button_width, height));
    ImGui::SameLine(0.0f, kRowSpacing);
    right_clicked = ImGui::Button(right_label, ImVec2(button_width, height));
}

void anchor_to_bottom(float area_height) {
    float y = ImGui::GetWindowHeight() - area_height - kBottomMargin;
    float min_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(y > min_y ? y : min_y);
}

}

enum class Page {
    MAIN,
    CONFIRM,
    PENDING_REBOOT,
    ERROR_PAGE,
    REBOOTING,
    EXIT,
};

struct AppState {
    DiskStats stats;
    std::map<std::string, std::string> last_status;
    Page current_page = Page::MAIN;
    std::string error_message;
    bool reboot_command_issued = false;

    void refresh() {
        stats = gather_stats();
        last_status = path_exists(kStatusFile) ? read_key_value_file(kStatusFile) : std::map<std::string, std::string>{};
    }
};

Page render_error(AppState& state) {
    Page next = Page::ERROR_PAGE;

    center_text("Something went wrong");
    ImGui::Spacing();
    center_wrapped(state.error_message);

    anchor_to_bottom(kButtonHeight);
    if (full_width_button("Okay")) {
        state.refresh();
        next = Page::MAIN;
    }

    return next;
}

Page render_reboot(AppState& state) {
    ImGui::Spacing();
    ImGui::Spacing();
    center_text("Rebooting...");
    center_wrapped("Please wait while the device restarts.");
    return Page::REBOOTING;
}

Page render_confirm(AppState& state) {
    Page next = Page::CONFIRM;

    center_text("Are you sure?");
    ImGui::Spacing();
    center_wrapped("You will gain " + human_size_kb(state.stats.free_unallocated_kb) +
                    " after resizing. This backs up your files, expands the partition "
                    "to fill the card, reformats it, and restores your files. The "
                    "device will reboot to do this.");

    if (state.stats.free_unallocated_kb == 0) {
        ImGui::Spacing();
        center_text_colored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
            "Heads up: there's no unused space left to reclaim.");
        center_wrapped("You probably don't need to do this, but it's safe to run anyway.");
    }

    anchor_to_bottom(kButtonHeight);
    bool back_clicked = false;
    bool confirm_clicked = false;
    two_button_row("Back", "Do It.", back_clicked, confirm_clicked);

    if (back_clicked) {
        next = Page::MAIN;
    } else if (confirm_clicked) {
        std::string err;
        if (write_request_flag(err)) {
            state.reboot_command_issued = false;
            next = Page::REBOOTING;
        } else {
            state.error_message = err;
            next = Page::ERROR_PAGE;
        }
    }

    return next;
}

Page render_pending_reboot(AppState& state) {
    Page next = Page::PENDING_REBOOT;

    center_text_colored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "A resize is already scheduled for the next boot.");
    ImGui::Spacing();
    center_wrapped("Reboot the device to apply it. If you didn't request this, you can cancel the request below.");

    anchor_to_bottom(kButtonHeight);
    bool close_clicked = false;
    bool cancel_clicked = false;
    two_button_row("Close", "Cancel Resize Request", close_clicked, cancel_clicked);

    if (close_clicked) {
        next = Page::EXIT;
    } else if (cancel_clicked) {
        std::error_code ec;
        bool removed = fs::remove(kRequestFlag, ec);
        if (ec) {
            state.error_message = "Could not remove request flag: " + ec.message();
            next = Page::ERROR_PAGE;
        } else if (!removed) {
            state.error_message = "Request flag was not found (it may have already been removed).";
            next = Page::ERROR_PAGE;
        } else {
            state.refresh();
            next = Page::MAIN;
        }
    }

    return next;
}

Page render_main(AppState& state) {
    Page next = Page::MAIN;
    const DiskStats& stats = state.stats;

    center_wrapped("This utility resizes your HOME partition to fill the remaining space on your microSD card.");
    ImGui::Spacing();

    if (!stats.valid) {
        center_text_colored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), stats.error);
    } else {
        center_text("microSD card total size: " + human_size_kb(stats.disk_sectors * stats.sector_size / 1024));
        center_text("HOME partition size: " + human_size_kb(stats.part_size_kb));

        if (stats.fs_mounted) {
            center_text("HOME filesystem size: " + human_size_kb(stats.fs_total_kb));
            center_text("HOME used: " + human_size_kb(stats.fs_used_kb));
            center_text("HOME free: " + human_size_kb(stats.fs_free_kb));
        } else {
            center_text_colored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "HOME is not currently mounted.");
        }

        ImGui::Spacing();
        center_text("Unused space on card: " + human_size_kb(stats.free_unallocated_kb));
        ImGui::Spacing();
        ImGui::Separator();

        if (!state.last_status.empty()) {
            auto res_it = state.last_status.find("result");
            auto time_it = state.last_status.find("time");
            std::string res = res_it != state.last_status.end() ? res_it->second : "unknown";
            std::string time = time_it != state.last_status.end() ? time_it->second : "";
            ImVec4 color = (res == "success") ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            center_text_colored(color, "Last resize attempt: " + res + " (" + time + ")");
            ImGui::Spacing();
        }
    }

    anchor_to_bottom(kButtonHeight * 2.0f + kRowSpacing);

    if (stats.valid && full_width_button("Resize HOME Partition")) {
        next = Page::CONFIRM;
    }

    bool refresh_clicked = false;
    bool close_clicked = false;
    two_button_row("Refresh", "Close", refresh_clicked, close_clicked);

    if (refresh_clicked) {
        state.refresh();
    } else if (close_clicked) {
        next = Page::EXIT;
    }

    return next;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Resize HOME",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640,
        480,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    ImGui::GetStyle().FontScaleMain = 1.65;

    SDL_GameController* controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) break;
        }
    }

    AppState state;
    state.refresh();
    if (state.stats.pending_reboot) {
        state.current_page = Page::PENDING_REBOOT;
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        if (SDL_WaitEvent(&event)) {
            do {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) running = false;
            } while (SDL_PollEvent(&event));
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("ResizeHome", nullptr, window_flags);

        center_text("Resize HOME Partition Utility");
        ImGui::Separator();
        ImGui::Spacing();

        switch (state.current_page) {
            case Page::MAIN:            state.current_page = render_main(state); break;
            case Page::CONFIRM:         state.current_page = render_confirm(state); break;
            case Page::PENDING_REBOOT:  state.current_page = render_pending_reboot(state); break;
            case Page::ERROR_PAGE:      state.current_page = render_error(state); break;
            case Page::REBOOTING:       state.current_page = render_reboot(state); break;
            case Page::EXIT:            running = false; break;
        }

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        if (state.current_page == Page::REBOOTING && !state.reboot_command_issued) {
            state.reboot_command_issued = true;
            int rc = std::system("doas -n /sbin/reboot");
            if (rc != 0) {
                std::error_code ec;
                fs::remove(kRequestFlag, ec);
                state.error_message = "Failed to reboot. Does this app have permission to run 'doas reboot'?";
                state.current_page = Page::ERROR_PAGE;
            }
        }
    }

    if (controller) SDL_GameControllerClose(controller);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
