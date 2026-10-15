# Standalone build of the engine-agnostic flight core + its tests.
# Runs anywhere with a C++17 compiler (cloud sessions included); no Unreal needed.
#   cmake -S Tools/FlightCoreTests -B build/flightcore -G Ninja && cmake --build build/flightcore
#   ./build/flightcore/flightcore_tests
cmake_minimum_required(VERSION 3.20)
project(FlightCoreTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Source/FlightCore)

# The core library: everything in Private/ except Unreal module boilerplate.
add_library(flightcore STATIC
  ${CORE_DIR}/Private/FlightModel.cpp
  ${CORE_DIR}/Private/WindField.cpp
  ${CORE_DIR}/Private/LookControl.cpp
  ${CORE_DIR}/Private/WindStreaks.cpp)
target_include_directories(flightcore PUBLIC ${CORE_DIR}/Public)

# Strict warnings approximating Unreal's (shadowing and conversions are errors there).
if(MSVC)
  target_compile_options(flightcore PRIVATE /W4 /WX)
else()
  target_compile_options(flightcore PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    -Wdouble-promotion -Werror -fno-exceptions)
endif()

add_executable(flightcore_tests
  test_main.cpp
  test_math.cpp
  test_flight_model.cpp
  test_wind.cpp
  test_look.cpp
  test_streaks.cpp)
target_include_directories(flightcore_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(flightcore_tests PRIVATE flightcore)

# Flight lab: runs scripted scenarios and writes CSV trajectories for charts.
add_executable(flight_lab flight_lab.cpp)
target_link_libraries(flight_lab PRIVATE flightcore)

enable_testing()
add_test(NAME flightcore_tests COMMAND flightcore_tests)

# Wind lab: renders visible-wind streaks around a building to an SVG.
add_executable(wind_lab wind_lab.cpp)
target_link_libraries(wind_lab PRIVATE flightcore)
