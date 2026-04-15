#!/usr/bin/env bash
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-build}"
install_dir="$(mktemp -d "${TMPDIR:-/tmp}/asr-tool-install.XXXXXX")"
trap 'rm -rf "$install_dir"' EXIT

exe_suffix=""
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) exe_suffix=".exe" ;;
esac

cmake -S "$source_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER_LAUNCHER= \
  -DCMAKE_CXX_COMPILER_LAUNCHER= \
  -DGGML_CCACHE=OFF >/dev/null
cmake --build "$build_dir" --parallel >/dev/null

expected_build_bin="$(find "$build_dir" -maxdepth 2 -type f -name "asr-tool${exe_suffix}" | head -n 1 || true)"
legacy_build_bin="$(find "$build_dir" -maxdepth 2 -type f -name "asr${exe_suffix}" | head -n 1 || true)"

if [[ -z "$expected_build_bin" ]]; then
  echo "expected build output named asr-tool${exe_suffix}"
  find "$build_dir" -maxdepth 2 -type f | sort
  exit 1
fi

if [[ -n "$legacy_build_bin" ]]; then
  echo "legacy build output should not remain: $legacy_build_bin"
  find "$build_dir" -maxdepth 2 -type f | sort
  exit 1
fi

cmake --install "$build_dir" --prefix "$install_dir" >/dev/null

expected_bin="$install_dir/bin/asr-tool${exe_suffix}"
legacy_bin="$install_dir/bin/asr${exe_suffix}"

if [[ ! -x "$expected_bin" ]]; then
  echo "expected installed binary at $expected_bin"
  if [[ -d "$install_dir/bin" ]]; then
    ls -la "$install_dir/bin"
  else
    echo "install bin directory is missing"
  fi
  exit 1
fi

if [[ -e "$legacy_bin" ]]; then
  echo "legacy binary should not be installed: $legacy_bin"
  ls -la "$install_dir/bin"
  exit 1
fi

version_output="$($expected_bin --version)"
expected_version="$(git -C "$source_dir" describe --tags --dirty --always 2>/dev/null | sed 's/^v//')"
expected_output="asr-tool ${expected_version}"

if [[ "$version_output" != "$expected_output" ]]; then
  echo "unexpected version output: $version_output"
  echo "expected version output: $expected_output"
  exit 1
fi

echo "installed binary name looks correct: $version_output"
