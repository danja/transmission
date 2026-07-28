# native/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(transmission_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRANSMISSION_BUILD_TESTS "Build native unit tests" ON)
option(TRANSMISSION_WITH_VST3 "Build the Steinberg SDK-backed VST3 inspector" OFF)
set(VST3_SDK_ROOT "/chalet/VST_SDK/vst3sdk" CACHE PATH "Path to the Steinberg VST3 SDK")

add_library(transmission_engine STATIC
  src/AudioEngine.cpp
  src/AudioGraph.cpp
  src/AudioProcessor.cpp
  src/FakeAudioDevice.cpp
  src/PluginScanner.cpp
  src/TransportClock.cpp
)
target_include_directories(transmission_engine
  PUBLIC include
)
target_compile_options(transmission_engine PRIVATE -Wall -Wextra -Wpedantic)

if(TRANSMISSION_WITH_VST3)
  if(NOT EXISTS "${VST3_SDK_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "VST3_SDK_ROOT does not contain a VST3 SDK: ${VST3_SDK_ROOT}")
  endif()
  # Metadata inspection does not need VSTGUI or plugin editor dependencies.
  set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "Disable VSTGUI for metadata-only build" FORCE)
  set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "Disable SDK hosting examples" FORCE)
  set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "Disable SDK plugin examples" FORCE)
  add_subdirectory("${VST3_SDK_ROOT}" "${CMAKE_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)
  add_library(transmission_vst3_inspector STATIC src/Vst3Inspector.cpp)
  target_include_directories(transmission_vst3_inspector PRIVATE include "${VST3_SDK_ROOT}")
  target_link_libraries(transmission_vst3_inspector PRIVATE sdk_hosting)
  target_compile_features(transmission_vst3_inspector PRIVATE cxx_std_17)
endif()

# VST3 and JACK/PipeWire integration will be added behind these stable engine
# interfaces. Keeping the first target dependency-free makes lifecycle tests fast.
if(TRANSMISSION_BUILD_TESTS)
  enable_testing()
  add_executable(transmission_engine_test tests/AudioEngineTest.cpp)
  target_link_libraries(transmission_engine_test PRIVATE transmission_engine)
  add_test(NAME transmission_engine_test COMMAND transmission_engine_test)
  add_executable(transmission_plugin_scanner_test tests/PluginScannerTest.cpp)
  target_link_libraries(transmission_plugin_scanner_test PRIVATE transmission_engine)
  add_test(NAME transmission_plugin_scanner_test COMMAND transmission_plugin_scanner_test)
  add_executable(transmission_audio_processor_test tests/AudioProcessorTest.cpp)
  target_link_libraries(transmission_audio_processor_test PRIVATE transmission_engine)
  add_test(NAME transmission_audio_processor_test COMMAND transmission_audio_processor_test)
  add_executable(transmission_audio_graph_test tests/AudioGraphTest.cpp)
  target_link_libraries(transmission_audio_graph_test PRIVATE transmission_engine)
  add_test(NAME transmission_audio_graph_test COMMAND transmission_audio_graph_test)
  add_executable(transmission_audio_device_test tests/AudioDeviceTest.cpp)
  target_link_libraries(transmission_audio_device_test PRIVATE transmission_engine)
  add_test(NAME transmission_audio_device_test COMMAND transmission_audio_device_test)
  add_executable(transmission_transport_test tests/TransportClockTest.cpp)
  target_link_libraries(transmission_transport_test PRIVATE transmission_engine)
  add_test(NAME transmission_transport_test COMMAND transmission_transport_test)
endif()
