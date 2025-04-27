#!/usr/bin/env bash
# cleanup-ipc.sh: Clean up System V IPC objects and kill stray ipc-pipeline processes
set -euo pipefail
IFS=$'\n\t'

# Default binary name (can be overridden with -b)
BINARY_NAME="ipc-pipeline"

usage() {
  cat <<EOF
Usage: $0 [-b binary_name] [-h]
Options:
  -b BINARY_NAME    Name of the IPC program binary (default: $BINARY_NAME)
  -h                Show this help message.
EOF
}

while getopts ":b:h" opt; do
  case $opt in
    b) BINARY_NAME="$OPTARG" ;;
    h) usage; exit 0 ;;
    \?) echo "Invalid option: -$OPTARG" >&2; usage; exit 1 ;;
    :) echo "Option -$OPTARG requires an argument." >&2; usage; exit 1 ;;
  esac
done

cleanup_ipc() {
  local flag="$1" name="$2"
  echo "Removing $name..."
  ipcs "-$flag" | awk '/0x[0-9a-f]+/ {print $2}' | while read -r id; do
    if ipcrm "-$flag" "$id"; then
      echo "  • Removed $name ID $id"
    else
      echo "  • Failed to remove $name ID $id" >&2
    fi
  done
}

kill_procs() {
  echo "Killing processes named '$BINARY_NAME'..."
  if pids=$(pgrep -x "$BINARY_NAME" || true) && [[ -n "$pids" ]]; then
    echo "  • Found PIDs: $pids"
    kill -TERM $pids
    echo "  • Sent SIGTERM"
  else
    echo "  • No running '$BINARY_NAME' processes found."
  fi
}

main() {
  # must be root to remove IPC
  if [[ $EUID -ne 0 ]]; then
    echo "Error: this script must be run as root." >&2
    exit 1
  fi

  cleanup_ipc m "shared memory"
  cleanup_ipc q "message queues"
  cleanup_ipc s "semaphores"
  kill_procs

  echo "Cleanup complete."
}

main "$@"
