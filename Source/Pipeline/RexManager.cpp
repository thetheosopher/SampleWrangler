#include "RexManager.h"
#include "Util/Logging.h"

#include <atomic>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>

#if SW_HAVE_REX
#include "REX.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#endif

namespace sw
{

    static std::atomic<bool> sRexAvailable{false};

#if SW_HAVE_REX

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /// Read an entire file into a byte buffer.
    static std::vector<char> readFileBytes(const std::string &path)
    {
        std::ifstream ifs(path, std::ios::binary | std::ios::ate);
        if (!ifs.is_open())
            return {};

        const auto size = ifs.tellg();
        if (size <= 0)
            return {};

        std::vector<char> buf(static_cast<size_t>(size));
        ifs.seekg(0, std::ios::beg);
        ifs.read(buf.data(), size);
        return buf;
    }

    /// No-op callback for REXCreate (we don't need progress during scanning).
    static REX::REXCallbackResult REXCALL rexNullCallback(REX::REX_int32_t /*percent*/, void * /*ud*/)
    {
        return REX::kREXCallback_Continue;
    }

    /// RAII wrapper for a REXHandle.
    class ScopedRexHandle
    {
    public:
        ScopedRexHandle() = default;
        ~ScopedRexHandle()
        {
            if (handle)
                REX::REXDelete(&handle);
        }

        ScopedRexHandle(const ScopedRexHandle &) = delete;
        ScopedRexHandle &operator=(const ScopedRexHandle &) = delete;

        /// Create a REX handle from raw file bytes.
        /// Returns a REXError code.
        REX::REXError create(const char *data, int size)
        {
            return REX::REXCreate(&handle, data, size, rexNullCallback, nullptr);
        }

        REX::REXHandle get() const noexcept { return handle; }
        explicit operator bool() const noexcept { return handle != nullptr; }

    private:
        REX::REXHandle handle = nullptr;
    };

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    bool RexManager::initialize()
    {
        if (sRexAvailable.load(std::memory_order_relaxed))
            return true;

        // Determine the directory containing the running executable so we can
        // locate the REX Shared Library DLL next to it.
        const auto exeFile = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile);
        const auto exeDir = exeFile.getParentDirectory().getFullPathName();

        // REX.c expects a wide-char path on Windows.
        const std::wstring dirW = exeDir.toWideCharPointer();

        REX::REXError err = REX::REXInitializeDLL_DirPath(dirW.c_str());
        if (err != REX::kREXError_NoError)
        {
            SW_LOG_WARN("REX SDK init failed (error " << static_cast<int>(err)
                                                      << "). REX file support disabled.");
            return false;
        }

        sRexAvailable.store(true, std::memory_order_release);
        SW_LOG("REX SDK initialised successfully.");
        return true;
    }

    void RexManager::shutdown()
    {
        if (!sRexAvailable.load(std::memory_order_relaxed))
            return;

        REX::REXUninitializeDLL();
        sRexAvailable.store(false, std::memory_order_release);
        SW_LOG("REX SDK shut down.");
    }

    bool RexManager::isAvailable() noexcept
    {
        return sRexAvailable.load(std::memory_order_acquire);
    }

    // -----------------------------------------------------------------------
    // Metadata
    // -----------------------------------------------------------------------

    std::optional<RexFileInfo> RexManager::readInfo(const std::string &absolutePath)
    {
        if (!isAvailable())
            return std::nullopt;

        auto fileBytes = readFileBytes(absolutePath);
        if (fileBytes.empty())
            return std::nullopt;

        // Try the lightweight REXGetInfoFromBuffer first (no full decode).
        REX::REXInfo ri{};
        REX::REXError err = REX::REXGetInfoFromBuffer(
            static_cast<REX::REX_int32_t>(fileBytes.size()),
            fileBytes.data(),
            static_cast<REX::REX_int32_t>(sizeof(ri)),
            &ri);

        if (err != REX::kREXError_NoError)
        {
            // Fall back to creating a handle and reading info from it.
            ScopedRexHandle h;
            err = h.create(fileBytes.data(), static_cast<int>(fileBytes.size()));
            if (err != REX::kREXError_NoError)
            {
                SW_LOG_WARN("REX: failed to open " << absolutePath << " (error " << static_cast<int>(err) << ")");
                return std::nullopt;
            }

            err = REX::REXGetInfo(h.get(), static_cast<REX::REX_int32_t>(sizeof(ri)), &ri);
            if (err != REX::kREXError_NoError)
                return std::nullopt;
        }

        RexFileInfo info;
        info.channels = ri.fChannels;
        info.sampleRate = ri.fSampleRate;
        info.sliceCount = ri.fSliceCount;
        info.bpm = static_cast<double>(ri.fTempo) / 1000.0;
        info.originalBpm = static_cast<double>(ri.fOriginalTempo) / 1000.0;
        info.ppqLength = ri.fPPQLength;
        info.timeSignatureNum = ri.fTimeSignNom;
        info.timeSignatureDen = ri.fTimeSignDenom;
        info.bitDepth = ri.fBitDepth;

        // Compute duration: PPQ length / (PPQ per quarter * (tempo / 60))
        // kREXPPQ = 15360 ticks per quarter note
        constexpr double kPPQ = 15360.0;
        if (info.bpm > 0.0)
        {
            const double quartersPerSecond = info.bpm / 60.0;
            const double totalQuarters = static_cast<double>(info.ppqLength) / kPPQ;
            info.durationSec = totalQuarters / quartersPerSecond;
        }

        if (info.sampleRate > 0 && info.durationSec > 0.0)
            info.totalSamples = static_cast<int64_t>(info.durationSec * info.sampleRate);

        return info;
    }

    std::vector<RexSliceInfo> RexManager::readSliceInfo(const std::string &absolutePath)
    {
        if (!isAvailable())
            return {};

        auto fileBytes = readFileBytes(absolutePath);
        if (fileBytes.empty())
            return {};

        ScopedRexHandle h;
        auto err = h.create(fileBytes.data(), static_cast<int>(fileBytes.size()));
        if (err != REX::kREXError_NoError)
            return {};

        REX::REXInfo ri{};
        err = REX::REXGetInfo(h.get(), static_cast<REX::REX_int32_t>(sizeof(ri)), &ri);
        if (err != REX::kREXError_NoError)
            return {};

        std::vector<RexSliceInfo> slices;
        slices.reserve(static_cast<size_t>(ri.fSliceCount));

        for (int i = 0; i < ri.fSliceCount; ++i)
        {
            REX::REXSliceInfo si{};
            err = REX::REXGetSliceInfo(h.get(), i,
                                       static_cast<REX::REX_int32_t>(sizeof(si)), &si);
            if (err != REX::kREXError_NoError)
                break;

            slices.push_back(RexSliceInfo{si.fPPQPos, si.fSampleLength});
        }

        return slices;
    }

    // -----------------------------------------------------------------------
    // Decode to AudioBuffer
    // -----------------------------------------------------------------------

    std::unique_ptr<juce::AudioBuffer<float>> RexManager::decodeToBuffer(
        const std::string &absolutePath,
        double &outSampleRate)
    {
        outSampleRate = 0.0;

        if (!isAvailable())
            return nullptr;

        auto fileBytes = readFileBytes(absolutePath);
        if (fileBytes.empty())
            return nullptr;

        ScopedRexHandle h;
        auto err = h.create(fileBytes.data(), static_cast<int>(fileBytes.size()));
        if (err != REX::kREXError_NoError)
        {
            SW_LOG_WARN("REX decode: failed to open " << absolutePath);
            return nullptr;
        }

        REX::REXInfo ri{};
        err = REX::REXGetInfo(h.get(), static_cast<REX::REX_int32_t>(sizeof(ri)), &ri);
        if (err != REX::kREXError_NoError)
            return nullptr;

        const int numChannels = std::clamp(ri.fChannels, 1, 2);
        outSampleRate = static_cast<double>(ri.fSampleRate);

        // Set the output sample rate so slices render at the original rate.
        err = REX::REXSetOutputSampleRate(h.get(), ri.fSampleRate);
        if (err != REX::kREXError_NoError)
            return nullptr;

        // First pass: collect per-slice sample lengths to compute total buffer size.
        struct SliceMeta
        {
            int sampleLength = 0;
        };
        std::vector<SliceMeta> sliceMeta(static_cast<size_t>(ri.fSliceCount));
        int64_t totalSamples = 0;

        for (int i = 0; i < ri.fSliceCount; ++i)
        {
            REX::REXSliceInfo si{};
            err = REX::REXGetSliceInfo(h.get(), i,
                                       static_cast<REX::REX_int32_t>(sizeof(si)), &si);
            if (err != REX::kREXError_NoError)
                return nullptr;

            sliceMeta[static_cast<size_t>(i)].sampleLength = si.fSampleLength;
            totalSamples += si.fSampleLength;
        }

        if (totalSamples <= 0 || totalSamples > 600 * static_cast<int64_t>(ri.fSampleRate))
        {
            SW_LOG_WARN("REX decode: unreasonable total samples (" << totalSamples << ")");
            return nullptr;
        }

        auto buffer = std::make_unique<juce::AudioBuffer<float>>(numChannels, static_cast<int>(totalSamples));
        buffer->clear();

        // Render each slice sequentially into the output buffer.
        int failedSlices = 0;
        int64_t writePos = 0;
        for (int i = 0; i < ri.fSliceCount; ++i)
        {
            const int sliceLen = sliceMeta[static_cast<size_t>(i)].sampleLength;
            if (sliceLen <= 0)
                continue;

            // Per REX SDK test app: allocate one contiguous block for all channels
            // and point outputBuffers[0/1] into that block.
            std::vector<float> sliceSamples(static_cast<size_t>(numChannels) * static_cast<size_t>(sliceLen), 0.0f);
            float *outputPtrs[2] = {sliceSamples.data(), numChannels > 1 ? (sliceSamples.data() + sliceLen) : nullptr};

            err = REX::REXRenderSlice(h.get(), i, sliceLen, outputPtrs);
            if (err == REX::kREXImplError_BufferTooSmall)
            {
                // Some files report a slightly short fSampleLength; retry once
                // with a larger frame length to satisfy the decoder.
                const int retryLen = std::max(sliceLen + 512, static_cast<int>(sliceLen * 1.25f));
                std::vector<float> retrySamples(static_cast<size_t>(numChannels) * static_cast<size_t>(retryLen), 0.0f);
                float *retryPtrs[2] = {retrySamples.data(), numChannels > 1 ? (retrySamples.data() + retryLen) : nullptr};

                err = REX::REXRenderSlice(h.get(), i, retryLen, retryPtrs);
                if (err == REX::kREXError_NoError)
                {
                    const int samplesToCopy = static_cast<int>(
                        std::min<int64_t>(sliceLen, totalSamples - writePos));
                    buffer->copyFrom(0, static_cast<int>(writePos), retryPtrs[0], samplesToCopy);
                    if (numChannels > 1)
                        buffer->copyFrom(1, static_cast<int>(writePos), retryPtrs[1], samplesToCopy);
                    writePos += samplesToCopy;
                    continue;
                }
            }

            if (err != REX::kREXError_NoError)
            {
                ++failedSlices;
                SW_LOG_WARN("REX decode: RenderSlice failed on slice " << i
                                                                       << " (error " << static_cast<int>(err)
                                                                       << ", channels " << numChannels
                                                                       << ", requestedFrames " << sliceLen << ")");
                continue;
            }

            const int samplesToCopy = static_cast<int>(
                std::min<int64_t>(sliceLen, totalSamples - writePos));

            buffer->copyFrom(0, static_cast<int>(writePos), outputPtrs[0], samplesToCopy);
            if (numChannels > 1)
                buffer->copyFrom(1, static_cast<int>(writePos), outputPtrs[1], samplesToCopy);

            writePos += samplesToCopy;
        }

        if (failedSlices >= ri.fSliceCount)
        {
            SW_LOG_WARN("REX decode: all slices failed for " << absolutePath);
            return nullptr;
        }

        return buffer;
    }

#else // !SW_HAVE_REX — stub implementations when SDK not available

    bool RexManager::initialize() { return false; }
    void RexManager::shutdown() {}
    bool RexManager::isAvailable() noexcept { return false; }

    std::optional<RexFileInfo> RexManager::readInfo(const std::string & /*absolutePath*/)
    {
        return std::nullopt;
    }

    std::vector<RexSliceInfo> RexManager::readSliceInfo(const std::string & /*absolutePath*/)
    {
        return {};
    }

#endif // SW_HAVE_REX

} // namespace sw
