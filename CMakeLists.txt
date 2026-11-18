# Maestruc — o regente da família RASGO: janela (JUCE) + motor de áudio.
# Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
cmake_minimum_required(VERSION 3.22)
project(Maestruc VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(MAESTRUC_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/JUCE-9.0.3"
    CACHE PATH "JUCE 9.0.3 da família")

enable_testing()
add_subdirectory(motor)
add_subdirectory("${MAESTRUC_JUCE_DIR}" juce EXCLUDE_FROM_ALL)

juce_add_binary_data(maestruc_fontes SOURCES
    app/fonts/IBMPlexSans-Regular.ttf
    app/fonts/IBMPlexSans-Medium.ttf
    app/fonts/IBMPlexSans-SemiBold.ttf
    app/fonts/IBMPlexSans-Bold.ttf
    app/fonts/IBMPlexMono-Regular.ttf
    app/fonts/IBMPlexMono-Bold.ttf)

# O catálogo dos instrumentos (18/11/2026): dados, não código.
juce_add_binary_data(maestruc_dados NAMESPACE DadosMaestruc HEADER_NAME DadosMaestruc.h
    SOURCES app/catalogo.json)

juce_add_gui_app(MaestrucApp
    PRODUCT_NAME "Maestruc"
    # ícone (escolha do autor, 18/11/2026): o JUCE gera o .ico e o .icns
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/identidade/maestruc-1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/identidade/maestruc-256.png"
    COMPANY_NAME "RASGO"
    VERSION ${PROJECT_VERSION})

target_sources(MaestrucApp PRIVATE app/Main.cpp app/Traducoes.cpp app/Processos.cpp)
# Regra UTF-8 da família (RASGO/AGENTS.md §4).
if(MSVC)
    target_compile_options(MaestrucApp PRIVATE /utf-8)
else()
    target_compile_options(MaestrucApp PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
target_include_directories(MaestrucApp PRIVATE app receptor)
target_compile_features(MaestrucApp PRIVATE cxx_std_17)
target_compile_definitions(MaestrucApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MaestrucApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MaestrucApp,JUCE_VERSION>"
    MAESTRUC_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MAESTRUC_PROJETOS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(MaestrucApp PRIVATE
    maestruc_motor
    maestruc_fontes
    maestruc_dados
    juce::juce_gui_basics
    juce::juce_audio_devices   # modo portátil: a placa pelo JUCE (15/11/2026)
    juce::juce_osc
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Catálogo: o embutido é válido, cada instrumento publicado diz onde o pacote
# oficial instala nos três sistemas, e a leitura do dpkg acha um pacote real.
juce_add_console_app(test_catalogo PRODUCT_NAME "test_catalogo")
target_sources(test_catalogo PRIVATE app/test_catalogo.cpp app/Processos.cpp)
target_include_directories(test_catalogo PRIVATE app)
target_compile_features(test_catalogo PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_catalogo PRIVATE /utf-8)
else()
    target_compile_options(test_catalogo PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
target_compile_definitions(test_catalogo PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
    MAESTRUC_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MAESTRUC_PROJETOS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME catalogo COMMAND test_catalogo)
target_link_libraries(test_catalogo PRIVATE maestruc_dados juce::juce_core juce::juce_events
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
