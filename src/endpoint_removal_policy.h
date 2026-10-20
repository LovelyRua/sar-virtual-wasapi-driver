#pragma once

#include "wasapi_inventory.h"

#ifdef _WIN32
namespace sar::devices {

// MMDevice retains NOTPRESENT endpoints after their PnP devnodes disappear.
// They cannot block removal of a different live adapter. Unknown or mixed
// states stay conservative; disabled and unplugged endpoints still count.
inline bool endpoint_can_block_removal(const WasapiEndpoint& endpoint) noexcept {
    return endpoint.state != DEVICE_STATE_NOTPRESENT;
}

inline bool endpoint_removal_ownership_unknown(const WasapiEndpoint& endpoint) noexcept {
    return endpoint_can_block_removal(endpoint) &&
           (endpoint.parent_device_id.empty() || FAILED(endpoint.parent_lookup_error));
}

} // namespace sar::devices
#endif
