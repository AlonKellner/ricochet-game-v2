#!/usr/bin/env python3
"""Compare the installed Godot's dumped extension_api.json with the API file
godot-cpp generates its bindings from.

    check_api.py <dumped extension_api.json> <godot-cpp extension_api-4-7.json>

Exit 0 when the headers match and the API bodies are identical. Exit 1 with a
clear verdict otherwise; then build with
-DGODOTCPP_CUSTOM_API_FILE=native/godot-api/extension_api.json instead of
GODOTCPP_API_VERSION=4.7. Standard library only.

Regenerate the dump from inside native/godot-api/ with
    godot --headless --dump-extension-api --dump-gdextension-interface
"""
import json
import sys


def load(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def describe(header):
    return "{} ({} precision)".format(header.get("version_full_name", "?"), header.get("precision", "?"))


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    dumped_path, bundled_path = argv[1], argv[2]
    dumped = load(dumped_path)
    bundled = load(bundled_path)
    dh, bh = dumped["header"], bundled["header"]
    print("installed binary API : {}".format(describe(dh)))
    print("godot-cpp API file   : {}".format(describe(bh)))
    if dh != bh:
        for k in sorted(set(dh) | set(bh)):
            if dh.get(k) != bh.get(k):
                print("  header[{}]: binary={!r} godot-cpp={!r}".format(k, dh.get(k), bh.get(k)))
        print("VERDICT: MISMATCH -- the API headers differ. Build godot-cpp with "
              "-DGODOTCPP_CUSTOM_API_FILE={}".format(dumped_path))
        return 1
    if dumped != bundled:
        changed = [k for k in sorted(set(dumped) | set(bundled)) if dumped.get(k) != bundled.get(k)]
        print("VERDICT: MISMATCH -- same header but the API bodies differ in: {}. "
              "Build godot-cpp with -DGODOTCPP_CUSTOM_API_FILE={}".format(", ".join(changed), dumped_path))
        return 1
    print("VERDICT: MATCH -- godot-cpp's bundled API file is identical to the installed binary's API.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
