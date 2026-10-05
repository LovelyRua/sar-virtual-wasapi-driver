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
The TabletAudioSample adapter currently activates only the Speaker render and
MicArray1 capture miniport pair. Other upstream SysVAD miniport definitions and
INF templates remain in the sample tree but are not activated by this adapter.
After installation on the lab VM, run the inventory check in its logged-on
desktop session:

```powershell
scripts/lab-endpoint-inventory.ps1 -ProbePath <wasapi_bridge_probe.exe>
```

It fails if the sample exposes anything other than this one render/capture pair;
the optional `-ReportPath` writes the same JSON evidence to disk. Its parser
has fixture tests in the Transport workflow. A passing build alone does not
prove the installed endpoint inventory.

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
It also runs Inf2Cat for Windows 11 25H2 x64 over a self-contained
`core-package/` directory containing the core INF, SYS, adapter DLL, and
unsigned `sysvad.cat`. Catalog generation checks package signability but does
not sign the catalog or make the package installable under normal boot policy.
It is not a release or an installable package: CI's temporary signing identity
is not provisioned on the driver lab. Build and sign locally inside the
dedicated lab before attempting endpoint installation.

On the lab, run `scripts/lab-preflight.ps1 -PackagePath <core-package>` to
record the package hashes, administrator token, Secure Boot, test-signing, and
catalog signature state. A restricted PowerShell execution policy can be left
unchanged by invoking the script over WinRM with `Invoke-Command -FilePath`.
The preflight does not install a driver or alter boot policy.

After installing the experimental driver in the dedicated lab, build the
standalone user-mode probe with CMake and run it in an interactive audio
session. A manual `Transport` workflow run also retains the x64 probe
executable for three days, without changing driver signing or installation:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --target wasapi_bridge_probe
build\Release\wasapi_bridge_probe.exe --list
build\Release\wasapi_bridge_probe.exe --run "<Speaker render ID>" "<MicArray1 capture ID>"
build\Release\wasapi_bridge_probe.exe --default "<Speaker render ID>" "<MicArray1 capture ID>"
build\Release\wasapi_bridge_probe.exe --route "<Speaker render ID>" "<downstream capture ID>"
build\Release\wasapi_bridge_probe.exe --exclusive "<Speaker render ID>" "<MicArray1 capture ID>"
```

The probe requests 48 kHz, 16-bit stereo. `--run` and `--exclusive` request
RAW capture; `--default` and `--route` use ordinary shared-mode capture without RAW.
`--route` accepts a 48 kHz mono or stereo float32 downstream capture endpoint
and can test an external routing path, such as Speaker -> driver MicArray1 ->
SAR matrix -> VB-Cable Input -> VB-Cable Output. Start SAR's route before this
probe; do not open MicArray1 in the probe at the same time. It sends different
tones on left and right for three seconds, records four seconds, then reports
frame counts, silence flags, desired-tone power, and cross-channel power.
Exit code 0 requires at least two seconds of capture for the driver-pair modes
or one second for the downstream `--route` mode, plus nontrivial tone energy.
RAW modes also require 20 dB of cross-channel separation; `--route` requires
signal on both downstream channels and rejects the sample's fixed 2 kHz tone.
This is a functional smoke test, not a
latency, xrun, clock-drift, or long-run stability certification. Save the full
output and HRESULT on failure; do not substitute another endpoint silently.

## VM24 lab result (2026-09-30)

On the dedicated Windows 11 driver lab, a locally test-signed core package
installed successfully as `ROOT\MEDIA\0001` with PnP status `OK`. WASAPI
enumerated the sample Speaker render and MicArray1 Front capture endpoints.
This establishes installation and enumeration only, not audio transfer.

The probe ran under a non-interactive WinRM session with no logged-on desktop
user. Shared-mode capture returned 191040 silent frames after 144096 rendered
frames. Exclusive-mode capture returned 118080 frames without the silent flag,
but every sample was zero. The unbridged MicArray2 Rear control capture was
also silent in shared mode. These results do not isolate a kernel bridge defect
from session, endpoint, or format behavior. Repeat in an interactive audio
session and inspect negotiated kernel format and processing mode before
claiming the bridge works or changing its realtime path.

After the test, the sample device and its OEM INF were removed. The temporary
lab certificate was removed from LocalMachine My, Root, and TrustedPublisher;
test signing was turned off and Secure Boot was restored and verified after
reboot. The pre-test VM snapshot remains available for the next lab iteration.

As a Session 0 control, the same probe rendered 144096 frames into the VM's
pre-existing VB-Cable pair and captured 180672 frames, all flagged silent.
Both endpoints reported 48 kHz, 32-bit stereo mix formats and neither was
muted. This strengthens the need for an interactive-session control before
attributing silence to the sample driver.

When a desktop user is logged on, `scripts/lab-interactive-probe.ps1` can be
invoked on the lab through WinRM with `Invoke-Command -FilePath` and the probe
path, endpoint IDs, and output path. It runs the probe under that user's
interactive token, writes a report on the VM, and removes its temporary
scheduled task. It does not accept or store a password. First run it against
the known VB-Cable pair; only then repeat against the experimental driver.

## REAPER shared-mode finding (2026-10-01)

An interactive-session RAW probe against the experimental Speaker and MicArray1
Front endpoints passed: 142464 rendered frames, 188544 captured frames, no
silent flags, and `PROBE_EXIT=0`. REAPER 7.41 also opened the same endpoints in
shared mode at 48 kHz (one input, two outputs), but its 28.28-second recording
was a constant -6.0 dBFS peak / -9.0 dBFS RMS signal, including after the
10-second click source ended. This was the upstream SysVAD 2 kHz tone, not a
successful user-mode loopback. The test recording remains on VM24 at
`C:\Users\codex\Documents\REAPER Media\02-260930_1847.wav`.

The bridge's capture predicate had required RAW processing mode, so ordinary
shared-mode capture fell through to the sample tone generator. Removing the
mode restriction alone did not fix it: the processed MicArray1 kernel stream
uses 48 kHz, 16-bit mono PCM, whereas RAW uses 16-bit stereo PCM.

## Processed capture retest (2026-10-03)

The bridge now downmixes stereo ring frames to 16-bit mono on the processed
MicArray1 path without allocation or floating-point work in the kernel stream.
On VM24, the interactive `--default` probe passed with 144192 rendered frames,
192000 captured frames, target power 2.54e16, fixed 2 kHz tone power 1.92e9,
and `PROBE_EXIT=0`. The RAW stereo control probe also passed with 144576
rendered frames, 191520 captured frames, and `PROBE_EXIT=0`.

REAPER 7.41 then recorded the experimental Speaker-to-MicArray1 path in shared
mode at 48 kHz (one input, two outputs, 512 samples). Full-sample analysis of
the 34.7-second mono 24-bit WAV found click peaks of -12/-6 dBFS during the
first ten seconds, followed by about -90.3 dBFS peak / -106.2 dBFS RMS after
the click source ended. This confirms that the capture followed the playback
source rather than the sample driver's continuous 2 kHz tone. The recording is
`C:\Users\codex\Documents\REAPER Media\02-261003_0644.wav` on VM24.

These are functional lab results for the Microsoft SysVAD-derived experimental
device, not a production driver reliability or latency claim. After testing,
the device, INF and temporary signing certificate were removed. VM24 was
rebooted and verified with Secure Boot on, test signing off, and no remaining
experimental device or lab certificate.

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
