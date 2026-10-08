#ifdef _WIN32

#include "wasapi_inventory.h"

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <propsys.h>
#include <wrl/client.h>

#include <algorithm>
#include <memory>
#include <new>
#include <utility>

namespace sar::devices {
namespace {

using Microsoft::WRL::ComPtr;

struct CoTaskMemDeleter {
    void operator()(void* memory) const noexcept { CoTaskMemFree(memory); }
};

using CoTaskString = std::unique_ptr<wchar_t, CoTaskMemDeleter>;
using CoTaskFormat = std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter>;

class ComApartment final {
public:
    ComApartment() noexcept : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment() {
        if (m_result == S_OK || m_result == S_FALSE) CoUninitialize();
    }
    HRESULT result() const noexcept {
        return (m_result == RPC_E_CHANGED_MODE) ? S_OK : m_result;
    }
private:
    HRESULT m_result;
};

HRESULT get_default_id(IMMDeviceEnumerator* enumerator, EDataFlow flow,
                       std::wstring& id) {
    ComPtr<IMMDevice> device;
    const HRESULT activation = enumerator->GetDefaultAudioEndpoint(
        flow, eMultimedia, device.GetAddressOf());
    if (FAILED(activation)) return activation;
    CoTaskString raw_id;
    const HRESULT result = device->GetId(reinterpret_cast<LPWSTR*>(raw_id.GetAddressOf()));
    if (FAILED(result)) return result;
    if (!raw_id) return E_UNEXPECTED;
    id.assign(raw_id.get());
    return S_OK;
}

HRESULT get_friendly_name(IMMDevice* device, std::wstring& name) {
    ComPtr<IPropertyStore> properties;
    HRESULT result = device->OpenPropertyStore(STGM_READ, properties.GetAddressOf());
    if (FAILED(result)) return result;
    PROPVARIANT value;
    PropVariantInit(&value);
    result = properties->GetValue(PKEY_Device_FriendlyName, &value);
    if (SUCCEEDED(result)) {
        if (value.vt == VT_LPWSTR && value.pwszVal) name.assign(value.pwszVal);
        else result = DISP_E_TYPEMISMATCH;
    }
    PropVariantClear(&value);
    return result;
}

void read_mix_format(IMMDevice* device, WasapiEndpoint& endpoint) {
    if (endpoint.state != DEVICE_STATE_ACTIVE) {
        endpoint.format_error = AUDCLNT_E_DEVICE_INVALIDATED;
        return;
    }
    ComPtr<IAudioClient> client;
    endpoint.format_error = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER,
                                              nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(endpoint.format_error)) return;
    WAVEFORMATEX* raw_format = nullptr;
    endpoint.format_error = client->GetMixFormat(&raw_format);
    CoTaskFormat format(raw_format);
    if (FAILED(endpoint.format_error)) return;
    if (!format || format->nChannels == 0 || format->nSamplesPerSec == 0) {
        endpoint.format_error = E_UNEXPECTED;
        return;
    }
    endpoint.channels = format->nChannels;
    endpoint.sample_rate = format->nSamplesPerSec;
    endpoint.sample_format = format->wFormatTag;
    endpoint.container_bits = format->wBitsPerSample;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format.get());
        endpoint.channel_mask = extensible->dwChannelMask;
        endpoint.sample_format = IsEqualGUID(extensible->SubFormat,
                                             KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
                                     ? WAVE_FORMAT_IEEE_FLOAT
                                     : IsEqualGUID(extensible->SubFormat,
                                                   KSDATAFORMAT_SUBTYPE_PCM)
                                           ? WAVE_FORMAT_PCM : WAVE_FORMAT_EXTENSIBLE;
        if (extensible->Samples.wValidBitsPerSample != 0) {
            endpoint.container_bits = extensible->Samples.wValidBitsPerSample;
        }
    }
    endpoint.mix_format_available = true;
}

HRESULT append_flow(IMMDeviceEnumerator* enumerator, EDataFlow flow,
                    const std::wstring& default_id,
                    std::vector<WasapiEndpoint>& endpoints) {
    ComPtr<IMMDeviceCollection> collection;
    HRESULT result = enumerator->EnumAudioEndpoints(
        flow, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED |
                  DEVICE_STATE_NOTPRESENT | DEVICE_STATE_UNPLUGGED,
        collection.GetAddressOf());
    if (FAILED(result)) return result;

    UINT count = 0;
    result = collection->GetCount(&count);
    if (FAILED(result)) return result;
    if (count > 1024) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        result = collection->Item(index, device.GetAddressOf());
        if (FAILED(result)) return result;
        WasapiEndpoint endpoint;
        endpoint.flow = flow == eRender ? EndpointFlow::render : EndpointFlow::capture;
        result = device->GetState(&endpoint.state);
        if (FAILED(result)) return result;
        CoTaskString raw_id;
        result = device->GetId(reinterpret_cast<LPWSTR*>(raw_id.GetAddressOf()));
        if (FAILED(result)) return result;
        if (!raw_id) return E_UNEXPECTED;
        endpoint.id.assign(raw_id.get());
        endpoint.is_default = !default_id.empty() && endpoint.id == default_id;
        const HRESULT name_result = get_friendly_name(device.Get(), endpoint.name);
        if (FAILED(name_result)) endpoint.name = L"(friendly name unavailable)";
        read_mix_format(device.Get(), endpoint);
        endpoints.push_back(std::move(endpoint));
    }
    return S_OK;
}

} // namespace

HRESULT enumerate_wasapi_endpoints(std::vector<WasapiEndpoint>& endpoints) {
    endpoints.clear();
    ComApartment apartment;
    if (FAILED(apartment.result())) return apartment.result();
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(enumerator.GetAddressOf()));
    if (FAILED(result)) return result;
    std::wstring default_render;
    std::wstring default_capture;
    if (FAILED(get_default_id(enumerator.Get(), eRender, default_render))) default_render.clear();
    if (FAILED(get_default_id(enumerator.Get(), eCapture, default_capture))) default_capture.clear();
    result = append_flow(enumerator.Get(), eRender, default_render, endpoints);
    if (FAILED(result)) {
        endpoints.clear();
        return result;
    }
    result = append_flow(enumerator.Get(), eCapture, default_capture, endpoints);
    if (FAILED(result)) {
        endpoints.clear();
        return result;
    }
    std::sort(endpoints.begin(), endpoints.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.flow != rhs.flow) return lhs.flow < rhs.flow;
        if (lhs.name != rhs.name) return lhs.name < rhs.name;
        return lhs.id < rhs.id;
    });
    return S_OK;
}

const wchar_t* flow_name(EndpointFlow flow) {
    return flow == EndpointFlow::render ? L"render" : L"capture";
}

const wchar_t* state_name(DWORD state) {
    switch (state) {
    case DEVICE_STATE_ACTIVE: return L"active";
    case DEVICE_STATE_DISABLED: return L"disabled";
    case DEVICE_STATE_NOTPRESENT: return L"not-present";
    case DEVICE_STATE_UNPLUGGED: return L"unplugged";
    default: return L"unknown";
    }
}

const wchar_t* sample_format_name(WORD tag) {
    switch (tag) {
    case WAVE_FORMAT_PCM: return L"PCM";
    case WAVE_FORMAT_IEEE_FLOAT: return L"IEEE-float";
    case WAVE_FORMAT_EXTENSIBLE: return L"extensible";
    default: return L"other";
    }
}

} // namespace sar::devices

#endif
