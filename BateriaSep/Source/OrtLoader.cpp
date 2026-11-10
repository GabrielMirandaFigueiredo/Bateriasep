// Este arquivo não inclui o JUCE de propósito (evita conflito de macros com windows.h).
#ifndef ORT_API_MANUAL_INIT
 #define ORT_API_MANUAL_INIT
#endif
#include <onnxruntime_cxx_api.h>

#include "OrtLoader.h"

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace bateriasep
{

bool initOnnxRuntime (const std::filesystem::path& libraryPath, std::string& error)
{
    static bool done = false;
    if (done) return true;

    using GetApiBaseFn = const OrtApiBase* (ORT_API_CALL*) (void);
    GetApiBaseFn getBase = nullptr;

#if defined(_WIN32)
    HMODULE lib = LoadLibraryExW (libraryPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (lib == nullptr)
    {
        error = "Não consegui carregar " + libraryPath.filename().string() + " (erro " + std::to_string (GetLastError())
                + "). Se for o erro 126, instale o \"Microsoft Visual C++ Redistributable 2015-2022 (x64)\" e tente de novo.";
        return false;
    }
    getBase = reinterpret_cast<GetApiBaseFn> (reinterpret_cast<void*> (GetProcAddress (lib, "OrtGetApiBase")));
#else
    void* lib = dlopen (libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        const char* e = dlerror();
        error = std::string ("Não consegui carregar a biblioteca do ONNX Runtime: ") + (e ? e : "");
        return false;
    }
    getBase = reinterpret_cast<GetApiBaseFn> (dlsym (lib, "OrtGetApiBase"));
#endif

    if (getBase == nullptr) { error = "Biblioteca do ONNX Runtime inválida (OrtGetApiBase não encontrado)."; return false; }

    const OrtApi* api = getBase()->GetApi (ORT_API_VERSION);
    if (api == nullptr) { error = "Versão do ONNX Runtime incompatível com o plugin."; return false; }

    Ort::InitApi (api);
    done = true;
    return true;
}

} // namespace bateriasep
