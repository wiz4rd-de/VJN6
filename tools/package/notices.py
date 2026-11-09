#!/usr/bin/env python3
"""Writes NOTICES.txt for a VJN6 binary distribution (DECISIONS.md "Releases").

The file holds the licence text of every third-party component compiled into the plugin and standalone
builds: the shipped components of docs/LICENSES.md, plus the code that JUCE vendors in the modules VJN6
links. That JUCE list comes from JUCE's own SPDX inventory (third_party/JUCE/JUCE.spdx.json) and the module
dependency closure, so it follows JUCE updates by itself; the script fails when it cannot find a licence
file, rather than ship an incomplete notice.

    python3 tools/package/notices.py --out NOTICES.txt
    python3 tools/package/notices.py --install build/plugin/VJN6_artefacts/Release
    python3 tools/package/notices.py --install <dir> --webview2-license <NuGet package>/LICENSE.txt

--install writes NOTICES.txt into each built bundle (app, VST3, AU) or next to the standalone executable,
before signing, so the notice travels with every binary however it is distributed. Standard library only.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JUCE = ROOT / "third_party" / "JUCE"

# Vendored JUCE packages inside linked modules that are not compiled into VJN6.
NOT_COMPILED = {
    "ASIO SDK": "JUCE_ASIO is off",
    "Oboe": "Android only",
    "AAX SDK": "VJN6 builds no AAX",
    "LV2": "VJN6 builds no LV2 and hosts no plugins",
    "lilv": "plugin hosting is off",
    "serd": "plugin hosting is off",
    "sord": "plugin hosting is off",
    "sratom": "plugin hosting is off",
    "pslextensions": "plugin hosting is off",
}

LICENCE_NAMES = ["LICENSE", "LICENSE.txt", "LICENSE.md", "COPYING", "COPYING.txt"]

RULE = "-" * 79


def fail(msg):
    print("notices: " + msg, file=sys.stderr)
    sys.exit(1)


def rel(path):
    try:
        return path.relative_to(ROOT)
    except ValueError:
        return path


def read(path):
    if not path.is_file():
        fail("missing licence file %s" % rel(path))
    return path.read_text(encoding="utf-8", errors="replace").strip()


def project_version(cmake, name):
    m = re.search(r"project\(%s VERSION ([0-9.]+)" % name, cmake.read_text(encoding="utf-8"))
    return m.group(1) if m else "unknown"


def linked_modules():
    text = (ROOT / "plugin" / "CMakeLists.txt").read_text(encoding="utf-8")
    start = {m for m in re.findall(r"juce::(juce_[a-z0-9_]+)", text) if not m.startswith("juce_recommended")}
    start.add("juce_audio_plugin_client")  # the format wrappers
    seen, todo = set(), sorted(start)
    while todo:
        mod = todo.pop()
        if mod in seen:
            continue
        header = JUCE / "modules" / mod / (mod + ".h")
        if not header.is_file():
            fail("JUCE module header not found: %s" % rel(header))
        seen.add(mod)
        m = re.search(r"^\s*dependencies:(.*)$", header.read_text(encoding="utf-8", errors="replace"), re.M)
        if m:
            todo += [d for d in re.split(r"[\s,]+", m.group(1).strip()) if d]
    return seen


def licence_file(directory):
    for name in LICENCE_NAMES:
        if (directory / name).is_file():
            return directory / name
    for f in sorted(directory.iterdir()):
        if f.is_file() and re.search(r"licen[cs]e", f.name, re.I):
            return f
    if (directory / "README").is_file():  # the IJG terms live in jpglib/README
        return directory / "README"
    fail("no licence file in %s" % rel(directory))


def juce_vendored(modules):
    spdx = json.loads((JUCE / "JUCE.spdx.json").read_text(encoding="utf-8"))
    out = []
    for p in spdx["packages"]:
        m = re.match(r"Vendored at (modules/(juce_[a-z0-9_]+)\S*) in the JUCE source tree", p.get("sourceInfo", ""))
        if not m or m.group(2) not in modules or p["name"] in NOT_COMPILED:
            continue
        if "LicenseRef-JUCE-Commercial" in p.get("licenseConcluded", ""):
            continue  # JUCE's own code (e.g. @juce-framework/webview): the JUCE section covers it
        directory = JUCE / m.group(1)
        if not directory.is_dir():
            # a single vendored file (e.g. a header): its licence is in the file itself
            fail("%s: %s is not a directory; add it to the script" % (p["name"], m.group(1)))
        out.append((p["name"], p.get("versionInfo", ""), p.get("licenseConcluded", ""), licence_file(directory)))
    return out


def section(title, licence, body):
    return "%s\n%s\nLicence: %s\n%s\n\n%s\n\n" % (RULE, title, licence, RULE, body)


def build(webview2_license):
    modules = ROOT / "node_modules"  # npm workspace: the UI's packages are hoisted to the root
    fonts = ROOT / "ui" / "src" / "fonts"
    parts = [
        "VJN6 %s: third-party licence notices\n\n"
        "VJN6 is a personal project owned by Marco Funk (c) 2026 Marco Funk.\n"
        "The sections below are the third-party components compiled into this build,\n"
        "each under its own licence. docs/LICENSES.md in the source repository lists\n"
        "them by use.\n\n" % project_version(ROOT / "CMakeLists.txt", "VJN6"),
        section(
            "JUCE %s" % project_version(JUCE / "CMakeLists.txt", "JUCE"),
            "JUCE 9 End User Licence Agreement (Starter)",
            "VJN6 uses the JUCE framework under the JUCE 9 End User Licence Agreement,\n"
            "https://juce.com/legal/juce-9-licence/. Code that JUCE vendors follows\n"
            "under its own licences.",
        ),
    ]
    for name, version, lic, path in juce_vendored(linked_modules()):
        parts.append(section("%s %s (vendored by JUCE)" % (name, version), lic, read(path)))
    parts += [
        section("Svelte", "MIT", read(modules / "svelte" / "LICENSE.md")),
        section("clsx (Svelte runtime dependency)", "MIT", read(modules / "clsx" / "license")),
        section("esm-env (Svelte runtime dependency)", "MIT", read(modules / "esm-env" / "LICENSE")),
        section("Barlow Condensed", "SIL OFL 1.1", read(fonts / "OFL-BarlowCondensed.txt")),
        section("Oswald", "SIL OFL 1.1", read(fonts / "OFL-Oswald.txt")),
    ]
    if webview2_license:
        parts.append(section("Microsoft WebView2 SDK (static loader, Windows)",
                             "BSD-3-Clause", read(Path(webview2_license))))
    return "".join(parts).rstrip() + "\n"


def install(text, artefacts):
    targets = []
    for bundle in ("Standalone/VJN6.app", "VST3/VJN6.vst3", "AU/VJN6.component"):
        if (artefacts / bundle).is_dir():
            targets.append(artefacts / bundle / "Contents" / "Resources")
    if not (artefacts / "Standalone" / "VJN6.app").exists() and (artefacts / "Standalone").is_dir():
        targets.append(artefacts / "Standalone")  # Windows/Linux: next to the executable
    if not targets:
        fail("no VJN6 bundles under %s" % artefacts)
    for t in targets:
        t.mkdir(parents=True, exist_ok=True)
        (t / "NOTICES.txt").write_text(text, encoding="utf-8", newline="\n")
        print("notices: wrote %s" % (t / "NOTICES.txt"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", type=Path, help="write NOTICES.txt to this path")
    ap.add_argument("--install", type=Path, help="VJN6_artefacts/<config> directory to install NOTICES.txt into")
    ap.add_argument("--webview2-license", help="LICENSE.txt of the Microsoft.Web.WebView2 NuGet package (Windows);"
                    " its NOTICE.txt covers the SDK's .NET code generator, not the static loader")
    args = ap.parse_args()
    if not args.out and not args.install:
        ap.error("give --out and/or --install")
    text = build(args.webview2_license)
    if args.out:
        args.out.write_text(text, encoding="utf-8", newline="\n")
        print("notices: wrote %s" % args.out)
    if args.install:
        install(text, args.install)
    return 0


if __name__ == "__main__":
    sys.exit(main())
