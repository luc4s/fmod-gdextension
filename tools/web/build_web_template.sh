#!/usr/bin/env bash
# Builds a Godot web export template (dlink_enabled=yes, threads=no) with FMOD linked into its main module.
# fmod-gdextension's web library is a side module that resolves FMOD symbols against this template at load time,
# so exporting a project using fmod-gdextension to web requires such a custom template.
#
# Usage: build_web_template.sh <godot source dir> <fmod web api dir> <template_debug|template_release> [extra scons args]
#   <godot source dir>   Godot checkout matching the editor version used for export (e.g. tag 4.5-stable).
#   <fmod web api dir>   The "api" folder of the FMOD Engine HTML5 package (contains core/ and studio/).
# Requires emscripten (emcc, emnm) on PATH, same version as the one Godot's own CI uses for that release.
set -euo pipefail

if [ "$#" -lt 3 ]; then
  sed -n '2,9p' "$0"
  exit 1
fi

GODOT_DIR="$(cd "$1" && pwd)"
FMOD_DIR="$(cd "$2" && pwd)"
TARGET="$3"
shift 3
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "$TARGET" in
  template_debug) LFIX="L" ;;
  template_release) LFIX="" ;;
  *) echo "ERROR: target must be template_debug or template_release" && exit 1 ;;
esac

STUDIO_LIB="$FMOD_DIR/studio/lib/w32/fmodstudio${LFIX}_wasm.a"
CORE_LIB="$FMOD_DIR/core/lib/w32/fmod${LFIX}_wasm.a"
if [ ! -f "$STUDIO_LIB" ]; then
  echo "ERROR: $STUDIO_LIB not found. Available FMOD libraries:"
  find "$FMOD_DIR" -name "*.a"
  exit 1
fi

# The studio library may already embed the core library: only add core if studio does not define it.
MAIN_MODULE_LIBS="$STUDIO_LIB"
STUDIO_SYMBOLS="$(emnm --defined-only "$STUDIO_LIB" 2>/dev/null || true)"
if grep -q "FMOD5_System_Create" <<< "$STUDIO_SYMBOLS"; then
  echo "INFO: $(basename "$STUDIO_LIB") embeds FMOD core."
elif [ -f "$CORE_LIB" ]; then
  echo "INFO: linking $(basename "$CORE_LIB") alongside $(basename "$STUDIO_LIB")."
  MAIN_MODULE_LIBS="$MAIN_MODULE_LIBS:$CORE_LIB"
else
  echo "ERROR: FMOD core symbols not found in $STUDIO_LIB and $CORE_LIB does not exist."
  exit 1
fi

python "$SCRIPT_DIR/patch_godot_web_scsub.py" "$GODOT_DIR"

cd "$GODOT_DIR"
# EMCC_FORCE_STDLIBS: link and export all of libc/libc++ from the main runtime, side modules (the extension, FMOD's
# C++ code) may import libc++ globals the runtime itself does not use (e.g. std::ctype<char>::id).
EMCC_FORCE_STDLIBS=1 GODOT_WEB_MAIN_MODULE_LIBS="$MAIN_MODULE_LIBS" scons platform=web target="$TARGET" dlink_enabled=yes threads=no "$@"

ls -la bin/*.zip
