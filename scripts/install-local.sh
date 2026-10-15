#!/bin/bash
# Build a release binary, install it to ~/.local/bin and restart borders.
#
# Usage: scripts/install-local.sh
#   SDKROOT=/path/to/MacOSX.sdk scripts/install-local.sh   # force an SDK

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
DEST_DIR="$HOME/.local/bin"
DEST="$DEST_DIR/borders"
CONFIG="$HOME/.config/borders/bordersrc"
LOG="$HOME/Library/Logs/borders.log"
SDK_DIR="/Library/Developer/CommandLineTools/SDKs"

info() { printf '==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

build() {
  rm -f "$ROOT/bin/borders"
  make -C "$ROOT" bin/borders >/dev/null
}

info "building release binary"
if [[ -n "${SDKROOT:-}" ]]; then
  build || die "build failed with SDKROOT=$SDKROOT"
elif ! build 2>/dev/null; then
  info "default SDK failed"
  # Newer SDKs can fail to link with older toolchains; fall back to the
  # newest versioned SDK that works.
  built=0
  for sdk in $(ls -d "$SDK_DIR"/MacOSX[0-9]*.*.sdk 2>/dev/null | sort -V -r); do
    info "trying $(basename "$sdk")"
    if SDKROOT="$sdk" build 2>/dev/null; then
      built=1
      break
    fi
  done
  [[ $built -eq 1 ]] || die "build failed with every available SDK"
fi

info "installing to $DEST"
mkdir -p "$DEST_DIR"
# Replace the file instead of overwriting it in place: macOS caches code
# signatures per inode and SIGKILLs a binary rewritten underneath it.
rm -f "$DEST"
cp "$ROOT/bin/borders" "$DEST"
codesign -v "$DEST" || die "code signature check failed: $DEST"

info "restarting borders"
pkill -f "bash $CONFIG" 2>/dev/null || true
pkill -x borders 2>/dev/null || true
for _ in 1 2 3 4 5 6 7 8 9 10; do
  pgrep -x borders >/dev/null || break
  sleep 0.2
done
pgrep -x borders >/dev/null && die "old borders process did not exit"

if [[ -x "$CONFIG" || -f "$CONFIG" ]]; then
  (PATH="$DEST_DIR:$PATH" nohup bash "$CONFIG" >>"$LOG" 2>&1 &)
else
  (nohup "$DEST" >>"$LOG" 2>&1 &)
fi

sleep 1.5
pid="$(pgrep -x borders | head -1 || true)"
[[ -n "$pid" ]] || die "borders is not running, see $LOG"
running="$(lsof -p "$pid" 2>/dev/null | awk '$4 == "txt" { print $9; exit }')"
[[ "$running" == "$DEST" ]] || die "borders is running from $running, expected $DEST"

info "borders is running (pid $pid) from $DEST"
