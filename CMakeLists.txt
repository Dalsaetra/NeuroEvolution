cmake_minimum_required(VERSION 3.24)

project(NeuroEvolution VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

add_library(neuroevo_core
    src/brain.cpp
    src/brain_state.cpp
    src/ecosystem.cpp
    src/ecosystem_ancestor.cpp
    src/ecosystem_frontier.cpp
    src/ecosystem_establishment.cpp
    src/ecosystem_evaluation.cpp
    src/ecosystem_sensing.cpp
    src/ecosystem_io.cpp
    src/environment.cpp
    src/evolution.cpp
    src/fitness.cpp
    src/neat.cpp
    src/nsga2.cpp
    src/objectives.cpp
    src/sensorimotor.cpp
    src/task.cpp
)

target_include_directories(neuroevo_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(neuroevo_core PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(neuroevo_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(neuroevo_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(neuroevo_sim src/main.cpp)
target_link_libraries(neuroevo_sim PRIVATE neuroevo_core)

add_executable(neuroevo_ecosystem src/ecosystem_main.cpp)
target_link_libraries(neuroevo_ecosystem PRIVATE neuroevo_core)

enable_testing()

add_executable(neuroevo_pruning_tests tests/pruning.cpp)
target_link_libraries(neuroevo_pruning_tests PRIVATE neuroevo_core)
add_test(NAME neuroevo_pruning_tests COMMAND neuroevo_pruning_tests)

add_executable(neuroevo_ecosystem_frontier tests/ecosystem_frontier.cpp)
target_link_libraries(neuroevo_ecosystem_frontier PRIVATE neuroevo_core)
add_test(NAME neuroevo_ecosystem_frontier COMMAND neuroevo_ecosystem_frontier)

add_executable(neuroevo_ecosystem_calibration tests/ecosystem_calibration.cpp)
target_link_libraries(neuroevo_ecosystem_calibration PRIVATE neuroevo_core)
add_test(NAME neuroevo_ecosystem_calibration COMMAND neuroevo_ecosystem_calibration)

add_executable(neuroevo_ecosystem_mechanics tests/ecosystem_mechanics.cpp)
target_link_libraries(neuroevo_ecosystem_mechanics PRIVATE neuroevo_core)
add_test(NAME neuroevo_ecosystem_mechanics COMMAND neuroevo_ecosystem_mechanics)

add_executable(neuroevo_ecosystem_sensing tests/ecosystem_sensing.cpp)
target_link_libraries(neuroevo_ecosystem_sensing PRIVATE neuroevo_core)
add_test(NAME neuroevo_ecosystem_sensing COMMAND neuroevo_ecosystem_sensing)

add_executable(neuroevo_ecosystem_checkpoint tests/ecosystem_checkpoint.cpp)
target_link_libraries(neuroevo_ecosystem_checkpoint PRIVATE neuroevo_core)
add_test(NAME neuroevo_ecosystem_checkpoint COMMAND neuroevo_ecosystem_checkpoint)

add_executable(neuroevo_ecosystem_establishment tests/ecosystem_establishment.cpp)
target_link_libraries(neuroevo_ecosystem_establishment PRIVATE neuroevo_core)
add_test(NAME neuroevo_ecosystem_establishment COMMAND neuroevo_ecosystem_establishment)

add_executable(neuroevo_ecosystem_ancestor tests/ecosystem_ancestor.cpp)
target_link_libraries(neuroevo_ecosystem_ancestor PRIVATE neuroevo_core)
add_test(NAME neuroevo_ecosystem_ancestor COMMAND neuroevo_ecosystem_ancestor)

add_executable(neuroevo_smoke tests/smoke.cpp)
target_link_libraries(neuroevo_smoke PRIVATE neuroevo_core)

add_test(NAME neuroevo_smoke COMMAND neuroevo_smoke)

add_executable(neuroevo_neat_nsga2_tests tests/neat_nsga2.cpp)
target_link_libraries(neuroevo_neat_nsga2_tests PRIVATE neuroevo_core)

add_test(NAME neuroevo_neat_nsga2_tests COMMAND neuroevo_neat_nsga2_tests)

add_executable(neuroevo_sensorimotor_tests tests/sensorimotor.cpp)
target_link_libraries(neuroevo_sensorimotor_tests PRIVATE neuroevo_core)

add_test(NAME neuroevo_sensorimotor_tests COMMAND neuroevo_sensorimotor_tests)

add_executable(neuroevo_mechanics_tests tests/mechanics.cpp)
target_link_libraries(neuroevo_mechanics_tests PRIVATE neuroevo_core)

add_test(NAME neuroevo_mechanics_tests COMMAND neuroevo_mechanics_tests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME neuroevo_ecosystem_viewer
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ecosystem_viewer_test.py)
    add_test(NAME neuroevo_ecosystem_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ecosystem_cli_test.py $<TARGET_FILE:neuroevo_ecosystem>)
    add_test(
        NAME neuroevo_dashboard_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dashboard_test.py
    )
endif()

add_executable(neuroevo_ecosystem_dynamic_food tests/ecosystem_dynamic_food.cpp)
target_link_libraries(neuroevo_ecosystem_dynamic_food PRIVATE neuroevo_core)
add_test(NAME neuroevo_ecosystem_dynamic_food COMMAND neuroevo_ecosystem_dynamic_food)
