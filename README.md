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

The bridge currently connects Speaker render to MicArray1 capture and
SpeakerHeadphone render to MicArray2 capture as two isolated experimental
buses. Each uses a fixed 4096-frame ring in nonpaged adapter storage and emits
silence on an empty read. The bridge accepts 48 kHz, 16-bit stereo PCM render
and 48 kHz, 16-bit mono or stereo PCM capture. It does not resample, mix
simultaneous clients, synchronize independent clocks, or provide production
diagnostics. Both buses and cross-bus isolation passed short Windows lab
playback/capture probes; see the dated result below.
The sample Speaker default is 48 kHz for this experiment, and both bridge
endpoint pins are limited to one kernel stream because there is no mixer.
If MicArray1 negotiates a format outside the bridge's supported PCM layouts,
its capture buffer is silent rather than falling back to SysVAD's synthetic
tone. Silence is a failure to route, not evidence of successful audio transfer.
The TabletAudioSample adapter currently activates only the Speaker/MicArray1
and SpeakerHeadphone/MicArray2 miniport pairs. Other upstream SysVAD miniport
definitions and INF templates remain in the sample tree but are not activated
by this adapter. The installed PnP device uses the SAR-specific
`Root\SystemAudioRoute\VirtualAudio` hardware ID and is labeled `System Audio
Route Experimental`; its capture endpoints are `SAR Experimental Capture 1`
and `SAR Experimental Capture 2`. The unique ID prevents the manager from
claiming or removing a separate upstream SysVAD sample device. These labels
distinguish the lab buses from physical microphones and from a release-ready SAR
driver.
The unique hardware ID is a source/package change and still requires a fresh
VM24 install test; earlier lab records used the upstream sample ID. The manager
will not claim or remove instances registered under that old ID.
After installation on the lab VM, run the inventory check from the desktop or
WinRM. Endpoint enumeration is valid in Session 0; audio transfer probes still
require a logged-on desktop session:

```powershell
scripts/lab-endpoint-inventory.ps1 -ProbePath <wasapi_bridge_probe.exe>
```

It fails if the sample exposes anything other than these two render/capture pairs;
the optional `-ReportPath` writes the same JSON evidence to disk. Its parser
has fixture tests in the Transport workflow. A passing build alone does not
prove the installed endpoint inventory.

The first bus accepts ordinary Windows playback; SAR reads its paired capture
endpoint. For the reverse direction, SAR renders to the second bus and ordinary
applications read its paired capture endpoint. Both buses passed independent
signal and cross-bus silence tests on VM24. Dynamic bus counts and channel
layouts remain future work.

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

The endpoint manager's `add` command waits up to 30 seconds for that exact
instance's two render and two capture endpoints to appear in WASAPI, become
active, and expose a mix format. The readiness sample must pass three
consecutive checks. Exit code 0 means the Windows audio topology is ready;
exit code 3 means the PnP device remains installed but did not become
audio-ready before timeout. It is deliberately not removed on timeout:

```bat
sar_endpoint_manager.exe diagnose ROOT\MEDIA\0001
sar_endpoint_manager.exe diagnose ROOT\MEDIA\0001 --json
sar_endpoint_manager.exe wait-ready ROOT\MEDIA\0001 30000 --json
```

Endpoint inventory includes its PnP parent instance ID. When two driver
instances have identical INF-provided endpoint display names, readiness is
attributed by the Windows `SWD\MMDEVAPI` parent relationship, never by a
friendly-name guess or a machine-wide endpoint count. Reports show expected,
present, active, and mix-format endpoint counts, stable-sample count, PnP
status, endpoint IDs, and state-specific diagnostic guidance. `diagnose` is a
point-in-time snapshot; `wait-ready` polls for up to 120 seconds and returns
success only after stable readiness.

After installing the experimental driver in the dedicated lab, build the
standalone user-mode probes with CMake and run them in an interactive audio
session. They use WASAPI event-driven buffering, so service cadence follows
the endpoint buffer-ready notifications instead of a fixed 2 ms polling loop.
This improves continuity measurement but does not provide hard realtime
scheduling or replace a long-duration soak test. A manual `Transport` workflow run also retains the x64 probe
executable for three days, without changing driver signing or installation:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --target wasapi_bridge_probe
cmake --build build --config Release --target wasapi_dual_bus_probe
build\Release\wasapi_bridge_probe.exe --list
build\Release\wasapi_bridge_probe.exe --run "<Speaker render ID>" "<MicArray1 capture ID>"
build\Release\wasapi_bridge_probe.exe --default "<Speaker render ID>" "<MicArray1 capture ID>"
build\Release\wasapi_bridge_probe.exe --route "<Speaker render ID>" "<downstream capture ID>"
build\Release\wasapi_bridge_probe.exe --exclusive "<Speaker render ID>" "<MicArray1 capture ID>"
build\Release\wasapi_dual_bus_probe.exe "<Bus 0 render ID>" "<Bus 0 capture ID>" "<Bus 1 render ID>" "<Bus 1 capture ID>" 15
```

The multi-bus probe can also run inside the already logged-on desktop session
through the interactive launcher. It reports packet cadence, discontinuities,
silence, peak level, clipping, and non-finite sample counts for each bus:

```powershell
scripts/lab-interactive-probe.ps1 -ProbePath <wasapi_dual_bus_probe.exe> -Mode dual_bus -RenderId "<Bus 0 render ID>" -CaptureId "<Bus 0 capture ID>" -RenderId2 "<Bus 1 render ID>" -CaptureId2 "<Bus 1 capture ID>" -DurationSeconds 15 -OutputPath <report.txt>
scripts/lab-interactive-probe.ps1 -ProbePath <wasapi_dual_bus_probe.exe> -Mode multi_bus -RenderId "<Bus 0 render ID>" -CaptureId "<Bus 0 capture ID>" -AdditionalRenderIds @("<Bus 1 render ID>", "<Bus 2 render ID>", "<Bus 3 render ID>") -AdditionalCaptureIds @("<Bus 1 capture ID>", "<Bus 2 capture ID>", "<Bus 3 capture ID>") -DurationSeconds 15 -OutputPath <report.txt>
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

## Endpoint pair retest (2026-10-05)

CI artifact from commit `d8e3a7e` was installed on VM24 with a temporary lab
signature. Session 0 enumeration found exactly one sample Speaker render and
one MicArray1 Front capture endpoint; `lab-endpoint-inventory.ps1` passed with
`SampleEndpointCount=2`. In the logged-on desktop session, the shared-mode
Speaker-to-MicArray1 probe rendered 143808 frames and captured 191328 frames,
with zero silent frames, target power 2.87847e16, fixed-tone power 1628.29,
and `PROBE_EXIT=0`. This verifies the reduced endpoint inventory did not break
the previously working functional audio path; it is not a longevity test.

Afterward, the lab device and `oem10.inf` were removed, the temporary signing
certificate was removed from My, Root, and TrustedPublisher, test signing was
disabled, and Secure Boot was restored. Post-reboot checks found Secure Boot
on, no sample device or INF, no matching certificate, and all firewall profiles
enabled. The pre-test VM snapshot was retained.

## SAR matrix signal gate (2026-10-07)

VM24 installed the `6df4544` driver CI artifact with a temporary lab catalog
signature. The interactive SAR matrix preflight routed the sample Speaker
render through MicArray1 capture, SAR, and VB-Cable. With the matrix route
enabled, both captured channels had target power 1.99771e16 and SAR processed
2935 blocks. With the route omitted, both channel powers were exactly zero
while SAR processed 2690 blocks. Both probes exited successfully, and the
owned engine service stopped after each run. This is functional end-to-end
evidence for one endpoint pair, not latency or sustained-reliability evidence.

VM24 is now a dedicated driver lab with Secure Boot disabled and Windows test
signing enabled by explicit user authorization. Its firewall profiles remain
enabled. After this run, the sample device, OEM INF, and seven-day temporary
signing identity were removed; the boot test mode remains for faster follow-up
installations. This configuration is not suitable for release acceptance or
production.

The renamed lab package at `c68a9cc` passed the same Windows 11 VM24
installation and endpoint checks: PnP reported `System Audio Route Experimental
Bus 1` as healthy, and WASAPI enumerated exactly its Speaker render and
`SAR Experimental Capture 1` endpoints. An interactive shared-mode probe
rendered 143328 frames and captured 189888 non-silent frames with exit code 0.
The SAR matrix route to VB-Cable then processed 2685 blocks; both output
channels had target power 1.61923e16. The sample device, OEM INF, and temporary
signing identity were removed again; the authorized boot test mode remains.

## Exit criteria for the first SAR endpoint

- Device Manager and WASAPI enumerate the SAR-named render/capture endpoints.
- User-mode playback into the virtual render side arrives in SAR without
  synthetic tone or file-backed simulation.
- SAR output arrives at an ordinary WASAPI capture client with verified
  channel order, format, timestamps, and bounded latency.
- Uninstall leaves no driver, endpoint, service, or test certificate behind.
- Tests cover silence, clipping, sustained playback, simultaneous clients,
  restart, and device disable/enable on the driver lab.

The short functional signal path is verified on the lab VM. The remaining
reliability, timing, format, multi-client, and uninstall criteria are not met.

## Two-bus matrix lab result (2026-10-07)

The Windows CI artifact from `8278ef0` was test-signed and installed only on
VM24. The inventory check found exactly two render and two capture endpoints:
Speaker/Capture 1 and Headphones/Capture 2. Direct interactive WASAPI probes
passed on both paired buses. Both cross-bus probes measured zero target and
fixed-tone power, confirming that the kernel bridge did not leak audio across
the buses.

The SAR matrix preflight then used the PCM16-capable probe from `7d484c8` to
test the full route without VB-Cable. Speaker -> Capture 1 -> SAR -> Headphones
-> Capture 2 passed with 725 processed blocks and target power 2.44832e16.
Omitting the SAR route still processed 757 blocks but yielded zero target and
second-channel power. The reverse Headphones -> Capture 2 -> SAR -> Speaker ->
Capture 1 route passed with 1268 processed blocks and target power 3.01272e15.
Each preflight observed the engine for three seconds. The capture endpoints
negotiated mono in these probes, so duplicated analysis values do not verify
stereo channel independence. These results establish functional signal flow
and route control, not sustained glitch-free operation, latency, drift,
multi-client behavior, or release readiness.

After testing, VM24's experimental device and `oem10.inf` were removed. The
temporary signing certificate was removed from My, Root, and TrustedPublisher;
checks found no remaining sample device, INF, or matching certificate. All
firewall profiles remained enabled. The user-authorized lab boot mode remains
on (Secure Boot off, test signing on).

## Stereo default lab result (2026-10-08)

Commit `64c8370` changes the two experimental capture endpoints' default
48 kHz/16-bit format to stereo and advertises two channels in their 48 kHz
processed range. Lower-rate mono formats remain unchanged. The Windows driver
and probe CI jobs passed; the downloaded package's nine files matched its
SHA256 manifest before installation on VM24.

In the logged-on session, both direct paired-bus `--stereo` probes negotiated
two-channel capture. Each returned 0, with target power 1.15156e17 and
cross-channel power below 8e4. The two unpaired cross-bus probes returned 3
with zero target and cross-channel power. A two-channel SAR matrix route from
Capture 1 to Headphones passed with 717 processed blocks; the route-free
control processed 758 blocks and captured zero power on both analysis
channels. These were four-second signal probes followed by a three-second
engine observation, not a soak or latency measurement. The matrix probe
checked output-channel activity but did not independently measure left/right
crosstalk through SAR. The follow-up probe at `1944f6d` closed that gap: the
same two-channel SAR route passed with target power 1.10668e17,
cross-channel power 7.06931e9, and 552 processed blocks. The no-route control
processed 720 blocks and measured zero target, cross-channel, and second-channel
power. This is a short functional channel-order check, not a noise-floor or
long-duration crosstalk specification.

The experimental device, its OEM INF, and the non-exportable two-day lab
certificate were removed after testing. Verification found none remaining;
all VM24 firewall profiles stayed enabled, and its user-authorized test-signing
boot mode remains active.

## Concurrent two-bus continuity probe (2026-10-08)

`wasapi_dual_bus_probe` opens one to four render/capture pairs concurrently,
matching the current two instances times two buses topology. It uses a unique
stereo tone pair per bus, checks one-second windows for channel order and bleed
from every other bus, and records WASAPI capture discontinuity flags and packet
stalls. The legacy four-endpoint invocation still runs buses 0 and 1; `--single
0` through `--single 3` isolates one bus. The new `--multi` form accepts two to
four endpoint pairs, for example:

```bat
wasapi_dual_bus_probe.exe --multi 15 "<render 0>" "<capture 0>" "<render 1>" "<capture 1>" "<render 2>" "<capture 2>" "<render 3>" "<capture 3>"
```

The portable signal analyzer tests silence, swapped/duplicated channels,
all-pairs bus isolation, four-bus leakage, attenuation, and invalid input. A
separate options test covers legacy, single-bus, and multi-bus argument bounds.
Windows CI builds the probe and runs both test executables.

VM24's 15-second concurrent run passed: each bus produced 13 valid windows,
with zero failed windows, zero silent frames, and zero discontinuities. Longer
runs exposed a continuity limit: the 60-second concurrent run reported 7 and
5 WASAPI discontinuities on buses 0 and 1, despite 58 valid windows per bus,
zero failed windows, and zero silent frames. Single-bus 60-second controls
reported 22 and 8 discontinuities. An independent VB-Cable 60-second control
on the same VM and probe reported 10 discontinuities, also with 58 valid
windows and zero silent frames. The VB-Cable baseline means these flags cannot
currently be attributed uniquely to the SAR experimental driver; the strict
continuity gate still fails. The probe reports content and continuity outcomes
separately and returns failure when either fails. Further investigation needs
host scheduling/VM load and hardware baseline measurements, not a claim of a
glitch-free release driver.

The stress-run experimental device, INF, and temporary certificate were
removed from VM24 after testing. All firewall profiles remained enabled;
its user-authorized test-signing boot mode remains active.

## Instance readiness and continuity retest (2026-10-09)

The `e88e3b8` manager and driver package passed CI and were installed on VM24
with a short-lived test certificate. Each of two simultaneously installed
instances independently reached `ready`: two active render and two active
capture endpoints, all with 48 kHz stereo mix formats, parented to the correct
`ROOT\MEDIA\0001` or `ROOT\MEDIA\0002`, across three consecutive samples.
Removing instance 0001 left instance 0002 ready; removing 0002 returned the
present Media-device inventory to the pre-test VB-Cable-only baseline. The new
`oem12.inf` and temporary certificate were removed; pre-existing `oem10.inf`
and `oem11.inf` were retained. Secure Boot/test-signing and firewall settings
were unchanged, and the detailed probe logs remain on VM24 under
`C:\sar-lab\readiness-e88e3b8`.

The interactive two-instance 15-second WASAPI probe measured target tones on
all analyzed windows, zero silent frames, and very low wrong-channel/cross-bus
power. It **failed continuity**: bus 0 reported 134 discontinuities and
662112 captured versus 699456 sent frames; bus 1 reported 86 discontinuities
and 681120 captured versus 691104 sent frames. A serial single-instance SAR
run also failed with 120 discontinuities, 12 valid windows, and 669984 captured
versus 683520 sent frames. The same-session VB-Cable control failed the strict
continuity gate too, but less severely: 19 discontinuities, 13 valid windows,
and 712032 captured versus 712800 sent frames. This demonstrates correct
signal content and endpoint attribution, but exposes a substantial SAR-path
continuity regression beyond the control baseline. Do not describe the current
two-instance path as glitch-free or release-ready; investigate stream pacing,
queue behavior, and VM scheduling before increasing the supported instance
limit or claiming stable multi-device operation.

## Bridge diagnostics and VM24 retest (2026-10-09)

Commit `7453129` passed the Windows Transport tests and the full pinned WDK
build. The shared PCM ring now reports per-write queued depth and peak depth;
`IAdapterCommon::BridgeGetStats` exposes queued/peak frames, dropped and
silence-filled frames, invalid transfer bytes, and read/write calls under the
bridge lock. The diagnostic snapshot is currently kernel-internal and is not
yet surfaced by the user-mode manager or GUI. The WaveRT stream also clears a
partial mono sample tail rather than leaving stale DMA-buffer data.

On VM24, the exact CI package installed as `ROOT\MEDIA\0001`; its two render
and two capture endpoints were active, parented to that instance, and exposed
48 kHz stereo mix formats across three stable readiness samples. Both
single-bus shared-mode probes passed with non-silent target tones:

- Bus 0: 141312 rendered / 188256 captured frames; zero silent frames.
- Bus 1: 136224 rendered / 189408 captured frames; zero silent frames.

The 15-second simultaneous two-bus probe produced correct tone/channel and
cross-bus analysis in all 12 windows per bus, with zero failed windows and zero
silent frames, but **failed continuity**: bus 0 had 120 discontinuities
(676992 sent / 666432 captured frames), and bus 1 had 136 (708096 / 659712).
An identical 15-second VB-Cable single-bus control in the same interactive
session also failed continuity with 121 discontinuities (706272 / 667104
frames), zero silent frames, and zero failed analysis windows. This VM baseline
is therefore highly discontinuous under the current probe and the SAR result
does not isolate a driver-only cause. The stricter continuity acceptance gate
remains unmet; these results are not evidence of glitch-free operation.

The new `oem12.inf`, test instance, and temporary certificate were removed.
Original `oem10.inf` and `oem11.inf` remain installed. Test-signing remains
enabled, Secure Boot remains disabled, and all firewall profiles remain
enabled as previously authorized. Probe reports are retained at
`C:\sar-lab\bridge-diag-7453129\probe` on VM24.

## Event-driven probe retest (2026-10-09)

Commits `b0139eb` and `8816df2` replace fixed-interval probe servicing with a
shared WASAPI event pump, drain other signaled streams in the same pass, and
count capture packets, discontinuities, silent frames, clipping, and non-finite
samples. The Transport workflow and pinned WDK build both pass; the interactive
launcher argument tests pass locally.

The exact CI package was installed on VM24 as `ROOT\MEDIA\0001`, and both
simultaneous 48 kHz stereo buses produced the expected signals. Across two
15-second runs, every one-second analysis window passed, with zero silent
frames/packets, zero clipping, and zero non-finite samples. Continuity did not
pass: run one measured 3 discontinuities on each bus (720384/719904 sent and
719904/718848 captured frames); run two measured 13 and 9 discontinuities
(717216/716832 sent and 715680/716352 captured frames). The repeat variance is
material, so the event-driven change is not evidence of a stable or glitch-free
driver. It improves measurement and signal-content validation, while the
remaining discontinuities still require investigation across the VM scheduler,
capture packet cadence, and the driver path. Keep the strict zero-discontinuity
acceptance gate.

After the retest, only the newly added `ROOT\MEDIA\0001`, `oem12.inf`, its
temporary signing certificate, and the unique probe staging directory were
removed. Earlier `oem10.inf` and `oem11.inf`, test-signing mode, disabled Secure
Boot, and all enabled firewall profiles were preserved.

## Capture packet timeline diagnosis (2026-10-09)

Commit `33645c7` adds per-packet WASAPI device-frame and QPC tracking to the
multi-bus probe. It reports position gaps/overlaps, timestamp errors, QPC
regressions, and maximum frame-clock/QPC delta error in addition to the
`DATA_DISCONTINUITY` flag. The tracker has deterministic tests for contiguous
packets, gaps, overlaps, invalid timestamps, QPC regressions, and arithmetic
bounds. The probe does not use these diagnostic values to relax its strict
zero-discontinuity acceptance gate.

The first VM24 runs exposed a measurement issue: the probe synchronously
printed every discontinuity from the capture servicing thread. Commit
`441f995` removes that per-event output while preserving counters and the
once-per-window analysis output. Treat runs before and after that change as
different probe conditions, not directly comparable performance samples.

With the quiet probe, a 60-second concurrent run produced 58 valid analysis
windows per bus, no silent frames, no failed signal/channel/isolation windows,
no timestamp errors, and no QPC regressions. A SAR-only two-bus run reported
38 and 44 discontinuity packets with 23,808 and 26,688 device-position gap
frames. A same-session concurrent VB-Cable/SAR control reported 22
discontinuities and 13,728 gap frames on VB-Cable, and 32 discontinuities and
16,416 gap frames on SAR. Both runs failed the strict continuity gate. The
paired result shows the current VM and audio scheduling baseline is materially
noisy; SAR was somewhat worse than VB-Cable in that sample, but these short
runs do not establish the source or a stable comparative rate. Do not claim
glitch-free or release-ready behavior from the successful content windows.

The exact CI-built driver package reached stable readiness on VM24 with two
active render and two active capture endpoints, all 48 kHz stereo. After the
test, only the new `ROOT\MEDIA\0001`, its `oem12.inf`, and the temporary
certificate were removed. The original VB-Cable and OEM driver packages were
retained; certificate residual count was zero, all firewall profiles remained
enabled, and the previously authorized test-signing/Secure-Boot lab settings
were unchanged. Detailed probe output remains under
`C:\sar-lab\continuity-33645c7` on VM24.

## Capture service latency probe (2026-10-10)

Commits `e07a23a` and `eb99eed` add allocation-free per-packet capture service
metrics to the WASAPI probe: event-service pass counts, empty passes, maximum
packets drained per pass, packet timestamp age, and packets older than two
device periods. The age threshold is diagnostic only; it does not change the strict
zero-gap/zero-discontinuity continuity gate. The signal-analysis test also
records its contract: a spectral content pass does not prove frame continuity.
The Windows Transport workflow (including all CTest and probe-script tests)
and pinned SysVAD build both pass at `eb99eed`.

VM24 was sampled as a VB-Cable-only control because no SAR instance was
installed during this probe run. The guest had 2 vCPUs, 4 GB RAM, about 1.9 GB
free, AudioSrv running, and 0-3% aggregate CPU during the short baseline sample.
In a 15-second interactive control, all 14 analyzed signal windows passed,
with zero silence, timestamp errors, or QPC regressions. Continuity still
failed: 1,504 capture packets contained five device-position gaps (2,400
frames) and three discontinuity flags; the metrics therefore count these as
separate signals. The probe serviced 1,492 capture-event passes, including 220
empty passes, with up to three packets drained in one pass. Mean packet age was
13.49 ms, maximum age 47.91 ms, and 279 packets exceeded the two-period
diagnostic threshold. This establishes that the lab control itself can show
packet gaps despite correct signal content, but does not compare the current
SAR driver revision or attribute the gaps to either driver. Keep the strict
continuity acceptance gate.

The probe and control report are retained on VM24 under
`C:\sar-lab\capture-service-metrics-eb99eed`; no driver, certificate,
firewall, or VM security setting was changed for this measurement.
