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
    // This output keeps a queue instead. Every frame the main thread asks how much audio is still queued in the
    // worklet and mixes as many DSP blocks as needed to keep `queue_ms` of audio ahead, so a frame that takes
    // longer than one block no longer matters as long as it stays shorter than the queue.
    class FmodWebOutput {
    public:
        // Registers the output with the core system and selects it. Must be called before System::init.
        static bool install(FMOD::System* p_core_system, int p_queue_ms);
        static bool is_installed();

        // Mixes ahead until the worklet queue holds `queue_ms` of audio. Called every frame from FmodServer::update,
        // and from FMOD's own output callbacks when it invokes them.
        static void fill();
    };
}// namespace godot

#endif// WEB_ENABLED

#endif// GODOTFMOD_FMOD_WEB_OUTPUT_H
