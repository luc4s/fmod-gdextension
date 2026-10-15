# Web (HTML5) export

!!! warning
    Web support is experimental. It requires a custom Godot web export template, and it only works without threads.

## Why a custom export template?

On the web, Godot loads GDExtensions as Emscripten *side modules*.
FMOD's HTML5 libraries rely on JavaScript glue code that Emscripten only emits for the *main module*.
If FMOD is linked into the extension, it fails at runtime with errors such as
`Module.FMOD_JS_MixerFastpathFunction is not a function`
([FMOD forum thread](https://qa.fmod.com/t/trying-to-build-fmod-html5-as-gdextension/23774)).

So this plugin splits the work:

- The **extension** (`libGodotFmod.web.<target>.wasm32.wasm`) is compiled against the FMOD headers only.
- **FMOD** is linked into the main module of a custom Godot web export template.
  When Godot loads the extension, the extension resolves the FMOD symbols against that template.

## Prerequisites

- The FMOD Engine HTML5 package, from the FMOD website. Put its `api` folder under `libs/fmod/web`, next to the other platforms.
- [Emscripten](https://emscripten.org/docs/getting_started/downloads.html), at the version Godot uses for your
  Godot release. For 4.7.2, that's `4.0.11` (see `EM_VERSION` in Godot's `.github/workflows/web_builds.yml`).
- The Godot source code at the tag matching your editor version, for example `4.7.2-stable`, with
  [its build requirements](https://docs.godotengine.org/en/stable/contributing/development/compiling/compiling_for_web.html).

## Building the extension

```
scons platform=web target=<template_debug|template_release>
```

`threads` defaults to `no` on web. Threaded web builds are not supported.

## Getting the export template

Each release attaches the templates built by CI for its Godot version: `godot-web-fmod-template_debug.zip` and
`godot-web-fmod-template_release.zip`, next to `addons.zip`. Use them unless you need another Godot version or a
different FMOD build, in which case build them as described below.

## Building the export template

```
tools/web/build_web_template.sh <godot source dir> ../libs/fmod/web <template_debug|template_release>
```

The script patches Godot's `platform/web/SCsub` so FMOD is linked into the main runtime module only.
It then builds the template with `dlink_enabled=yes threads=no`.
The template is written to `<godot source dir>/bin/godot.web.<target>.wasm32.nothreads.dlink.zip`.
`template_debug` links FMOD's logging libraries (`fmodstudioL_wasm.a`), and `template_release` links the release ones.

The `🌐 Web build` GitHub workflow (`.github/workflows/web.yml`) builds both templates and the extension.
It also exports the demo and runs it in headless Chrome as a smoke test (`tools/web/smoke_test.mjs`).
The test drives the browser over the DevTools protocol, clicks the page (FMOD only starts its output from a
click or touch handler), and checks that an event's timeline keeps advancing and that FMOD's mixer keeps up
with its AudioWorklet. To run it on an export of your own:

```
node tools/web/smoke_test.mjs <export dir> [timeout seconds]
```

Set `CHROMIUM_PATH` to pick the browser, and `HEADED=1` to watch it run (and hear it).

!!! note
    The templates embed FMOD, like the FMOD libraries shipped in `addons.zip`. FMOD's license applies to your use of them.

## Exporting your project

In the Web export preset:

- Enable **Extensions Support** (`variant/extensions_support`).
- Disable **Thread Support** (`variant/thread_support`).
- Set **Custom Template > Debug / Release** to the templates you built.

Banks are exported like on other platforms, and they are read through Godot's file system.

## How audio reaches the browser

Without threads, FMOD mixes on the main thread, between Godot frames, and the sound is played by an AudioWorklet,
which runs in the browser's audio thread. The extension ships its own output for that (prototype, on by default):

- A 4 ms timer, running between Godot frames, and FMOD's update callback both ask the worklet how much audio is
  still queued and mix as many DSP blocks as needed to keep the queue at its target depth. A frame that takes
  longer than one block does not matter any more; only a freeze longer than the queue makes the worklet run dry,
  in which case it plays silence until the mixer catches up.
- The target depth adapts, like a jitter buffer: it starts at **Web > Queue Min Ms** (`Fmod/Web/queue_min_ms`,
  default 30), grows by half each time the worklet ran dry or nearly did, up to **Web > Queue Max Ms**
  (`Fmod/Web/queue_max_ms`, default 300), and shrinks back slowly after fifteen seconds without trouble. On a
  machine with regular frames the latency stays near the minimum; on one that hitches it rises just enough.
- The queue depth is the audio latency, plus one DSP block and the browser's own output latency. With this output
  the DSP block size defaults to 256 on the web (6 ms at 44.1 kHz), so the extension's part of the latency is
  about 40 ms at the minimum, comparable to FMOD's desktop default of 512 x 4 samples. A larger `Dsp Buffer
  Size` only makes the mixing coarser and raises that floor.

**Web > Queued Output** (`Fmod/Web/queued_output`) set to false falls back to FMOD's own web output. That one
double-buffers a single DSP block: when the main thread does not deliver the next block in time, the worklet plays
the previous one again, which is heard as crackling. With it, keep **Dsp Buffer Size** at 2048 or more (the
extension's web default in that case, and it warns below that); measured on the demo, 512 and 1024 crackle in
Chrome and Firefox, 2048 is clean in Firefox and nearly so in Chrome, 4096 is clean in both.

The CI smoke test covers both: it plays music, then blocks every frame for 60 ms during three seconds and fails if
the worklet ran dry (queued output) or replayed stale blocks (FMOD's output) for more than a quarter of the audio.

## Limitations

- **No threads:** banks are read synchronously, and FMOD mixes on the main thread (see above).
- **No FMOD plugins:** dynamic libraries can't be loaded on the web. Plugins would have to be linked into the template.
- **User gesture:** audio only starts from a gesture on the page. The extension's queued output listens for
  `click`, `touchend` and `keydown` on the window and resumes its AudioContext from the first one. FMOD's own
  output (`Fmod/Web/queued_output` off) only installs `click` and `touchend` listeners, so a key press is not
  enough with it. Chromium's `--autoplay-policy=no-user-gesture-required` flag is not enough for either: both
  wait for the listener, not for the policy.
- **Browsers:** Firefox hangs were reported early in PR #210, and later reports say it works. Chrome 152 and
  Firefox 136 on Linux were verified with this extension. Test the browsers you target.

## Credits

Based on the work of the contributors to
[utopia-rise/fmod-gdextension#210](https://github.com/utopia-rise/fmod-gdextension/pull/210):
bitbrain, pmdevita, sphynx-owner, RafaelVidaurre, esklarski and others.
