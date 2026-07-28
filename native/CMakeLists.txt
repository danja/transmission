# native/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(transmission_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRANSMISSION_BUILD_TESTS "Build native unit tests" ON)
set(VST3_SDK_ROOT "/chalet/VST_SDK/vst3sdk" CACHE PATH "Path to the Steinberg VST3 SDK")

add_library(transmission_engine STATIC
  src/AudioEngine.cpp
)
target_include_directories(transmission_engine
  PUBLIC include
)
target_compile_options(transmission_engine PRIVATE -Wall -Wextra -Wpedantic)

# VST3 and JACK/PipeWire integration will be added behind these stable engine
# interfaces. Keeping the first target dependency-free makes lifecycle tests fast.
if(TRANSMISSION_BUILD_TESTS)
  enable_testing()
  add_executable(transmission_engine_test tests/AudioEngineTest.cpp)
  target_link_libraries(transmission_engine_test PRIVATE transmission_engine)
  add_test(NAME transmission_engine_test COMMAND transmission_engine_test)
endif()
