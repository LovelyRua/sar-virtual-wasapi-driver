#pragma once

#ifdef _WIN32

#include <windows.h>

#include <string>
#include <vector>

namespace sar::devices {

enum class EndpointFlow { render, capture };

struct WasapiEndpoint {
    std::wstring id;
    std::wstring name;
    EndpointFlow flow = EndpointFlow::render;
    DWORD state = 0;
    bool is_default = false;
    bool mix_format_available = false;
    WORD channels = 0;
    DWORD sample_rate = 0;
    WORD sample_format = 0;
    WORD container_bits = 0;
    DWORD channel_mask = 0;
    HRESULT format_error = S_OK;
};

HRESULT enumerate_wasapi_endpoints(std::vector<WasapiEndpoint>& endpoints);
const wchar_t* flow_name(EndpointFlow flow);
const wchar_t* state_name(DWORD state);
const wchar_t* sample_format_name(WORD format_tag);

} // namespace sar::devices

#endif
