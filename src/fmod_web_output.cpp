#if defined(WEB_ENABLED)

#include "fmod_web_output.h"

#include <helpers/common.h>

#include <emscripten.h>
#include <fmod_output.h>

#include <cstring>

using namespace godot;

namespace {
    struct OutputState {
        FMOD_OUTPUT_STATE* fmod_state = nullptr;
        unsigned int handle = 0;
        int rate = 48000;
        int channels = 2;
        unsigned int block_frames = 0;
        int queue_ms = 200;
        float* scratch = nullptr;
        bool installed = false;
        bool running = false;
        bool filling = false;
        unsigned long long blocks_pushed = 0;
        unsigned int mixer_calls = 0;
        unsigned int update_calls = 0;
    };

    OutputState g_state;

    // The processor plays blocks from a queue. An empty queue is an underrun: it outputs silence and reports the
    // number of silent frames to the page (at most once per second).
    const char* PROCESSOR_SOURCE =
      "class GodotFmodOutputProcessor extends AudioWorkletProcessor {"
      "  constructor(options) {"
      "    super();"
      "    this.channels = options.outputChannelCount[0];"
      "    this.queue = [];"
      "    this.offset = 0;"
      "    this.underrunFrames = 0;"
      "    this.lastReport = 0;"
      "    this.port.onmessage = (event) => {"
      "      if (event.data instanceof Float32Array) this.queue.push(event.data);"
      "      else if (event.data === 'reset') { this.queue = []; this.offset = 0; }"
      "    };"
      "  }"
      "  process(inputs, outputs) {"
      "    const output = outputs[0];"
      "    if (output.length === 0) return true;"
      "    const frames = output[0].length;"
      "    let written = 0;"
      "    while (written < frames && this.queue.length > 0) {"
      "      const block = this.queue[0];"
      "      const blockFrames = block.length / this.channels;"
      "      const count = Math.min(frames - written, blockFrames - this.offset);"
      "      for (let channel = 0; channel < output.length; channel++) {"
      "        const dest = output[channel];"
      "        const source = Math.min(channel, this.channels - 1);"
      "        let index = (this.offset) * this.channels + source;"
      "        for (let i = 0; i < count; i++, index += this.channels) dest[written + i] = block[index];"
      "      }"
      "      written += count;"
      "      this.offset += count;"
      "      if (this.offset >= blockFrames) { this.queue.shift(); this.offset = 0; }"
      "    }"
      "    if (written < frames) {"
      "      this.underrunFrames += frames - written;"
      "      if (currentFrame - this.lastReport >= sampleRate) {"
      "        this.lastReport = currentFrame;"
      "        this.port.postMessage({ underrunFrames: this.underrunFrames, queued: this.queue.length });"
      "      }"
      "    }"
      "    return true;"
      "  }"
      "}"
      "registerProcessor('godot-fmod-output', GodotFmodOutputProcessor);";

    FMOD_RESULT F_CALL cb_getnumdrivers(FMOD_OUTPUT_STATE* p_state, int* r_numdrivers) {
        *r_numdrivers = 1;
        return FMOD_OK;
    }

    FMOD_RESULT F_CALL cb_getdriverinfo(
      FMOD_OUTPUT_STATE* p_state,
      int p_id,
      char* r_name,
      int p_namelen,
      FMOD_GUID* r_guid,
      int* r_systemrate,
      FMOD_SPEAKERMODE* r_speakermode,
      int* r_speakermodechannels
    ) {
        if (p_id != 0) { return FMOD_ERR_INVALID_PARAM; }
        if (r_name && p_namelen > 0) {
            std::strncpy(r_name, "Godot FMOD queued AudioWorklet", p_namelen - 1);
            r_name[p_namelen - 1] = '\0';
        }
        if (r_guid) { std::memset(r_guid, 0, sizeof(FMOD_GUID)); }
        if (r_systemrate) { *r_systemrate = g_state.rate; }
        if (r_speakermode) { *r_speakermode = FMOD_SPEAKERMODE_STEREO; }
        if (r_speakermodechannels) { *r_speakermodechannels = 2; }
        return FMOD_OK;
    }

    FMOD_RESULT F_CALL cb_init(
      FMOD_OUTPUT_STATE* p_state,
      int p_selecteddriver,
      FMOD_INITFLAGS p_flags,
      int* r_outputrate,
      FMOD_SPEAKERMODE* r_speakermode,
      int* r_speakermodechannels,
      FMOD_SOUND_FORMAT* r_outputformat,
      int p_dspbufferlength,
      int* r_dspnumbuffers,
      int* r_dspnumadditionalbuffers,
      void* p_extradriverdata
    ) {
        g_state.fmod_state = p_state;
        g_state.block_frames = static_cast<unsigned int>(p_dspbufferlength);
        g_state.channels = 2;

        // Creates the AudioContext and the worklet node. Browsers only let audio start after a user gesture, so the
        // context is resumed and the module loaded from the first click or touch on the page, like FMOD does.
        g_state.rate = EM_ASM_INT(
          {
              const source = UTF8ToString($0);
              // Parenthesised: the C preprocessor splits EM_ASM arguments on commas outside parentheses.
              const state = ({
                  ctx: new (window.AudioContext || window.webkitAudioContext)(),
                  node: null,
                  ready: false,
                  connected: false,
                  pushedFrames: 0,
                  baseTime: 0,
                  underrunFrames: 0,
                  queuedInWorklet: 0,
                  blockFrames: $1
              });
              Module.godotFmodOutput = state;
              window.__godotFmodOutput = state;
              const url = URL.createObjectURL(new Blob([source], { type: "application/javascript" }));
              const start = () => {
                  window.removeEventListener("click", start, true);
                  window.removeEventListener("touchend", start, true);
                  window.removeEventListener("keydown", start, true);
                  state.ctx.resume().then(() => state.ctx.audioWorklet.addModule(url)).then(() => {
                      state.node = new AudioWorkletNode(state.ctx, "godot-fmod-output", { outputChannelCount: [2] });
                      state.node.port.onmessage = (event) => {
                          if (event.data && typeof event.data.underrunFrames === "number") {
                              state.underrunFrames = event.data.underrunFrames;
                              state.queuedInWorklet = event.data.queued;
                          }
                      };
                      state.ready = true;
                      state.baseTime = state.ctx.currentTime;
                      state.pushedFrames = 0;
                      if (state.wantConnected) { state.node.connect(state.ctx.destination); state.connected = true; }
                      console.log("FMOD web output: AudioWorklet ready at " + state.ctx.sampleRate + " Hz");
                  }).catch((error) => console.log("FMOD web output: cannot start AudioWorklet: " + error));
              };
              window.addEventListener("click", start, true);
              window.addEventListener("touchend", start, true);
              window.addEventListener("keydown", start, true);
              return state.ctx.sampleRate;
          },
          PROCESSOR_SOURCE,
          p_dspbufferlength
        );

        *r_outputrate = g_state.rate;
        *r_speakermode = FMOD_SPEAKERMODE_STEREO;
        *r_speakermodechannels = 2;
        *r_outputformat = FMOD_SOUND_FORMAT_PCMFLOAT;

        delete[] g_state.scratch;
        g_state.scratch = new float[g_state.block_frames * g_state.channels];
        GODOT_LOG_INFO(
          vformat(
            "FMOD web output: queued AudioWorklet output, %d Hz, %d frames per block, %d ms queue",
            g_state.rate,
            g_state.block_frames,
            g_state.queue_ms
          )
        )
        return FMOD_OK;
    }

    FMOD_RESULT F_CALL cb_start(FMOD_OUTPUT_STATE* p_state) {
        g_state.running = true;
        EM_ASM({
            const state = Module.godotFmodOutput;
            state.wantConnected = true;
            if (state.ready && !state.connected) { state.node.connect(state.ctx.destination); state.connected = true; }
        });
        return FMOD_OK;
    }

    FMOD_RESULT F_CALL cb_stop(FMOD_OUTPUT_STATE* p_state) {
        g_state.running = false;
        EM_ASM({
            const state = Module.godotFmodOutput;
            state.wantConnected = false;
            if (state.connected) { state.node.disconnect(); state.connected = false; }
            if (state.node) { state.node.port.postMessage("reset"); }
        });
        return FMOD_OK;
    }

    FMOD_RESULT F_CALL cb_close(FMOD_OUTPUT_STATE* p_state) {
        g_state.running = false;
        g_state.fmod_state = nullptr;
        delete[] g_state.scratch;
        g_state.scratch = nullptr;
        EM_ASM({
            const state = Module.godotFmodOutput;
            if (state && state.ctx) { state.ctx.close(); }
            Module.godotFmodOutput = null;
        });
        return FMOD_OK;
    }

    // Called by FMOD from System::update.
    FMOD_RESULT F_CALL cb_update(FMOD_OUTPUT_STATE* p_state) {
        g_state.update_calls++;
        FmodWebOutput::fill();
        return FMOD_OK;
    }

    // Called by FMOD when it wants the output to mix (from System::update with FMOD_INIT_MIX_FROM_UPDATE).
    FMOD_RESULT F_CALL cb_mixer(FMOD_OUTPUT_STATE* p_state) {
        if (g_state.mixer_calls == 0) { GODOT_LOG_VERBOSE("FMOD web output: FMOD drives the mixer callback from System::update") }
        g_state.mixer_calls++;
        FmodWebOutput::fill();
        return FMOD_OK;
    }

    FMOD_OUTPUT_DESCRIPTION g_description = {
      FMOD_OUTPUT_PLUGIN_VERSION,
      "Godot FMOD queued AudioWorklet",
      1,
      FMOD_OUTPUT_METHOD_MIX_DIRECT,
      cb_getnumdrivers,
      cb_getdriverinfo,
      cb_init,
      cb_start,
      cb_stop,
      cb_close,
      cb_update,
      nullptr,// gethandle
      cb_mixer,
      nullptr,// object3dgetinfo
      nullptr,// object3dalloc
      nullptr,// object3dfree
      nullptr,// object3dupdate
      nullptr,// openport
      nullptr,// closeport
      nullptr // devicelistchanged
    };
}// namespace

bool FmodWebOutput::install(FMOD::System* p_core_system, int p_queue_ms) {
    if (g_state.installed) { return true; }
    g_state.queue_ms = p_queue_ms;
    unsigned int handle = 0;
    if (p_core_system->registerOutput(&g_description, &handle) != FMOD_OK) {
        GODOT_LOG_ERROR("FMOD web output: cannot register the queued output, using FMOD's own output.")
        return false;
    }
    if (p_core_system->setOutputByPlugin(handle) != FMOD_OK) {
        GODOT_LOG_ERROR("FMOD web output: cannot select the queued output, using FMOD's own output.")
        return false;
    }
    g_state.handle = handle;
    g_state.installed = true;
    return true;
}

bool FmodWebOutput::is_installed() {
    return g_state.installed;
}

void FmodWebOutput::fill() {
    if (!g_state.installed || !g_state.running || g_state.fmod_state == nullptr || g_state.filling) { return; }
    g_state.filling = true;

    const int target_frames = g_state.queue_ms * g_state.rate / 1000;
    // Never mix more than this per frame: a very long freeze is caught up gradually instead of in one burst.
    const int max_blocks_per_fill = 64;
    int pushed = 0;
    while (pushed < max_blocks_per_fill) {
        // How much audio is still queued, estimated from the context clock: the worklet consumes exactly
        // sampleRate frames per second of context time, whether or not it has data.
        const int queued = EM_ASM_INT({
            const state = Module.godotFmodOutput;
            if (!state || !state.ready || !state.connected) return -1;
            const consumed = Math.floor((state.ctx.currentTime - state.baseTime) * state.ctx.sampleRate);
            let queued = state.pushedFrames - consumed;
            if (queued < 0) {
                // The queue ran dry: re-base so the estimate starts from an empty queue again.
                state.baseTime = state.ctx.currentTime - state.pushedFrames / state.ctx.sampleRate;
                queued = 0;
            }
            return queued;
        });
        if (queued < 0 || queued + static_cast<int>(g_state.block_frames) > target_frames) { break; }

        if (g_state.fmod_state->readfrommixer(g_state.fmod_state, g_state.scratch, g_state.block_frames) != FMOD_OK) { break; }
        EM_ASM(
          {
              const state = Module.godotFmodOutput;
              const samples = HEAPF32.slice($0 >> 2, ($0 >> 2) + $1 * $2);
              state.node.port.postMessage(samples, [samples.buffer]);
              state.pushedFrames += $1;
          },
          g_state.scratch,
          g_state.block_frames,
          g_state.channels
        );
        pushed++;
        g_state.blocks_pushed++;
    }

    g_state.filling = false;
}

#endif// WEB_ENABLED
