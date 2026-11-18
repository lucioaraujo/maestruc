# Maestruc — motor de áudio (C++17, sem dependências).
# Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
cmake_minimum_required(VERSION 3.22)
project(MaestrucMotor VERSION 0.0.1 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(maestruc_motor STATIC src/Mixer.cpp src/Recorder.cpp src/Wav.cpp src/Jack.cpp src/LiveHost.cpp
    # saida-ref 1.1 (16/11/2026): a versão de entrega do Master gravado
    src/VersaoEntrega.cpp src/saida/DeliveryMaster.cpp src/saida/OutputStage.cpp
    src/saida/LoudnessMeter.cpp src/saida/TruePeakDetector.cpp src/saida/TakeAnalysis.cpp)
target_include_directories(maestruc_motor PUBLIC src ${CMAKE_CURRENT_SOURCE_DIR}/../receptor)
target_compile_features(maestruc_motor PUBLIC cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(maestruc_motor PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(NOT MSVC)
    target_compile_options(maestruc_motor PRIVATE -Wall -Wextra)
else()
    # M_PI no MSVC (os testes e as ferramentas o usam)
    target_compile_definitions(maestruc_motor PUBLIC _USE_MATH_DEFINES)
endif()

add_executable(maestruc_motor_offline tools/motor_offline.cpp)
target_link_libraries(maestruc_motor_offline PRIVATE maestruc_motor)

add_executable(maestruc_motor_vivo tools/motor_vivo.cpp)
target_link_libraries(maestruc_motor_vivo PRIVATE maestruc_motor)

enable_testing()
add_executable(test_motor tests/test_motor.cpp)
target_link_libraries(test_motor PRIVATE maestruc_motor)
add_test(NAME motor COMMAND test_motor)
add_executable(test_vivo tests/test_vivo.cpp)
target_link_libraries(test_vivo PRIVATE maestruc_motor)
add_test(NAME vivo COMMAND test_vivo)
# Ponte de som (receptor/PonteDeSom.h, 15/11/2026): o mesmo cabeçalho que os
# instrumentos copiam.
# (usa fork(): não há no Windows)
if(NOT WIN32)
add_executable(test_ponte tests/test_ponte.cpp)
target_link_libraries(test_ponte PRIVATE maestruc_motor)
target_compile_features(test_ponte PRIVATE cxx_std_17)
if(NOT MSVC)
    target_compile_options(test_ponte PRIVATE -Wall -Wextra)
endif()
add_test(NAME ponte COMMAND test_ponte)
endif()
