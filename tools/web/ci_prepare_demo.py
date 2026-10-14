#!/usr/bin/env python
"""
Prepares the demo project for a CI web export smoke test:
- points the "Web" export preset to the custom template and includes FMOD banks
  (the bank export plugin lives in the extension, which is not loaded by the CI editor),
- makes the web smoke test scene the main scene.

Usage: python ci_prepare_demo.py <demo dir> <custom template zip> <template_debug|template_release>
"""
import os
import re
import sys

SMOKE_SCENE = "res://test/web/WebSmoke.tscn"


def set_option(section_text, key, value):
    pattern = re.compile(r"^{}=.*$".format(re.escape(key)), re.M)
    line = '{}="{}"'.format(key, value)
    if pattern.search(section_text):
        return pattern.sub(lambda _: line, section_text, count=1)
    return section_text.rstrip("\n") + "\n" + line + "\n"


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    demo_dir, template_zip, target = sys.argv[1], os.path.abspath(sys.argv[2]), sys.argv[3]
    template_key = {"template_debug": "custom_template/debug", "template_release": "custom_template/release"}[target]

    presets_path = os.path.join(demo_dir, "export_presets.cfg")
    with open(presets_path) as f:
        presets = f.read()
    # Split into sections, keeping headers.
    parts = re.split(r"(?m)^(\[preset\.\d+(?:\.options)?\])$", presets)
    web_index = None
    for i in range(1, len(parts), 2):
        if parts[i].endswith("]") and ".options" not in parts[i] and 'platform="Web"' in parts[i + 1]:
            web_index = parts[i][len("[preset."):-1]
    if web_index is None:
        print("No Web export preset in {}".format(presets_path))
        sys.exit(1)
    for i in range(1, len(parts), 2):
        if parts[i] == "[preset.{}]".format(web_index):
            parts[i + 1] = set_option(parts[i + 1], "include_filter", "*.bank")
        elif parts[i] == "[preset.{}.options]".format(web_index):
            parts[i + 1] = set_option(parts[i + 1], template_key, template_zip)
    with open(presets_path, "w") as f:
        f.write("".join(parts))

    project_path = os.path.join(demo_dir, "project.godot")
    with open(project_path) as f:
        project = f.read()
    project, count = re.subn(r'(?m)^run/main_scene=.*$', 'run/main_scene="{}"'.format(SMOKE_SCENE), project)
    if count != 1:
        print("Cannot set main scene in {}".format(project_path))
        sys.exit(1)
    with open(project_path, "w") as f:
        f.write(project)
    print("Demo prepared: preset {} uses {}, main scene {}".format(web_index, template_zip, SMOKE_SCENE))


if __name__ == "__main__":
    main()
