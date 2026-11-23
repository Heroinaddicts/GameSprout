#include "PlayerInput.h"
#include "PlayerPackage.h"
#include "PlatformInput/SdlInputDevices.h"
#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include "RuntimeScriptApi.h"
#include <SDL3/SDL_platform_defines.h>
#if !defined(SDL_PLATFORM_IOS) && !defined(SDL_PLATFORM_ANDROID) && !defined(GAMESPROUT_PLAYER_HARMONYOS)
#define SDL_MAIN_HANDLED
#endif
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <iostream>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#if defined(SDL_PLATFORM_ANDROID) || defined(GAMESPROUT_PLAYER_HARMONYOS)
namespace GameSproutPlayer {
bool MobilePaths(std::filesystem::path& binary, std::filesystem::path& directory, std::string& error);
}
#endif

namespace {
using GameSproutRuntime::IGameSproutRuntime;
using GameSproutRuntime::InputViewportOrientation;
using GameSproutRuntime::IScene;
volatile std::sig_atomic_t Interrupted = 0;
void OnSignal(int) { Interrupted = 1; }

class PlayerLog final : public GameSproutRuntime::IRuntimeLogListener {
public:
    void OnRuntimeLog(const GameSproutRuntime::RuntimeLogRecord& record) override {
        constexpr const char* levels[]{"Debug", "Trace", "Warning", "Error"};
        const auto level = static_cast<unsigned>(record.Level);
        std::cerr << (level < 4 ? levels[level] : "Log") << ' ';
        if (!record.File.empty()) std::cerr << record.File << ':' << record.Line << ' ';
        std::cerr << record.Message << '\n';
    }
};

// Owns host objects in shutdown order. Runtime may invoke script Release while
// destroying the live scene, so GameLogic stays mapped until Runtime is gone.
struct PlayerSession {
    PlayerLog Log;
    GameSproutPlayer::Package Package;
    IGameSproutRuntime* Runtime = nullptr;
    GameSproutRuntime::IRuntimeViewport* Viewport = nullptr;
    SDL_Window* Window = nullptr;
    GameSproutPlayer::PlayerInput WindowInput;
    GameSproutPlatform::SdlInputDevices InputDevices;
    GameSproutRuntime::RuntimeInputViewportMetrics InputViewport;
    std::uint64_t InputViewportSceneID = 0;
    void* GameLogic = nullptr;
    std::string RenderDiagnostic;
    bool Executing = false;

    ~PlayerSession() {
        if (Runtime && Executing) {
            std::string error;
            if (!Runtime->EndExecution(error)) std::cerr << "Cannot stop gameplay: " << error << '\n';
        }
        if (Runtime && Window) {
            Runtime->SetInputOutputHandler(GameSproutRuntime::InputOutputTarget::Window, nullptr, nullptr);
            Runtime->SetInputOutputHandler(GameSproutRuntime::InputOutputTarget::Device, nullptr, nullptr);
        }
        InputDevices.Shutdown();
        delete Viewport;
        if (Runtime) Runtime->UnregisterLogListener(&Log);
        delete Runtime;
#if defined(_WIN32)
        if (GameLogic) ::FreeLibrary(reinterpret_cast<HMODULE>(GameLogic));
#else
        if (GameLogic) ::dlclose(GameLogic);
#endif
        if (Window) SDL_DestroyWindow(Window);
        SDL_Quit();
    }

    bool LoadGameLogic(const std::filesystem::path& directory, std::string& error) {
        if (!Package.HasGameLogic()) return true;
#if defined(_WIN32)
        const auto path = directory / "GameLogic.dll";
        const auto library = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        GameLogic = reinterpret_cast<void*>(library);
        const auto get = library ? reinterpret_cast<GameSproutRuntime::fGetScriptModule>(
            ::GetProcAddress(library, GameSproutRuntime::ScriptModuleEntryName)) : nullptr;
        if (!library) { error = "Cannot load packaged GameLogic"; return false; }
#else
#if defined(SDL_PLATFORM_IOS)
        const auto path = directory / "Frameworks/GameLogic.framework/GameLogic";
#elif defined(SDL_PLATFORM_MACOS)
        const auto path = directory / "libGameLogic.dylib";
#else
        const auto path = directory / "libGameLogic.so";
#endif
        GameLogic = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!GameLogic) {
            const char* detail = ::dlerror();
            error = "Cannot load packaged GameLogic";
            if (detail) error += std::string(": ") + detail;
            return false;
        }
        const auto get = reinterpret_cast<GameSproutRuntime::fGetScriptModule>(
            ::dlsym(GameLogic, GameSproutRuntime::ScriptModuleEntryName));
#endif
        const auto* api = get ? get() : nullptr;
        if (!api || api->Version != GameSproutRuntime::ScriptModuleABIVersion ||
            api->Count > 65536 || (api->Count && !api->Types) ||
            api->ModuleCount > 65536 || (api->ModuleCount && !api->Modules)) {
            error = "Packaged GameLogic export or SDK ABI is invalid";
            return false;
        }
        std::vector<GameSproutRuntime::ScriptTypeRegistration> types;
        if (api->Count) types.assign(api->Types, api->Types + api->Count);
        std::vector<GameSproutRuntime::GameModuleRegistration> modules;
        if (api->ModuleCount) modules.assign(api->Modules, api->Modules + api->ModuleCount);
        if (!Runtime->ReplaceGameLogicTypes({}, types, modules, error)) return false;
        std::cout << "Loaded GameLogic (" << api->Count << " scripts, " << api->ModuleCount << " modules)" << std::endl;
        return true;
    }
};

struct LaunchOptions {
    bool Headless = false;
    unsigned Frames = 0;
};

bool ParseLaunchOptions(int argc, char** argv, LaunchOptions& options, std::string& error) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--headless") options.Headless = true;
        else if (argument.starts_with("--frames=")) {
            const auto number = argument.substr(9);
            const auto result = std::from_chars(number.data(), number.data() + number.size(), options.Frames);
            if (result.ec != std::errc{} || result.ptr != number.data() + number.size() || !options.Frames) {
                error = "Expected a positive frame count in --frames";
                return false;
            }
        } else {
            error = "Unknown Player argument: " + std::string(argument);
            return false;
        }
    }
    return true;
}

InputViewportOrientation GetInputOrientation(SDL_DisplayID display) {
    if (!display) return InputViewportOrientation::Unknown;
    switch (SDL_GetCurrentDisplayOrientation(display)) {
    case SDL_ORIENTATION_LANDSCAPE: return InputViewportOrientation::Landscape;
    case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return InputViewportOrientation::LandscapeFlipped;
    case SDL_ORIENTATION_PORTRAIT: return InputViewportOrientation::Portrait;
    case SDL_ORIENTATION_PORTRAIT_FLIPPED: return InputViewportOrientation::PortraitFlipped;
    default: return InputViewportOrientation::Unknown;
    }
}

bool IsViewportChange(const SDL_Event& event, SDL_Window* window) {
    switch (event.type) {
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_RESTORED:
        return event.window.windowID == SDL_GetWindowID(window);
    case SDL_EVENT_DISPLAY_ORIENTATION:
        return event.display.displayID == SDL_GetDisplayForWindow(window);
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        return true;
    default:
        return false;
    }
}

void PublishInputViewport(PlayerSession& player, IScene* scene) {
    player.InputViewportSceneID = scene ? scene->GetInstanceID() : 0;
    player.Runtime->SetInputViewportMetrics(scene, player.InputViewport);
}

void RefreshInputViewport(PlayerSession& player, IScene* scene) {
    int logicalWidth = 0, logicalHeight = 0, pixelWidth = 0, pixelHeight = 0;
    if (SDL_GetWindowSize(player.Window, &logicalWidth, &logicalHeight) && logicalWidth > 0 && logicalHeight > 0 &&
        SDL_GetWindowSizeInPixels(player.Window, &pixelWidth, &pixelHeight) && pixelWidth > 0 && pixelHeight > 0) {
        auto& metrics = player.InputViewport;
        metrics.ContentRect.Size = {float(logicalWidth), float(logicalHeight)};
        metrics.RenderSize = {float(pixelWidth), float(pixelHeight)};
        metrics.PixelRatio = float(pixelWidth) / float(logicalWidth);
        metrics.Orientation = GetInputOrientation(SDL_GetDisplayForWindow(player.Window));
        metrics.SafeArea = {};
#if !defined(GAMESPROUT_PLAYER_HARMONYOS)
        SDL_Rect bounds{0, 0, logicalWidth, logicalHeight}, safeArea{}, intersection{};
        if (SDL_GetWindowSafeArea(player.Window, &safeArea) && SDL_GetRectIntersection(&bounds, &safeArea, &intersection))
            metrics.SafeArea = {{float(intersection.x), float(intersection.y)}, {float(intersection.w), float(intersection.h)}};
#endif
        // SDL supplies pixel density separately. It does not report physical display DPI.
    }
    PublishInputViewport(player, scene);
}

IScene* FindActiveScene(const PlayerSession& player, std::uint64_t startupSceneID) {
    auto* startupRoot = player.Runtime->QueryGameObject(startupSceneID);
    return startupRoot ? startupRoot->GetScene() : player.Runtime->GetScene(0);
}

void BindInputScene(PlayerSession& player, IScene* scene) {
    if (player.InputViewportSceneID != (scene ? scene->GetInstanceID() : 0)) PublishInputViewport(player, scene);
}

bool RenderFrame(PlayerSession& player, GameSproutRuntime::IScene* scene, bool& presented, std::string& error) {
    GameSproutRuntime::RenderView view;
    view.Scene = scene;
    view.DrawGrid = false;
    view.DrawScene = false;
    view.DrawUI = scene != nullptr;
    view.WaitForSurface = true;
    view.EnableTexturePrefetch = true;
    if (auto* camera = scene ? scene->GetMainCamera() : nullptr; camera && camera->GetGameObject()->IsActiveInHierarchy() &&
        camera->GetViewMatrix(view.Camera.View)) {
        view.DrawScene = true;
        view.Camera.Projection = camera->GetSettings();
        const auto matrix = camera->GetGameObject()->GetTransform()->GetWorldMatrix();
        view.Camera.Position = {matrix.Data[12], matrix.Data[13], matrix.Data[14]};
    }
    if (!player.Viewport->Render(view)) {
        error = player.Viewport->GetLastError();
        return false;
    }
    const auto& diagnostic = player.Viewport->GetLastError();
    if (diagnostic != player.RenderDiagnostic) {
        player.RenderDiagnostic = diagnostic;
        if (!diagnostic.empty()) std::cerr << "Player rendering: " << diagnostic << '\n';
    }
    const auto& stats = player.Viewport->GetStatistics();
    if (stats.Width && stats.Height) {
        if (!presented) std::cout << "Presented first frame: " << stats.Width << 'x' << stats.Height
            << " (" << stats.DrawCalls << " draws)" << std::endl;
        presented = true;
        const GameSproutRuntime::Vector2 renderedSize{float(stats.Width), float(stats.Height)};
        if (player.InputViewport.RenderSize.X != renderedSize.X || player.InputViewport.RenderSize.Y != renderedSize.Y) {
            player.InputViewport.RenderSize = renderedSize;
            PublishInputViewport(player, scene);
        }
    }
    return true;
}

bool RunPlayer(int argc, char** argv, std::string& error) {
    LaunchOptions options;
    if (!ParseLaunchOptions(argc, argv, options, error)) return false;

    PlayerSession player;
    std::filesystem::path binary, directory;
#if defined(SDL_PLATFORM_ANDROID) || defined(GAMESPROUT_PLAYER_HARMONYOS)
    if (!GameSproutPlayer::MobilePaths(binary, directory, error)) return false;
#else
    const char* base = SDL_GetBasePath();
    if (!base) { error = SDL_GetError(); return false; }
#if defined(SDL_PLATFORM_MACOS)
    auto resources = std::filesystem::path(base).lexically_normal();
    if (resources.filename().empty()) resources = resources.parent_path();
    binary = resources.parent_path() / "MacOS";
    directory = resources / "Game";
#else
    binary = std::filesystem::path(base);
    directory = binary / "Game";
#endif
#endif
    if (!player.Package.Open(directory, error)) return false;
    const bool headless = options.Headless || player.Package.Headless();
    if (!SDL_Init(headless ? SDL_INIT_EVENTS : SDL_INIT_VIDEO)) { error = SDL_GetError(); return false; }

#if defined(GAMESPROUT_PLAYER_STATIC_RUNTIME)
    player.Runtime = CreateGameSproutRuntime();
#else
    player.Runtime = Export(binary.string());
#endif
    if (!player.Runtime) { error = "Cannot load packaged GameSproutRuntime"; return false; }
    player.Runtime->RegisterLogListener(&player.Log);
    player.Runtime->SetResourceProvider(&player.Package);
    if (!player.Package.LoadProjectSettings(*player.Runtime, error) || !player.LoadGameLogic(binary, error)) return false;

    GameSproutRuntime::IScene* scene = nullptr;
    if (!player.Package.LoadFirstScene(*player.Runtime, scene, error)) return false;
    const auto startupSceneID = scene->GetInstanceID();
    const auto startupSceneName = scene->GetName();
    if (!headless) {
        player.Window = SDL_CreateWindow(startupSceneName.c_str(), 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!player.Window) { error = SDL_GetError(); return false; }
        // Rendering and input share the Player's SDL window identity. Wrapping its
        // NSView on macOS creates a second SDL window that can take keyboard focus.
        const GameSproutRuntime::NativeWindow nativeWindow{
            GameSproutRuntime::NativeWindowSystem::SDL, reinterpret_cast<std::uintptr_t>(player.Window)};
        player.Viewport = player.Runtime->CreateViewport(nativeWindow, &player.Package, error);
        if (!player.Viewport) return false;
        player.WindowInput.SetWindow(player.Window);
        player.InputDevices.Initialize();
        player.Runtime->SetInputOutputHandler(GameSproutRuntime::InputOutputTarget::Window, &GameSproutPlayer::PlayerInput::Output,
            &player.WindowInput);
        player.Runtime->SetInputOutputHandler(GameSproutRuntime::InputOutputTarget::Device, &GameSproutPlatform::SdlInputDevices::Output,
            &player.InputDevices);
        RefreshInputViewport(player, scene);
    }
    scene->SetSimulationEnabled(true);
    if (!player.Runtime->BeginExecution(error)) return false;
    player.Executing = true;
    player.Runtime->SetInputEnabled(!headless);
    if (player.Window && (SDL_GetWindowFlags(player.Window) & SDL_WINDOW_INPUT_FOCUS))
        player.Runtime->SubmitInputEvent({GameSproutRuntime::InputEventType::FocusGained});
    if (player.Window) player.InputDevices.BeginFrame(*player.Runtime);
    std::cout << "Opened startup scene: " << startupSceneName << (headless ? " (headless)" : "") << std::endl;

    using Clock = std::chrono::steady_clock;
    constexpr double fixedSeconds = 1.0 / 60.0;
    double accumulator = 0;
    bool presented = false;
    bool backgrounded = false;
    auto previous = Clock::now();
    unsigned frame = 0;
    while (!Interrupted && (!options.Frames || frame < options.Frames)) {
        if (player.Window) BindInputScene(player, FindActiveScene(player, startupSceneID));
        SDL_Event event;
        bool close = false;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_TERMINATING ||
                event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) close = true;
            if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND || event.type == SDL_EVENT_DID_ENTER_BACKGROUND) {
                backgrounded = true;
            }
            if (event.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
                backgrounded = false;
                if (player.Window && (SDL_GetWindowFlags(player.Window) & SDL_WINDOW_INPUT_FOCUS))
                    player.Runtime->SubmitInputEvent({GameSproutRuntime::InputEventType::FocusGained});
            }
            if (player.Window) {
                if (IsViewportChange(event, player.Window)) RefreshInputViewport(player, FindActiveScene(player, startupSceneID));
                if (!player.InputDevices.HandleEvent(*player.Runtime, event)) player.WindowInput.HandleEvent(*player.Runtime, event);
                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_DID_ENTER_FOREGROUND)
                    player.InputDevices.BeginFrame(*player.Runtime);
            }
        }
        if (close) break;
        if (backgrounded) {
            accumulator = 0;
            previous = Clock::now();
            SDL_Delay(16);
            continue;
        }
        if (player.Window) player.InputDevices.BeginFrame(*player.Runtime);
        const auto now = Clock::now();
        const double elapsed = std::clamp(std::chrono::duration<double>(now - previous).count(), 0.0, 0.1);
        previous = now;
        accumulator += elapsed;
        const auto steps = static_cast<unsigned>(std::min(6.0, accumulator / fixedSeconds));
        accumulator -= steps * fixedSeconds;
        player.Runtime->AdvanceFrame(static_cast<float>(elapsed), steps, static_cast<float>(fixedSeconds));
        player.Runtime->CompleteInputFrame();
        if (player.Viewport) {
            auto* activeScene = FindActiveScene(player, startupSceneID);
            BindInputScene(player, activeScene);
            if (!RenderFrame(player, activeScene, presented, error)) return false;
        }
        if (!player.Runtime->EndFrame(error)) return false;
        ++frame;
        if (player.Viewport) SDL_Delay(player.Viewport->GetStatistics().Width ? 1 : 16);
        else SDL_Delay(static_cast<Uint32>(std::max(1.0, (fixedSeconds - accumulator) * 1000.0)));
    }
    if (options.Frames && player.Viewport && !presented) {
        error = "No frame was presented within the requested frame count";
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
#if defined(SDL_MAIN_HANDLED)
    SDL_SetMainReady();
#endif
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    try {
        std::string error;
        if (RunPlayer(argc, argv, error)) return 0;
        std::cerr << "GameSprout Player: " << error << '\n';
    } catch (const std::exception& failure) {
        std::cerr << "GameSprout Player: " << failure.what() << '\n';
    }
    return 1;
}
