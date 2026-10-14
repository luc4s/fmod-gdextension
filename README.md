![logo](docs/src/doc/assets/fmod-gdextension-logo.png)

[![🌈 Build](https://github.com/luc4s/fmod-gdextension/actions/workflows/release.yml/badge.svg)](https://github.com/luc4s/fmod-gdextension/actions/workflows/release.yml) 
[![](https://img.shields.io/discord/1012326818365325352.svg?label=&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2)](https://discord.com/invite/u2NM2vTGMn)
---

**Godot 4 GDExtension that integrates with the FMOD Studio API.** [FMOD is an audio engine and middleware solution](https://www.fmod.com/) for interactive audio in games. It has been the audio engine behind many
titles such as **Transistor**, **Into the Breach** and **Celeste**. This Godot extension is used by games such as [Koira](https://dont-nod.com/en/games/koira/).

If you need any help, you can join our [Discord Server](https://discord.com/invite/u2NM2vTGMn).

# Installation

1. [Download Latest Release](https://github.com/luc4s/fmod-gdextension/releases/latest): `addons.zip` contains the
   extension for Windows, Linux, macOS, Android, iOS and Web. Pick the release matching your Godot version, the
   release tag is `<extension version>-<Godot version>`.
2. Unpack the `addons/fmod` folder into your `/addons` folder within the Godot project
3. Enable this addon within the Godot settings: `Project > Project Settings > Plugins`
4. Android exports need the Gradle build, see the [setup guide](docs/src/doc/user-guide/1-install.md).

#### Read the [official docs](https://fmod-gdextension.readthedocs.io/en/latest/) to get started with this addon.

## Web (HTML5) export

Web support is experimental. The extension itself is in `addons.zip`, but FMOD has to be linked into a custom Godot
web export template, which the release cannot ship because it would redistribute FMOD's binaries.
See [the web export guide](docs/src/doc/advanced/2-web.md) for the details, in short:

1. Get the FMOD Engine HTML5 package from [fmod.com](https://www.fmod.com/download) and the Godot source at the tag
   matching your editor (`4.7.2-stable`), plus Emscripten at the version that Godot release uses (`4.0.11` for 4.7.2).
2. Build the templates, each one ends up as a zip in Godot's `bin/` folder:
   ```
   tools/web/build_web_template.sh <godot source dir> <fmod html5 package>/api template_debug
   tools/web/build_web_template.sh <godot source dir> <fmod html5 package>/api template_release
   ```
3. In your project's Web export preset, enable **Extensions Support**, disable **Thread Support**, and set
   **Custom Template > Debug / Release** to those zips.
4. Export as usual, and serve the result over HTTP. No cross-origin isolation headers are needed.

Sound starts after the first click or tap on the page, which FMOD requires on the web. The extension uses a larger
mixer buffer on the web (2048 samples) to avoid crackling, see the guide if you need to tune it.

# Features

## 🔉 Seamless integration with FMOD

Use FMOD Studio to create bank files this addon will auto-load all events for you inside Godot Engine. Live updating works out of the box.

![fmod-events](docs/src/doc/assets/screenshot-01.png)

## 🔉 Dedicated Godot nodes

This GDExtension provides nodes such as `FmodEventEmitter2D`, `FmodEventEmitter3D`, `FmodEventListener2D` and `FmodEventListener3D` that can be used in any Godot scene or GDScript code.

![fmod-nodes](docs/src/doc/assets/screenshot-02.png)

# Contributing

In order to be able to PR this repo from a fork, you need to add `FMODUSER` and `FMODPASS` secrets to your fork repo.  
This enables CI to download FMOD api.

Feel free to raise pull requests. We hope you'll enjoy this addon!

## How this extension works

This GDExtension exposes most of the Studio API functions to Godot's GDScript and also provides helpers for performing
common functions like attaching Studio events to Godot nodes and playing 3D/positional audio.

> **Note:** This plugin doesn't provide C# bindings to FMOD. There is technically a C# FMOD API but we choose to develop it as a C++ GDExtension. Any language binding with a auto-binding feature for extensions should be able to use this plugin, which is the case for GDScript. C# doesn't offer this feature yet.

## Continuous Delivery

This project uses [Github Actions](https://github.com/features/actions) to continuously deploy released drivers. If you do not want to use those releases, you
can compile from sources by looking to [compile from sources section](./docs/src/doc/advanced/1-compiling.md). This project uses [SEMVER](https://semver.org/).

# Special Thanks

This project is a forked from [godot-fmod-integration](https://github.com/alexfonseka/godot-fmod-integration)
from [alexfonseka](https://github.com/alexfonseka). We'd like to thank him for the work he did, we simply adapted his
work to GDNative.

# Tested Versions

- **Godot Version:** 4.7.2 stable
- **FMOD Version:** 2.03.06
- **Web:** Chrome 152 and Firefox 136 on Linux

[fmodsingleton]: .README/fmodsingleton.png
[usecustombuild]: .README/usecustombuild.png
