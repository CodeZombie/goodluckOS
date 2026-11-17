#include <SDL2/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <fstream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <cstdlib>
#include <string>

const char* ALSA_MIXER_NAME = "Headphone";
const char* ALSA_CARD = "GA36mbAudio";

int get_brightness() {
    return 5;
}
void set_brightness(int brightness) {
    std::ofstream file("/sys/class/backlight/backlight/brightness");
    if (file.is_open()) {
        file << brightness;
    }
}

// The USB data port: HOME over MTP, or a network adapter for SSH and the PC's shared internet (usb-mode.sh)
bool get_usb_network() {
    std::ifstream file("/etc/player-flags/usb-mode");
    std::string mode;
    return std::getline(file, mode) && mode == "network";
}
void set_usb_network(bool network) {
    system(network ? "doas /usr/local/bin/usb-mode.sh network" : "doas /usr/local/bin/usb-mode.sh mtp");
}
// The address a PC or phone sharing its internet handed out, next to the console's own 192.168.36.1
std::string get_usb_address() {
    std::string address;
    struct ifaddrs* list;
    if (getifaddrs(&list) != 0) return address;
    for (struct ifaddrs* a = list; a; a = a->ifa_next) {
        if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && std::string(a->ifa_name) == "usb0") {
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in*)a->ifa_addr)->sin_addr, buf, sizeof(buf));
            if (std::string(buf) != "192.168.36.1") address = buf;
        }
    }
    freeifaddrs(list);
    return address;
}

long get_alsa_volume() {
    long min, max, vol = 0;
    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;

    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, ALSA_CARD);
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, ALSA_MIXER_NAME);

    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);
    if (elem) {
        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &vol);
        vol = (vol * 100) / max;
    }
    snd_mixer_close(handle);
    return vol;
}
void set_alsa_volume(long volume) {
    long min, max;
    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;

    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, ALSA_CARD);
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, ALSA_MIXER_NAME);

    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);
    if (elem) {
        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        long scaled_vol = (volume * max) / 100;
        snd_mixer_selem_set_playback_volume_all(elem, scaled_vol);
    }
    snd_mixer_close(handle);
}
bool get_alsa_mute() {
    int switch_state = 1;
    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;

    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, ALSA_CARD);
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, ALSA_MIXER_NAME);

    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);
    if (elem && snd_mixer_selem_has_playback_switch(elem)) {
        snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_FRONT_LEFT, &switch_state);
    }
    snd_mixer_close(handle);

    return (switch_state == 0); // 0 = muted, 1 = unmuted
}

void set_alsa_mute(bool mute) {
    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;

    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, ALSA_CARD);
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, ALSA_MIXER_NAME);

    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);
    if (elem && snd_mixer_selem_has_playback_switch(elem)) {
        snd_mixer_selem_set_playback_switch_all(elem, mute ? 0 : 1);
    }
    snd_mixer_close(handle);
}



void section_headear(const char* text) {
    float windowWidth = ImGui::GetWindowSize().x;
    float textWidth = ImGui::CalcTextSize(text).x;

    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::Text("%s", text);
    ImGui::Separator();
    ImGui::Spacing();
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("System Settings",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 480, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

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

    // Settings Values
    bool usb_network = get_usb_network();
    int display_brightness = get_brightness();
    int current_volume = get_alsa_volume();
    bool current_mute = get_alsa_mute();


    bool running = true;
    while (running) {
        SDL_Event event;

        // On the USB network, redraw every second so the address shows up when the PC hands it out
        if (usb_network ? SDL_WaitEventTimeout(&event, 1000) : SDL_WaitEvent(&event)) {
            do {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) {
                    running = false;
                }
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

        ImGui::Begin("Settings", nullptr, window_flags);
        ImGui::Text("System Settings");
        ImGui::Separator();
        ImGui::Spacing();

        section_headear("Display");
        if (ImGui::SliderInt("Brightness", &display_brightness, 1, 10)) {
            set_brightness(display_brightness);
        }

        section_headear("Audio Settings");
        if (ImGui::SliderInt("Master Volume", &current_volume, 0, 100)) {
            set_alsa_volume(current_volume);

            if (current_mute && current_volume > 0) {
                current_mute = false;
                set_alsa_mute(current_mute);
            }
        }

        if (ImGui::Checkbox("Global Mute", &current_mute)) {
            set_alsa_mute(current_mute);
        }

        section_headear("USB");
        if (ImGui::RadioButton("Files (MTP)", !usb_network) && usb_network) {
            usb_network = false;
            set_usb_network(false);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Network", usb_network) && !usb_network) {
            usb_network = true;
            set_usb_network(true);
        }
        if (usb_network) {
            ImGui::Text("SSH: root@192.168.36.1");
            std::string address = get_usb_address();
            if (address.empty()) {
                ImGui::Text("Internet: share the PC's connection");
            } else {
                ImGui::Text("Internet: %s", address.c_str());
            }
        }

        if (ImGui::Button("System Settings")) {
            // CPU Governer [select box]
            // CPU info [label]
            // Ram info [label]
            // Show free space on SD card
            // Maybe move this to a dedicated System Info app?
        }
        if (ImGui::Button("Input Settings")) {
            // Swap A/B [toggle]
            // Input Tester (maybe this should be it's own app?)
        }
        if (ImGui::Button("Date/Time")) {
            // Set date
            // Set Time
        }

        ImGui::Spacing();
        if (ImGui::Button("Exit Application")) {
            running = false;
        }

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
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
