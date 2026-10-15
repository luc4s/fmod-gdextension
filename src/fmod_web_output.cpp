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
        int queue_min_ms = 60;
        int queue_max_ms = 300;
        float* scratch = nullptr;
        bool installed = false;
        bool running = false;
        bool filling = false;
        unsigned long long blocks_pushed = 0;
        unsigned int mixer_calls = 0;
        unsigned int update_calls = 0;
        const char* first_driver = nullptr;
    };

    OutputState g_state;

    // Period of the JS refill timer, see timer_tick.
    const int TIMER_INTERVAL_MS = 4;

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
      "    this.wasDry = false;"
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
      "      if (!this.wasDry || currentFrame - this.lastReport >= sampleRate) {"
      "        this.lastReport = currentFrame;"
      "        this.port.postMessage({ dry: !this.wasDry, underrunFrames: this.underrunFrames, queued: this.queue.length });"
      "      }"
      "      this.wasDry = true;"
      "    } else {"
      "      this.wasDry = false;"
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
        // context is resumed and the module loaded from the first click, touch or key press on the page.
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
                  blockFrames: $1,
                  minMs: $2,
                  maxMs: $3,
                  targetMs: $2,
                  dryPending: false,
                  dryEvents: 0,
                  bumps: 0,
                  lastBumpTime: -1e9,
                  lastDecayTime: 0,
                  lowestQueuedMs: 1e9,
                  disposed: false
              });
              // Adaptive queue, like a VoIP jitter buffer: start short, grow when the worklet ran dry or nearly did,
              // shrink slowly after a quiet period. Returns how many blocks to mix now.
              state.blocksToMix = (blockFrames) => {
                  const rate = state.ctx.sampleRate;
                  const now = state.ctx.currentTime;
                  const consumed = Math.floor((now - state.baseTime) * rate);
                  let queued = state.pushedFrames - consumed;
                  let dry = state.dryPending;
                  state.dryPending = false;
                  if (queued < 0) {
                      // The estimate says the queue ran dry: re-base so it starts from empty again.
                      state.baseTime = now - state.pushedFrames / rate;
                      queued = 0;
                      dry = true;
                  }
                  const queuedMs = queued * 1000 / rate;
                  if (queuedMs < state.lowestQueuedMs) state.lowestQueuedMs = queuedMs;
                  const blockMs = blockFrames * 1000 / rate;
                  // The queue can never usefully be shorter than one block, the worklet's own quantum and the time
                  // between two refills.
                  const floorMs = Math.max(state.minMs, blockMs + 128 * 1000 / rate + 2 * $4);
                  if ((dry || queued < blockFrames) && now - state.lastBumpTime > 0.5) {
                      state.targetMs = Math.min(state.maxMs, Math.max(floorMs, state.targetMs * 1.5 + blockMs));
                      state.lastBumpTime = now;
                      state.lastDecayTime = now;
                      state.bumps++;
                      if (dry) state.dryEvents++;
                  } else if (now - state.lastBumpTime > 15 && now - state.lastDecayTime > 1 && state.targetMs > floorMs) {
                      state.targetMs = Math.max(floorMs, state.targetMs - 10);
                      state.lastDecayTime = now;
                  }
                  const targetFrames = Math.max(floorMs, state.targetMs) * rate / 1000;
                  return Math.max(0, Math.min(64, Math.floor((targetFrames - queued) / blockFrames)));
              };
              const url = URL.createObjectURL(new Blob([source], { type: "application/javascript" }));
              // Parenthesised for the preprocessor too.
              const gestures = (["click", "touchend", "keydown"]);
              const start = () => {
                  gestures.forEach((name) => window.removeEventListener(name, start, true));
                  if (state.disposed) return;
                  state.ctx.resume().then(() => state.ctx.audioWorklet.addModule(url)).then(() => {
                      if (state.disposed) return;
                      state.node = new AudioWorkletNode(state.ctx, "godot-fmod-output", { outputChannelCount: [2] });
                      state.node.port.onmessage = (event) => {
                          if (event.data && typeof event.data.underrunFrames === "number") {
                              state.underrunFrames = event.data.underrunFrames;
                              state.queuedInWorklet = event.data.queued;
                              if (event.data.dry) state.dryPending = true;
                          }
                      };
                      state.ready = true;
                      state.baseTime = state.ctx.currentTime;
                      state.pushedFrames = 0;
                      if (state.wantConnected) { state.node.connect(state.ctx.destination); state.connected = true; }
                      console.log("FMOD web output: AudioWorklet ready at " + state.ctx.sampleRate + " Hz");
                  }).catch((error) => console.log("FMOD web output: cannot start AudioWorklet: " + error));
              };
              // Called from close: the listeners of a context that never started would otherwise stay on the
              // window and try to resume a closed context on the next click.
              state.dispose = () => {
                  state.disposed = true;
                  if (state.timer && state.timer !== -1) { clearInterval(state.timer); }
                  state.timer = null;
                  gestures.forEach((name) => window.removeEventListener(name, start, true));
                  if (state.node) { state.node.port.onmessage = null; state.node.disconnect(); state.node = null; }
                  state.connected = false;
                  state.ready = false;
                  URL.revokeObjectURL(url);
                  state.ctx.close().catch(() => {});
                  if (Module.godotFmodOutput === state) Module.godotFmodOutput = null;
                  if (window.__godotFmodOutput === state) window.__godotFmodOutput = null;
              };
              if (Module.godotFmodOutput && Module.godotFmodOutput.dispose) Module.godotFmodOutput.dispose();
              Module.godotFmodOutput = state;
              window.__godotFmodOutput = state;
              gestures.forEach((name) => window.addEventListener(name, start, true));
              return state.ctx.sampleRate;
          },
          PROCESSOR_SOURCE,
          p_dspbufferlength,
          g_state.queue_min_ms,
          g_state.queue_max_ms,
          TIMER_INTERVAL_MS
        );

        *r_outputrate = g_state.rate;
        *r_speakermode = FMOD_SPEAKERMODE_STEREO;
        *r_speakermodechannels = 2;
        *r_outputformat = FMOD_SOUND_FORMAT_PCMFLOAT;

        delete[] g_state.scratch;
        g_state.scratch = new float[g_state.block_frames * g_state.channels];
        GODOT_LOG_INFO(
          vformat(
            "FMOD web output: queued AudioWorklet output, %d Hz, %d frames per block, adaptive queue %d to %d ms",
            g_state.rate,
            g_state.block_frames,
            g_state.queue_min_ms,
            g_state.queue_max_ms
          )
        )
        return FMOD_OK;
    }

    // Refill between frames: a JS interval calls this through the shared wasm function table, so the queue only
    // has to cover the work of one frame, not the interval between two frames.
    void timer_tick() {
        FmodWebOutput::fill("a 4 ms timer");
    }

    FMOD_RESULT F_CALL cb_start(FMOD_OUTPUT_STATE* p_state) {
        g_state.running = true;
        EM_ASM(
          {
              const state = Module.godotFmodOutput;
              if (!state) return;
              if (!state.timer) {
                  if (typeof wasmTable !== "undefined" && wasmTable.get) {
                      const tick = wasmTable.get($0);
                      state.timer = setInterval(() => { if (state.ready && state.connected) tick(); }, $1);
                  } else {
                      console.log("FMOD web output: no wasm table access, refilling once per frame only");
                      state.timer = -1;
                  }
              }
              state.wantConnected = true;
            if (state.ready && !state.connected) {
                // Stop emptied the worklet queue: the consumption estimate starts again from an empty queue.
                state.pushedFrames = 0;
                state.baseTime = state.ctx.currentTime;
                state.node.connect(state.ctx.destination);
                state.connected = true;
            }
          },
          reinterpret_cast<int>(&timer_tick),
          TIMER_INTERVAL_MS
        );
        return FMOD_OK;
    }

    FMOD_RESULT F_CALL cb_stop(FMOD_OUTPUT_STATE* p_state) {
        g_state.running = false;
        EM_ASM({
            const state = Module.godotFmodOutput;
            if (!state) return;
            state.wantConnected = false;
            if (state.timer && state.timer !== -1) { clearInterval(state.timer); }
            state.timer = null;
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
            if (state) state.dispose();
        });
        return FMOD_OK;
    }

    // Called by FMOD from System::update. Measured on the HTML5 build without threads: this is the callback that
    // actually runs each frame, so this is where the queue gets topped up (the smoke test logs the first caller).
    FMOD_RESULT F_CALL cb_update(FMOD_OUTPUT_STATE* p_state) {
        g_state.update_calls++;
        FmodWebOutput::fill("the output's update callback");
        return FMOD_OK;
    }

    // Called by FMOD when it wants the output to mix. Measured on the HTML5 build without threads: never invoked,
    // even with FMOD_INIT_MIX_FROM_UPDATE (a build that mixed only from here never produced a block). Kept wired
    // in case a future FMOD release behaves differently.
    FMOD_RESULT F_CALL cb_mixer(FMOD_OUTPUT_STATE* p_state) {
        g_state.mixer_calls++;
        FmodWebOutput::fill("the output's mixer callback");
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

bool FmodWebOutput::install(FMOD::System* p_core_system, int p_queue_min_ms, int p_queue_max_ms) {
    if (g_state.installed) { return true; }
    g_state.queue_min_ms = p_queue_min_ms;
    g_state.queue_max_ms = p_queue_max_ms < p_queue_min_ms ? p_queue_min_ms : p_queue_max_ms;
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

// Mixes ahead until the worklet queue holds the current target depth. On this build the caller that actually
// delivers blocks is the output's update callback, inside System::update; the call from FmodServer::update right
// after it is a safety net that normally finds the queue full. The first caller that mixes is logged once, so a
// change of behaviour in a future FMOD shows up in the console.
void FmodWebOutput::fill(const char* p_driver) {
    if (!g_state.installed || !g_state.running || g_state.fmod_state == nullptr || g_state.filling) { return; }
    g_state.filling = true;
    const unsigned long long before = g_state.blocks_pushed;
    // The controller lives in JS, next to the context clock and the worklet's reports.
    const int blocks = EM_ASM_INT(
      {
          const state = Module.godotFmodOutput;
          if (!state || !state.ready || !state.connected) return 0;
          return state.blocksToMix($0);
      },
      g_state.block_frames
    );
    for (int i = 0; i < blocks; i++) {
        if (g_state.fmod_state->readfrommixer(g_state.fmod_state, g_state.scratch, g_state.block_frames) != FMOD_OK) {
            break;
        }
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
        g_state.blocks_pushed++;
    }
    if (g_state.blocks_pushed > before) {
        if (g_state.first_driver == nullptr) {
            g_state.first_driver = p_driver;
            GODOT_LOG_INFO(vformat("FMOD web output: mixing driven by %s", p_driver))
        }
        // Per-caller block counts, read by the smoke test.
        EM_ASM(
          {
              const state = Module.godotFmodOutput;
              if (!state) return;
              const driver = UTF8ToString($0);
              state.blocksByDriver = state.blocksByDriver || {};
              state.blocksByDriver[driver] = (state.blocksByDriver[driver] || 0) + $1;
          },
          p_driver,
          static_cast<int>(g_state.blocks_pushed - before)
        );
    }
    g_state.filling = false;
}

#endif// WEB_ENABLED
