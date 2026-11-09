#!/usr/bin/env python3
"""VJN6 licence and branding check (SPEC R-L2 to R-L5; AC-L1.1, AC-L2.1, AC-L4.1, AC-L5.1).

Checks (all run by default; pick some with --check NAME, repeatable):

  gpl       R-L3 / AC-L1.1. No git-tracked first-party file contains the full title of the GNU GPL
            (the phrase "General Public License" preceded by "GNU") or of the GNU AGPL ("Affero"
            preceded by "GNU"). Case-insensitive; whitespace, line breaks and comment leaders
            (// # * ; -- % !) may separate the words. Excluded: third_party/JUCE, third_party/Catch2
            (submodules), docs/ (docs/LICENSES.md names licences), SPEC.md and KICKOFF.md.
  sources   R-L2 / AC-L2.1. None of the GPL-3 projects that SPEC.md cites as sources of numbers is in
            the tree (any path, tracked or on disk) or fetched: .gitmodules, every file under .github/
            and every other fetch-capable file (CMake, npm manifests, shell scripts) must not name them.
  docs      R-L4 / AC-L4.1. docs/LICENSES.md lists every shipped component with its licence in a
            "Shipped" section and the build and test tools in a separate "Build and test tools" section.
  branding  R-L5 / AC-L5.1. "Roland", "JUNO" and "Juno-" (case-sensitive) appear in ui/src and plugin/
            only inside the two allowed texts "Juno-106 SysEx files (*.syx)" and
            "Not a VJN6 or Juno-106 file".
  metadata  R-L5 / AC-L5.1 for the built plugin: the same rule over Info.plist, moduleinfo.json and
            *.rc files found under each --metadata DIR (a build directory). Runs only with --metadata;
            finding no metadata file there is a failure.

--self-test (AC-L1.1, second half) copies the git-tracked first-party files into a temporary
repository, plants the GNU GPL title (and the AGPL title) in a first-party file there and verifies
that the check fails, verifies that the excluded locations do not trigger it, and plants violations
for the other checks as well. The real working tree is never modified; the temporary copy is removed.

Exit status: 0 = pass, 1 = violations found, 2 = usage or environment error.
Standard library only; Python 3.8+.
"""
import argparse
import os
import pathlib
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------------------------
# gpl (R-L3)

GPL_EXCLUDED_PREFIXES = ("third_party/JUCE/", "third_party/Catch2/", "docs/")
GPL_EXCLUDED_FILES = {"SPEC.md", "KICKOFF.md", "third_party/JUCE", "third_party/Catch2"}
# Separators allowed between the words of a title: whitespace and common comment leaders.
_SEP = r"(?:\s|//|/\*|\*/|\*|#|;|--|%|!)+"
GPL_TITLE = re.compile(
    "GNU" + _SEP + "(?:" + "General" + _SEP + "Public" + _SEP + "Licen[cs]e" + "|" + "Affero" + ")",
    re.IGNORECASE,
)

# ---------------------------------------------------------------------------------------------
# sources (R-L2)

# GPL-3 projects used only as sources of numbers (R-L2), matched in lowercase path components and in
# the lowercase text of fetch-capable files.
FORBIDDEN_SOURCES = [
    ("Ultramaster KR-106", re.compile(r"kr[-_ ]?106")),
    ("Ultramaster KR-106", re.compile(r"ultramaster")),
    ("junox", re.compile(r"junox")),
    ("Hera", re.compile(r"(?<![a-z0-9])hera(?![a-z0-9])")),
    ("mpc-vst-jn106", re.compile(r"mpc[-_]?vst[-_]?jn[-_]?106")),
]
FETCH_FILE_NAMES = {".gitmodules", "CMakeLists.txt", "package.json", "package-lock.json", "pnpm-lock.yaml",
                    "yarn.lock", "requirements.txt", "pyproject.toml", "Makefile"}
FETCH_FILE_SUFFIXES = (".cmake", ".sh", ".ps1", ".bat", ".cmd")
WALK_SKIP_DIRS = {".git", "node_modules", ".cache", "__pycache__"}

# ---------------------------------------------------------------------------------------------
# docs (R-L4)

LICENSES_MD = "docs/LICENSES.md"
SHIPPED_REQUIRED = [  # (component as written in the first table cell, licence token on the same row)
    ("JUCE", "Starter"),
    ("VST3 SDK", "MIT"),
    ("Svelte", "MIT"),
    ("Barlow Condensed", "OFL"),
    ("Oswald", "OFL"),
]
TOOLS_REQUIRED = [
    ("Vite", "MIT"),
    ("Vitest", "MIT"),
    ("Playwright", "Apache-2.0"),
    ("Catch2", "BSL-1.0"),
    ("Emscripten", "MIT"),
    ("pluginval", "GPL-3"),
]
OPTIONAL_TOOL_IF_REFERENCED = ("Pamplejuce", "MIT")

# ---------------------------------------------------------------------------------------------
# branding (R-L5)

BRANDING_DIRS = ("ui/src", "plugin")
BRANDING_PATTERN = re.compile(r"Roland|JUNO|Juno-")
BRANDING_ALLOWED = ("Juno-106 SysEx files (*.syx)", "Not a VJN6 or Juno-106 file")
METADATA_NAMES = {"Info.plist", "moduleinfo.json"}
METADATA_SUFFIXES = (".rc",)


class Usage(Exception):
    """Usage or environment problem (exit status 2)."""


# ---------------------------------------------------------------------------------------------
# helpers


def git_index(root):
    """Returns (files, gitlinks): tracked paths (posix, relative) and submodule paths."""
    try:
        out = subprocess.run(["git", "-C", str(root), "ls-files", "-s", "-z"], check=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    except FileNotFoundError:
        raise Usage("git not found on PATH")
    except subprocess.CalledProcessError as e:
        raise Usage("not a git working tree: %s (%s)" % (root, e.stderr.decode(errors="replace").strip()))
    files, links = [], []
    for entry in out.split(b"\0"):
        if not entry:
            continue
        meta, _, path = entry.partition(b"\t")
        mode = meta.split(b" ")[0]
        p = path.decode("utf-8", errors="surrogateescape")
        if mode == b"160000":
            links.append(p)
        elif mode != b"120000":  # symlinks are not content
            files.append(p)
    return sorted(set(files)), sorted(set(links))


def read_text(path):
    try:
        data = path.read_bytes()
    except OSError:
        return None
    return data.decode("utf-8", errors="replace")


def is_binary(path):
    try:
        with open(path, "rb") as f:
            return b"\0" in f.read(8192)
    except OSError:
        return True


def is_build_dir(dirpath, names):
    return "CMakeCache.txt" in names


def walk_files(base):
    """Files under base on disk, skipping VCS, dependency and CMake build directories."""
    for dirpath, dirnames, filenames in os.walk(base):
        if is_build_dir(dirpath, filenames):
            dirnames[:] = []
            continue
        dirnames[:] = sorted(d for d in dirnames if d not in WALK_SKIP_DIRS)
        for name in sorted(filenames):
            yield pathlib.Path(dirpath) / name


def walk_paths(base):
    """Every directory and file path under base (relative, posix), with the same skips."""
    for dirpath, dirnames, filenames in os.walk(base):
        rel_dir = pathlib.Path(dirpath).relative_to(base).as_posix()
        if is_build_dir(dirpath, filenames):
            dirnames[:] = []
            continue
        dirnames[:] = sorted(d for d in dirnames if d not in WALK_SKIP_DIRS)
        for name in dirnames + filenames:
            yield name if rel_dir == "." else rel_dir + "/" + name


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


# ---------------------------------------------------------------------------------------------
# checks; each returns (list of violation strings, summary string)


def check_gpl(root):
    files, _ = git_index(root)
    violations, scanned = [], 0
    for rel in files:
        if rel in GPL_EXCLUDED_FILES or rel.startswith(GPL_EXCLUDED_PREFIXES):
            continue
        text = read_text(root / rel)
        if text is None:  # tracked but deleted in the working tree
            continue
        scanned += 1
        for m in GPL_TITLE.finditer(text):
            snippet = " ".join(m.group(0).split())
            violations.append("%s:%d: licence title '%s'" % (rel, line_of(text, m.start()), snippet))
    return violations, "%d tracked first-party files scanned" % scanned


def forbidden_in(text):
    low = text.lower()
    return [(name, m) for name, rx in FORBIDDEN_SOURCES for m in rx.finditer(low)]


def check_sources(root):
    files, links = git_index(root)
    violations = []
    seen = set()
    # 1. the tree: tracked paths, submodule paths and everything on disk
    for rel in list(files) + list(links) + list(walk_paths(root)):
        for comp in rel.split("/"):
            for name, _ in forbidden_in(comp):
                key = (rel, name)
                if key not in seen:
                    seen.add(key)
                    violations.append("%s: path names %s (GPL-3, R-L2: never in the tree)" % (rel, name))
    # 2. what CI and the build fetch: .gitmodules, .github/** (tracked or not), fetch-capable files
    fetch = set()
    for rel in files:
        p = pathlib.PurePosixPath(rel)
        if rel.startswith(GPL_EXCLUDED_PREFIXES[:2]):
            continue
        if p.name in FETCH_FILE_NAMES or p.name.endswith(FETCH_FILE_SUFFIXES) or rel.startswith(".github/"):
            fetch.add(rel)
    if (root / ".gitmodules").is_file():
        fetch.add(".gitmodules")
    gh = root / ".github"
    if gh.is_dir():
        for f in walk_files(gh):
            fetch.add(f.relative_to(root).as_posix())
    for rel in sorted(fetch):
        text = read_text(root / rel)
        if text is None:
            continue
        for name, m in forbidden_in(text):
            violations.append("%s:%d: names %s (GPL-3, R-L2: never fetched or used by CI)"
                              % (rel, line_of(text, m.start()), name))
    return violations, "%d paths and %d fetch-capable files checked" % (
        len(files) + len(links), len(fetch))


def md_sections(text):
    """{heading text: [lines]} for '## ' sections (deeper headings stay inside their section)."""
    sections, current = {}, None
    for line in text.splitlines():
        m = re.match(r"^##\s+(.*?)\s*#*\s*$", line)
        if m and not line.startswith("###"):
            current = m.group(1)
            sections[current] = []
        elif current is not None:
            sections[current].append(line)
    return sections


def table_row_ok(lines, component, licence):
    name_rx = re.compile(r"(?<![A-Za-z0-9])" + re.escape(component) + r"(?![A-Za-z0-9])")
    for line in lines:
        if not line.lstrip().startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if cells and name_rx.search(cells[0]) and licence in line:
            return True
    return False


def pamplejuce_referenced(root):
    files, _ = git_index(root)
    for rel in files:
        if rel in GPL_EXCLUDED_FILES or rel.startswith(GPL_EXCLUDED_PREFIXES) or rel == "tools/check_licences.py":
            continue
        if is_binary(root / rel):
            continue
        text = read_text(root / rel)
        if text and "pamplejuce" in text.lower():
            return True
    return False


def check_docs(root):
    path = root / LICENSES_MD
    if not path.is_file():
        return ["%s is missing (R-L4)" % LICENSES_MD], "no file"
    sections = md_sections(read_text(path) or "")
    shipped = [h for h in sections if "shipped" in h.lower()]
    tools = [h for h in sections if "build and test tools" in h.lower()]
    violations = []
    if not shipped:
        violations.append("%s: no '## ... Shipped ...' section" % LICENSES_MD)
    if not tools:
        violations.append("%s: no '## Build and test tools ...' section (R-L4: tools listed separately)"
                          % LICENSES_MD)
    shipped_lines = [l for h in shipped for l in sections[h]]
    tool_lines = [l for h in tools for l in sections[h]]
    required_tools = list(TOOLS_REQUIRED)
    if pamplejuce_referenced(root):
        required_tools.append(OPTIONAL_TOOL_IF_REFERENCED)
    for comp, lic in SHIPPED_REQUIRED:
        if shipped and not table_row_ok(shipped_lines, comp, lic):
            violations.append("%s: shipped section has no table row for %s with licence %s" % (LICENSES_MD, comp, lic))
    for comp, lic in required_tools:
        if tools and not table_row_ok(tool_lines, comp, lic):
            violations.append("%s: tools section has no table row for %s with licence %s" % (LICENSES_MD, comp, lic))
    return violations, "%d shipped and %d tool entries required" % (len(SHIPPED_REQUIRED), len(required_tools))


def branding_hits(text):
    hits = []
    for no, line in enumerate(text.splitlines(), 1):
        stripped = line
        for allowed in BRANDING_ALLOWED:
            stripped = stripped.replace(allowed, "")
        for m in BRANDING_PATTERN.finditer(stripped):
            hits.append((no, m.group(0), line.strip()))
    return hits


def check_branding(root):
    violations, scanned, missing = [], 0, []
    for d in BRANDING_DIRS:
        base = root / d
        if not base.is_dir():
            missing.append(d)
            continue
        for f in walk_files(base):
            if is_binary(f):
                continue
            text = read_text(f)
            if text is None:
                continue
            scanned += 1
            for no, word, line in branding_hits(text):
                violations.append("%s:%d: '%s' outside the R-L5 texts: %s"
                                  % (f.relative_to(root).as_posix(), no, word, line[:120]))
    summary = "%d files in %s scanned" % (scanned, ", ".join(BRANDING_DIRS))
    if missing:
        summary += " (%s not present)" % ", ".join(missing)
    return violations, summary


def metadata_text(path):
    if path.name == "Info.plist":
        try:
            with open(path, "rb") as f:
                return repr(plistlib.load(f))
        except Exception:  # not a parseable plist: scan the raw text
            pass
    return read_text(path)


def check_metadata(dirs):
    violations, found = [], []
    for d in dirs:
        base = pathlib.Path(d).resolve()
        if not base.is_dir():
            violations.append("%s: metadata directory does not exist" % d)
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [x for x in dirnames if x not in {".git", "node_modules"}]
            for name in filenames:
                if name in METADATA_NAMES or name.endswith(METADATA_SUFFIXES):
                    p = pathlib.Path(dirpath) / name
                    found.append(p)
                    text = metadata_text(p)
                    for no, word, line in branding_hits(text or ""):
                        violations.append("%s:%d: '%s' in built plugin metadata: %s" % (p, no, word, line[:120]))
    if dirs and not found:
        violations.append("no built plugin metadata (Info.plist, moduleinfo.json, *.rc) under %s; build the plugin "
                          "first" % ", ".join(dirs))
    return violations, "%d metadata files scanned" % len(found)


CHECKS = ["gpl", "sources", "docs", "branding", "metadata"]


def run_checks(root, names, metadata_dirs, quiet=False):
    failed = False
    for name in names:
        if name == "metadata":
            if not metadata_dirs:
                continue
            violations, summary = check_metadata(metadata_dirs)
        else:
            violations, summary = {"gpl": check_gpl, "sources": check_sources, "docs": check_docs,
                                   "branding": check_branding}[name](root)
        status = "FAIL" if violations else "ok"
        if not quiet:
            print("licence check %-8s %s (%s)" % (name, status, summary))
            for v in violations:
                print("  " + v)
        failed = failed or bool(violations)
    return 1 if failed else 0


# ---------------------------------------------------------------------------------------------
# self-test (AC-L1.1)


def _copy_tracked_tree(src, dst):
    """Copies the tracked files plus the untracked, not ignored ones (work not yet committed)."""
    files, _ = git_index(src)
    others = subprocess.run(["git", "-C", str(src), "ls-files", "--others", "--exclude-standard", "-z"],
                            check=True, stdout=subprocess.PIPE).stdout.decode("utf-8", "surrogateescape")
    rels = sorted(set(files) | {p for p in others.split("\0") if p})
    copied = 0
    for rel in rels:
        s = src / rel
        if not s.is_file() or s.is_symlink():
            continue
        d = dst / rel
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(s, d)
        copied += 1
    subprocess.run(["git", "init", "-q", str(dst)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _git_add(dst)
    return copied


def _git_add(repo):
    subprocess.run(["git", "-C", str(repo), "add", "-A", "-f", "."], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _run_cli(repo, *args):
    p = subprocess.run([sys.executable, str(pathlib.Path(__file__).resolve()), "--root", str(repo)] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode(errors="replace")


def self_test(root):
    gpl_title = "GNU " + "General Public " + "License"
    agpl_title = "GNU " + "Affero General Public " + "License"
    results = []

    def expect(label, code, want, out, needle=None):
        ok = code == want and (needle is None or needle in out)
        results.append(ok)
        print("self-test %-62s %s" % (label, "ok" if ok else "FAILED (exit %d, wanted %d)" % (code, want)))
        if not ok:
            print("    " + out.strip().replace("\n", "\n    "))

    code, out = _run_cli(root, "--check", "gpl")
    expect("the repository passes the GPL title check", code, 0, out)

    with tempfile.TemporaryDirectory(prefix="vjn6-licence-selftest-") as tmp:
        repo = pathlib.Path(tmp) / "repo"
        repo.mkdir()
        n = _copy_tracked_tree(root, repo)
        print("self-test copied %d files (tracked and not yet committed) to %s" % (n, repo))

        code, out = _run_cli(repo, "--check", "gpl")
        expect("the copy passes before planting", code, 0, out)

        # Plant the GNU GPL title in a first-party file (appended to a tracked source file).
        target = repo / "CMakeLists.txt"
        original = target.read_bytes()
        target.write_bytes(original + ("\n# Licensed under the %s, version 3\n" % gpl_title).encode())
        code, out = _run_cli(repo, "--check", "gpl")
        expect("planted GPL title in CMakeLists.txt fails", code, 1, out, "CMakeLists.txt:")
        target.write_bytes(original)

        # A new first-party file, title split over comment lines.
        planted = repo / "core" / "planted_selftest.cpp"
        planted.parent.mkdir(parents=True, exist_ok=True)
        planted.write_text("// This file is part of X.\n// GNU\n//   General Public\n// License v3\n", encoding="utf-8")
        _git_add(repo)
        code, out = _run_cli(repo, "--check", "gpl")
        expect("planted GPL title split over comment lines fails", code, 1, out, "planted_selftest.cpp:2")
        planted.write_text("/* " + agpl_title + " */\n", encoding="utf-8")
        code, out = _run_cli(repo, "--check", "gpl")
        expect("planted AGPL title fails", code, 1, out, "planted_selftest.cpp:1")
        planted.unlink()
        _git_add(repo)

        # The excluded locations never trigger the check.
        for rel in ("docs/LICENSES.md", "SPEC.md", "KICKOFF.md", "third_party/JUCE/LICENSE.md",
                    "third_party/Catch2/LICENSE.txt"):
            p = repo / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            with open(p, "a", encoding="utf-8") as f:
                f.write("\n%s\n%s\n" % (gpl_title, agpl_title))
        _git_add(repo)
        code, out = _run_cli(repo, "--check", "gpl")
        expect("titles in docs/, SPEC.md, KICKOFF.md, submodules pass", code, 0, out)

        # An untracked file is not part of the check (git-tracked files only).
        (repo / "untracked.txt").write_text(gpl_title + "\n", encoding="utf-8")
        code, out = _run_cli(repo, "--check", "gpl")
        expect("an untracked file is ignored", code, 0, out)
        (repo / "untracked.txt").unlink()

        # sources: a submodule URL or a CI clone of a GPL-3 number source fails.
        gm = repo / ".gitmodules"
        gm_orig = gm.read_bytes() if gm.exists() else b""
        name = "juno" + "x"
        gm.write_bytes(gm_orig + ('[submodule "x"]\n\tpath = third_party/x\n\turl = https://example.invalid/%s.git\n'
                                  % name).encode())
        code, out = _run_cli(repo, "--check", "sources")
        expect("a .gitmodules entry for a GPL-3 source fails", code, 1, out, ".gitmodules:")
        gm.write_bytes(gm_orig)
        wf = repo / ".github" / "workflows" / "planted.yml"
        wf.parent.mkdir(parents=True, exist_ok=True)
        wf.write_text("steps:\n  - run: git clone https://example.invalid/%s\n" % ("He" + "ra"), encoding="utf-8")
        code, out = _run_cli(repo, "--check", "sources")
        expect("a CI step fetching a GPL-3 source fails", code, 1, out, "planted.yml:2")
        wf.unlink()
        d = repo / "third_party" / ("kr" + "106-copy")
        d.mkdir(parents=True)
        code, out = _run_cli(repo, "--check", "sources")
        expect("a GPL-3 source directory in the tree fails", code, 1, out)
        d.rmdir()

        # docs: a missing R-L4 row fails.
        lic = repo / LICENSES_MD
        if lic.exists():
            lic_orig = lic.read_text(encoding="utf-8")
            lic.write_text("\n".join(l for l in lic_orig.splitlines() if "Oswald" not in l) + "\n", encoding="utf-8")
            code, out = _run_cli(repo, "--check", "docs")
            expect("docs/LICENSES.md without the Oswald row fails", code, 1, out, "Oswald")
            lic.write_text(lic_orig, encoding="utf-8")

        # branding: only the two R-L5 texts may name the hardware.
        f = repo / "ui" / "src" / "planted_selftest.ts"
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text("export const a = '%s';\nexport const b = '%s';\n" % BRANDING_ALLOWED, encoding="utf-8")
        code, out = _run_cli(repo, "--check", "branding")
        expect("the two R-L5 texts pass", code, 0, out)
        f.write_text("export const title = 'VJN6 for ' + 'JUNO' + ' fans';\n", encoding="utf-8")
        code, out = _run_cli(repo, "--check", "branding")
        expect("a brand name in ui/src fails", code, 1, out, "planted_selftest.ts:1")
        meta = pathlib.Path(tmp) / "build" / "VJN6.vst3" / "Contents" / "Resources"
        meta.mkdir(parents=True)
        (meta / "moduleinfo.json").write_text('{"Name": "VJN6", "Vendor": "Ro' 'land"}\n', encoding="utf-8")
        code, out = _run_cli(repo, "--check", "metadata", "--metadata", str(pathlib.Path(tmp) / "build"))
        expect("a brand name in built plugin metadata fails", code, 1, out, "moduleinfo.json")

    print("self-test %s (%d/%d)" % ("passed" if all(results) else "FAILED", sum(results), len(results)))
    return 0 if all(results) else 1


# ---------------------------------------------------------------------------------------------


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=str(ROOT), help="repository root (default: this checkout)")
    ap.add_argument("--check", action="append", choices=CHECKS,
                    help="run only this check (repeatable); default: all, metadata only with --metadata")
    ap.add_argument("--metadata", action="append", default=[], metavar="DIR",
                    help="build directory whose plugin metadata (Info.plist, moduleinfo.json, *.rc) is scanned")
    ap.add_argument("--self-test", action="store_true", help="AC-L1.1: plant licence titles in a temporary copy")
    args = ap.parse_args(argv)
    root = pathlib.Path(args.root).resolve()
    try:
        if args.self_test:
            return self_test(root)
        names = list(args.check or CHECKS)
        if "metadata" in names and not args.metadata:
            if args.check:
                raise Usage("--check metadata needs --metadata DIR")
        elif args.metadata and "metadata" not in names:
            names.append("metadata")
        return run_checks(root, names, args.metadata)
    except Usage as e:
        print("check_licences: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
