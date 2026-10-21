#pragma once
#include <windows.h>
#include <psapi.h>
#include <array>
#include <mutex>
#include <string>
#include <cstdio>
#include <memory>

// Opt-in, bounded in-memory recording. No disk I/O in measured callbacks.
namespace Performance {
struct Event { LONGLONG ticks; DWORD thread; std::wstring name; };
inline bool enabled = false;
inline LARGE_INTEGER origin{}, frequency{};
inline double creationToEntryMs = 0;
inline std::mutex mutex;
inline constexpr size_t capacity = 4096;
inline std::unique_ptr<std::array<Event, capacity>> events;
inline size_t count = 0, dropped = 0;
inline void Mark(const std::wstring& name) {
    if (!enabled) return;
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    std::lock_guard lock(mutex);
    if (count == capacity) { ++dropped; return; }
    (*events)[count++] = {now.QuadPart, GetCurrentThreadId(), name};
}
inline void Mark(const wchar_t* name) { if (enabled) Mark(std::wstring(name)); }
inline void Begin() {
    QueryPerformanceCounter(&origin);
    QueryPerformanceFrequency(&frequency);
    enabled = GetEnvironmentVariableW(L"GLASSSPLIT_PERF", nullptr, 0) != 0;
    if (enabled) {
        events = std::make_unique<std::array<Event, capacity>>();
        SetEnvironmentVariableW(L"GLASSSPLIT_DIAGNOSTICS", L"1");
        FILETIME created{}, exited{}, kernel{}, user{}, now{};
        GetSystemTimePreciseAsFileTime(&now);
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            const ULARGE_INTEGER start{{created.dwLowDateTime, created.dwHighDateTime}};
            const ULARGE_INTEGER end{{now.dwLowDateTime, now.dwHighDateTime}};
            creationToEntryMs = static_cast<double>(end.QuadPart - start.QuadPart) / 10000.0;
        }
    }
    if (enabled) (*events)[count++] = {origin.QuadPart, GetCurrentThreadId(), L"process.entry"};
    Mark(IsDebuggerPresent() ? L"debugger.attached" : L"debugger.absent");
}
struct Scope {
    const wchar_t* name;
    explicit Scope(const wchar_t* value) : name(value) { if (enabled) Mark(std::wstring(name) + L".begin"); }
    ~Scope() { if (enabled) Mark(std::wstring(name) + L".end"); }
};
inline void Memory() {
    if (!enabled) return;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        Mark(L"memory.private=" + std::to_wstring(memory.PrivateUsage) + L";working=" + std::to_wstring(memory.WorkingSetSize));
}
inline void Flush() {
    if (!enabled) return;
    Memory();
    wchar_t local[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    const auto folder = std::wstring(local) + L"\\GlassSplit";
    CreateDirectoryW(folder.c_str(), nullptr);
    FILE* file = nullptr;
    const auto path = folder + L"\\performance-" + std::to_wstring(GetCurrentProcessId()) + L".tsv";
    if (_wfopen_s(&file, path.c_str(), L"w, ccs=UTF-8") || !file) return;
    std::lock_guard lock(mutex);
    fwprintf(file, L"# pid=%lu qpc_frequency=%lld dropped=%zu\n", GetCurrentProcessId(), frequency.QuadPart, dropped);
    fwprintf(file, L"# creation_to_entry_setup_ms=%.3f (loader, scheduling, debugger and recorder setup; not application QPC duration)\n", creationToEntryMs);
    fwprintf(file, L"elapsed_ms\tthread\tevent\n");
    for (size_t i = 0; i < count; ++i)
        fwprintf(file, L"%.3f\t%lu\t%ls\n", 1000.0 * ((*events)[i].ticks - origin.QuadPart) / frequency.QuadPart, (*events)[i].thread, (*events)[i].name.c_str());
    fclose(file);
}
}
