#!/usr/bin/env bash
#
# Launcher for nimbus. Installed on PATH as `nimbus`.
#
# The wrapper exists to do two things the binary cannot do for itself:
#
#   1. build when the binary is missing or older than the sources, so a fresh clone
#      runs on the first invocation rather than failing obscurely;
#   2. pass arguments through, so `nimbus --login` and `nimbus --verbose` reach the
#      app.
#
# It deliberately does not handle credentials. The session lives in
# ~/.config/nimbus/session, written by the app at mode 0600, and read by it. An
# earlier version of this script located a token file and injected it into the
# environment; that path is gone, and nothing here should reintroduce one.

set -euo pipefail

script_path="$(readlink -f "${BASH_SOURCE[0]}")"
repo_root="$(dirname "$(dirname "$script_path")")"
build_dir="${NIMBUS_BUILD_DIR:-$repo_root/build}"
binary="$build_dir/bin/nimbus"

# --help and --version should answer instantly and never trigger a build, since
# neither needs the binary to have been compiled.
for arg in "$@"; do
  case "$arg" in
    -h | --help | --help-all | -v | --version)
      if [[ ! -x "$binary" ]]; then
        echo "nimbus: not built yet. Run 'nimbus' once to build it." >&2
        exit 1
      fi
      exec "$binary" "$@"
      ;;
  esac
done

if [[ ! -x "$binary" ]]; then
  needs_build=1
else
  # -newer on the binary against the newest source file. Cheap enough to run on
  # every launch, which is the only way it stays correct -- a timestamp baked in at
  # install time goes stale the moment anyone edits a file.
  newest_source="$(find "$repo_root/src" "$repo_root/CMakeLists.txt" \
    -type f -newer "$binary" -print -quit 2>/dev/null || true)"
  [[ -n "$newest_source" ]] && needs_build=1
fi

if [[ ${needs_build:-0} -eq 1 ]]; then
  echo "nimbus: building..." >&2
  if command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc)"
  else
    jobs=4
  fi
  if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    cmake -S "$repo_root" -B "$build_dir" -DNIMBUS_BUILD_TESTS=OFF >/dev/null
  fi
  cmake --build "$build_dir" -j"$jobs" >&2
fi

# A missing session is not an error here. The client opens its sign-in form, and a
# launcher that refused to start without a credential made that form unreachable.
exec "$binary" "$@"
