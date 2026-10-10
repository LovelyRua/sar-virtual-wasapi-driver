#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include "src/dual_bus_signal.h"
#include "src/wasapi_event_pump.h"
#include "src/signal_window_queue.h"
#include "src/wasapi_capture_timeline.h"
#include "src/wasapi_capture_service_metrics.h"
#include "src/wasapi_probe_options.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

using Clock = std::chrono::steady_clock;
std::mutex g_analysis_output_mutex;

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
    using AnalysisQueue = sar_driver::SignalWindowQueue<
        sar_driver::kProbeRate, sar_driver::kProbeChannels, 4>;

    ~StreamPair() {
        Stop();
        StopAnalyzer();
    }

    void Open(IMMDeviceEnumerator* enumerator, const wchar_t* renderId,
              const wchar_t* captureId, unsigned bus,
              HANDLE renderEvent, HANDLE captureEvent) {
        bus_ = bus;
        renderEvent_ = renderEvent;
        captureEvent_ = captureEvent;
        Check(enumerator->GetDevice(renderId, &renderDevice_), "Get render device");
        Check(enumerator->GetDevice(captureId, &captureDevice_), "Get capture device");
        Check(renderDevice_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(render_.GetAddressOf())),
              "Activate render client");
        Check(captureDevice_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(capture_.GetAddressOf())),
              "Activate capture client");

        auto renderFormat = RenderFormat();
        Check(render_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0,
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
            AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            0, 0, mix, nullptr);
        CoTaskMemFree(mix);
        Check(captureResult, "Initialize capture");
        Check(render_->SetEventHandle(renderEvent_), "Set render event");
        Check(capture_->SetEventHandle(captureEvent_), "Set capture event");
        Check(render_->GetService(IID_PPV_ARGS(&writer_)), "Get render service");
        Check(capture_->GetService(IID_PPV_ARGS(&reader_)), "Get capture service");
        Check(render_->GetBufferSize(&renderCapacity_), "Get render buffer size");
        REFERENCE_TIME defaultPeriod = 0;
        REFERENCE_TIME minimumPeriod = 0;
        Check(capture_->GetDevicePeriod(&defaultPeriod, &minimumPeriod),
              "Get capture device period");
        capture_period_100ns_ = static_cast<std::uint64_t>(
            defaultPeriod > minimumPeriod ? defaultPeriod : minimumPeriod);
        LARGE_INTEGER qpcFrequency{};
        if (!QueryPerformanceFrequency(&qpcFrequency) || qpcFrequency.QuadPart <= 0) {
            throw std::runtime_error("Query performance counter frequency failed");
        }
        qpc_frequency_ = static_cast<std::uint64_t>(qpcFrequency.QuadPart);
        toneTable_.reserve(sar_driver::kProbeRate);
        for (unsigned frame = 0; frame < sar_driver::kProbeRate; ++frame) {
            toneTable_.push_back(sar_driver::ProbeFrame(bus_, frame));
        }
        warmupFrames_ = sar_driver::kProbeRate;
    }

    void StartAnalyzer() {
        analysis_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (analysis_event_ == nullptr) {
            throw std::runtime_error("Create analysis event failed");
        }
        try {
            analyzer_ = std::thread([this] { AnalyzeLoop(); });
        } catch (...) {
            CloseHandle(analysis_event_);
            analysis_event_ = nullptr;
            throw;
        }
    }

    void StopAnalyzer() noexcept {
        if (analyzer_.joinable()) {
            stop_analyzer_.store(true, std::memory_order_release);
            SetEvent(analysis_event_);
            analyzer_.join();
        }
        if (analysis_event_ != nullptr) {
            CloseHandle(analysis_event_);
            analysis_event_ = nullptr;
        }
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

    void PumpEvent(std::size_t eventIndex) {
        if (eventIndex == renderEventIndex_) {
            PumpRender();
        } else if (eventIndex == captureEventIndex_) {
            PumpCapture();
        }
    }

    HANDLE renderEvent() const { return renderEvent_; }
    HANDLE captureEvent() const { return captureEvent_; }

    static constexpr std::size_t renderEventIndex_ = 0;
    static constexpr std::size_t captureEventIndex_ = 1;

    void PumpCapture() {
        service_metrics_.BeginPass();
        UINT32 packetFrames = 0;
        Check(reader_->GetNextPacketSize(&packetFrames), "Get capture packet size");
        while (packetFrames != 0) {
            BYTE* bytes = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devicePosition = 0;
            UINT64 qpcPosition = 0;
            Check(reader_->GetBuffer(&bytes, &frames, &flags,
                                     &devicePosition, &qpcPosition),
                  "Get capture buffer");
            LARGE_INTEGER servicedQpc{};
            if (!QueryPerformanceCounter(&servicedQpc) || servicedQpc.QuadPart < 0) {
                throw std::runtime_error("Query capture service timestamp failed");
            }
            const auto counter = static_cast<std::uint64_t>(servicedQpc.QuadPart);
            const auto wholeSeconds = counter / qpc_frequency_;
            const auto remainder = counter % qpc_frequency_;
            const auto serviceQpc100ns = wholeSeconds *
                sar_driver::WasapiCaptureTimeline::kQpcUnitsPerSecond +
                remainder * sar_driver::WasapiCaptureTimeline::kQpcUnitsPerSecond /
                    qpc_frequency_;
            const auto lateThreshold = capture_period_100ns_ >
                    std::numeric_limits<std::uint64_t>::max() / 2
                ? std::numeric_limits<std::uint64_t>::max()
                : capture_period_100ns_ * 2;
            service_metrics_.ObservePacket(qpcPosition, serviceQpc100ns,
                                           lateThreshold);
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            timeline_.Observe(devicePosition, qpcPosition, frames,
                              sar_driver::kProbeRate,
                              (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0,
                              silent,
                              (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0);
            ++capturePackets_;
            if (frames > maximumPacketFrames_) maximumPacketFrames_ = frames;
            if (silent) ++silentPackets_;
            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0 &&
                warmupFrames_ == 0) {
                ++discontinuities_;
            }
            for (UINT32 frame = 0; frame < frames; ++frame) {
                if (warmupFrames_ != 0) {
                    --warmupFrames_;
                    continue;
                }
                float frame_samples[2]{};
                for (unsigned channel = 0; channel < 2; ++channel) {
                    float value = 0.0f;
                    if (!silent && format_.float32) {
                        value = reinterpret_cast<const float*>(bytes)[frame * 2 + channel];
                    } else if (!silent && format_.pcm16) {
                        value = reinterpret_cast<const std::int16_t*>(bytes)
                                    [frame * 2 + channel] / 32768.0f;
                    }
                    if (!std::isfinite(value)) value = 0.0f;
                    frame_samples[channel] = value;
                }
                pending_window_[pending_frames_ * 2] = frame_samples[0];
                pending_window_[pending_frames_ * 2 + 1] = frame_samples[1];
                if (++pending_frames_ == sar_driver::kProbeRate) {
                    if (analysis_queue_.try_push(pending_window_.data(),
                                                 pending_window_.size())) {
                        if (!SetEvent(analysis_event_)) {
                            analysis_signal_failures_.fetch_add(
                                1, std::memory_order_relaxed);
                        }
                    }
                    pending_frames_ = 0;
                }
            }
            capturedFrames_ += frames;
            if (silent) silentFrames_ += frames;
            lastPacket_ = Clock::now();
            Check(reader_->ReleaseBuffer(frames), "Release capture buffer");
            Check(reader_->GetNextPacketSize(&packetFrames), "Get capture packet size");
        }
        service_metrics_.EndPass();
    }

    void AnalyzeLoop() {
        for (;;) {
            const DWORD wait = WaitForSingleObject(analysis_event_, 100);
            if (wait == WAIT_TIMEOUT) {
                if (stop_analyzer_.load(std::memory_order_acquire) &&
                    analysis_queue_.queued_windows() == 0) {
                    return;
                }
                continue;
            }
            if (wait != WAIT_OBJECT_0) {
                analysis_wait_failures_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            while (analysis_queue_.try_consume_one(
                [this](const float* samples, std::size_t) {
                    const auto result = sar_driver::AnalyzeSignalWindow(
                        samples, sar_driver::kProbeRate, bus_);
                    ++windows_;
                    if (!result.passed()) ++failed_windows_;
                    const std::lock_guard<std::mutex> lock(g_analysis_output_mutex);
                    std::cout << "bus=" << bus_ << " window=" << windows_
                              << " left=" << result.expected_power[0]
                              << " right=" << result.expected_power[1]
                              << " channel_leak_left=" << result.wrong_channel_power[0]
                              << " channel_leak_right=" << result.wrong_channel_power[1]
                              << " bus_leak_left=" << result.other_bus_power[0]
                              << " bus_leak_right=" << result.other_bus_power[1]
                              << " peak=" << result.peak_absolute_sample
                              << " clipped_samples=" << result.clipped_samples
                              << " non_finite_samples=" << result.non_finite_samples
                              << " passed=" << static_cast<int>(result.passed()) << '\n';
                })) {
            }
            if (stop_analyzer_.load(std::memory_order_acquire)) return;
        }
    }

    bool Stalled(Clock::time_point now) const {
        return now - lastPacket_ > std::chrono::milliseconds(250);
    }

    bool ContentPassed(unsigned seconds) const {
        return windows_ >= seconds - 2 && failed_windows_ == 0 &&
               analysis_queue_.dropped_windows() == 0 &&
               analysis_queue_.invalid_pushes() == 0 &&
               analysis_signal_failures_.load(std::memory_order_relaxed) == 0 &&
               analysis_wait_failures_.load(std::memory_order_relaxed) == 0;
    }

    bool ContinuityPassed(unsigned seconds) const {
        return discontinuities_ == 0 &&
               capturedFrames_ >= sar_driver::kProbeRate * (seconds - 2) &&
               sentFrames_ >= sar_driver::kProbeRate * (seconds - 2);
    }

    void Summary() const {
        const auto& timeline = timeline_.stats();
        const auto& service = service_metrics_.stats();
        const auto meanPacketAge = service.packets_with_valid_timestamp == 0 ? 0 :
            service.total_timestamp_age_100ns / service.packets_with_valid_timestamp;
        std::cout << "bus=" << bus_ << " sent_frames=" << sentFrames_
                  << " captured_frames=" << capturedFrames_
                  << " silent_frames=" << silentFrames_
                  << " capture_packets=" << capturePackets_
                  << " silent_packets=" << silentPackets_
                  << " max_packet_frames=" << maximumPacketFrames_
                  << " discontinuities=" << discontinuities_
                  << " timeline_packets=" << timeline.packets
                  << " timestamp_errors=" << timeline.timestamp_error_packets
                  << " position_gap_packets=" << timeline.position_gap_packets
                  << " position_gap_frames=" << timeline.position_gap_frames
                  << " position_overlap_packets=" << timeline.position_overlap_packets
                  << " position_overlap_frames=" << timeline.position_overlap_frames
                  << " qpc_regressions=" << timeline.qpc_regressions
                  << " max_qpc_delta_error_100ns="
                  << timeline.maximum_qpc_delta_error_100ns
                  << " capture_service_passes=" << service.service_passes
                  << " empty_capture_service_passes=" << service.empty_service_passes
                  << " max_packets_per_service_pass=" << service.maximum_packets_per_pass
                  << " mean_packet_age_100ns=" << meanPacketAge
                  << " max_packet_age_100ns=" << service.maximum_timestamp_age_100ns
                  << " packets_over_2_device_periods="
                  << service.packets_over_late_threshold
                  << " future_packet_timestamps="
                  << service.packets_with_future_timestamp
                  << " windows=" << windows_
                  << " failed_windows=" << failed_windows_
                  << " dropped_analysis_windows=" << analysis_queue_.dropped_windows()
                  << " invalid_analysis_windows=" << analysis_queue_.invalid_pushes()
                  << " analysis_signal_failures="
                  << analysis_signal_failures_.load(std::memory_order_relaxed)
                  << " analysis_wait_failures="
                  << analysis_wait_failures_.load(std::memory_order_relaxed) << '\n';
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
    std::uint64_t capturePackets_ = 0;
    std::uint64_t silentPackets_ = 0;
    UINT32 maximumPacketFrames_ = 0;
    unsigned discontinuities_ = 0;
    sar_driver::WasapiCaptureTimeline timeline_;
    sar_driver::WasapiCaptureServiceMetrics service_metrics_;
    std::uint64_t capture_period_100ns_ = 0;
    std::uint64_t qpc_frequency_ = 0;
    std::size_t warmupFrames_ = 0;
    std::array<float, AnalysisQueue::kSamplesPerWindow> pending_window_{};
    std::size_t pending_frames_ = 0;
    AnalysisQueue analysis_queue_;
    HANDLE analysis_event_ = nullptr;
    std::thread analyzer_;
    std::atomic_bool stop_analyzer_{false};
    std::atomic_uint64_t analysis_signal_failures_{0};
    std::atomic_uint64_t analysis_wait_failures_{0};
    unsigned windows_ = 0;
    unsigned failed_windows_ = 0;
    bool renderStarted_ = false;
    bool captureStarted_ = false;
    Clock::time_point lastPacket_{};
    HANDLE renderEvent_ = nullptr;
    HANDLE captureEvent_ = nullptr;
    std::vector<std::array<std::int16_t, 2>> toneTable_;
};

int Run(const wchar_t* const* ids, unsigned pairCount, unsigned firstBus,
        unsigned seconds) {
    if (pairCount == 0 || pairCount > sar_driver::kMaximumProbeBuses ||
        firstBus + pairCount > sar_driver::kProbeBuses) {
        throw std::runtime_error("Invalid WASAPI probe bus range");
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(&enumerator)), "Create endpoint enumerator");
    sar_driver::WasapiEventPump eventPump;
    std::vector<StreamPair> streams(pairCount);
    for (unsigned index = 0; index < pairCount; ++index) {
        const HANDLE renderEvent = eventPump.Create();
        const HANDLE captureEvent = eventPump.Create();
        if (renderEvent == nullptr || captureEvent == nullptr) {
            throw std::runtime_error("Create WASAPI event failed");
        }
        streams[index].Open(enumerator.Get(), ids[index * 2], ids[index * 2 + 1],
                            firstBus + index, renderEvent, captureEvent);
    }
    std::vector<std::size_t> readyEvents;
    if (!eventPump.Prepare(readyEvents)) {
        throw std::runtime_error("Prepare WASAPI event buffer failed: " +
                                 std::to_string(GetLastError()));
    }
    try {
        for (auto& stream : streams) stream.StartAnalyzer();
        for (auto& stream : streams) stream.StartCapture();
        for (auto& stream : streams) stream.StartRender();
        for (const auto& stream : streams) {
            if (stream.renderEvent() == nullptr || stream.captureEvent() == nullptr) {
                throw std::runtime_error("WASAPI event was not initialized");
            }
        }
        const auto start = Clock::now();
        while (Clock::now() - start < std::chrono::seconds(seconds)) {
            const DWORD wait = eventPump.Wait(50, readyEvents);
            if (wait == WAIT_FAILED) {
                const DWORD error = GetLastError();
                throw std::runtime_error("Wait for audio events failed: " +
                                         std::to_string(error));
            }
            for (const std::size_t eventIndex : readyEvents) {
                streams[eventIndex / 2].PumpEvent(eventIndex % 2);
            }
            const auto now = Clock::now();
            for (auto& stream : streams) {
                if (now - start > std::chrono::seconds(1) && stream.Stalled(now)) {
                    throw std::runtime_error("Capture packet stalled for 250 ms");
                }
            }
        }
        for (auto& stream : streams) stream.Stop();
        for (auto& stream : streams) stream.StopAnalyzer();
    } catch (...) {
        for (auto& stream : streams) stream.Stop();
        for (auto& stream : streams) stream.StopAnalyzer();
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
    std::cout << "multi_bus_probe passed=" << static_cast<int>(passed)
              << " content_passed=" << static_cast<int>(contentPassed)
              << " continuity_passed=" << static_cast<int>(continuityPassed)
              << " active_buses=" << pairCount
              << " duration_seconds=" << seconds << '\n';
    return passed ? 0 : 3;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::array<const wchar_t*, 11> arguments{};
    if (argc > static_cast<int>(arguments.size())) {
        std::wcerr << L"Probe accepts at most four endpoint pairs.\n";
        return 1;
    }
    const int copied = std::min(argc, static_cast<int>(arguments.size()));
    for (int index = 0; index < copied; ++index) arguments[index] = argv[index];
    sar_driver::WasapiProbeOptions options;
    std::wstring option_error;
    if (!sar_driver::ParseWasapiProbeOptions(copied, arguments.data(),
                                            options, option_error)) {
        std::wcerr << L"Usage: wasapi_dual_bus_probe <render1> <capture1>"
                      L" <render2> <capture2> <seconds>\n"
                      L"   or: wasapi_dual_bus_probe --single <bus:0..3>"
                      L" <render> <capture> <seconds>\n"
                      L"   or: wasapi_dual_bus_probe --multi <seconds>"
                      L" <render1> <capture1> ... <render4> <capture4>\n"
                   << option_error << L'\n';
        return 1;
    }

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) {
        std::cerr << "COM initialization failed\n";
        return 1;
    }
    int result = 1;
    try {
        result = Run(options.endpoint_ids.data(),
                     static_cast<unsigned>(options.pair_count),
                     options.first_bus, options.duration_seconds);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    CoUninitialize();
    return result;
}
