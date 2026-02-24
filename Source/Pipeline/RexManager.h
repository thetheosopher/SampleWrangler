#pragma once

/// REX SDK integration for SampleWrangler.
///
/// Provides DLL lifecycle management (init / shutdown) and high-level helpers
/// for scanning REX metadata and rendering REX slices to juce::AudioBuffer.
///
/// All functions except isAvailable() must be called from background / message
/// threads only — never from the audio callback.

#include <string>
#include <optional>
#include <memory>
#include <vector>

// Forward declare juce types used in the decode API.
namespace juce
{
    template <typename>
    class AudioBuffer;
}

namespace sw
{

    /// Metadata extracted from a REX/RX2 file via the REX SDK.
    struct RexFileInfo
    {
        int channels = 0;
        int sampleRate = 0;
        int sliceCount = 0;
        double bpm = 0.0;         ///< Tempo set when exported from ReCycle
        double originalBpm = 0.0; ///< Original tempo of loop
        int ppqLength = 0;        ///< Length in PPQ ticks (kREXPPQ = 15360 per quarter note)
        int timeSignatureNum = 4;
        int timeSignatureDen = 4;
        int bitDepth = 0;
        double durationSec = 0.0; ///< Computed from PPQ length and tempo
        int64_t totalSamples = 0; ///< Computed total rendered samples at original sample rate
    };

    /// Per-slice positional info.
    struct RexSliceInfo
    {
        int ppqPos = 0;       ///< Position in PPQ ticks within the loop
        int sampleLength = 0; ///< Length of rendered slice at original sample rate
    };

    /// Manages the REX Shared Library DLL lifecycle and provides
    /// high-level decode / metadata APIs.
    ///
    /// Thread safety: initialize() and shutdown() must be called from the
    /// message thread (typically at app startup / shutdown). All other methods
    /// are safe to call from any non-audio thread.
    class RexManager
    {
    public:
        /// Attempt to load the REX Shared Library from the directory
        /// containing the running executable.
        /// Returns true on success.
        static bool initialize();

        /// Unload the REX Shared Library. Call once at shutdown.
        static void shutdown();

        /// Returns true if the REX SDK was successfully initialised.
        /// Safe to call from any thread (including audio — but don't
        /// call any other RexManager methods from audio).
        static bool isAvailable() noexcept;

        // ----- Metadata (background / message thread) -------------------------

        /// Read REX metadata from a file on disk.
        /// Returns nullopt if the file cannot be opened as a REX file.
        static std::optional<RexFileInfo> readInfo(const std::string &absolutePath);

        /// Read per-slice info (positions and lengths).
        static std::vector<RexSliceInfo> readSliceInfo(const std::string &absolutePath);

#if SW_HAVE_REX
        // ----- Decode (background thread) -------------------------------------

        /// Decode a REX file into a mono or stereo AudioBuffer at the file's
        /// native sample rate. Slices are composited sequentially (at original
        /// tempo) into a single contiguous buffer suitable for preview playback.
        ///
        /// Returns nullptr on failure.
        static std::unique_ptr<juce::AudioBuffer<float>> decodeToBuffer(
            const std::string &absolutePath,
            double &outSampleRate);
#endif

    private:
        RexManager() = delete;
    };

    /// Returns true if the extension (lowercase, no dot) is a REX format.
    inline bool isRexExtension(const std::string &ext)
    {
        return ext == "rex" || ext == "rx2";
    }

} // namespace sw
