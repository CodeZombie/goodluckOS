#include <SDL2/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <array>
#include <cstdio>
#include <sys/wait.h>

// Helper to execute a command, capture its stdout/stderr, and return the exit code
std::string ExecuteCommand(const char* cmd, int& out_exit_code) {
    // Append 2>&1 to capture stderr in the same stream as stdout
    std::string full_cmd = std::string(cmd) + " 2>&1";

    std::array<char, 128> buffer;
    std::string result;

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        out_exit_code = -1;
        return "popen() failed to execute command.";
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    int status = pclose(pipe);

    if (WIFEXITED(status)) {
        out_exit_code = WEXITSTATUS(status);
    } else {
        out_exit_code = -1; // Process terminated abnormally
    }

    return result;
}

enum class AppState {
    Prompt,
    Error
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: are-you-sure \"<Message>\" \"<command>\"" << std::endl;
        return -1;
    }

    const char* message = argv[1];
    const char* command = argv[2];

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("Are You Sure?",
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

    AppState state = AppState::Prompt;
    std::string errorOutput = "";
    int exitCode = 0;

    bool running = true;
    while (running) {
        SDL_Event event;

        if (SDL_WaitEvent(&event)) {
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

        ImGui::Begin("Are You Sure?", nullptr, window_flags);
        ImVec2 windowSize = ImGui::GetWindowSize();

        if (state == AppState::Prompt) {
            ImVec2 textSize = ImGui::CalcTextSize(message);
            ImGui::SetCursorPos(ImVec2((windowSize.x - textSize.x) * 0.5f, windowSize.y * 0.4f));
            ImGui::Text("%s", message);

            float buttonWidth = 140.0f;
            float buttonHeight = 50.0f;
            float spacing = 40.0f;
            float totalButtonWidth = (buttonWidth * 2) + spacing;

            ImGui::SetCursorPos(ImVec2((windowSize.x - totalButtonWidth) * 0.5f, windowSize.y * 0.6f));

            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, buttonHeight))) {
                running = false;
            }

            ImGui::SameLine(0.0f, spacing);

            if (ImGui::Button("Accept", ImVec2(buttonWidth, buttonHeight))) {
                errorOutput = ExecuteCommand(command, exitCode);

                if (exitCode != 0) {
                    state = AppState::Error;
                } else {
                    running = false;
                }
            }
        }
        else if (state == AppState::Error) {
            float buttonWidth = 140.0f;
            float buttonHeight = 50.0f;

            ImGui::Text("Command Failed (Exit Code: %d)", exitCode);
            ImGui::Separator();

            // Create a scrollable child region for the error text in case it's long
            ImGui::BeginChild("ErrorTextRegion", ImVec2(0, windowSize.y - buttonHeight - 60.0f), true);
            if (errorOutput.empty()) {
                ImGui::TextWrapped("No output returned.");
            } else {
                ImGui::TextWrapped("%s", errorOutput.c_str());
            }
            ImGui::EndChild();

            ImGui::SetCursorPos(ImVec2((windowSize.x - buttonWidth) * 0.5f, windowSize.y - buttonHeight - 10.0f));

            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }

            if (ImGui::Button("Close", ImVec2(buttonWidth, buttonHeight))) {
                running = false;
            }
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
