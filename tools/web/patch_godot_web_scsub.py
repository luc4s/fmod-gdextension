#!/usr/bin/env python
"""
Patches Godot's platform/web/SCsub so that extra static libraries can be linked into the main runtime module of a
web export template built with dlink_enabled=yes.

FMOD's HTML5 libraries rely on Emscripten JS glue that is only emitted for the main module. With dlink_enabled=yes,
Godot itself is built as a side module and the main module is a small runtime (web_runtime.cpp). Godot's `linkflags`
option would add FMOD to both, so we hook only the main runtime environment (sys_env).

Libraries to link are read at build time from the GODOT_WEB_MAIN_MODULE_LIBS environment variable
(paths separated by os.pathsep). The main module is built with MAIN_MODULE=1, for which emscripten already wraps the
whole link in --whole-archive and exports everything, so every FMOD symbol can be resolved by the GDExtension side
module. Do not add --whole-archive/--no-whole-archive here: they do not nest, and a --no-whole-archive would stop
emscripten's system libraries (libc, libc++...) from being fully linked and exported.

Usage: python patch_godot_web_scsub.py <path to godot source>
"""
import os
import sys

MARKER = "# fmod-gdextension: main module libs"
ANCHOR = """    # Reset libraries. The main runtime will only link emscripten libraries, not godot ones.
    sys_env["LIBS"] = []
"""
PATCH = """    {marker}
    import os as _fmod_os

    for _lib in [l for l in _fmod_os.environ.get("GODOT_WEB_MAIN_MODULE_LIBS", "").split(_fmod_os.pathsep) if l]:
        sys_env.Append(LINKFLAGS=[_lib])
""".format(marker=MARKER)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)

    scsub_path = os.path.join(sys.argv[1], "platform", "web", "SCsub")
    with open(scsub_path) as f:
        content = f.read()

    if MARKER in content:
        print("{} is already patched.".format(scsub_path))
        return

    if content.count(ANCHOR) != 1:
        print("Could not find the dlink main module section in {}, unsupported Godot version?".format(scsub_path))
        sys.exit(1)

    with open(scsub_path, "w") as f:
        f.write(content.replace(ANCHOR, ANCHOR + PATCH))
    print("Patched {}".format(scsub_path))


if __name__ == "__main__":
    main()
