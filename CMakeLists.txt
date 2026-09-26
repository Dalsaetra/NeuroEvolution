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
    src/ecosystem_food_sources.cpp
    src/ecosystem_sensing.cpp
    src/ecosystem_io.cpp
)

target_include_directories(neuroevo_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(OpenMP REQUIRED COMPONENTS CXX)
target_link_libraries(neuroevo_core PUBLIC OpenMP::OpenMP_CXX)

if(MSVC)
    target_compile_options(neuroevo_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(neuroevo_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(neuroevo_ecosystem src/ecosystem_main.cpp)
target_link_libraries(neuroevo_ecosystem PRIVATE neuroevo_core)

enable_testing()
foreach(test IN ITEMS ecosystem_allometry gestation ecosystem_weather_background ecosystem_parallel meta_mutation sensory_mutation pruning ecosystem_predation ecosystem_frontier
        ecosystem_calibration ecosystem_mechanics ecosystem_sensing ecosystem_checkpoint
        ecosystem_ancestor ecosystem_dynamic_food ecosystem_food_sources autapse izhikevich filtered_lif)
    add_executable(neuroevo_${test} tests/${test}.cpp)
    target_link_libraries(neuroevo_${test} PRIVATE neuroevo_core)
    add_test(NAME neuroevo_${test} COMMAND neuroevo_${test})
endforeach()

add_executable(neuroevo_autapse_experiment tools/autapse_experiment.cpp)
target_link_libraries(neuroevo_autapse_experiment PRIVATE neuroevo_core)
add_executable(neuroevo_izhikevich_experiment tools/izhikevich_experiment.cpp)
target_link_libraries(neuroevo_izhikevich_experiment PRIVATE neuroevo_core)
add_executable(neuroevo_cheap_neuron_experiment tools/cheap_neuron_experiment.cpp)
add_executable(neuroevo_filtered_gain_experiment tools/filtered_gain_experiment.cpp)
target_link_libraries(neuroevo_filtered_gain_experiment PRIVATE neuroevo_core)
target_link_libraries(neuroevo_cheap_neuron_experiment PRIVATE neuroevo_core)
add_test(NAME neuroevo_cheap_neuron_kernels COMMAND neuroevo_cheap_neuron_experiment verify)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME neuroevo_ecosystem_viewer
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ecosystem_viewer_test.py)
    add_test(NAME neuroevo_ecosystem_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ecosystem_cli_test.py $<TARGET_FILE:neuroevo_ecosystem>)
endif()
