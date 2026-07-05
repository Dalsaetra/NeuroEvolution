cmake_minimum_required(VERSION 3.24)

project(NeuroEvolution VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(neuroevo_core
    src/brain.cpp
    src/environment.cpp
    src/evolution.cpp
    src/neat.cpp
    src/nsga2.cpp
    src/objectives.cpp
    src/sensorimotor.cpp
)

target_include_directories(neuroevo_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(neuroevo_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(neuroevo_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(neuroevo_sim src/main.cpp)
target_link_libraries(neuroevo_sim PRIVATE neuroevo_core)

enable_testing()

add_executable(neuroevo_smoke tests/smoke.cpp)
target_link_libraries(neuroevo_smoke PRIVATE neuroevo_core)

add_test(NAME neuroevo_smoke COMMAND neuroevo_smoke)

add_executable(neuroevo_neat_nsga2_tests tests/neat_nsga2.cpp)
target_link_libraries(neuroevo_neat_nsga2_tests PRIVATE neuroevo_core)

add_test(NAME neuroevo_neat_nsga2_tests COMMAND neuroevo_neat_nsga2_tests)

add_executable(neuroevo_sensorimotor_tests tests/sensorimotor.cpp)
target_link_libraries(neuroevo_sensorimotor_tests PRIVATE neuroevo_core)

add_test(NAME neuroevo_sensorimotor_tests COMMAND neuroevo_sensorimotor_tests)
