// Linked into the test tools of a REX86_FORCE_TRANSLATION=WASM build
// (design #45, decision 5): before main, every Cpu made afterwards
// translates every block at its first entry through the web adapter.
#if !defined(__EMSCRIPTEN__)
#error "force_wasm.cpp belongs to the Emscripten test build"
#endif

#include "host/web/wasm_module_services.h"
#include "rex86/cpu.h"

namespace
{

rex86::host::web::WebWasmModuleServices g_services;

[[maybe_unused]] const bool g_forced = [] {
    rex86::TranslationOptions options;
    options.mode = rex86::TranslationMode::kWasm;
    options.threshold = 0;
    options.wasm = &g_services;
    rex86::SetDefaultTranslation(options);
    return true;
}();

}  // namespace
