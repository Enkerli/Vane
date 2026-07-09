# MiniSax — minimal waveguide reed instrument lab.
# Standalone project: builds without JUCE or anything from the Vane plugin.
#
#   cmake -S MiniSax -B MiniSax/build
#   cmake --build MiniSax/build
#   ctest --test-dir MiniSax/build
cmake_minimum_required(VERSION 3.22)
project(MiniSax VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_library(minisax_engine STATIC
    Source/MiniSaxVoice.cpp
    Source/PresetIO.cpp
    Source/SuiteIO.cpp
    Source/Renderer.cpp
)
target_include_directories(minisax_engine PUBLIC Source third_party)
if(NOT MSVC)
    target_compile_options(minisax_engine PRIVATE -Wall -Wextra)
endif()

add_executable(minisax-render cli/minisax_render_main.cpp)
target_link_libraries(minisax-render PRIVATE minisax_engine)

enable_testing()
add_executable(minisax-tests unit_tests/main.cpp)
target_link_libraries(minisax-tests PRIVATE minisax_engine)
add_test(NAME minisax-unit-tests COMMAND minisax-tests)
