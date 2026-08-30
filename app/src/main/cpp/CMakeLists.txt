cmake_minimum_required(VERSION 3.22.1)
project(musicenhancer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# O nucleo DSP vive fora de app/ para poder ser compilado tambem no host
# (testes do CI) e reaproveitado em futuras plataformas.
set(ME_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../native)

add_library(musicenhancer SHARED
    jni_bridge.cpp
    ${ME_CORE}/src/biquad.cpp
    ${ME_CORE}/src/fft.cpp
    ${ME_CORE}/src/loudness.cpp
    ${ME_CORE}/src/analyzer.cpp
    ${ME_CORE}/src/decision.cpp
    ${ME_CORE}/src/chain.cpp
    ${ME_CORE}/src/engine.cpp
)

target_include_directories(musicenhancer PRIVATE ${ME_CORE}/include)
target_compile_options(musicenhancer PRIVATE -O3 -ffast-math -fno-finite-math-only)
find_library(log-lib log)
target_link_libraries(musicenhancer ${log-lib})
