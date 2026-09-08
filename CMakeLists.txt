cmake_minimum_required(VERSION 3.24)

project(NeuroEvolution VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(neuroevo_core
    src/config.cpp
    src/brain.cpp
    src/brain_state.cpp
    src/ecosystem.cpp
    src/ecosystem_predation.cpp
    src/ecosystem_ancestor.cpp
    src/ecosystem_frontier.cpp
    src/ecosystem_sensing.cpp
    src/ecosystem_io.cpp
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

add_executable(neuroevo_ecosystem src/ecosystem_main.cpp)
target_link_libraries(neuroevo_ecosystem PRIVATE neuroevo_core)

enable_testing()
foreach(test IN ITEMS sensory_mutation pruning ecosystem_predation ecosystem_frontier
        ecosystem_calibration ecosystem_mechanics ecosystem_sensing ecosystem_checkpoint
        ecosystem_ancestor ecosystem_dynamic_food)
    add_executable(neuroevo_${test} tests/${test}.cpp)
    target_link_libraries(neuroevo_${test} PRIVATE neuroevo_core)
    add_test(NAME neuroevo_${test} COMMAND neuroevo_${test})
endforeach()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME neuroevo_ecosystem_viewer
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ecosystem_viewer_test.py)
    add_test(NAME neuroevo_ecosystem_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ecosystem_cli_test.py $<TARGET_FILE:neuroevo_ecosystem>)
endif()
