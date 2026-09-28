// Entry point that wires this project's recompiled game code up to
// N64ModernRuntime (ultramodern + librecomp) and RecompFrontend
// (recompui + recompinput). Structurally modeled on
// bdragoncore/battle-tanx-recomp's src/main/main.cpp (same toolchain, same
// runtime, same recompui/librecomp APIs) -- nothing here is BattleTanx's
// own game logic or symbol addresses, just the generic plumbing every
// N64Recomp-based project using this runtime needs.
//
// What's still a placeholder, and why (see PROGRESS.md/STATUS.md for the
// underlying reverse-engineering status):
//   - `get_rsp_microcode` returns nullptr for every task. RT64 has its own
//     generic F3DEX-family GBI walker (see lib/rt64/src/gbi/*.cpp) that
//     handles GFX tasks via HLE without needing this game's own recompiled
//     microcode, so graphics may work without it. Audio tasks are not so
//     lucky -- this ROM's RSP audio microcode hasn't been identified yet
//     (PROGRESS.md item 6), so the game will hit `quit_exit` the first time
//     it submits an M_AUDTASK, printing which task type was unhandled.
//   - `GameEntry::save_type` is `AllowAll` (accepts EEPROM/SRAM/FlashRAM)
//     rather than this ROM's real save type, which hasn't been determined.
//   - The registered primary font (`LatoLatin-Regular.ttf`, under
//     `assets/`) is a bootstrap placeholder, not the game's real UI
//     typeface -- see STATUS.md round 33 for why one has to be registered
//     at all (RmlUi hard-requires it; recompui::start throws otherwise).
//   - Audio playback uses a plain SDL_QueueAudio push -- no resampling or
//     jitter smoothing. Good enough to hear whether audio works at all,
//     not tuned for it to sound clean.
//   - No mod/texture-pack content types, no launcher menu customization
//     (the library's own default_launcher_init_callback runs instead), no
//     patches/overlays registration -- there's nothing to register yet
//     (PROGRESS.md item 8).

#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include <atomic>
#include <mutex>
#include <vector>
#include <filesystem>
#include <exception>

#include "nfd.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#define SDL_MAIN_HANDLED
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "SDL.h"
#include "SDL_syswm.h"
#else
#include "SDL2/SDL.h"
#endif

#include "recompui/recompui.h"
#include "recompui/program_config.h"
#include "recompui/renderer.h"
#include "util/file.h"

#include "recompinput/input_state.h"
#include "recompinput/profiles.h"
#include "recompinput/players.h"

#include "librecomp/game.hpp"
#include "librecomp/helpers.hpp"

// Generated into RecompiledFuncs/funcs.h by N64Recomp from this project's
// own battletanxga.us.rev0.toml (renamed from func_80071000 -- see
// [input] entrypoint in that file).
extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);

static const std::string program_name = "BattleTanx: Global Assault Recompiled";
static const std::u8string program_id = u8"btgarecomp";
static const std::string version_string = "0.1.0";

template <typename... Ts>
[[noreturn]] static void exit_error(const char* fmt, Ts... args) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    fprintf(stderr, "%s\n", buf);
    ultramodern::error_handling::message_box(buf);
    ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
}

// --- RSP microcode dispatch -------------------------------------------
// See the file-level comment: neither this ROM's GFX nor audio RSP
// microcode has been identified/recompiled yet. Returning nullptr here is
// the documented "not implemented" signal (recomp::rsp::callbacks_t::
// get_rsp_microcode_t's doc comment) -- ultramodern prints the task type
// to stderr and exits rather than running with garbage microcode.
static RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    fprintf(stderr, "No RSP microcode registered for task type %" PRIu32 " yet "
        "(this ROM's RSP microcode hasn't been identified -- see PROGRESS.md item 6).\n",
        task->t.type);
    return nullptr;
}

// --- Graphics / windowing -----------------------------------------------

// recompui's own code (ui_state.cpp) declares this as `extern SDL_Window*
// window;` and reads it directly -- not `static`, needs external linkage.
SDL_Window* window = nullptr;

static ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) != 0) {
        exit_error("Failed to initialize SDL2: %s", SDL_GetError());
    }

    fprintf(stdout, "SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver());

    return {};
}

static ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    uint32_t flags = SDL_WINDOW_RESIZABLE;
#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;
#elif defined(RT64_SDL_WINDOW_VULKAN)
    // Only defined on Linux (see CMakeLists.txt) -- Windows uses plume's
    // D3D12 backend instead, which doesn't need an SDL window flag (it
    // talks to the GPU through the raw HWND returned below), same as
    // bdragoncore/battle-tanx-recomp's own create_window.
    flags |= SDL_WINDOW_VULKAN;
#endif

    window = SDL_CreateWindow(program_name.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, flags);
    if (window == nullptr) {
        exit_error("Failed to create window: %s", SDL_GetError());
    }

#if defined(_WIN32)
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    if (!SDL_GetWindowWMInfo(window, &wm_info)) {
        exit_error("Failed to get window info: %s", SDL_GetError());
    }
    return ultramodern::renderer::WindowHandle{ wm_info.info.win.window, GetCurrentThreadId() };
#elif defined(__linux__) || defined(__ANDROID__)
    return ultramodern::renderer::WindowHandle{ window };
#else
    static_assert(false && "Only Linux and Windows are set up in this file so far -- see PROGRESS.md.");
#endif
}

static void update_gfx(void*) {
    recompinput::poll_inputs();
}

// --- Audio ----------------------------------------------------------------
// Plain push-based playback: queue whatever the game hands us, report how
// much is still queued. No resampling/smoothing -- see file-level comment.

static SDL_AudioDeviceID audio_device = 0;
static std::mutex audio_mutex;
static uint32_t audio_channels = 2;
static uint32_t audio_frequency = 44100;

static bool open_audio_device(uint32_t frequency) {
    std::lock_guard<std::mutex> lock(audio_mutex);

    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }

    SDL_AudioSpec desired{};
    desired.freq = static_cast<int>(frequency);
    desired.format = AUDIO_S16SYS;
    desired.channels = static_cast<Uint8>(audio_channels);
    desired.samples = 1024;

    SDL_AudioSpec obtained{};
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (audio_device == 0) {
        fprintf(stderr, "Failed to open audio device: %s (continuing without sound)\n", SDL_GetError());
        return false;
    }

    audio_frequency = static_cast<uint32_t>(obtained.freq);
    SDL_PauseAudioDevice(audio_device, 0);
    return true;
}

static void queue_samples(int16_t* audio_data, size_t sample_count) {
    if (audio_device == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(audio_mutex);
    SDL_QueueAudio(audio_device, audio_data, static_cast<Uint32>(sample_count * sizeof(int16_t)));
}

static size_t get_frames_remaining() {
    if (audio_device == 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(audio_mutex);
    Uint32 queued_bytes = SDL_GetQueuedAudioSize(audio_device);
    return queued_bytes / (audio_channels * sizeof(int16_t));
}

static void set_frequency(uint32_t frequency) {
    if (frequency != audio_frequency) {
        open_audio_device(frequency);
    }
}

// --- Input ------------------------------------------------------------

static ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    if (controller_num == 0) {
        return ultramodern::input::connected_device_info_t{
            .connected_device = ultramodern::input::Device::Controller,
            .connected_pak = ultramodern::input::Pak::None,
        };
    }
    return ultramodern::input::connected_device_info_t{
        .connected_device = ultramodern::input::Device::None,
        .connected_pak = ultramodern::input::Pak::None,
    };
}

// --- Misc callbacks -----------------------------------------------------

static std::string get_game_thread_name(const OSThread* t) {
    return "Game " + std::to_string(t->id);
}

// --- Game registration --------------------------------------------------
// recompui's own default launcher menu (ui_launcher.cpp) reads this exact
// global by name -- it must exist with this type for the link to succeed.
std::vector<recomp::GameEntry> supported_games = {
    {
        // XXH3_64 of the normalized big-endian .z64 (tools/normalize_rom.py),
        // computed against the ROM this project has been developed against
        // (see syms/rom_info.md) -- NOT the N64 header CRC1/CRC2.
        .rom_hash = 0x9c7467e763553529ULL,
        .internal_name = "BattleTanxGA",
        .display_name = "BattleTanx: Global Assault",
        .game_id = u8"btga.n64.us.1.0",
        .mod_game_id = "btga",
        // Not yet determined which save type this ROM actually uses --
        // AllowAll accepts EEPROM/SRAM/FlashRAM rather than guessing wrong.
        .save_type = recomp::SaveType::AllowAll,
        .thumbnail_bytes = {},
        .is_enabled = true,
        .decompression_routine = nullptr,
        .has_compressed_code = false,
        .entrypoint_address = 0x80071000,
        .entrypoint = recomp_entrypoint,
        .on_init_callback = nullptr,
    },
};

// std::terminate (any uncaught C++ exception, on any thread -- recomp::start
// spawns several) leads straight into ucrtbase's fail-fast abort path, which
// tears the process down without running atexit handlers or flushing stdio
// buffers. That's why redirected runs (`... > run_output.txt 2>&1`) have
// been showing up empty even when something threw and printed nothing --
// this handler force-flushes and prints the exception's own message first
// so it actually survives redirection.
[[noreturn]] static void report_unhandled_exception_and_abort() {
    if (std::exception_ptr eptr = std::current_exception()) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& e) {
            fprintf(stderr, "UNHANDLED EXCEPTION: %s\n", e.what());
        } catch (...) {
            fprintf(stderr, "UNHANDLED EXCEPTION: (unrecognized exception type)\n");
        }
    } else {
        fprintf(stderr, "std::terminate() called with no active exception.\n");
    }
    fflush(stdout);
    fflush(stderr);
    std::abort();
}

int main(int argc, char** argv) {
    std::set_terminate(report_unhandled_exception_and_abort);
    fprintf(stdout, "main() started\n");
    fflush(stdout);

    recomp::Version project_version{};
    if (!recomp::Version::from_string(version_string, project_version)) {
        fprintf(stderr, "Invalid version string: %s\n", version_string.c_str());
        return EXIT_FAILURE;
    }

    NFD_Init();

    recompui::programconfig::set_program_name(program_name);
    recompui::programconfig::set_program_id(program_id);

    SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!open_audio_device(audio_frequency)) {
        fprintf(stderr, "Continuing without sound.\n");
    }

    // Lato (SIL Open Font License 1.1, assets/FONT_LICENSE.txt), reused from
    // RmlUi's own bundled sample assets -- see the file-level comment. This
    // is a bootstrap placeholder to get recompui::start past its hard
    // requirement for *some* primary font (UIState's constructor throws
    // std::runtime_error otherwise -- see STATUS.md round 33), not a
    // considered choice of the game's real UI typeface.
    recompui::register_primary_font("LatoLatin-Regular.ttf", "Lato");

    recomp::register_config_path(recompui::file::get_app_folder_path());

    for (const auto& game : supported_games) {
        recomp::register_game(game);
    }

    recompinput::players::set_single_player_mode(true);

    recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = get_rsp_microcode,
    };

    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context = [](uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
            auto presentation_mode = ultramodern::renderer::PresentationMode::PresentEarly;
            return recompui::renderer::create_render_context(rdram, window_handle, presentation_mode, developer_mode);
        },
    };

    ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx = create_gfx,
        .create_window = create_window,
        .update_gfx = update_gfx,
    };

    ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = queue_samples,
        .get_frames_remaining = get_frames_remaining,
        .set_frequency = set_frequency,
    };

    ultramodern::input::callbacks_t input_callbacks{
        .poll_input = recompinput::poll_inputs,
        .get_input = recompinput::profiles::get_n64_input,
        .set_rumble = recompinput::set_rumble,
        .get_connected_device_info = get_connected_device_info,
    };

    ultramodern::events::callbacks_t events_callbacks{
        .vi_callback = recompinput::update_rumble,
        .gfx_init_callback = nullptr,
    };

    ultramodern::error_handling::callbacks_t error_handling_callbacks{
        .message_box = recompui::message_box,
    };

    ultramodern::threads::callbacks_t threads_callbacks{
        .get_game_thread_name = get_game_thread_name,
    };

    // recompui's own default launcher menu is used -- no
    // register_launcher_init_callback call here (see file-level comment).

    recomp::start({
        .argc = argc,
        .argv = argv,
        .project_version = project_version,
        .rsp_callbacks = rsp_callbacks,
        .renderer_callbacks = renderer_callbacks,
        .audio_callbacks = audio_callbacks,
        .input_callbacks = input_callbacks,
        .gfx_callbacks = gfx_callbacks,
        .events_callbacks = events_callbacks,
        .error_handling_callbacks = error_handling_callbacks,
        .threads_callbacks = threads_callbacks,
    });

    NFD_Quit();

    // The game's threads are never joined by the runtime and may still be
    // blocked on (or spinning over) runtime state when recomp::start
    // returns, so running static destructors/atexit handlers underneath
    // them can crash on quit -- exit directly instead (same approach as
    // other N64Recomp projects' main()).
    fflush(stdout);
    fflush(stderr);
    std::_Exit(EXIT_SUCCESS);
}
