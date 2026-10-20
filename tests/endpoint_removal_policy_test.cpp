#include "src/endpoint_removal_policy.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace sar::devices;
    WasapiEndpoint endpoint;
    endpoint.state = DEVICE_STATE_NOTPRESENT;
    endpoint.parent_lookup_error = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    require(!endpoint_can_block_removal(endpoint), "Historical endpoint blocks teardown");
    require(!endpoint_removal_ownership_unknown(endpoint), "Ghost needs a live parent");
    endpoint.parent_device_id = L"ROOT\\MEDIA\\0003";
    require(!endpoint_can_block_removal(endpoint), "Known historical endpoint blocks teardown");

    for (const DWORD state : {DWORD{0}, DWORD{DEVICE_STATE_ACTIVE},
                             DWORD{DEVICE_STATE_DISABLED}, DWORD{DEVICE_STATE_UNPLUGGED},
                             DWORD{DEVICE_STATE_ACTIVE | DEVICE_STATE_NOTPRESENT}}) {
        endpoint.state = state;
        endpoint.parent_device_id.clear();
        endpoint.parent_lookup_error = S_OK;
        require(endpoint_can_block_removal(endpoint), "Live/unknown state was silently ignored");
        require(endpoint_removal_ownership_unknown(endpoint), "Missing live owner was ignored");
        endpoint.parent_device_id = L"ROOT\\MEDIA\\0003";
        require(!endpoint_removal_ownership_unknown(endpoint), "Known owner rejected");
        endpoint.parent_lookup_error = E_FAIL;
        require(endpoint_removal_ownership_unknown(endpoint), "Failed live lookup was ignored");
    }
    std::cout << "Endpoint removal state policy passed\n";
}
