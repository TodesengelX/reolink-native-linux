#!/usr/bin/env bash
# Decode/render benchmark: run an N-pane grid of looping test streams for a fixed
# duration under hardware and software decode, and report CPU time for each.
#
# Usage: tools/bench.sh [panes] [seconds]
#   panes:   number of simultaneous streams (default 9)
#   seconds: run duration (default 20)
#
# Requires a built ./build/reolink-client and ffmpeg. Uses a throwaway database
# so it never touches the user's real device list.
set -euo pipefail

PANES="${1:-9}"
SECS="${2:-20}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/build/reolink-client"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -x "$APP" ] || { echo "build first: cmake --build build" >&2; exit 1; }

echo "Generating test clip…"
ffmpeg -v error -y -f lavfi -i "testsrc2=size=1280x720:rate=25" -t 10 \
    -c:v libx264 -pix_fmt yuv420p "$WORK/clip.mp4"

# Seed N stream devices into a throwaway database.
DB="$WORK/reolink.db"
sqlite3 "$DB" "CREATE TABLE schema_version(version INTEGER NOT NULL);
INSERT INTO schema_version VALUES(1);
CREATE TABLE hosts(id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL,
  name TEXT NOT NULL, addr TEXT NOT NULL, port INTEGER NOT NULL DEFAULT 443,
  https INTEGER NOT NULL DEFAULT 1, username TEXT NOT NULL DEFAULT '',
  model TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL DEFAULT (datetime('now')));"
for i in $(seq 1 "$PANES"); do
    sqlite3 "$DB" "INSERT INTO hosts(kind,name,addr,port,https) \
        VALUES('stream','cam$i','$WORK/clip.mp4',0,0);"
done

# Point the app at the throwaway DB via XDG_DATA_HOME.
export XDG_DATA_HOME="$WORK/xdg"
mkdir -p "$XDG_DATA_HOME/reolink-linux/reolink-client"
cp "$DB" "$XDG_DATA_HOME/reolink-linux/reolink-client/reolink.db"

run() {
    local label="$1" hw="$2"
    echo "=== $label ($PANES panes, ${SECS}s) ==="
    # Launch the app directly (not under timeout) so we can sample its own
    # /proc/<pid>/stat — fields 14/15 (utime+stime) include all decode threads.
    RL_HWDECODE="$hw" QT_FORCE_STDERR_LOGGING=1 \
        "$APP" >/dev/null 2>"$WORK/log_$label.txt" &
    local pid=$!
    sleep 2
    local start; start=$(awk '{print $14+$15}' "/proc/$pid/stat" 2>/dev/null || echo 0)
    sleep "$((SECS - 3))"
    local end; end=$(awk '{print $14+$15}' "/proc/$pid/stat" 2>/dev/null || echo "$start")
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    local ticks=$((end - start))
    local hz; hz=$(getconf CLK_TCK)
    local window=$((SECS - 3))
    awk -v t="$ticks" -v hz="$hz" -v w="$window" 'BEGIN{
        cpu=t/hz; printf "  CPU: %.1f cpu-seconds over %ds (%.0f%% of one core)\n", cpu, w, 100*cpu/w}'
    echo "  decode path: $(grep -o '\[hw\]\|\[sw\]' "$WORK/log_$label.txt" | head -1)"
}

run hardware 1
run software 0
echo "(Hardware decode offloads to the GPU; expect materially lower CPU for H.264/H.265.)"
