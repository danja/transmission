#include "transmission/AudioEngine.h"
#include "transmission/RoutedAudioGraph.h"
#ifdef TRANSMISSION_NAPI_WITH_JACK
#include "transmission/JackAudioDevice.h"
#endif
#ifdef TRANSMISSION_NAPI_WITH_VST3
#include "transmission/Vst3Processor.h"
#endif

#include <node_api.h>

#include <array>
#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <utility>

namespace {
std::unique_ptr<transmission::AudioEngine> engine;
#ifdef TRANSMISSION_NAPI_WITH_JACK
std::unique_ptr<transmission::JackAudioDevice> jackDevice;
#endif
std::size_t engineChannels = 2;
std::size_t engineFrames = 1024;
double engineSampleRate = 48000.0;

napi_value undefined(napi_env env) {
    napi_value value;
    napi_get_undefined(env, &value);
    return value;
}

napi_value fail(napi_env env, const char* message) {
    napi_throw_error(env, nullptr, message);
    return nullptr;
}

transmission::AudioEngine* requireEngine(napi_env env) {
    if (!engine) {
        fail(env, "Transmission engine has not been created");
        return nullptr;
    }
    return engine.get();
}

bool getNumber(napi_env env, napi_value object, const char* name, double& value) {
    napi_value property;
    if (napi_get_named_property(env, object, name, &property) != napi_ok) return false;
    return napi_get_value_double(env, property, &value) == napi_ok;
}

bool getBoolean(napi_env env, napi_value object, const char* name, bool& value) {
    napi_value property;
    napi_valuetype type = napi_undefined;
    if (napi_get_named_property(env, object, name, &property) != napi_ok ||
        napi_typeof(env, property, &type) != napi_ok || type != napi_boolean) return false;
    return napi_get_value_bool(env, property, &value) == napi_ok;
}

bool getString(napi_env env, napi_value object, const char* name, std::string& value) {
    napi_value property;
    napi_valuetype type = napi_undefined;
    if (napi_get_named_property(env, object, name, &property) != napi_ok ||
        napi_typeof(env, property, &type) != napi_ok || type != napi_string) return false;
    std::size_t length = 0;
    napi_get_value_string_utf8(env, property, nullptr, 0, &length);
    value.resize(length);
    napi_get_value_string_utf8(env, property, value.data(), length + 1, &length);
    return true;
}

bool getUint32(napi_env env, napi_value value, std::uint32_t& result) {
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok) return false;
    if (type == napi_number) return napi_get_value_uint32(env, value, &result) == napi_ok;
    if (type != napi_string) return false;
    std::size_t length = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok) return false;
    std::string text(length, '\0');
    napi_get_value_string_utf8(env, value, text.data(), length + 1, &length);
    if (text.empty()) return false;
    try {
        const auto parsed = std::stoull(text);
        if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
        result = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool getArray(napi_env env, napi_value object, const char* name, napi_value& value) {
    bool isArray = false;
    if (napi_get_named_property(env, object, name, &value) != napi_ok ||
        napi_is_array(env, value, &isArray) != napi_ok) return false;
    return isArray;
}

napi_value createEngine(napi_env env, napi_callback_info info) {
    napi_value argv[1];
    std::size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    if (argc > 0) {
        double value = 0.0;
        if (getNumber(env, argv[0], "channels", value) && value > 0) engineChannels = static_cast<std::size_t>(value);
        if (getNumber(env, argv[0], "blockSize", value) && value > 0) engineFrames = static_cast<std::size_t>(value);
        if (getNumber(env, argv[0], "sampleRate", value) && value > 0) engineSampleRate = value;
    }
    engine = std::make_unique<transmission::AudioEngine>();
    engine->setSampleRate(engineSampleRate);
#ifdef TRANSMISSION_NAPI_WITH_JACK
    std::string deviceName;
    if (argc > 0 && getString(env, argv[0], "device", deviceName) && deviceName == "jack") {
        jackDevice = std::make_unique<transmission::JackAudioDevice>();
        transmission::AudioDeviceConfig config;
        config.channels = engineChannels;
        config.blockSize = engineFrames;
        config.sampleRate = engineSampleRate;
        getBoolean(env, argv[0], "autoConnect", config.autoConnect);
        if (!engine->configureDevice(*jackDevice, config)) {
            const auto message = jackDevice->lastError();
            jackDevice.reset();
            engine.reset();
            return fail(env, message.empty() ? "Unable to configure JACK device" : message.c_str());
        }
    }
#else
    if (argc > 0) {
        std::string deviceName;
        if (getString(env, argv[0], "device", deviceName) && deviceName == "jack") {
            engine.reset();
            return fail(env, "JACK device requires a JACK-enabled N-API build");
        }
    }
#endif
    return undefined(env);
}

napi_value loadProject(napi_env env, napi_callback_info info) {
    napi_value argv[1];
    std::size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    auto* current = requireEngine(env);
    if (!current) return nullptr;
    if (argc < 1) return fail(env, "Compiled project is required");
    napi_value nodes;
    napi_value connections;
    if (!getArray(env, argv[0], "nodes", nodes) || !getArray(env, argv[0], "connections", connections))
        return fail(env, "Compiled project must contain nodes and connections arrays");
    auto routed = std::make_unique<transmission::RoutedAudioGraph>();
    std::uint32_t nodeCount = 0;
    napi_get_array_length(env, nodes, &nodeCount);
    for (std::uint32_t index = 0; index < nodeCount; ++index) {
        napi_value node;
        napi_get_element(env, nodes, index, &node);
        std::string id;
        if (!getString(env, node, "id", id)) return fail(env, "Native graph node id is required");
        std::unique_ptr<transmission::AudioProcessor> processor;
        napi_value settings;
        std::string pluginPath;
        napi_valuetype settingsType = napi_undefined;
        if (napi_get_named_property(env, node, "settings", &settings) == napi_ok &&
            napi_typeof(env, settings, &settingsType) == napi_ok && settingsType == napi_object)
            getString(env, settings, "pluginPath", pluginPath);
        if (!pluginPath.empty()) {
#ifdef TRANSMISSION_NAPI_WITH_VST3
            auto vst = std::make_unique<transmission::Vst3Processor>();
            std::string error;
            if (!vst->initialize(pluginPath, engineChannels, engineFrames, engineSampleRate, error))
                return fail(env, error.c_str());
            processor = std::move(vst);
#else
            return fail(env, "VST3 graph nodes require a VST3-enabled N-API build");
#endif
        } else {
            processor = std::make_unique<transmission::PassThroughProcessor>();
        }
        if (!routed->addNode(std::move(id), std::move(processor)))
            return fail(env, "Unable to add native graph node");
    }
    std::uint32_t connectionCount = 0;
    napi_get_array_length(env, connections, &connectionCount);
    for (std::uint32_t index = 0; index < connectionCount; ++index) {
        napi_value connection;
        napi_get_element(env, connections, index, &connection);
        std::string kind;
        std::string from;
        std::string to;
        if (!getString(env, connection, "kind", kind) || !getString(env, connection, "from", from) ||
            !getString(env, connection, "to", to)) return fail(env, "Invalid native graph connection");
        if (kind == "audio" && !routed->connect(from, to)) return fail(env, "Unable to connect native audio graph");
    }
    if (!current->loadRuntimeGraph("{\"nativeBridge\":true}") ||
        !current->setRoutedAudioGraph(std::move(routed), engineChannels, engineFrames))
        return fail(env, "Unable to load project into native engine");
    return undefined(env);
}

napi_value configureTransport(napi_env env, napi_callback_info info) {
    napi_value argv[1];
    std::size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    auto* current = requireEngine(env);
    if (!current || argc < 1) return argc < 1 ? fail(env, "Transport state is required") : nullptr;

    double sampleRate = 0.0;
    if (getNumber(env, argv[0], "sampleRate", sampleRate) && !current->setSampleRate(sampleRate))
        return fail(env, "Unable to configure native sample rate");
    napi_value tempoMap;
    if (napi_get_named_property(env, argv[0], "tempoMap", &tempoMap) == napi_ok) {
        bool isArray = false;
        napi_is_array(env, tempoMap, &isArray);
        if (isArray) {
            std::uint32_t length = 0;
            napi_get_array_length(env, tempoMap, &length);
            for (std::uint32_t index = 0; index < length; ++index) {
                napi_value change;
                double beat = 0.0;
                double bpm = 0.0;
                napi_get_element(env, tempoMap, index, &change);
                if (getNumber(env, change, "beat", beat) && getNumber(env, change, "bpm", bpm))
                    current->setTempo(bpm, beat);
            }
        }
    }
    napi_value loop;
    if (napi_get_named_property(env, argv[0], "loop", &loop) == napi_ok) {
        napi_valuetype loopType = napi_undefined;
        napi_typeof(env, loop, &loopType);
        if (loopType != napi_object) return undefined(env);
        double start = 0.0;
        double end = 0.0;
        bool enabled = true;
        if (getNumber(env, loop, "startBeat", start) && getNumber(env, loop, "endBeat", end)) {
            getBoolean(env, loop, "enabled", enabled);
            current->setLoop(start, end, enabled);
        }
    }
    return undefined(env);
}

napi_value startAudio(napi_env env, napi_callback_info info) {
    (void)info;
    auto* current = requireEngine(env);
    if (!current) return nullptr;
    return current->start() ? undefined(env) : fail(env, "Unable to start native engine");
}

napi_value stopAudio(napi_env env, napi_callback_info info) {
    (void)info;
    auto* current = requireEngine(env);
    if (!current) return nullptr;
    current->stop();
    return undefined(env);
}

napi_value sendMidi(napi_env env, napi_callback_info info) {
    napi_value argv[2];
    std::size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    auto* current = requireEngine(env);
    if (!current || argc < 2) return fail(env, "sendMidi requires a node id and event");
    napi_value data;
    bool isArray = false;
    if (napi_get_named_property(env, argv[1], "data", &data) != napi_ok ||
        napi_is_array(env, data, &isArray) != napi_ok || !isArray) return fail(env, "MIDI data must be an array");
    std::uint32_t length = 0;
    napi_get_array_length(env, data, &length);
    if (length == 0 || length > 3) return fail(env, "MIDI event must contain 1 to 3 bytes");
    transmission::MidiEvent event;
    event.size = static_cast<std::uint8_t>(length);
    for (std::uint32_t index = 0; index < length; ++index) {
        napi_value byte;
        std::uint32_t value = 0;
        napi_get_element(env, data, index, &byte);
        if (napi_get_value_uint32(env, byte, &value) != napi_ok || value > 255)
            return fail(env, "MIDI bytes must be integers in [0, 255]");
        if (index < event.data.size()) event.data[index] = static_cast<std::uint8_t>(value);
    }
    double offset = 0.0;
    if (getNumber(env, argv[1], "frameOffset", offset) && offset >= 0.0)
        event.frameOffset = static_cast<std::size_t>(offset);
    if (!current->enqueueMidi(event)) return fail(env, "Native MIDI queue is full");
    return undefined(env);
}

napi_value setParameter(napi_env env, napi_callback_info info) {
    napi_value argv[4];
    std::size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    auto* current = requireEngine(env);
    if (!current || argc < 3) return fail(env, "setParameter requires node id, parameter id, and value");

    napi_valuetype nodeType = napi_undefined;
    if (napi_typeof(env, argv[0], &nodeType) != napi_ok || nodeType != napi_string)
        return fail(env, "Parameter node id must be a string");
    std::size_t nodeLength = 0;
    napi_get_value_string_utf8(env, argv[0], nullptr, 0, &nodeLength);
    std::string nodeId(nodeLength, '\0');
    napi_get_value_string_utf8(env, argv[0], nodeId.data(), nodeLength + 1, &nodeLength);

    std::uint32_t parameterId = 0;
    if (!getUint32(env, argv[1], parameterId)) return fail(env, "Parameter id must be a uint32 number or string");
    double value = 0.0;
    if (napi_get_value_double(env, argv[2], &value) != napi_ok || value < 0.0 || value > 1.0)
        return fail(env, "Parameter value must be normalized to [0, 1]");
    std::string error;
    if (!current->setParameter(nodeId, parameterId, value, error))
        return fail(env, error.empty() ? "Unable to set native parameter" : error.c_str());
    return undefined(env);
}

napi_value diagnostics(napi_env env, napi_callback_info info) {
    (void)info;
    auto* current = requireEngine(env);
    if (!current) return nullptr;
    const auto value = current->diagnostics();
    napi_value result;
    napi_create_object(env, &result);
    napi_value number;
#define SET_DIAGNOSTIC(name, field) \
    napi_create_double(env, static_cast<double>(value.field), &number); \
    napi_set_named_property(env, result, name, number)
    SET_DIAGNOSTIC("underruns", underruns);
    SET_DIAGNOSTIC("processedBlocks", processedBlocks);
    SET_DIAGNOSTIC("midiEvents", midiEvents);
    SET_DIAGNOSTIC("positionBeats", positionBeats);
#undef SET_DIAGNOSTIC
    napi_value boolean;
    napi_get_boolean(env, value.running, &boolean);
    napi_set_named_property(env, result, "running", boolean);
    napi_get_boolean(env, value.graphLoaded, &boolean);
    napi_set_named_property(env, result, "graphLoaded", boolean);
    return result;
}

napi_value disposeEngine(napi_env env, napi_callback_info info) {
    (void)info;
    engine.reset();
#ifdef TRANSMISSION_NAPI_WITH_JACK
    jackDevice.reset();
#endif
    return undefined(env);
}
} // namespace

NAPI_MODULE_INIT() {
    const std::array<napi_property_descriptor, 9> properties{{
        {"createEngine", nullptr, createEngine, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"loadProject", nullptr, loadProject, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"configureTransport", nullptr, configureTransport, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"startAudio", nullptr, startAudio, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stopAudio", nullptr, stopAudio, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sendMidi", nullptr, sendMidi, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setParameter", nullptr, setParameter, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getDiagnostics", nullptr, diagnostics, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"disposeEngine", nullptr, disposeEngine, nullptr, nullptr, nullptr, napi_default, nullptr},
    }};
    napi_define_properties(env, exports, properties.size(), properties.data());
    return exports;
}
