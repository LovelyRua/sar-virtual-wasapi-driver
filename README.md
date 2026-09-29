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
and compiles only the core `TabletAudioSample` project. The optional APO
projects are deliberately excluded at this stage. CI does not publish or
install the generated driver binary.

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
