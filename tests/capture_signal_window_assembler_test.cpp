#include "src/capture_signal_window_assembler.h"
#include "src/signal_window_queue.h"

namespace {

using Queue = sar_driver::SignalWindowQueue<3, 2, 2>;
using Assembler = sar_driver::CaptureSignalWindowAssembler<Queue>;

bool incomplete_segment_is_discarded_before_new_audio() {
    Queue queue;
    Assembler assembler;
    if (assembler.AppendFrame(1.0f, 1.0f, queue) ||
        assembler.AppendFrame(2.0f, 2.0f, queue)) return false;
    if (!assembler.DiscardPartialSegment()) return false;
    if (assembler.pending_frames() != 0 || assembler.discarded_segment_frames() != 2) {
        return false;
    }
    if (assembler.AppendFrame(3.0f, 3.0f, queue) ||
        assembler.AppendFrame(4.0f, 4.0f, queue) ||
        !assembler.AppendFrame(5.0f, 5.0f, queue)) return false;
    bool contains_only_new_segment = false;
    const bool consumed = queue.try_consume_one(
        [&](const float* samples, std::size_t count) {
            contains_only_new_segment = count == 6 && samples[0] == 3.0f &&
                samples[2] == 4.0f && samples[4] == 5.0f;
        });
    return consumed && contains_only_new_segment && assembler.completed_windows() == 1;
}

bool empty_break_is_free_and_does_not_shift_window_boundaries() {
    Queue queue;
    Assembler assembler;
    if (assembler.DiscardPartialSegment() || assembler.discarded_segment_frames() != 0) {
        return false;
    }
    if (assembler.AppendFrame(1.0f, -1.0f, queue) ||
        assembler.AppendFrame(2.0f, -2.0f, queue) ||
        !assembler.AppendFrame(3.0f, -3.0f, queue) ||
        assembler.pending_frames() != 0) return false;
    return assembler.completed_windows() == 1 && queue.queued_windows() == 1;
}

bool repeated_breaks_count_only_frames_actually_discarded() {
    Queue queue;
    Assembler assembler;
    assembler.AppendFrame(1.0f, 1.0f, queue);
    if (!assembler.DiscardPartialSegment() || assembler.DiscardPartialSegment()) return false;
    assembler.AppendFrame(2.0f, 2.0f, queue);
    assembler.AppendFrame(3.0f, 3.0f, queue);
    if (!assembler.DiscardPartialSegment()) return false;
    return assembler.discarded_segment_frames() == 3 &&
           assembler.pending_frames() == 0 && assembler.completed_windows() == 0;
}

}  // namespace

int main() {
    return incomplete_segment_is_discarded_before_new_audio() &&
                   empty_break_is_free_and_does_not_shift_window_boundaries() &&
                   repeated_breaks_count_only_frames_actually_discarded()
               ? 0
               : 1;
}
