// Loads a module the way CPython loads an extension, then calls its probe.
// CPython: dlopen(RTLD_NOW), RTLD_LOCAL being the default, so nothing the module
// defines is visible to the next one; on Windows, LoadLibraryExW with the
// default directories plus the module's own, never PATH.
//
//   sextant_module_loader <module> <out_dir>
#include <cstdio>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using probe_fn = int (*)(const char*);

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <module> <out_dir>\n", argv[0]);
        return 2;
    }
    const std::filesystem::path module = std::filesystem::absolute(argv[1]);

#if defined(_WIN32)
    HMODULE h = LoadLibraryExW(module.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!h) {
        std::fprintf(stderr, "FAIL: LoadLibraryExW(%s): error %lu\n", module.string().c_str(),
                     GetLastError());
        return 1;
    }
    auto probe = reinterpret_cast<probe_fn>(GetProcAddress(h, "sextant_module_probe"));
#else
    void* h = dlopen(module.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        std::fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
    auto probe = reinterpret_cast<probe_fn>(dlsym(h, "sextant_module_probe"));
#endif
    if (!probe) {
        std::fprintf(stderr, "FAIL: sextant_module_probe not exported\n");
        return 1;
    }
    const int failures = probe(argv[2]);
    // Not unloaded: CPython never unloads an extension either.
    return failures == 0 ? 0 : 1;
}
