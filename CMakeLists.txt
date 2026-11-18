# Maestruc — o regente da família RASGO: janela (JUCE) + motor de áudio.
# Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
cmake_minimum_required(VERSION 3.22)

# Pacotes (18/11/2026), mesmo desenho da família (Batruc, Rasgo Modular):
# runtime estático no Windows (o .exe não depende do Visual C++
# Redistributable) e Universal 2 no macOS, com deployment target do projeto
# (std::filesystem pede 10.15). Antes do project(), senão não valem.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build a Universal 2 binary" FORCE)
endif()

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
target_link_libraries(test_catalogo PRIVATE maestruc_dados juce::juce_core juce::juce_events juce::juce_data_structures
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---- instalação e empacotamento (CPack), 18/11/2026 ----------------------
# Mesmo desenho do Batruc: nome de arquivo com a plataforma; .deb no Linux,
# instalador NSIS no Windows (pasta e chave sem a versão, atalhos apontando
# para o .exe na raiz), .dmg no macOS. O catálogo vai embutido no binário;
# nada além do programa, do .desktop e dos ícones é instalado.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(PROGRAMS "$<TARGET_FILE:MaestrucApp>" DESTINATION bin RENAME maestruc)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/maestruc.desktop"
        DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/identidade/maestruc-icone.svg"
        DESTINATION share/icons/hicolor/scalable/apps RENAME maestruc.svg)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/identidade/maestruc-512.png"
        DESTINATION share/icons/hicolor/512x512/apps RENAME maestruc.png)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    install(TARGETS MaestrucApp RUNTIME DESTINATION .)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    install(TARGETS MaestrucApp BUNDLE DESTINATION .)
endif()

set(CPACK_PACKAGE_NAME "maestruc")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "RASGO")
set(CPACK_PACKAGE_CONTACT "Lúcio de Araújo <rasgo.instruments@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Maestruc — o regente da família RASGO (abre, mistura e grava os instrumentos)")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(LENGTH CMAKE_OSX_ARCHITECTURES _maestruc_n_arch)
    if(_maestruc_n_arch GREATER 1)
        set(_maestruc_plat "macos-universal")
    else()
        set(_maestruc_plat "macos-${CMAKE_SYSTEM_PROCESSOR}")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_maestruc_plat "windows-x64")
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _maestruc_os)
    set(_maestruc_plat "${_maestruc_os}-${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_PACKAGE_FILE_NAME "maestruc-${PROJECT_VERSION}-${_maestruc_plat}")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Lúcio de Araújo <rasgo.instruments@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # o aviso de versão usa o programa curl do sistema; o JACK é aberto em
    # tempo de execução (dlopen), sem ele o Maestruc usa a placa pelo JUCE
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "curl, libjack-jackd2-0 | pipewire-jack")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "Maestruc")
    set(CPACK_NSIS_DISPLAY_NAME "Maestruc")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Maestruc")
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "Maestruc")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_PACKAGE_EXECUTABLES "Maestruc" "Maestruc")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_CREATE_DESKTOP_LINKS "Maestruc")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "Maestruc.exe")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "Maestruc.exe")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/identidade/maestruc.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/identidade/maestruc.ico")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Maestruc")
endif()
include(CPack)
