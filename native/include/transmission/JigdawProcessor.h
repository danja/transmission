// native/include/transmission/JigdawProcessor.h

#pragma once

#include "AudioProcessor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace transmission {

/** One named value a JigDAW parameter can take, from lv2:scalePoint. */
struct JigdawScalePoint {
    std::string label;
    double value = 0.0;
};

/**
 * A declared JigDAW parameter. `id` is the plugin's jig:paramIndex and is what
 * the engine's normalized parameter interface addresses; the range is the one
 * the profile declares, which is what the index means to the module.
 */
struct JigdawParameterDescriptor {
    std::uint32_t id = 0;
    std::string symbol;
    std::string name;
    std::string unit;
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    bool toggled = false;
    bool enumeration = false;
    std::vector<JigdawScalePoint> scalePoints;
};

/**
 * A `jig:asset` the profile names — a file the module loads at start-up and,
 * when `userReplaceable`, one a person may load a different one into (Ferrite's
 * neural amp model and cabinet impulse response are one of these each).
 */
struct JigdawAssetDescriptor {
    std::string key;    ///< the asset node's IRI fragment, e.g. "nam"
    bool userReplaceable = false;
};

/**
 * What a host observes about a JigDAW plugin once its profile has been read:
 * discovered facts in the sense docs/plugin-profiles.md gives the word, even
 * though a profile is curated, because a profile can name a module this host
 * cannot load and only loading finds out.
 */
struct JigdawPluginTopology {
    std::string iri;
    std::string name;
    std::string comment;
    std::string vendor;
    std::string abi;
    std::size_t audioInputs = 0;
    std::size_t audioOutputs = 0;
    std::size_t midiInputs = 0;
    std::size_t midiOutputs = 0;
    bool requiresTransport = false;
    /// The plugin's declared processing latency in frames (jig:latencyFrames).
    /// Surfaced for hosts to see; this engine does not compensate it.
    int latencyFrames = 0;
    std::vector<JigdawParameterDescriptor> parameters;
    std::vector<JigdawAssetDescriptor> assets;
};

/**
 * Reads a JigDAW plugin profile and reports the port shape the graph editor
 * and compiler need. Control thread only: it dereferences an IRI.
 */
class JigdawInspector {
public:
    bool inspectTopology(const std::string& pluginIri,
                         JigdawPluginTopology& topology, std::string& error) const;
};

/**
 * A JigDAW plugin hosted through the engine's real-time processor contract.
 *
 * Everything that fetches, verifies, compiles or allocates happens in
 * initialize(); process() only moves samples between preallocated buffers and
 * calls into an already-compiled WebAssembly module.
 *
 * The module's jig_max_frames() is usually smaller than the device block size
 * (128 against 256 or 1024), so a block is processed in sub-blocks of at most
 * that many frames, with MIDI offsets rebased and the transport advanced across
 * each one. Processing only the first jig_max_frames() of a block and leaving
 * the rest is silence with a click in it.
 */
class JigdawProcessor final : public AudioProcessor {
public:
    JigdawProcessor();
    ~JigdawProcessor() override;

    JigdawProcessor(const JigdawProcessor&) = delete;
    JigdawProcessor& operator=(const JigdawProcessor&) = delete;

    /**
     * Dereference the IRI, verify and load the module, then fetch, verify and
     * load every `jig:asset` it declares — the shipped default, unless
     * `assetOverridePaths` names a local file for that key (a `jig:asset` the
     * profile marked `jig:userReplaceable`, chosen by a person instead of
     * fetched). Control thread only.
     */
    bool initialize(const std::string& pluginIri, std::size_t blockSize,
                    double sampleRate, std::string& error,
                    const std::unordered_map<std::string, std::string>& assetOverridePaths = {});

    bool ready() const noexcept override;
    const std::string& pluginName() const noexcept;
    const JigdawPluginTopology& topology() const noexcept;

    bool setParameter(std::uint32_t parameterId, double normalizedValue,
                      std::string& error) override;
    bool enqueueParameter(std::uint32_t parameterId,
                          double normalizedValue,
                          std::uint32_t sampleOffset) noexcept override;
    void applyPendingParameters() noexcept override;
    void setProcessContext(const AudioProcessContext& context) noexcept override;
    std::size_t takeOutputMidi(MidiEvent* events,
                               std::size_t capacity) noexcept override;
    bool reconfigure(std::size_t frames, std::string& error) override;

    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
    void process(const float* const* inputs, std::size_t inputChannels,
                 float* const* outputs, std::size_t outputChannels,
                 std::size_t frames) noexcept override;
    void processWithMidi(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const MidiEvent* events,
                         std::size_t eventCount) noexcept override;
    void processWithMidi(const float* const* inputs, std::size_t inputChannels,
                         float* const* outputs, std::size_t outputChannels,
                         std::size_t frames, const MidiEvent* events,
                         std::size_t eventCount) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace transmission
