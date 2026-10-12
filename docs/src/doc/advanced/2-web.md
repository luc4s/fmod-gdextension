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
  Godot release. For 4.5, that's `4.0.11` (see `EM_VERSION` in Godot's `.github/workflows/web_builds.yml`).
- The Godot source code at the tag matching your editor version, for example `4.5-stable`, with
  [its build requirements](https://docs.godotengine.org/en/stable/contributing/development/compiling/compiling_for_web.html).

## Building the extension

```
scons platform=web target=<template_debug|template_release>
```

`threads` defaults to `no` on web. Threaded web builds are not supported.

## Building the export template

```
tools/web/build_web_template.sh <godot source dir> ../libs/fmod/web <template_debug|template_release>
```

The script patches Godot's `platform/web/SCsub` so FMOD is linked into the main runtime module only.
It then builds the template with `dlink_enabled=yes threads=no` and `EMCC_FORCE_STDLIBS=1`, so the runtime exports all of
libc/libc++ for the extension to use.
The template is written to `<godot source dir>/bin/godot.web.<target>.wasm32.nothreads.dlink.zip`.
`template_debug` links FMOD's logging libraries (`fmodstudioL_wasm.a`), and `template_release` links the release ones.

The `🌐 Web build` GitHub workflow (`.github/workflows/web.yml`) builds both templates and the extension.
It also runs the demo in headless Chromium as a smoke test.

!!! note
    The templates contain FMOD binaries. Check the FMOD license before redistributing them.

## Exporting your project

In the Web export preset:

- Enable **Extensions Support** (`variant/extensions_support`).
- Disable **Thread Support** (`variant/thread_support`).
- Set **Custom Template > Debug / Release** to the templates you built.

Banks are exported like on other platforms, and they are read through Godot's file system.

## Limitations

- **No threads:** banks are read synchronously, and FMOD runs on the main thread.
- **No FMOD plugins:** dynamic libraries can't be loaded on the web. Plugins would have to be linked into the template.
- **User gesture:** browsers only start audio after a user interaction (click, key press) on the page.
- **Browsers:** Firefox hangs were reported early in PR #210, and later reports say it works. Test the browsers you target.

## Credits

Based on the work of the contributors to
[utopia-rise/fmod-gdextension#210](https://github.com/utopia-rise/fmod-gdextension/pull/210):
bitbrain, pmdevita, sphynx-owner, RafaelVidaurre, esklarski and others.
