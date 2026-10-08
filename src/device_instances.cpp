#ifdef _WIN32

#include "device_instances.h"

#include <cfgmgr32.h>
#include <devguid.h>
#include <setupapi.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace sar::devices {
namespace {

using DeviceSet = std::unique_ptr<std::remove_pointer_t<HDEVINFO>,
                                  decltype(&SetupDiDestroyDeviceInfoList)>;

DeviceSet open_media_set(DWORD flags) {
    return DeviceSet(SetupDiGetClassDevsW(&GUID_DEVCLASS_MEDIA, nullptr,
                                          nullptr, flags),
                     &SetupDiDestroyDeviceInfoList);
}

bool valid_set(const DeviceSet& devices) {
    return devices.get() != INVALID_HANDLE_VALUE;
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

bool same_id(const std::wstring& lhs, const std::wstring& rhs) {
    return lower(lhs) == lower(rhs);
}

bool acceptable_label(const std::wstring& label) {
    if (label.empty() || label.size() > 80) return false;
    if (std::iswspace(label.front()) || std::iswspace(label.back())) return false;
    for (const wchar_t c : label) {
        if (c < 0x20 || c == 0x7f || c == L'\\' || c == L'/') return false;
    }
    return true;
}

bool acceptable_instance_id(const std::wstring& id) {
    // Root-enumerated Media devices use ROOT\MEDIA\NNNN. This guard is
    // deliberately narrower than the set of IDs SetupAPI could open.
    constexpr wchar_t prefix[] = L"ROOT\\MEDIA\\";
    if (id.size() != 15 || !same_id(id.substr(0, 11), prefix)) return false;
    return std::all_of(id.begin() + 11, id.end(),
                       [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

Result property(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD key,
                std::vector<wchar_t>& text, DWORD* type = nullptr) {
    DWORD bytes = 0;
    DWORD property_type = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, key, &property_type,
                                       nullptr, 0, &bytes);
    const DWORD status = GetLastError();
    if (status != ERROR_INSUFFICIENT_BUFFER || bytes == 0 || bytes > 65536) {
        return Result::failure(status, L"Read device property");
    }
    text.assign((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t) + 2, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, key, &property_type,
                                            reinterpret_cast<PBYTE>(text.data()),
                                            static_cast<DWORD>(text.size() * sizeof(wchar_t)),
                                            nullptr)) {
        return Result::failure(GetLastError(), L"Read device property");
    }
    if (type) *type = property_type;
    return Result::success();
}

Result instance_id(HDEVINFO set, SP_DEVINFO_DATA& data, std::wstring& id) {
    DWORD needed = 0;
    SetupDiGetDeviceInstanceIdW(set, &data, nullptr, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed < 2 || needed > 4096) {
        return Result::failure(GetLastError(), L"Read instance ID length");
    }
    std::vector<wchar_t> buffer(needed, L'\0');
    if (!SetupDiGetDeviceInstanceIdW(set, &data, buffer.data(), needed, nullptr)) {
        return Result::failure(GetLastError(), L"Read instance ID");
    }
    id.assign(buffer.data());
    return Result::success();
}

bool ours(HDEVINFO set, SP_DEVINFO_DATA& data) {
    std::vector<wchar_t> ids;
    DWORD type = 0;
    if (!property(set, data, SPDRP_HARDWAREID, ids, &type).ok ||
        type != REG_MULTI_SZ) return false;
    size_t offset = 0;
    while (offset < ids.size() && ids[offset]) {
        const size_t length = std::wcslen(ids.data() + offset);
        if (same_id(std::wstring(ids.data() + offset, length), kHardwareId)) {
            return true;
        }
        offset += length + 1;
    }
    return false;
}

std::wstring friendly_name(HDEVINFO set, SP_DEVINFO_DATA& data) {
    std::vector<wchar_t> text;
    DWORD type = 0;
    if (property(set, data, SPDRP_FRIENDLYNAME, text, &type).ok &&
        type == REG_SZ && text[0]) return std::wstring(text.data());
    if (property(set, data, SPDRP_DEVICEDESC, text, &type).ok &&
        type == REG_SZ && text[0]) return std::wstring(text.data());
    return L"System Audio Route experimental device";
}

Result describe(HDEVINFO set, SP_DEVINFO_DATA& data, Instance& instance) {
    Result result = instance_id(set, data, instance.id);
    if (!result.ok) return result;
    instance.label = friendly_name(set, data);
    ULONG status = 0;
    ULONG problem = 0;
    const CONFIGRET cr = CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0);
    if (cr != CR_SUCCESS) return Result::failure(ERROR_GEN_FAILURE, L"Read device state");
    instance.started = (status & DN_STARTED) != 0;
    instance.problem = (status & DN_HAS_PROBLEM) != 0;
    instance.problem_code = problem;
    return Result::success();
}

Result find_exact(const std::wstring& id, DeviceSet& set, SP_DEVINFO_DATA& data) {
    if (!acceptable_instance_id(id)) {
        return Result::failure(ERROR_INVALID_PARAMETER, L"Invalid root Media instance ID");
    }
    set = open_media_set(DIGCF_PRESENT);
    if (!valid_set(set)) return Result::failure(GetLastError(), L"Enumerate Media devices");
    data.cbSize = sizeof(data);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set.get(), index, &data); ++index) {
        std::wstring found;
        if (!instance_id(set.get(), data, found).ok || !same_id(found, id)) continue;
        if (!ours(set.get(), data)) {
            return Result::failure(ERROR_ACCESS_DENIED, L"Instance is not the SAR experiment driver");
        }
        return Result::success();
    }
    return Result::failure(ERROR_NOT_FOUND, L"Find SAR device instance");
}

Result set_label(HDEVINFO set, SP_DEVINFO_DATA& data, const std::wstring& label) {
    if (!acceptable_label(label)) {
        return Result::failure(ERROR_INVALID_PARAMETER, L"Invalid device label");
    }
    const DWORD bytes = static_cast<DWORD>((label.size() + 1) * sizeof(wchar_t));
    if (!SetupDiSetDeviceRegistryPropertyW(set, &data, SPDRP_FRIENDLYNAME,
                                            reinterpret_cast<const BYTE*>(label.c_str()), bytes)) {
        return Result::failure(GetLastError(), L"Set device label");
    }
    return Result::success();
}

Result remove_registered(HDEVINFO set, SP_DEVINFO_DATA& data) {
    SP_REMOVEDEVICE_PARAMS params = {};
    params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    params.ClassInstallHeader.InstallFunction = DIF_REMOVE;
    params.Scope = DI_REMOVEDEVICE_GLOBAL;
    if (!SetupDiSetClassInstallParamsW(set, &data,
                                      &params.ClassInstallHeader, sizeof(params))) {
        return Result::failure(GetLastError(), L"Prepare device removal");
    }
    if (!SetupDiCallClassInstaller(DIF_REMOVE, set, &data)) {
        return Result::failure(GetLastError(), L"Remove device instance");
    }
    return Result::success();
}

Result validate_inf(const std::wstring& path) {
    if (path.empty() || path.size() >= MAX_PATH) {
        return Result::failure(ERROR_INVALID_PARAMETER, L"Invalid INF path");
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return Result::failure(ERROR_FILE_NOT_FOUND, L"Driver INF does not exist");
    }
    const auto suffix = lower(path.substr(path.size() >= 4 ? path.size() - 4 : 0));
    if (suffix != L".inf") {
        return Result::failure(ERROR_INVALID_PARAMETER, L"Driver path must be an INF");
    }
    return Result::success();
}

Result select_driver(HDEVINFO set, SP_DEVINFO_DATA& data,
                     const std::wstring& published_inf) {
    SP_DEVINSTALL_PARAMS_W params = {};
    params.cbSize = sizeof(params);
    if (!SetupDiGetDeviceInstallParamsW(set, &data, &params)) {
        return Result::failure(GetLastError(), L"Read driver selection parameters");
    }
    params.Flags |= DI_ENUMSINGLEINF;
    params.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
    if (published_inf.size() >= MAX_PATH) {
        return Result::failure(ERROR_FILENAME_EXCED_RANGE, L"Published INF path too long");
    }
    std::wmemcpy(params.DriverPath, published_inf.c_str(), published_inf.size() + 1);
    if (!SetupDiSetDeviceInstallParamsW(set, &data, &params)) {
        return Result::failure(GetLastError(), L"Select SAR INF path");
    }
    if (!SetupDiBuildDriverInfoList(set, &data, SPDIT_COMPATDRIVER)) {
        return Result::failure(GetLastError(), L"Find compatible SAR driver");
    }
    SP_DRVINFO_DATA_W driver = {};
    driver.cbSize = sizeof(driver);
    if (!SetupDiEnumDriverInfoW(set, &data, SPDIT_COMPATDRIVER, 0, &driver)) {
        return Result::failure(GetLastError(), L"Select compatible SAR driver");
    }
    if (!SetupDiSetSelectedDriverW(set, &data, &driver)) {
        return Result::failure(GetLastError(), L"Bind compatible SAR driver");
    }
    return Result::success();
}

} // namespace

Result Result::success() { return {true, ERROR_SUCCESS, L""}; }

Result Result::failure(DWORD error, const wchar_t* operation) {
    return {false, error, operation};
}

Result list(std::vector<Instance>& instances) {
    instances.clear();
    DeviceSet set = open_media_set(DIGCF_PRESENT);
    if (!valid_set(set)) return Result::failure(GetLastError(), L"Enumerate Media devices");
    SP_DEVINFO_DATA data = {};
    data.cbSize = sizeof(data);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set.get(), index, &data); ++index) {
        if (!ours(set.get(), data)) continue;
        Instance instance;
        Result result = describe(set.get(), data, instance);
        if (!result.ok) return result;
        if (acceptable_instance_id(instance.id)) instances.push_back(std::move(instance));
    }
    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_ITEMS) return Result::failure(error, L"Enumerate Media devices");
    std::sort(instances.begin(), instances.end(),
              [](const Instance& a, const Instance& b) { return lower(a.id) < lower(b.id); });
    return Result::success();
}

Result add(const std::wstring& inf_path, const std::wstring& label, Instance& created) {
    created = {};
    if (!acceptable_label(label)) return Result::failure(ERROR_INVALID_PARAMETER, L"Invalid device label");
    Result result = validate_inf(inf_path);
    if (!result.ok) return result;
    std::vector<Instance> current;
    result = list(current);
    if (!result.ok) return result;
    if (current.size() >= kMaximumInstances) {
        return Result::failure(ERROR_TOO_MANY_NAMES,
                               L"Driver currently supports one SAR instance; remove it first");
    }
    for (const Instance& item : current) {
        if (same_id(item.label, label)) {
            return Result::failure(ERROR_ALREADY_EXISTS, L"Device label already exists");
        }
    }

    // Stage only: DiInstallDriver would also rebind already-present instances.
    // New and existing SAR devices may be in active audio sessions.
    wchar_t published_inf[MAX_PATH] = {};
    if (!SetupCopyOEMInfW(inf_path.c_str(), nullptr, SPOST_PATH, 0,
                           published_inf, MAX_PATH, nullptr, nullptr)) {
        return Result::failure(GetLastError(), L"Stage signed SAR driver package");
    }
    DeviceSet set(SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_MEDIA, nullptr),
                  &SetupDiDestroyDeviceInfoList);
    if (!valid_set(set)) return Result::failure(GetLastError(), L"Create Media device set");
    SP_DEVINFO_DATA data = {};
    data.cbSize = sizeof(data);
    if (!SetupDiCreateDeviceInfoW(set.get(), L"MEDIA", &GUID_DEVCLASS_MEDIA,
                                   nullptr, nullptr, DICD_GENERATE_ID, &data)) {
        return Result::failure(GetLastError(), L"Create root Media device");
    }
    const DWORD hardware_bytes = sizeof(kHardwareId) + sizeof(wchar_t);
    wchar_t hardware_id[sizeof(kHardwareId) / sizeof(wchar_t) + 1] = {};
    std::wmemcpy(hardware_id, kHardwareId, sizeof(kHardwareId) / sizeof(wchar_t));
    if (!SetupDiSetDeviceRegistryPropertyW(set.get(), &data, SPDRP_HARDWAREID,
                                            reinterpret_cast<const BYTE*>(hardware_id),
                                            hardware_bytes)) {
        return Result::failure(GetLastError(), L"Set SAR hardware ID");
    }
    result = set_label(set.get(), data, label);
    if (!result.ok) return result;
    result = select_driver(set.get(), data, published_inf);
    if (!result.ok) return result;
    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set.get(), &data)) {
        return Result::failure(GetLastError(), L"Register root Media device");
    }
    // A registered devnode is persistent; all later failures must remove it.
    if (!SetupDiCallClassInstaller(DIF_INSTALLDEVICE, set.get(), &data)) {
        const DWORD error = GetLastError();
        remove_registered(set.get(), data);
        return Result::failure(error, L"Install SAR device instance");
    }
    result = describe(set.get(), data, created);
    if (!result.ok || !acceptable_instance_id(created.id)) {
        remove_registered(set.get(), data);
        created = {};
        return result.ok ? Result::failure(ERROR_INVALID_DATA, L"Unexpected instance ID") : result;
    }
    // Windows may finish PnP start asynchronously. Return the actual state;
    // callers can refresh without interpreting a successful add as audio-ready.
    return Result::success();
}

Result rename(const std::wstring& id, const std::wstring& label) {
    if (!acceptable_label(label)) return Result::failure(ERROR_INVALID_PARAMETER, L"Invalid device label");
    std::vector<Instance> current;
    Result result = list(current);
    if (!result.ok) return result;
    for (const Instance& item : current) {
        if (!same_id(item.id, id) && same_id(item.label, label)) {
            return Result::failure(ERROR_ALREADY_EXISTS, L"Device label already exists");
        }
    }
    DeviceSet set(INVALID_HANDLE_VALUE, &SetupDiDestroyDeviceInfoList);
    SP_DEVINFO_DATA data = {};
    result = find_exact(id, set, data);
    if (!result.ok) return result;
    return set_label(set.get(), data, label);
}

Result remove(const std::wstring& id) {
    DeviceSet set(INVALID_HANDLE_VALUE, &SetupDiDestroyDeviceInfoList);
    SP_DEVINFO_DATA data = {};
    Result result = find_exact(id, set, data);
    if (!result.ok) return result;
    return remove_registered(set.get(), data);
}

} // namespace sar::devices

#endif
