# SAR Virtual WASAPI Driver

This is a separate MS-PL-licensed driver project for System Audio Route (SAR).
It is not part of the GPLv3 SAR engine repository. The only intended product
boundary is the public Windows audio device interface: SAR will discover and
open the driver endpoints using WASAPI. Do not copy GPL-only SAR code, ASIO SDK
headers, or private SAR transport structures into this repository.

## Current state

`audio/sysvad/` began as an upstream sample snapshot and now contains a narrow
experimental stereo bridge. It is not a product driver. Do not install or
distribute it as one. The source was copied from
Microsoft's [Windows driver samples](https://github.com/microsoft/Windows-driver-samples/tree/2dc3fd3a0cc84a2933f2194e7ec0871584979071/audio/sysvad)
at commit `2dc3fd3a0cc84a2933f2194e7ec0871584979071`. The original MS-PL
license and source notices are retained. See `NOTICE.md`.

`src/pcm_frame_ring.h` is an allocation-free, frame-aligned transport
primitive with standalone tests. The experimental bridge uses it in the
kernel stream callbacks. Passing its unit tests or the driver build does not
prove endpoint audio transfer.

The bridge currently connects the sample Speaker render stream to MicArray1
RAW capture when both negotiate 48 kHz, 16-bit stereo PCM. It uses a fixed
4096-frame ring in nonpaged adapter storage and emits silence on an empty
read. It does not resample, mix simultaneous clients, synchronize independent
clocks, expose SAR-branded endpoints, or provide production diagnostics. The
paired endpoints still require real Windows playback/capture testing.
The sample Speaker default is 48 kHz for this experiment, and both bridge
endpoint pins are limited to one kernel stream because there is no mixer.

The initial target is one paired stereo render/capture bus. The Windows app
renders to the virtual render endpoint; SAR reads that stream with WASAPI
loopback. For the reverse direction, SAR renders to a second virtual render
endpoint and the driver supplies its paired capture endpoint to applications.
This second path requires actual driver-side transfer code; the upstream
sample's generated capture tone is not sufficient. Multiple independent buses
and channel counts come after one bus passes bit-exact and long-run tests.

## Build boundary

Build and driver installation belong only on the designated Windows driver
lab or in dedicated CI. The development host is edit-only. The sample needs
Visual Studio, Windows SDK, WDK, and its WIL dependency; see
`audio/sysvad/README.md`. Do not enable test signing, disable Secure Boot, or
install an unsigned driver on a production machine. Test certificates and
build outputs must not be committed.

The `SysVAD build` workflow restores the upstream pinned WDK NuGet packages
and compiles `TabletAudioSample` plus the keyword detector adapter required
by its INF. The optional APO projects are deliberately excluded at this
stage. CI does not publish or install the generated driver binary.

A manually dispatched CI run retains a three-day build-evidence artifact with
the driver binary, INF-required adapter DLL, generated INF/catalog files if
present, and SHA-256 hashes.
It is not a release or an installable package: CI's temporary signing identity
is not provisioned on the driver lab. Build and sign locally inside the
dedicated lab before attempting endpoint installation.

After installing the experimental driver in the dedicated lab, build the
standalone user-mode probe with CMake and run it in an interactive audio
session. A manual `Transport` workflow run also retains the x64 probe
executable for three days, without changing driver signing or installation:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --target wasapi_bridge_probe
build\Release\wasapi_bridge_probe.exe --list
build\Release\wasapi_bridge_probe.exe --run "<Speaker render ID>" "<MicArray1 capture ID>"
```

The probe requests 48 kHz, 16-bit stereo and RAW capture. It sends different
tones on left and right for three seconds, records four seconds, then reports
frame counts, silence flags, desired-tone power, and cross-channel power.
Exit code 0 requires at least two seconds of capture, nontrivial tone energy,
and 20 dB of cross-channel separation. This is a functional smoke test, not a
latency, xrun, clock-drift, or long-run stability certification. Save the full
output and HRESULT on failure; do not substitute another endpoint silently.

## Exit criteria for the first SAR endpoint

- Device Manager and WASAPI enumerate the SAR-named render/capture endpoints.
- User-mode playback into the virtual render side arrives in SAR without
  synthetic tone or file-backed simulation.
- SAR output arrives at an ordinary WASAPI capture client with verified
  channel order, format, timestamps, and bounded latency.
- Uninstall leaves no driver, endpoint, service, or test certificate behind.
- Tests cover silence, clipping, sustained playback, simultaneous clients,
  restart, and device disable/enable on the driver lab.

No milestone above is claimed complete by this initial import.
