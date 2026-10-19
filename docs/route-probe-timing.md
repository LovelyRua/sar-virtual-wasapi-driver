# Route probe timing

`wasapi_bridge_probe --route` is a short content probe, not a soak test. It
renders 997 Hz on the left and 1501 Hz on the right for three seconds, then
actively supplies silence during the final capture second. Starting an empty
render client or simply ceasing to supply its running buffer would introduce
probe-side starvation, unrelated to the route being tested.

The probe pre-fills the render buffer before Start, preallocates at most five
seconds of stereo PCM16 capture storage, and registers its existing MMCSS audio
thread helper. A registration failure is an error, not a silent fallback. Capture
draining is bounded to 32 packets and one millisecond per event pass, with an
overall four-second deadline. Overflowing the capture storage fails the probe.
No capture-vector capacity growth is permitted after streaming starts.

## Evidence boundaries

Raw `discontinuity_packets` counts include startup. Separate `steady_*` counters
come from the existing device-position/QPC continuity tracker: the first 24000
captured frames are warmup, and only packets received before 2.5 seconds are
observed. `measured_capture_frames` makes a stalled or sparse observation visible.
Consumers must require coverage rather than accepting zero error counters alone.

Additional counters report MMCSS registration, render-empty observations, and
event-wait timeouts. A zero render-empty count alone does not prove that a driver
never underruns; it describes padding observed by this client.

The exit code still describes spectral content: 0 for both expected tones with
low crosstalk, 3 for no usable signal/crosstalk, and 4 for a missing expected
channel. A downstream acceptance tool must explicitly gate the steady timeline
if it requires timing evidence. Do not treat the strongest spectral window as
proof of waveform continuity; the engine can insert zeros without causing a
receiving endpoint's device position to jump.

## Reference

Microsoft's [rendering stream example](https://learn.microsoft.com/en-us/windows/win32/coreaudio/rendering-a-stream)
fills the endpoint buffer before starting and explicitly submits silent buffers
when real source data ends. Capture device positions and QPC timestamps come
from [IAudioCaptureClient::GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer).
