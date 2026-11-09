#!/bin/bash
# Packages the Linux build as VJN6-<version>-Linux-x64.tar.gz (DECISIONS.md "Releases").
# Usage: tools/package/linux.sh <artefacts-dir> <version> <out-dir>
#   <artefacts-dir> is plugin/VJN6_artefacts/Release in the build tree, after
#   tools/package/notices.py --install.
set -euo pipefail
art="${1:?usage: linux.sh <artefacts-dir> <version> <out-dir>}"
version="${2:?version}"
out="${3:?out dir}"
name="VJN6-$version-Linux-x64"
for p in "$art/Standalone/VJN6" "$art/Standalone/NOTICES.txt" "$art/VST3/VJN6.vst3/Contents/Resources/NOTICES.txt"; do
  [[ -e "$p" ]] || { echo "linux.sh: missing $p (build, then run tools/package/notices.py --install)" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/$name" "$out"
cp "$art/Standalone/VJN6" "$art/Standalone/NOTICES.txt" "$work/$name/"
cp -R "$art/VST3/VJN6.vst3" "$work/$name/"
cat > "$work/$name/INSTALL.txt" <<EOF
VJN6 $version for Linux x64

Built and tested on Ubuntu 24.04. Other distributions need glibc 2.39 and
libstdc++ from GCC 13 or newer. The interface needs WebKitGTK 4.1:
    sudo apt install libwebkit2gtk-4.1-0

Standalone app: run ./VJN6
VST3 plugin:    copy VJN6.vst3 into ~/.vst3 (for you) or /usr/lib/vst3 (for everyone)
    mkdir -p ~/.vst3 && cp -R VJN6.vst3 ~/.vst3/

NOTICES.txt holds the licences of the third-party code built into VJN6.
https://github.com/wiz4rd-de/VJN6
EOF
owner=()
tar --version 2>/dev/null | grep -q GNU && owner=(--owner=0 --group=0)
tar -C "$work" ${owner[@]+"${owner[@]}"} -czf "$out/$name.tar.gz" "$name"
echo "linux.sh: wrote $out/$name.tar.gz"
