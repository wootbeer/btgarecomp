// Crash reporting. On a fatal exception, writes crash_log.txt to the working
// directory (the exe's folder when launched from Explorer) and stderr:
// - the exception and its offset in the exe (as Event Viewer reports it)
// - the recompiled game function that faulted, by N64 address
// - for memory faults, the N64 address being accessed
// - a best-effort list of game functions found on the host stack (callers)
// Release builds have no console, so a message box points testers at the log.
#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <csignal>
#include <dlfcn.h>
#include <pthread.h>
#include <ucontext.h>
#include <unistd.h>
#endif

#include "recomp.h"
#include "librecomp/sections.h"

namespace {
    struct GameFunc {
        uintptr_t host;
        uint32_t vram;
        uint32_t rom_size;
    };
    std::vector<GameFunc> game_funcs;
    uint8_t* rdram_base = nullptr;

    // Debug builds link incrementally: function pointers point at `jmp rel32`
    // thunks rather than the bodies. Follow one if present.
    uintptr_t resolve_thunk(uintptr_t addr) {
#if defined(_WIN32)
        const uint8_t* p = reinterpret_cast<const uint8_t*>(addr);
        if (p[0] == 0xE9) {
            int32_t rel;
            memcpy(&rel, p + 1, sizeof(rel));
            return addr + 5 + rel;
        }
#endif
        return addr;
    }

    // The game function whose host code contains `addr`, if any: the nearest
    // function start at or below it, within a generous body size.
    const GameFunc* find_game_func(uintptr_t addr) {
        auto it = std::upper_bound(game_funcs.begin(), game_funcs.end(), addr,
            [](uintptr_t a, const GameFunc& f) { return a < f.host; });
        if (it == game_funcs.begin()) return nullptr;
        --it;
        // Host code is much larger than MIPS code; allow 64x plus slack.
        if (addr - it->host > (uintptr_t)it->rom_size * 64 + 0x10000) return nullptr;
        return &*it;
    }

    bool in_rdram(uintptr_t addr) {
        return rdram_base != nullptr && addr >= (uintptr_t)rdram_base && addr - (uintptr_t)rdram_base < 0x100000000ull;
    }

    uint32_t n64_address(uintptr_t addr) {
        return (uint32_t)(addr - (uintptr_t)rdram_base) + 0x80000000u;
    }

    // On quit, librecomp frees rdram while the game's threads (never joined)
    // can still be running; their next access faults. While the game runs,
    // the whole 512 MB KSEG0 window is mapped, so a fault inside it can only
    // be that -- exit quietly instead of reporting a crash.
    bool is_shutdown_fault(bool has_access, uintptr_t access) {
        return has_access && in_rdram(access) && (access - (uintptr_t)rdram_base) < 0x20000000ull;
    }

    struct Fault {
        const char* what;    // exception name
        uint32_t code;       // exception code (Windows) or signal number
        const char* module;  // file name of the module the fault is in
        const char* detail;  // extra description (C++ exception type and message), or empty
        uintptr_t pc;
        uintptr_t module_base;
        bool has_access;
        uintptr_t access;
        bool write;
    };

    void report(FILE* out, const Fault& fault, const uintptr_t* stack, size_t stack_words) {
        const uintptr_t pc = fault.pc;
        const bool has_access = fault.has_access;
        const uintptr_t access = fault.access;
        const bool write = fault.write;
        fprintf(out, "BattleTanx: Global Assault Recompiled crashed.\n");
        fprintf(out, "Exception: %s (code 0x%08X)\n", fault.what, fault.code);
        if (fault.detail[0] != '\0') {
            fprintf(out, "Details: %s\n", fault.detail);
        }
        fprintf(out, "Fault offset: 0x%" PRIxPTR " in %s\n", pc - fault.module_base, fault.module[0] != '\0' ? fault.module : "unknown module");
        if (const GameFunc* f = find_game_func(pc)) {
            fprintf(out, "In game function: func_%08X (+0x%" PRIxPTR " host bytes)\n", f->vram, pc - f->host);
        }
        else {
            fprintf(out, "Not in a recompiled game function.\n");
        }
        if (has_access) {
            if (in_rdram(access)) {
                fprintf(out, "%s N64 address 0x%08X\n", write ? "Writing" : "Reading", n64_address(access));
            }
            else {
                fprintf(out, "%s host address 0x%" PRIxPTR " (outside N64 memory)\n", write ? "Writing" : "Reading", access);
            }
        }
        fprintf(out, "Game functions on the stack (most recent first, best effort):\n");
        int listed = 0;
        uint32_t last = 0;
        for (size_t i = 0; i < stack_words && listed < 24; i++) {
            const GameFunc* f = find_game_func(stack[i]);
            if (f != nullptr && f->vram != last) {
                fprintf(out, "  func_%08X\n", f->vram);
                last = f->vram;
                listed++;
            }
        }
        fflush(out);
    }

    // The top (highest address) of the current thread's stack, or 0 if unknown.
    uintptr_t stack_top() {
#if defined(_WIN32)
        ULONG_PTR low = 0, high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        return (uintptr_t)high;
#elif defined(__linux__)
        pthread_attr_t attr;
        void* addr = nullptr;
        size_t size = 0;
        if (pthread_getattr_np(pthread_self(), &attr) != 0) return 0;
        pthread_attr_getstack(&attr, &addr, &size);
        pthread_attr_destroy(&attr);
        return (uintptr_t)addr + size;
#else
        return 0;
#endif
    }

    void write_reports(const Fault& fault, uintptr_t sp) {
        // Scan up to 64 KB of the faulting thread's stack (the handler runs on
        // that thread) for return addresses, never past its top.
        const uintptr_t* stack = reinterpret_cast<const uintptr_t*>(sp);
        uintptr_t top = stack_top();
        size_t words = 0;
        if (top > sp) {
            words = std::min<uintptr_t>(top - sp, 0x10000) / sizeof(uintptr_t);
        }
        report(stderr, fault, stack, words);
        if (FILE* f = fopen("crash_log.txt", "w")) {
            report(f, fault, stack, words);
            fclose(f);
        }
    }

    // The file name of a module path, without its folder (which can contain
    // the user's name).
    const char* file_name(const char* path) {
        const char* name = path;
        for (const char* p = path; *p != '\0'; p++) {
            if (*p == '/' || *p == '\\') {
                name = p + 1;
            }
        }
        return name;
    }

    void show_crash_message() {
#if defined(_WIN32)
        MessageBoxA(nullptr,
            "The game crashed. Details were saved to crash_log.txt in the game's folder -- "
            "please include that file when reporting the problem.",
            "BattleTanx: Global Assault Recompiled", MB_OK | MB_ICONERROR);
#endif
    }

#if defined(_WIN32)
    // MSVC-ABI C++ exceptions are raised as this SEH code ("msc").
    constexpr DWORD kCppExceptionCode = 0xE06D7363;

    const char* exception_name(DWORD code) {
        switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_BREAKPOINT: return "breakpoint";
        case 0xC0000409: return "stack buffer overrun / fast fail"; // STATUS_STACK_BUFFER_OVERRUN
        case kCppExceptionCode: return "uncaught C++ exception";
        default: return "unhandled exception";
        }
    }

    // ".?AVruntime_error@std@@" -> "std::runtime_error".
    void type_name(const char* decorated, char* out, size_t size) {
        if (strncmp(decorated, ".?A", 3) != 0 || decorated[3] == '\0') {
            snprintf(out, size, "%s", decorated);
            return;
        }
        const char* parts[8];
        size_t lengths[8];
        int count = 0;
        const char* p = decorated + 4;
        while (*p != '\0' && *p != '@' && count < 8) {
            const char* end = strchr(p, '@');
            if (end == nullptr) {
                end = p + strlen(p);
            }
            parts[count] = p;
            lengths[count] = (size_t)(end - p);
            count++;
            p = *end == '@' ? end + 1 : end;
        }
        size_t used = 0;
        out[0] = '\0';
        for (int i = count - 1; i >= 0 && used + 1 < size; i--) {
            int n = snprintf(out + used, size - used, "%s%.*s", used > 0 ? "::" : "", (int)lengths[i], parts[i]);
            if (n < 0) {
                break;
            }
            used += (size_t)n;
        }
    }

    // For a C++ exception: the thrown type and, for a std::exception, its
    // message, read from the exception's MSVC throw information.
    void cpp_exception_detail(const EXCEPTION_RECORD* rec, char* out, size_t size) {
        out[0] = '\0';
        if (rec->ExceptionCode != kCppExceptionCode || rec->NumberParameters < 4) {
            return;
        }
        __try {
            const uint8_t* object = (const uint8_t*)rec->ExceptionInformation[1];
            const uint8_t* throw_info = (const uint8_t*)rec->ExceptionInformation[2];
            const uint8_t* image = (const uint8_t*)rec->ExceptionInformation[3];
            if (object == nullptr || throw_info == nullptr || image == nullptr) {
                return;
            }
            // ThrowInfo.pCatchableTypeArray -> { count, CatchableType RVAs }
            const uint8_t* types = image + *(const int32_t*)(throw_info + 12);
            int32_t count = *(const int32_t*)types;
            char thrown[128] = "unknown type";
            const char* message = nullptr;
            for (int32_t i = 0; i < count && i < 32; i++) {
                // CatchableType: properties, pType (TypeDescriptor RVA), thisDisplacement.mdisp, ...
                const uint8_t* type = image + *(const int32_t*)(types + 4 + 4 * i);
                const char* name = (const char*)(image + *(const int32_t*)(type + 4) + 16);
                if (i == 0) {
                    type_name(name, thrown, sizeof(thrown));
                }
                if (strcmp(name, ".?AVexception@std@@") == 0) {
                    const std::exception* e = (const std::exception*)(object + *(const int32_t*)(type + 8));
                    message = e->what();
                }
            }
            snprintf(out, size, "C++ exception %s%s%s", thrown, message != nullptr ? ": " : "", message != nullptr ? message : "");
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            snprintf(out, size, "C++ exception (details unreadable)");
        }
    }

    LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info) {
        static volatile LONG entered = 0;
        if (InterlockedExchange(&entered, 1) != 0) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        const EXCEPTION_RECORD* rec = info->ExceptionRecord;
        uintptr_t pc = (uintptr_t)rec->ExceptionAddress;
        HMODULE module = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pc, &module);
        char module_path[MAX_PATH] = "";
        if (module != nullptr) {
            GetModuleFileNameA(module, module_path, sizeof(module_path));
        }
        bool has_access = (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && rec->NumberParameters >= 2;
        if (is_shutdown_fault(has_access, has_access ? (uintptr_t)rec->ExceptionInformation[1] : 0)) {
            TerminateProcess(GetCurrentProcess(), 0);
        }
        char detail[512];
        cpp_exception_detail(rec, detail, sizeof(detail));
        Fault fault{
            .what = exception_name(rec->ExceptionCode),
            .code = (uint32_t)rec->ExceptionCode,
            .module = file_name(module_path),
            .detail = detail,
            .pc = pc,
            .module_base = (uintptr_t)module,
            .has_access = has_access,
            .access = has_access ? (uintptr_t)rec->ExceptionInformation[1] : 0,
            .write = has_access && rec->ExceptionInformation[0] == 1,
        };
        write_reports(fault, (uintptr_t)info->ContextRecord->Rsp);
        show_crash_message();
        return EXCEPTION_CONTINUE_SEARCH;
    }
#elif defined(__linux__)
    void signal_handler(int sig, siginfo_t* info, void* uctx_void) {
        ucontext_t* uctx = static_cast<ucontext_t*>(uctx_void);
#if defined(__x86_64__)
        uintptr_t pc = (uintptr_t)uctx->uc_mcontext.gregs[REG_RIP];
        uintptr_t sp = (uintptr_t)uctx->uc_mcontext.gregs[REG_RSP];
#else
        uintptr_t pc = 0, sp = (uintptr_t)&uctx;
#endif
        const char* what = sig == SIGSEGV ? "segmentation fault" : sig == SIGBUS ? "bus error" : sig == SIGILL ? "illegal instruction" : "floating point exception";
        if (is_shutdown_fault(sig == SIGSEGV || sig == SIGBUS, (uintptr_t)info->si_addr)) {
            _exit(0);
        }
        Dl_info dl{};
        bool found = dladdr((void*)pc, &dl) != 0;
        Fault fault{
            .what = what,
            .code = (uint32_t)sig,
            .module = found && dl.dli_fname != nullptr ? file_name(dl.dli_fname) : "",
            .detail = "",
            .pc = pc,
            .module_base = found ? (uintptr_t)dl.dli_fbase : 0,
            .has_access = sig == SIGSEGV || sig == SIGBUS,
            .access = (uintptr_t)info->si_addr,
            .write = false,
        };
        write_reports(fault, sp);
        signal(sig, SIG_DFL);
        raise(sig);
    }
#endif
}

// Reports a fatal error that isn't a CPU exception, such as an uncaught C++
// exception reaching std::terminate: writes crash_log.txt and, on Windows,
// shows the crash message (release builds have no console for stderr).
void btga_report_fatal_error(const char* message) {
    for (FILE* out : { stderr, fopen("crash_log.txt", "w") }) {
        if (out == nullptr) {
            continue;
        }
        fprintf(out, "BattleTanx: Global Assault Recompiled crashed.\n");
        fprintf(out, "Error: %s\n", message);
        fflush(out);
        if (out != stderr) {
            fclose(out);
        }
    }
    show_crash_message();
}

// Records where N64 memory starts, to translate faulting addresses (called
// from a per-frame hook).
extern "C" void btga_crash_note_rdram(uint8_t* rdram) {
    rdram_base = rdram;
}

void btga_install_crash_handler(const SectionTableEntry* sections, size_t num_sections) {
    for (size_t s = 0; s < num_sections; s++) {
        for (size_t i = 0; i < sections[s].num_funcs; i++) {
            const FuncEntry& f = sections[s].funcs[i];
            game_funcs.push_back({ resolve_thunk((uintptr_t)f.func), sections[s].ram_addr + f.offset, f.rom_size });
        }
    }
    std::sort(game_funcs.begin(), game_funcs.end(), [](const GameFunc& a, const GameFunc& b) { return a.host < b.host; });

#if defined(_WIN32)
    SetUnhandledExceptionFilter(unhandled_filter);
#elif defined(__linux__)
    struct sigaction sa {};
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE }) {
        sigaction(sig, &sa, nullptr);
    }
#endif
}
