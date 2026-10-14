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

## Mixer buffer size and crackling

Without threads, FMOD mixes on the main thread: its AudioWorklet asks the page for one DSP buffer at a time,
and the page mixes it in between Godot frames. The worklet holds two buffers. If the next one is not ready when
the current one ends, it plays the previous buffer again, which is heard as crackling.

With the extension's desktop default of 512 samples (11.6 ms at 44.1 kHz) a single slow frame is enough for
that, and it happens many times per second in Chrome and Firefox. The extension therefore defaults
**Dsp Buffer Size** (`Fmod/DSP/dsp_buffer_size`) to **2048** on the web when the project does not set it,
and logs a warning on the web for smaller values. Measured on the demo:

| Dsp Buffer Size | Firefox 136 | Chrome 152 |
|---|---|---|
| 512 | hundreds of replayed buffers per 25 s | hundreds |
| 1024 | still crackles | still crackles |
| 2048 | clean | a few replays per minute |
| 4096 | clean | clean |

A larger buffer adds latency (2048 samples × 4 buffers ≈ 186 ms), which matters little for music and ambience.
If your project sets the buffer size explicitly, give the web its own value with a feature override in
`project.godot`:

```
[Fmod]

DSP/dsp_buffer_size=512
DSP/dsp_buffer_size.web=2048
```

Chrome's main thread pauses for about 100 ms once a second on some machines, even on an empty WebGL page.
If Chrome still crackles at 2048 on your target machines, try 4096.

## Limitations

- **No threads:** banks are read synchronously, and FMOD runs on the main thread (see above for the buffer size).
- **No FMOD plugins:** dynamic libraries can't be loaded on the web. Plugins would have to be linked into the template.
- **User gesture:** FMOD only starts its output from a click or touch on the page. A key press, and Chromium's
  `--autoplay-policy=no-user-gesture-required` flag, are not enough: FMOD's HTML5 output installs `click` and
  `touchend` listeners on the window and resumes its AudioContext from there.
- **Browsers:** Firefox hangs were reported early in PR #210, and later reports say it works. Chrome 152 and
  Firefox 136 on Linux were verified with this extension. Test the browsers you target.

## Credits

Based on the work of the contributors to
[utopia-rise/fmod-gdextension#210](https://github.com/utopia-rise/fmod-gdextension/pull/210):
bitbrain, pmdevita, sphynx-owner, RafaelVidaurre, esklarski and others.
