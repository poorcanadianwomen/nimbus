#!/usr/bin/env bash
#
# Build and test executor for nimbus.
#
# The runtime launcher (scripts/nimbus-launch.sh, installed on PATH as `nimbus`)
# answers "start the app". This answers the other half: "is the tree in a state
# worth starting". It exists because those two jobs want opposite defaults.
#
# The launcher builds with NIMBUS_BUILD_TESTS=OFF, because a user starting the
# app should never wait on a test build and should never have a test failure
# between them and their client. That is the right trade for launching and the
# wrong one for developing, where a green suite is the thing being asked for.
# Having one script try to serve both produced the two failure modes this
# replaces: a tree that built but could not be tested, and a tree that was tested
# by a different build directory than the one being run.
#
# So: exactly one build directory, always configured with tests on.

set -euo pipefail

script_path="$(readlink -f "${BASH_SOURCE[0]}")"
repo_root="$(dirname "$(dirname "$script_path")")"
build_dir="${NIMBUS_BUILD_DIR:-$repo_root/build}"
bin_dir="$build_dir/bin"

# The seven suites, in the order they fail most usefully: core, then the ones
# that need a platform plugin. Kept as a list rather than a glob so a suite that
# silently stops being built is a visible edit here, not a missing line.
SUITES=(nimbus_tests nimbus_format_tests nimbus_layout_tests nimbus_shell_tests nimbus_login_tests nimbus_icon_tests nimbus_motion_tests)

jobs="$(nproc 2>/dev/null || echo 4)"

die() { echo "executor: $*" >&2; exit 1; }

# Human-readable step banner, so a long clean build is legible in a terminal
# that is also showing compiler output.
step() { printf '\n\033[1m== %s\033[0m\n' "$*" >&2; }

configure() {
  step "configure ($build_dir)"
  # A stale cache from a different generator or source path is worse than no
  # cache, and CMake will happily reuse one it should not. NIMBUS_BUILD_TESTS is
  # forced on: this script exists to be the thing that runs the tests.
  cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE="${NIMBUS_BUILD_TYPE:-RelWithDebInfo}" \
    -DNIMBUS_BUILD_TESTS=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >&2
}

build() {
  configure
  step "build (-j$jobs)"
  cmake --build "$build_dir" -j"$jobs" >&2
}

# Runs every suite and reports one aggregate verdict. Returns non-zero if any
# suite failed, so `executor.sh test` is usable as a CI gate or a git hook.
run_tests() {
  step "test"
  local failed=0 total=0 passed=0
  local -a failed_suites=()

  for suite in "${SUITES[@]}"; do
    local bin="$bin_dir/$suite"
    if [[ ! -x "$bin" ]]; then
      echo "  MISSING  $suite (not built -- is NIMBUS_BUILD_TESTS=ON?)" >&2
      failed=$((failed + 1))
      failed_suites+=("$suite")
      continue
    fi

    # Offscreen, always. Two of these link QtWidgets and a headless box has no
    # display to connect to; a developer on a desktop would otherwise get a
    # different code path from CI without being told.
    local out
    if out="$(QT_QPA_PLATFORM=offscreen "$bin" 2>&1)"; then
      local line
      # QtTest outputs: "Totals: 13 passed, 0 failed, 0 skipped, 0 blacklisted, 3ms"
      line="$(printf '%s\n' "$out" | grep -E '^Totals:' | tail -1)"
      printf '  \033[32mPASS\033[0m  %-22s %s\n' "$suite" "${line:-ok}" >&2
    else
      echo "  \033[31mFAIL\033[0m  $suite" >&2
      printf '%s\n' "$out" | sed 's/^/        /' | tail -40 >&2
      failed=$((failed + 1))
      failed_suites+=("$suite")
      continue
    fi

    # Accumulate the totals so the summary is a real number rather than a count
    # of suites, which would hide a suite that quietly stopped checking anything.
    local n
    n="$(printf '%s\n' "$out" | sed -n 's/^Totals: \([0-9]\+\) passed.*/\1/p' | tail -1)"
    total=$((total + ${n:-0}))
    passed=$((passed + ${n:-0}))
  done

  step "summary"
  if [[ $failed -gt 0 ]]; then
    echo "  $passed/$total checks passed; ${failed} suite(s) failed:" >&2
    printf '        %s\n' "${failed_suites[@]}" >&2
    return 1
  fi
  echo "  $passed/$total checks passed across ${#SUITES[@]} suites" >&2
  return 0
}

clean() {
  step "clean"
  # Both trees, not just the default. A stale build-asan is how a session ended
  # up looking at an hours-old binary that none of the current sources produced:
  # the process was real, the code was not.
  rm -rf "$repo_root/build" "$repo_root/build-asan"
  echo "  removed build/ and build-asan/" >&2
}

usage() {
  cat <<'EOF'
usage: executor.sh [clean|build|test|run|all] [--jobs N] [--keep]

  clean   remove the build directories
  build   configure and compile, with tests enabled
  test    run all four suites offscreen and report one verdict
  run     build, then launch the app via the launcher (needs a token)
  all     clean, then build, then test          (default)

  --jobs N   parallelism, default nproc
  --keep     with clean: keep the tree instead of removing it
EOF
}

command="all"
keep=0
for arg in "$@"; do
  case "$arg" in
    clean | build | test | run | all) command="$arg" ;;
    --jobs)
      shift
      jobs="${1:-4}"
      ;;
    --jobs=*) jobs="${arg#--jobs=}" ;;
    --keep) keep=1 ;;
    -h | --help)
      usage
      exit 0
      ;;
    *) die "unknown argument '$arg' (try --help)" ;;
  esac
  shift || true
done

# `run` and `test` both need a tree, but neither should nuke a working build
# just because it was invoked without arguments -- that is what `--keep` and the
# default command split are for.
case "$command" in
  clean)
    if [[ $keep -eq 1 ]]; then
      echo "executor: --keep with clean does nothing; dropping the flag" >&2
      exit 0
    fi
    clean
    ;;
  build) build ;;
  test)
    build
    run_tests
    ;;
  run)
    build
    exec "$repo_root/scripts/nimbus-launch.sh"
    ;;
  all)
    clean
    build
    run_tests
    ;;
esac