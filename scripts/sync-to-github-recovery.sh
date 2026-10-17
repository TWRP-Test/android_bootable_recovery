#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: sync-to-github-recovery.sh [options]

Synchronize this recovery tree to the local GitHub checkout.

Options:
  --source DIR       Source recovery tree (default: this script's parent)
  --target DIR       Target checkout (default: ~/Documents/GitHub/android_bootable_recovery)
  --dry-run           Show changes without copying files
  --delete            Also remove files absent from the source tree
  --allow-dirty       Permit synchronization into a dirty target Git tree
  -h, --help          Show this help
EOF
}

# Keep the default source explicit because this script is also copied into the
# target checkout. Inferring the source from the script location would make a
# later invocation from that checkout synchronize it to itself.
source_dir="/home/yukonga/Documents/twrp-16.0/bootable/recovery"
target_dir="/home/yukonga/Documents/GitHub/android_bootable_recovery"
dry_run=0
delete_missing=0
allow_dirty=0

while (($# > 0)); do
  case "$1" in
    --source)
      (($# >= 2)) || { echo "--source requires a directory" >&2; exit 2; }
      source_dir="$2"
      shift 2
      ;;
    --target)
      (($# >= 2)) || { echo "--target requires a directory" >&2; exit 2; }
      target_dir="$2"
      shift 2
      ;;
    --dry-run)
      dry_run=1
      shift
      ;;
    --delete)
      delete_missing=1
      shift
      ;;
    --allow-dirty)
      allow_dirty=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

source_dir="$(cd -- "$source_dir" && pwd)"
target_dir="$(cd -- "$target_dir" && pwd)"

if [[ ! -d "$source_dir/.git" ]]; then
  echo "Source is not a Git recovery worktree: $source_dir" >&2
  exit 1
fi
if [[ ! -d "$target_dir/.git" ]]; then
  echo "Target is not a Git checkout: $target_dir" >&2
  exit 1
fi

if (( ! allow_dirty )); then
  if [[ -n "$(git -C "$target_dir" status --porcelain --untracked-files=all)" ]]; then
    echo "Target Git checkout has uncommitted changes: $target_dir" >&2
    echo "Commit/stash them or rerun with --allow-dirty." >&2
    exit 1
  fi
fi

rsync_args=(
  --archive
  --checksum
  --human-readable
  --itemize-changes
  --no-times
  --no-owner
  --no-group
  --exclude=.git
  --exclude=.vscode/
)
if (( dry_run )); then
  rsync_args+=(--dry-run)
fi
if (( delete_missing )); then
  rsync_args+=(--delete-delay)
fi

echo "Syncing: $source_dir/"
echo "To:     $target_dir/"
if (( delete_missing )); then
  echo "Mode:   update and delete files absent from source"
else
  echo "Mode:   update and add only (use --delete to remove stale files)"
fi

rsync "${rsync_args[@]}" "$source_dir/" "$target_dir/"
