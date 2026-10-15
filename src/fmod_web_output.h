#ifndef GODOTFMOD_FMOD_WEB_OUTPUT_H
#define GODOTFMOD_FMOD_WEB_OUTPUT_H

#if defined(WEB_ENABLED)

#include <fmod.hpp>

namespace godot {
    // FMOD output plugin for the web (prototype).
    //
    // FMOD's own HTML5 output double-buffers a single DSP block with its AudioWorklet: the worklet asks for the
    // next block, the main thread (which runs the mixer in a build without threads) answers, and when the answer
    // is late the worklet replays the previous block, which is heard as crackling.
    //
    // This output keeps a queue instead. Every frame, from the output's mixer callback (which FMOD invokes in
    // System::update with FMOD_INIT_MIX_FROM_UPDATE), the main thread estimates how much audio is still queued in the
    // worklet and mixes as many DSP blocks as needed to keep the target depth ahead, so a frame that takes
    // longer than one block no longer matters as long as it stays shorter than the queue. The depth adapts like a
    // jitter buffer: it starts at `queue_min_ms`, grows when the worklet ran dry (or nearly) and shrinks slowly
    // back after a quiet period, never above `queue_max_ms`.
    class FmodWebOutput {
    public:
        // Registers the output with the core system and selects it. Must be called before System::init.
        static bool install(FMOD::System* p_core_system, int p_queue_min_ms, int p_queue_max_ms);
        static bool is_installed();
    };
}// namespace godot

#endif// WEB_ENABLED

#endif// GODOTFMOD_FMOD_WEB_OUTPUT_H
