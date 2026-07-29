# native/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(transmission_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRANSMISSION_BUILD_TESTS "Build native unit tests" ON)
option(TRANSMISSION_WITH_VST3 "Build Steinberg SDK-backed VST3 hosting tools" OFF)
option(TRANSMISSION_WITH_JACK "Build the JACK audio-device backend" OFF)
option(TRANSMISSION_WITH_NAPI "Build the Node N-API control addon" OFF)
option(TRANSMISSION_WITH_GTK_UI "Build the native GTK graph editor" OFF)
set(VST3_SDK_ROOT "$ENV{HOME}/VST_SDK/vst3sdk" CACHE PATH "Path to the Steinberg VST3 SDK")

add_library(transmission_engine STATIC
  src/AudioEngine.cpp
  src/AudioGraph.cpp
  src/AudioProcessor.cpp
  src/FakeAudioDevice.cpp
  src/GraphRuntimeCompiler.cpp
  src/GraphRuntimeController.cpp
  src/JackPortIdentity.cpp
  src/PluginScanner.cpp
  src/RoutedAudioGraph.cpp
  src/TransportClock.cpp
  src/UiProjectCodec.cpp
)
target_include_directories(transmission_engine
  PUBLIC include
)
target_compile_options(transmission_engine PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(transmission_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(TRANSMISSION_WITH_JACK)
  find_path(JACK_INCLUDE_DIR jack/jack.h REQUIRED)
  find_library(JACK_LIBRARY jack REQUIRED)
  target_sources(transmission_engine PRIVATE src/JackAudioDevice.cpp)
  target_include_directories(transmission_engine PRIVATE "${JACK_INCLUDE_DIR}")
  target_link_libraries(transmission_engine PUBLIC "${JACK_LIBRARY}")
  add_executable(transmission_jack_engine_probe src/jack_engine_probe_main.cpp)
  target_link_libraries(transmission_jack_engine_probe PRIVATE transmission_engine)
  add_executable(transmission_jack_midi_source src/jack_midi_source_main.cpp)
  target_link_libraries(transmission_jack_midi_source PRIVATE "${JACK_LIBRARY}")
endif()

if(TRANSMISSION_WITH_NAPI)
  find_path(NODE_API_INCLUDE_DIR node_api.h PATHS /usr/include/node REQUIRED)
  add_library(transmission_native MODULE src/napi_bridge.cpp)
  target_include_directories(transmission_native PRIVATE "${NODE_API_INCLUDE_DIR}" include)
  target_link_libraries(transmission_native PRIVATE transmission_engine)
  set_target_properties(transmission_native PROPERTIES PREFIX "" SUFFIX ".node")
endif()

if(TRANSMISSION_WITH_VST3)
  if(NOT EXISTS "${VST3_SDK_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "VST3_SDK_ROOT does not contain a VST3 SDK: ${VST3_SDK_ROOT}")
  endif()
  # Hosting tools do not need VSTGUI or plugin editor dependencies.
  set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "Disable VSTGUI for metadata-only build" FORCE)
  set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "Disable SDK hosting examples" FORCE)
  set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "Disable SDK plugin examples" FORCE)
  add_subdirectory("${VST3_SDK_ROOT}" "${CMAKE_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)
  add_library(transmission_vst3_inspector STATIC
    src/Vst3Inspector.cpp
    src/Vst3OfflineProbe.cpp
    src/Vst3Processor.cpp
    "${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp"
    "${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp"
  )
  target_include_directories(transmission_vst3_inspector PUBLIC include "${VST3_SDK_ROOT}")
  target_link_libraries(transmission_vst3_inspector PRIVATE sdk_hosting)
  target_compile_features(transmission_vst3_inspector PRIVATE cxx_std_17)
  set_target_properties(transmission_vst3_inspector sdk_hosting sdk_common base pluginterfaces
                        PROPERTIES POSITION_INDEPENDENT_CODE ON)
  add_executable(transmission_vst3_inspect src/vst3_inspect_main.cpp)
  target_link_libraries(transmission_vst3_inspect PRIVATE transmission_vst3_inspector)
  add_executable(transmission_vst3_offline src/vst3_offline_probe_main.cpp)
  target_link_libraries(transmission_vst3_offline PRIVATE transmission_vst3_inspector)
  add_executable(transmission_vst3_graph_probe src/vst3_graph_probe_main.cpp)
  target_link_libraries(transmission_vst3_graph_probe PRIVATE transmission_vst3_inspector transmission_engine)
  add_executable(transmission_vst3_engine_probe src/vst3_engine_probe_main.cpp)
  target_link_libraries(transmission_vst3_engine_probe PRIVATE transmission_vst3_inspector transmission_engine)
  add_executable(transmission_vst3_chain_probe src/vst3_chain_probe_main.cpp)
  target_link_libraries(transmission_vst3_chain_probe PRIVATE transmission_vst3_inspector transmission_engine)
  if(TRANSMISSION_WITH_JACK)
    add_executable(transmission_vst3_jack_chain_probe
      src/vst3_jack_chain_probe_main.cpp
      src/JackConnectionManager.cpp
    )
    target_compile_definitions(transmission_vst3_jack_chain_probe
      PRIVATE TRANSMISSION_UI_WITH_JACK=1
    )
    target_link_libraries(transmission_vst3_jack_chain_probe PRIVATE transmission_vst3_inspector transmission_engine)
  endif()
endif()

if(TRANSMISSION_WITH_NAPI AND TRANSMISSION_WITH_VST3)
  target_compile_definitions(transmission_native PRIVATE TRANSMISSION_NAPI_WITH_VST3=1)
  target_link_libraries(transmission_native PRIVATE transmission_vst3_inspector)
endif()
if(TRANSMISSION_WITH_NAPI AND TRANSMISSION_WITH_JACK)
  target_compile_definitions(transmission_native PRIVATE TRANSMISSION_NAPI_WITH_JACK=1)
endif()

# JACK/PipeWire and the Node bridge sit behind these stable engine interfaces.
# Keeping the default target dependency-free makes lifecycle tests fast.
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
  add_executable(transmission_routed_audio_graph_test tests/RoutedAudioGraphTest.cpp)
  target_link_libraries(transmission_routed_audio_graph_test PRIVATE transmission_engine)
  add_test(NAME transmission_routed_audio_graph_test COMMAND transmission_routed_audio_graph_test)
  add_executable(transmission_graph_runtime_compiler_test tests/GraphRuntimeCompilerTest.cpp)
  target_link_libraries(transmission_graph_runtime_compiler_test PRIVATE transmission_engine)
  add_test(NAME transmission_graph_runtime_compiler_test COMMAND transmission_graph_runtime_compiler_test)
  add_executable(transmission_graph_runtime_controller_test tests/GraphRuntimeControllerTest.cpp)
  target_link_libraries(transmission_graph_runtime_controller_test PRIVATE transmission_engine)
  add_test(NAME transmission_graph_runtime_controller_test COMMAND transmission_graph_runtime_controller_test)
  add_executable(transmission_ui_project_codec_test tests/UiProjectCodecTest.cpp)
  target_link_libraries(transmission_ui_project_codec_test PRIVATE transmission_engine)
  add_test(NAME transmission_ui_project_codec_test COMMAND transmission_ui_project_codec_test)
  add_executable(transmission_jack_port_identity_test tests/JackPortIdentityTest.cpp)
  target_link_libraries(transmission_jack_port_identity_test PRIVATE transmission_engine)
  add_test(NAME transmission_jack_port_identity_test COMMAND transmission_jack_port_identity_test)
  add_executable(transmission_audio_device_test tests/AudioDeviceTest.cpp)
  target_link_libraries(transmission_audio_device_test PRIVATE transmission_engine)
  add_test(NAME transmission_audio_device_test COMMAND transmission_audio_device_test)
  if(TRANSMISSION_WITH_JACK)
    add_executable(transmission_jack_audio_device_test tests/JackAudioDeviceTest.cpp)
    target_link_libraries(transmission_jack_audio_device_test PRIVATE transmission_engine)
    add_test(NAME transmission_jack_audio_device_test COMMAND transmission_jack_audio_device_test)
  endif()
  add_executable(transmission_transport_test tests/TransportClockTest.cpp)
  target_link_libraries(transmission_transport_test PRIVATE transmission_engine)
  add_test(NAME transmission_transport_test COMMAND transmission_transport_test)
endif()

if(TRANSMISSION_WITH_GTK_UI)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  add_executable(transmission_graph_ui
    src/native_graph_ui_main.cpp
    src/JackConnectionManager.cpp
    src/Vst3EditorHost.cpp
  )
  target_link_libraries(transmission_graph_ui PRIVATE PkgConfig::GTK3 transmission_engine)
  target_include_directories(transmission_graph_ui PRIVATE include)
  target_compile_options(transmission_graph_ui PRIVATE -Wall -Wextra -Wpedantic)
  if(TRANSMISSION_WITH_VST3)
    target_compile_definitions(transmission_graph_ui PRIVATE TRANSMISSION_UI_WITH_VST3=1)
    target_include_directories(transmission_graph_ui PRIVATE "${VST3_SDK_ROOT}")
    target_link_libraries(transmission_graph_ui PRIVATE transmission_vst3_inspector)
  endif()
  if(TRANSMISSION_WITH_JACK)
    find_path(TRANSMISSION_UI_JACK_INCLUDE_DIR jack/jack.h REQUIRED)
    find_library(TRANSMISSION_UI_JACK_LIBRARY jack REQUIRED)
    target_compile_definitions(transmission_graph_ui PRIVATE TRANSMISSION_UI_WITH_JACK=1)
    target_include_directories(transmission_graph_ui PRIVATE "${TRANSMISSION_UI_JACK_INCLUDE_DIR}")
    target_link_libraries(transmission_graph_ui PRIVATE "${TRANSMISSION_UI_JACK_LIBRARY}")
  endif()
endif()
