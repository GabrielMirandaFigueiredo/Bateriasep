#pragma once

#include <filesystem>
#include <string>

namespace bateriasep
{

// Carrega a biblioteca do ONNX Runtime pelo caminho completo (sem depender do PATH do Windows nem
// de outra "onnxruntime.dll" já carregada pelo DAW) e inicializa a API do ONNX Runtime.
// Precisa ser chamada uma vez antes de qualquer uso de DrumSeparator.
bool initOnnxRuntime (const std::filesystem::path& libraryPath, std::string& error);

} // namespace bateriasep
