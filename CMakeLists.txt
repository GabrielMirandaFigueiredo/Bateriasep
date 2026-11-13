cmake_minimum_required(VERSION 3.22)
project(BateriaSep VERSION 1.0.0 LANGUAGES C CXX)

# Runtime do Visual C++ embutido: o plugin funciona sem instalar o "VC++ Redistributable".
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- ONNX Runtime (pasta descompactada do release oficial: include/ e lib/) ---
set(ONNXRUNTIME_ROOT "" CACHE PATH "Pasta do ONNX Runtime (contém include/ e lib/)")
if(NOT ONNXRUNTIME_ROOT OR NOT EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "Defina -DONNXRUNTIME_ROOT=<pasta do ONNX Runtime>")
endif()
# Modelo opcional: se informado, é copiado para dentro do .vst3 (Contents/Resources)
set(MODEL_ONNX "" CACHE FILEPATH "Caminho do drumsep_core.onnx (opcional)")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BateriaSep
    COMPANY_NAME "Gabriel"
    PLUGIN_MANUFACTURER_CODE Gabr
    PLUGIN_CODE Bsep
    FORMATS VST3 Standalone
    PRODUCT_NAME "BateriaSep"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

# kit padrão de triggers (samples em domínio público) embutido no plugin
juce_add_binary_data(BateriaSepKit
    HEADER_NAME "KitData.h"
    NAMESPACE KitData
    SOURCES assets/default_kit.zip)

target_sources(BateriaSep PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/OrtLoader.cpp
    Source/OrtLoader.h
    Source/DrumSeparator.h
    Source/BpmDetector.h
    Source/MidiExtractor.h
    Source/TriggerEngine.h
    Source/Project.h)

target_include_directories(BateriaSep PRIVATE Source "${ONNXRUNTIME_ROOT}/include")

target_compile_definitions(BateriaSep PUBLIC
    ORT_API_MANUAL_INIT=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(BateriaSep PRIVATE /utf-8)   # textos com acento nos .cpp
endif()

target_link_libraries(BateriaSep
    PRIVATE
        juce::juce_audio_utils
        BateriaSepKit
        ${CMAKE_DL_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags)

# --- Biblioteca do ONNX Runtime: copiada ao lado do binário, com outro nome (evita conflito com
#     a onnxruntime.dll antiga que o Windows/DAW podem já ter carregado) ---
if(WIN32)
    set(ORT_LIB_SRC "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
    set(ORT_LIB_DST "bateriasep_onnxruntime.dll")
elseif(APPLE)
    file(GLOB ORT_LIB_SRC "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.*.*.*.dylib")
    get_filename_component(ORT_LIB_DST "${ORT_LIB_SRC}" NAME)
else()
    file(GLOB ORT_LIB_SRC "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so.*.*.*")
    get_filename_component(ORT_LIB_DST "${ORT_LIB_SRC}" NAME)
endif()

if(NOT ORT_LIB_SRC OR NOT EXISTS "${ORT_LIB_SRC}")
    message(FATAL_ERROR "Biblioteca do ONNX Runtime nao encontrada em ${ONNXRUNTIME_ROOT}/lib")
endif()

foreach(fmt VST3 Standalone)
    if(TARGET BateriaSep_${fmt})
        add_custom_command(TARGET BateriaSep_${fmt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_LIB_SRC}" "$<TARGET_FILE_DIR:BateriaSep_${fmt}>/${ORT_LIB_DST}")
    endif()
endforeach()

if(MODEL_ONNX AND EXISTS "${MODEL_ONNX}")
    add_custom_command(TARGET BateriaSep_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_ONNX}" "$<TARGET_FILE_DIR:BateriaSep_VST3>/../Resources/drumsep_core.onnx")
endif()

# --- Teste de integração (opcional): cmake -DBATERIASEP_BUILD_TESTS=ON ---
option(BATERIASEP_BUILD_TESTS "Compila o teste de integração do plugin" OFF)
if(BATERIASEP_BUILD_TESTS)
    juce_add_console_app(BateriaSepTest PRODUCT_NAME "BateriaSepTest")
    target_sources(BateriaSepTest PRIVATE
        tests/test_plugin.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/OrtLoader.cpp)
    target_include_directories(BateriaSepTest PRIVATE Source "${ONNXRUNTIME_ROOT}/include")
    target_compile_definitions(BateriaSepTest PRIVATE
        ORT_API_MANUAL_INIT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    if(MSVC)
        target_compile_options(BateriaSepTest PRIVATE /utf-8)
    endif()
    target_link_libraries(BateriaSepTest PRIVATE juce::juce_audio_utils BateriaSepKit ${CMAKE_DL_LIBS}
        juce::juce_recommended_config_flags)
endif()
