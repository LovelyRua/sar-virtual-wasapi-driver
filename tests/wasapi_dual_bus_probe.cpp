#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include "src/dual_bus_signal.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

using Clock = std::chrono::steady_clock;

void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::cerr << operation << " failed: 0x" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << '\n';
        throw std::runtime_error(operation);
    }
}

WAVEFORMATEXTENSIBLE RenderFormat() {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 2;
    format.Format.nSamplesPerSec = sar_driver::kProbeRate;
    format.Format.nAvgBytesPerSec = sar_driver::kProbeRate * 4;
    format.Format.nBlockAlign = 4;
    format.Format.wBitsPerSample = 16;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 16;
    format.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    return format;
}

struct CaptureFormat {
    bool float32 = false;
    bool pcm16 = false;
};

CaptureFormat InspectCaptureFormat(const WAVEFORMATEX* format) {
    if (format == nullptr || format->nChannels != 2 ||
        format->nSamplesPerSec != sar_driver::kProbeRate) {
        throw std::runtime_error("Capture mix must be 48 kHz stereo");
    }
    const bool extensible = format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                            format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) -
                                              sizeof(WAVEFORMATEX);
    const auto* extended = extensible
        ? reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format) : nullptr;
    CaptureFormat result;
    result.float32 = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                     (extended != nullptr &&
                      IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
    result.pcm16 = format->wFormatTag == WAVE_FORMAT_PCM ||
                   (extended != nullptr &&
                    IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_PCM));
    result.float32 = result.float32 && format->wBitsPerSample == 32 &&
                     format->nBlockAlign == 8;
    result.pcm16 = result.pcm16 && format->wBitsPerSample == 16 &&
                   format->nBlockAlign == 4;
    if (!result.float32 && !result.pcm16) {
        throw std::runtime_error("Capture mix must be float32 or PCM16 stereo");
    }
    return result;
}

class StreamPair {
public:
    void Open(IMMDeviceEnumerator* enumerator, const wchar_t* renderId,
              const wchar_t* captureId, unsigned bus) {
        bus_ = bus;
        Check(enumerator->GetDevice(renderId, &renderDevice_), "Get render device");
        Check(enumerator->GetDevice(captureId, &captureDevice_), "Get capture device");
        Check(renderDevice_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(render_.GetAddressOf())),
              "Activate render client");
        Check(captureDevice_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(capture_.GetAddressOf())),
              "Activate capture client");

        auto renderFormat = RenderFormat();
        Check(render_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 200000, 0,
                                  &renderFormat.Format, nullptr), "Initialize render");
        WAVEFORMATEX* mix = nullptr;
        Check(capture_->GetMixFormat(&mix), "Get capture mix format");
        if (mix == nullptr) throw std::runtime_error("Capture mix format is null");
        try {
            format_ = InspectCaptureFormat(mix);
        } catch (...) {
            CoTaskMemFree(mix);
            throw;
        }
        const HRESULT captureResult = capture_->Initialize(
            AUDCLNT_SHAREMODE_SHARED, 0, 200000, 0, mix, nullptr);
        CoTaskMemFree(mix);
        Check(captureResult, "Initialize capture");
        Check(render_->GetService(IID_PPV_ARGS(&writer_)), "Get render service");
        Check(capture_->GetService(IID_PPV_ARGS(&reader_)), "Get capture service");
        Check(render_->GetBufferSize(&renderCapacity_), "Get render buffer size");
        toneTable_.reserve(sar_driver::kProbeRate);
        for (unsigned frame = 0; frame < sar_driver::kProbeRate; ++frame) {
            toneTable_.push_back(sar_driver::ProbeFrame(bus_, frame));
        }
        samples_.reserve(sar_driver::kProbeRate * 4);
        warmupFrames_ = sar_driver::kProbeRate;
    }

    void StartCapture() {
        Check(capture_->Start(), "Start capture");
        captureStarted_ = true;
        lastPacket_ = Clock::now();
    }

    void StartRender() {
        Check(render_->Start(), "Start render");
        renderStarted_ = true;
    }

    void Stop() {
        if (renderStarted_) {
            render_->Stop();
            renderStarted_ = false;
        }
        if (captureStarted_) {
            capture_->Stop();
            captureStarted_ = false;
        }
    }

    void PumpRender() {
        UINT32 padding = 0;
        Check(render_->GetCurrentPadding(&padding), "Get render padding");
        if (padding > renderCapacity_) throw std::runtime_error("Invalid render padding");
        const UINT32 available = renderCapacity_ - padding;
        if (available == 0) return;
        BYTE* bytes = nullptr;
        Check(writer_->GetBuffer(available, &bytes), "Get render buffer");
        auto* output = reinterpret_cast<std::int16_t*>(bytes);
        for (UINT32 frame = 0; frame < available; ++frame) {
            const auto& sample = toneTable_[(sentFrames_ + frame) % sar_driver::kProbeRate];
            output[frame * 2] = sample[0];
            output[frame * 2 + 1] = sample[1];
        }
        Check(writer_->ReleaseBuffer(available, 0), "Release render buffer");
        sentFrames_ += available;
    }

    void PumpCapture() {
        UINT32 packetFrames = 0;
        Check(reader_->GetNextPacketSize(&packetFrames), "Get capture packet size");
        while (packetFrames != 0) {
            BYTE* bytes = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            Check(reader_->GetBuffer(&bytes, &frames, &flags, nullptr, nullptr),
                  "Get capture buffer");
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0 &&
                warmupFrames_ == 0) {
                ++discontinuities_;
                std::cout << "bus=" << bus_ << " discontinuity=" << discontinuities_
                          << " capture_frame=" << capturedFrames_
                          << " packet_frames=" << frames << '\n';
            }
            for (UINT32 frame = 0; frame < frames; ++frame) {
                if (warmupFrames_ != 0) {
                    --warmupFrames_;
                    continue;
                }
                for (unsigned channel = 0; channel < 2; ++channel) {
                    float value = 0.0f;
                    if (!silent && format_.float32) {
                        value = reinterpret_cast<const float*>(bytes)[frame * 2 + channel];
                    } else if (!silent && format_.pcm16) {
                        value = reinterpret_cast<const std::int16_t*>(bytes)
                                    [frame * 2 + channel] / 32768.0f;
                    }
                    if (!std::isfinite(value)) value = 0.0f;
                    samples_.push_back(value);
                }
            }
            capturedFrames_ += frames;
            if (silent) silentFrames_ += frames;
            lastPacket_ = Clock::now();
            Check(reader_->ReleaseBuffer(frames), "Release capture buffer");
            Check(reader_->GetNextPacketSize(&packetFrames), "Get capture packet size");
        }
    }

    void AnalyzeReadyWindows() {
        const std::size_t windowSamples = sar_driver::kProbeRate * 2;
        while (samples_.size() - consumed_ >= windowSamples) {
            const auto result = sar_driver::AnalyzeSignalWindow(
                samples_.data() + consumed_, sar_driver::kProbeRate, bus_);
            ++windows_;
            if (!result.passed()) ++failedWindows_;
            std::cout << "bus=" << bus_ << " window=" << windows_
                      << " left=" << result.expected_power[0]
                      << " right=" << result.expected_power[1]
                      << " channel_leak_left=" << result.wrong_channel_power[0]
                      << " channel_leak_right=" << result.wrong_channel_power[1]
                      << " bus_leak_left=" << result.other_bus_power[0]
                      << " bus_leak_right=" << result.other_bus_power[1]
                      << " passed=" << static_cast<int>(result.passed()) << '\n';
            consumed_ += windowSamples;
        }
        if (consumed_ != 0) {
            samples_.erase(samples_.begin(), samples_.begin() + consumed_);
            consumed_ = 0;
        }
    }

    bool Stalled(Clock::time_point now) const {
        return now - lastPacket_ > std::chrono::milliseconds(250);
    }

    bool ContentPassed(unsigned seconds) const {
        return windows_ >= seconds - 2 && failedWindows_ == 0;
    }

    bool ContinuityPassed(unsigned seconds) const {
        return discontinuities_ == 0 &&
               capturedFrames_ >= sar_driver::kProbeRate * (seconds - 2) &&
               sentFrames_ >= sar_driver::kProbeRate * (seconds - 2);
    }

    void Summary() const {
        std::cout << "bus=" << bus_ << " sent_frames=" << sentFrames_
                  << " captured_frames=" << capturedFrames_
                  << " silent_frames=" << silentFrames_
                  << " discontinuities=" << discontinuities_
                  << " windows=" << windows_
                  << " failed_windows=" << failedWindows_ << '\n';
    }

private:
    unsigned bus_ = 0;
    CaptureFormat format_{};
    ComPtr<IMMDevice> renderDevice_;
    ComPtr<IMMDevice> captureDevice_;
    ComPtr<IAudioClient> render_;
    ComPtr<IAudioClient> capture_;
    ComPtr<IAudioRenderClient> writer_;
    ComPtr<IAudioCaptureClient> reader_;
    UINT32 renderCapacity_ = 0;
    std::uint64_t sentFrames_ = 0;
    std::uint64_t capturedFrames_ = 0;
    std::uint64_t silentFrames_ = 0;
    unsigned discontinuities_ = 0;
    std::size_t warmupFrames_ = 0;
    std::size_t consumed_ = 0;
    unsigned windows_ = 0;
    unsigned failedWindows_ = 0;
    bool renderStarted_ = false;
    bool captureStarted_ = false;
    Clock::time_point lastPacket_{};
    std::vector<float> samples_;
    std::vector<std::array<std::int16_t, 2>> toneTable_;
};

unsigned ParseSeconds(const wchar_t* text) {
    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || value < 5 || value > 3600) {
        throw std::runtime_error("Duration must be 5..3600 seconds");
    }
    return static_cast<unsigned>(value);
}

int Run(const wchar_t* const* ids, unsigned pairCount, unsigned firstBus,
        unsigned seconds) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(&enumerator)), "Create endpoint enumerator");
    std::vector<StreamPair> streams(pairCount);
    for (unsigned index = 0; index < pairCount; ++index) {
        streams[index].Open(enumerator.Get(), ids[index * 2], ids[index * 2 + 1],
                            firstBus + index);
    }
    try {
        for (auto& stream : streams) stream.StartCapture();
        for (auto& stream : streams) stream.StartRender();
        const auto start = Clock::now();
        while (Clock::now() - start < std::chrono::seconds(seconds)) {
            for (auto& stream : streams) stream.PumpRender();
            for (auto& stream : streams) stream.PumpCapture();
            for (auto& stream : streams) stream.AnalyzeReadyWindows();
            const auto now = Clock::now();
            for (auto& stream : streams) {
                if (now - start > std::chrono::seconds(1) && stream.Stalled(now)) {
                    throw std::runtime_error("Capture packet stalled for 250 ms");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        for (auto& stream : streams) stream.Stop();
    } catch (...) {
        for (auto& stream : streams) stream.Stop();
        throw;
    }
    for (const auto& stream : streams) stream.Summary();
    bool contentPassed = true;
    bool continuityPassed = true;
    for (const auto& stream : streams) {
        contentPassed = stream.ContentPassed(seconds) && contentPassed;
        continuityPassed = stream.ContinuityPassed(seconds) && continuityPassed;
    }
    const bool passed = contentPassed && continuityPassed;
    std::cout << "dual_bus_probe passed=" << static_cast<int>(passed)
              << " content_passed=" << static_cast<int>(contentPassed)
              << " continuity_passed=" << static_cast<int>(continuityPassed)
              << " active_buses=" << pairCount
              << " duration_seconds=" << seconds << '\n';
    return passed ? 0 : 3;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 6) {
        std::wcerr << L"Usage: wasapi_dual_bus_probe <render1> <capture1>"
                      L" <render2> <capture2> <seconds> | --single <bus:0|1>"
                      L" <render> <capture> <seconds>\n";
        return 1;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) {
        std::cerr << "COM initialization failed\n";
        return 1;
    }
    int result = 1;
    try {
        const unsigned seconds = ParseSeconds(argv[5]);
        if (std::wstring(argv[1]) == L"--single") {
            const std::wstring bus(argv[2]);
            if (bus != L"0" && bus != L"1") {
                throw std::runtime_error("Single-bus mode requires bus 0 or 1");
            }
            const std::array<const wchar_t*, 2> ids{{argv[3], argv[4]}};
            result = Run(ids.data(), 1, bus == L"1" ? 1 : 0, seconds);
        } else {
            const std::array<const wchar_t*, 4> ids{{argv[1], argv[2], argv[3], argv[4]}};
            result = Run(ids.data(), 2, 0, seconds);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    CoUninitialize();
    return result;
}
