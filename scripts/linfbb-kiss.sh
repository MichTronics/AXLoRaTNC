#!/usr/bin/env bash
set -euo pipefail

# Start/stop LinFBB with AXLoRaTNC in serial KISS mode via Linux AX.25.
#
# Defaults use the system LinFBB configuration in /etc/ax25. Override these from
# the shell if your setup differs, for example:
#
#   AX_SERIAL=/dev/ttyACM0 AXPORT=axlora ./scripts/linfbb-kiss.sh start

FBB_ROOT="${FBB_ROOT:-/}"
FBB_BIN="${FBB_BIN:-/usr/sbin/fbb}"
FBB_CONF="${FBB_CONF:-/etc/ax25/fbb/fbb.conf}"
FBB_LOG="${FBB_LOG:-/var/log/fbb.log}"

AXPORT="${AXPORT:-axlora}"
AX_SERIAL="${AX_SERIAL:-/dev/ttyACM0}"
KISS_TXDELAY="${KISS_TXDELAY:-0}"
KISS_SLOTTIME="${KISS_SLOTTIME:-10}"
KISS_PERSIST="${KISS_PERSIST:-255}"
KISS_FULLDUP="${KISS_FULLDUP:-n}"
KISS_CRC="${KISS_CRC:-1}"

RUNDIR="${RUNDIR:-/tmp/axloratnc-linfbb}"
FBB_PIDFILE="$RUNDIR/fbb.pid"
KISS_PIDFILE="$RUNDIR/kissattach.pid"
KISS_OUTFILE="$RUNDIR/kissattach.out"
AXDEV_FILE="$RUNDIR/axdev"

usage() {
  cat <<EOF
Usage: $0 {start|stop|restart|status}

Environment overrides:
  AX_SERIAL=/dev/ttyACM0           Serial device for AXLoRaTNC
  AXPORT=axlora                    Linux AX.25 port name from /etc/ax25/axports
  AXDEV=ax0                        Optional Linux network device created by kissattach
  KISS_FULLDUP=n                   Half-duplex for LoRa
  KISS_CRC=1                       kissparms CRC type: 1 = none
  FBB_ROOT=$FBB_ROOT
  FBB_BIN=$FBB_BIN
  FBB_CONF=$FBB_CONF

Recommended /etc/ax25/axports line:
  axlora  YOURCALL-1  115200  64  1  AXLoRaTNC LoRa KISS
EOF
}

need_root() {
  if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
    echo "Please run as root, or with sudo." >&2
    exit 1
  fi
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing command: $1" >&2
    exit 1
  }
}

find_serial() {
  if [[ -n "$AX_SERIAL" ]]; then
    echo "$AX_SERIAL"
    return
  fi

  local matches=()
  shopt -s nullglob
  matches=(/dev/serial/by-id/*)
  shopt -u nullglob

  if [[ "${#matches[@]}" -eq 1 ]]; then
    echo "${matches[0]}"
    return
  fi

  echo "Set AX_SERIAL=/dev/ttyACM0 before starting." >&2
  if [[ "${#matches[@]}" -gt 1 ]]; then
    printf 'Available serial devices:\n' >&2
    printf '  %s\n' "${matches[@]}" >&2
  fi
  exit 1
}

pid_alive() {
  local pid="$1"
  [[ -n "$pid" ]] && kill -0 "$pid" >/dev/null 2>&1
}

fbb_processes() {
  pgrep -x fbb 2>/dev/null || true
  pgrep -x xfbbd 2>/dev/null || true
}

fbb_running() {
  [[ -n "$(fbb_processes)" ]]
}

read_pid() {
  local file="$1"
  [[ -f "$file" ]] && sed -n '1p' "$file" || true
}

detect_axdev() {
  if [[ -n "${AXDEV:-}" ]]; then
    echo "$AXDEV"
    return
  fi

  if [[ -f "$AXDEV_FILE" ]]; then
    sed -n '1p' "$AXDEV_FILE"
    return
  fi

  if [[ -f "$KISS_OUTFILE" ]]; then
    local dev
    dev="$(sed -n 's/.*bound to device \([^[:space:]]\+\).*/\1/p' "$KISS_OUTFILE" | tail -n 1)"
    if [[ -n "$dev" ]]; then
      echo "$dev"
      return
    fi
  fi

  ip -o link show 2>/dev/null | awk -F': ' '$2 ~ /^ax[0-9]+(@.*)?$/ { sub(/@.*/, "", $2); print $2; exit }' || true
}

axport_up() {
  local dev
  dev="$(detect_axdev)"
  [[ -n "$dev" ]] && ip link show "$dev" >/dev/null 2>&1
}

check_axports() {
  if [[ ! -f /etc/ax25/axports ]]; then
    echo "Warning: /etc/ax25/axports not found. kissattach needs this file." >&2
    return
  fi
  if ! awk -v port="$AXPORT" '$1 == port { found = 1 } END { exit found ? 0 : 1 }' /etc/ax25/axports; then
    echo "Warning: AXPORT '$AXPORT' is not listed in /etc/ax25/axports." >&2
    echo "Recommended line:" >&2
    echo "  $AXPORT  YOURCALL-1  115200  64  1  AXLoRaTNC LoRa KISS" >&2
  fi
}

start_kiss() {
  need_cmd kissattach
  need_cmd kissparms
  need_cmd ip
  check_axports

  if axport_up; then
    echo "AX.25 port $AXPORT already exists."
  else
    local serial
    serial="$(find_serial)"
    [[ -e "$serial" ]] || {
      echo "Serial device not found: $serial" >&2
      exit 1
    }

    echo "Attaching KISS: $serial -> $AXPORT"
    : >"$KISS_OUTFILE"
    kissattach "$serial" "$AXPORT" >"$KISS_OUTFILE" 2>&1 &
    echo "$!" >"$KISS_PIDFILE"

    local tries=0
    until axport_up; do
      tries=$((tries + 1))
      if [[ "$tries" -gt 30 ]]; then
        echo "kissattach did not create AX.25 port $AXPORT" >&2
        cat "$KISS_OUTFILE" >&2 || true
        exit 1
      fi
      sleep 0.2
    done
  fi

  local axdev
  axdev="$(detect_axdev)"
  if [[ -n "$axdev" ]]; then
    echo "$axdev" >"$AXDEV_FILE"
    echo "AX.25 port $AXPORT bound to network device $axdev"
  fi

  echo "Applying KISS parameters on $AXPORT"
  kissparms -p "$AXPORT" \
    -c "$KISS_CRC" \
    -f "$KISS_FULLDUP" \
    -t "$KISS_TXDELAY" \
    -s "$KISS_SLOTTIME" \
    -r "$KISS_PERSIST"
}

start_fbb() {
  [[ -x "$FBB_BIN" ]] || {
    echo "FBB binary not executable: $FBB_BIN" >&2
    exit 1
  }
  [[ -f "$FBB_CONF" ]] || {
    echo "FBB config not found: $FBB_CONF" >&2
    exit 1
  }

  if pgrep -x fbb >/dev/null 2>&1 || pgrep -x xfbbd >/dev/null 2>&1; then
    echo "FBB already appears to be running."
    return
  fi

  mkdir -p "$(dirname "$FBB_LOG")"
  echo "Starting FBB: $FBB_BIN"
  "$FBB_BIN" -s -l "$FBB_LOG" &
  echo "$!" >"$FBB_PIDFILE"
}

stop_fbb() {
  local pid
  pid="$(read_pid "$FBB_PIDFILE")"
  if pid_alive "$pid"; then
    echo "Stopping FBB pid $pid"
    kill "$pid" || true
  fi

  echo "Stopping FBB frontend and xfbbd daemon"
  pkill -TERM -x fbb 2>/dev/null || true
  pkill -TERM -x xfbbd 2>/dev/null || true

  for _ in 1 2 3 4 5; do
    fbb_running || break
    sleep 1
  done

  if fbb_running; then
    echo "FBB still running; forcing fbb and xfbbd down"
    pkill -KILL -x fbb 2>/dev/null || true
    pkill -KILL -x xfbbd 2>/dev/null || true
  fi

  rm -f "$FBB_PIDFILE"
}

stop_kiss() {
  local axdev
  axdev="$(detect_axdev)"
  if axport_up; then
    echo "Bringing AX.25 network device $axdev down"
    ip link set "$axdev" down 2>/dev/null || true
  fi

  local pid
  pid="$(read_pid "$KISS_PIDFILE")"
  if pid_alive "$pid"; then
    echo "Stopping kissattach pid $pid"
    kill "$pid" || true
  fi

  pkill -TERM -f "kissattach .* ${AXPORT}($| )" 2>/dev/null || true
  sleep 1
  pkill -KILL -f "kissattach .* ${AXPORT}($| )" 2>/dev/null || true
  rm -f "$KISS_PIDFILE" "$AXDEV_FILE" "$KISS_OUTFILE"
}

start_all() {
  need_root
  mkdir -p "$RUNDIR"
  start_kiss
  start_fbb
  status_all
}

stop_all() {
  need_root
  stop_fbb
  stop_kiss
  status_all
}

status_all() {
  echo
  echo "Status:"
  local axdev
  axdev="$(detect_axdev)"
  if axport_up; then
    echo "  AX.25 port $AXPORT: up on ${axdev:-unknown}"
  else
    echo "  AX.25 port $AXPORT: down"
  fi

  if fbb_running; then
    echo "  FBB: running"
    pgrep -a -x fbb 2>/dev/null || true
    pgrep -a -x xfbbd 2>/dev/null || true
  else
    echo "  FBB: stopped"
  fi
}

case "${1:-}" in
  start) start_all ;;
  stop) stop_all ;;
  restart) stop_all; start_all ;;
  status) status_all ;;
  *) usage; exit 1 ;;
esac
