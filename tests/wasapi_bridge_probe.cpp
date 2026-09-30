#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <propkey.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT32 kSampleRate = 48000;
constexpr double kPi = 3.14159265358979323846;

void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::cerr << operation << " failed: 0x" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << '\n';
        throw std::runtime_error(operation);
    }
}

WAVEFORMATEXTENSIBLE StereoFormat() {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 2;
    format.Format.nSamplesPerSec = kSampleRate;
    format.Format.nAvgBytesPerSec = kSampleRate * 4;
    format.Format.nBlockAlign = 4;
    format.Format.wBitsPerSample = 16;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 16;
    format.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    return format;
}

void DescribeEndpoint(IMMDevice* device, IAudioClient* client, const char* role) {
    WAVEFORMATEX* mixFormat = nullptr;
    const HRESULT formatResult = client->GetMixFormat(&mixFormat);
    if (SUCCEEDED(formatResult) && mixFormat != nullptr) {
        std::cout << role << "_mix_format tag=" << mixFormat->wFormatTag
                  << " channels=" << mixFormat->nChannels
                  << " rate=" << mixFormat->nSamplesPerSec
                  << " bits=" << mixFormat->wBitsPerSample << '\n';
        CoTaskMemFree(mixFormat);
    } else {
        std::cout << role << "_mix_format_error=0x" << std::hex
                  << static_cast<unsigned long>(formatResult) << std::dec << '\n';
        if (mixFormat != nullptr) CoTaskMemFree(mixFormat);
    }

    ComPtr<IAudioEndpointVolume> volume;
    const HRESULT volumeResult = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                                                  nullptr, reinterpret_cast<void**>(volume.GetAddressOf()));
    BOOL muted = FALSE;
    const HRESULT muteResult = SUCCEEDED(volumeResult) ? volume->GetMute(&muted) : volumeResult;
    if (SUCCEEDED(muteResult)) {
        std::cout << role << "_muted=" << (muted ? 1 : 0) << '\n';
    } else {
        std::cout << role << "_mute_unavailable=0x" << std::hex
                  << static_cast<unsigned long>(muteResult) << std::dec << '\n';
    }
}

void ListEndpoints(IMMDeviceEnumerator* enumerator, EDataFlow direction) {
    ComPtr<IMMDeviceCollection> devices;
    Check(enumerator->EnumAudioEndpoints(direction, DEVICE_STATE_ACTIVE, &devices), "EnumAudioEndpoints");
    UINT count = 0;
    Check(devices->GetCount(&count), "GetCount");
    std::wcout << (direction == eRender ? L"Render endpoints:\n" : L"Capture endpoints:\n");
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        ComPtr<IPropertyStore> properties;
        LPWSTR id = nullptr;
        PROPVARIANT name;
        PropVariantInit(&name);
        Check(devices->Item(index, &device), "Item");
        Check(device->GetId(&id), "GetId");
        Check(device->OpenPropertyStore(STGM_READ, &properties), "OpenPropertyStore");
        Check(properties->GetValue(PKEY_Device_FriendlyName, &name), "GetValue");
        std::wcout << L"  " << (name.vt == VT_LPWSTR ? name.pwszVal : L"(unnamed)")
                   << L"\n    " << id << L'\n';
        PropVariantClear(&name);
        CoTaskMemFree(id);
    }
}

double TonePower(const std::vector<int16_t>& samples, size_t startFrame,
                 size_t frameCount, unsigned channel, double frequency) {
    const double coefficient = 2.0 * std::cos(2.0 * kPi * frequency / kSampleRate);
    double previous = 0.0;
    double previous2 = 0.0;
    for (size_t index = 0; index < frameCount; ++index) {
        const double current = static_cast<double>(samples[(startFrame + index) * 2 + channel])
                             + coefficient * previous - previous2;
        previous2 = previous;
        previous = current;
    }
    return previous * previous + previous2 * previous2 - coefficient * previous * previous2;
}

int Run(IMMDeviceEnumerator* enumerator, const wchar_t* renderId, const wchar_t* captureId,
        bool exclusive) {
    ComPtr<IMMDevice> renderDevice;
    ComPtr<IMMDevice> captureDevice;
    Check(enumerator->GetDevice(renderId, &renderDevice), "Get render device");
    Check(enumerator->GetDevice(captureId, &captureDevice), "Get capture device");

    ComPtr<IAudioClient> render;
    ComPtr<IAudioClient2> capture;
    Check(renderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void**>(render.GetAddressOf())), "Activate render");
    Check(captureDevice->Activate(__uuidof(IAudioClient2), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(capture.GetAddressOf())), "Activate capture");

    DWORD sessionId = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
        std::cout << "process_session=" << sessionId
                  << " console_session=" << WTSGetActiveConsoleSessionId() << '\n';
    }
    DescribeEndpoint(renderDevice.Get(), render.Get(), "render");
    DescribeEndpoint(captureDevice.Get(), capture.Get(), "capture");

    AudioClientProperties properties{};
    properties.cbSize = sizeof(properties);
    properties.eCategory = AudioCategory_Other;
    properties.Options = AUDCLNT_STREAMOPTIONS_RAW;
    Check(capture->SetClientProperties(&properties), "Enable RAW capture");

    WAVEFORMATEXTENSIBLE format = StereoFormat();
    const auto shareMode = exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
    const REFERENCE_TIME period = exclusive ? 200000 : 0;
    Check(render->Initialize(shareMode, 0, 200000, period, &format.Format, nullptr),
           "Initialize render at 48 kHz 16-bit stereo");
    Check(capture->Initialize(shareMode, 0, 200000, period, &format.Format, nullptr),
           "Initialize RAW capture at 48 kHz 16-bit stereo");

    ComPtr<IAudioRenderClient> writer;
    ComPtr<IAudioCaptureClient> reader;
    Check(render->GetService(IID_PPV_ARGS(&writer)), "Get render service");
    Check(capture->GetService(IID_PPV_ARGS(&reader)), "Get capture service");
    UINT32 renderCapacity = 0;
    Check(render->GetBufferSize(&renderCapacity), "Get render buffer size");

    Check(capture->Start(), "Start capture");
    Check(render->Start(), "Start render");
    std::vector<int16_t> received;
    UINT64 sentFrames = 0;
    UINT64 silentFrames = 0;
    const auto begin = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - begin < std::chrono::seconds(4)) {
        if (std::chrono::steady_clock::now() - begin < std::chrono::seconds(3)) {
            UINT32 padding = 0;
            Check(render->GetCurrentPadding(&padding), "Get render padding");
            const UINT32 available = renderCapacity - padding;
            if (available != 0) {
                BYTE* bytes = nullptr;
                Check(writer->GetBuffer(available, &bytes), "Get render buffer");
                auto* output = reinterpret_cast<int16_t*>(bytes);
                for (UINT32 frame = 0; frame < available; ++frame) {
                    const double t = static_cast<double>(sentFrames + frame) / kSampleRate;
                    output[frame * 2] = static_cast<int16_t>(10000 * std::sin(2 * kPi * 997 * t));
                    output[frame * 2 + 1] = static_cast<int16_t>(10000 * std::sin(2 * kPi * 1501 * t));
                }
                Check(writer->ReleaseBuffer(available, 0), "Release render buffer");
                sentFrames += available;
            }
        }

        UINT32 packetFrames = 0;
        Check(reader->GetNextPacketSize(&packetFrames), "Get capture packet size");
        while (packetFrames != 0) {
            BYTE* bytes = nullptr;
            DWORD flags = 0;
            UINT32 frames = 0;
            Check(reader->GetBuffer(&bytes, &frames, &flags, nullptr, nullptr), "Get capture buffer");
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                received.insert(received.end(), static_cast<size_t>(frames) * 2, 0);
                silentFrames += frames;
            } else {
                const auto* input = reinterpret_cast<const int16_t*>(bytes);
                received.insert(received.end(), input, input + static_cast<size_t>(frames) * 2);
            }
            Check(reader->ReleaseBuffer(frames), "Release capture buffer");
            Check(reader->GetNextPacketSize(&packetFrames), "Get capture packet size");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(render->Stop(), "Stop render");
    Check(capture->Stop(), "Stop capture");

    const size_t capturedFrames = received.size() / 2;
    std::cout << "sent_frames=" << sentFrames << " captured_frames=" << capturedFrames
              << " silent_frames=" << silentFrames << '\n';
    if (capturedFrames < kSampleRate * 2) return 2;

    double strongest = 0.0;
    double leakage = 0.0;
    for (size_t start = 0; start + kSampleRate <= capturedFrames; start += kSampleRate / 2) {
        const double left = TonePower(received, start, kSampleRate, 0, 997);
        const double right = TonePower(received, start, kSampleRate, 1, 1501);
        if (left + right > strongest) {
            strongest = left + right;
            leakage = TonePower(received, start, kSampleRate, 0, 1501)
                    + TonePower(received, start, kSampleRate, 1, 997);
        }
    }
    std::cout << "target_power=" << strongest << " cross_channel_power=" << leakage << '\n';
    if (strongest < 1e12 || strongest < leakage * 100.0) return 3;
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 1;
    struct Apartment {
        ~Apartment() { CoUninitialize(); }
    } apartment;
    try {
        ComPtr<IMMDeviceEnumerator> enumerator;
        Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               IID_PPV_ARGS(&enumerator)), "Create endpoint enumerator");
        if (argc == 2 && std::wstring(argv[1]) == L"--list") {
            ListEndpoints(enumerator.Get(), eRender);
            ListEndpoints(enumerator.Get(), eCapture);
        } else if (argc == 4 && std::wstring(argv[1]) == L"--run") {
            return Run(enumerator.Get(), argv[2], argv[3], false);
        } else if (argc == 4 && std::wstring(argv[1]) == L"--exclusive") {
            return Run(enumerator.Get(), argv[2], argv[3], true);
        } else {
            std::wcerr << L"Usage: wasapi_bridge_probe --list | --run <render-id> <capture-id>"
                          L" | --exclusive <render-id> <capture-id>\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
