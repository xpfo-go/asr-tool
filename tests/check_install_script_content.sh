#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
script_path="$repo_root/install.sh"

assert_eq() {
  local expected="$1"
  local actual="$2"
  if [[ "$expected" != "$actual" ]]; then
    echo "expected: $expected"
    echo "actual:   $actual"
    exit 1
  fi
}

[[ -f "$script_path" ]] || {
  echo "missing install.sh"
  exit 1
}

ASR_TOOL_INSTALL_SOURCE_ONLY=1 source "$script_path"

assert_eq "asr-tool-macos-arm64" "$(release_asset_name macOS arm64)"
assert_eq "asr-tool-linux-x86_64.zip" "$(release_asset_name Linux x86_64)"
assert_eq "asr-tool-windows-x86_64.zip" "$(release_asset_name Windows x86_64)"
assert_eq "asr-tool" "$(release_binary_name macOS)"
assert_eq "asr-tool.exe" "$(release_binary_name Windows)"

original_home="$HOME"
temp_home="$(mktemp -d "${TMPDIR:-/tmp}/asr-tool-home.XXXXXX")"
trap 'HOME="$original_home"; rm -rf "$temp_home"' EXIT
mkdir -p "$temp_home/.claude/skills" "$temp_home/.openclaw/skills" "$temp_home/.hermes/skills"
HOME="$temp_home"
detected_roots=()
while IFS= read -r detected_root; do
  detected_roots+=("$detected_root")
done < <(detect_skill_roots)
assert_eq "3" "${#detected_roots[@]}"
assert_eq "Claude|$temp_home/.claude/skills" "${detected_roots[0]}"
assert_eq "OpenClaw|$temp_home/.openclaw/skills" "${detected_roots[1]}"
assert_eq "Hermes|$temp_home/.hermes/skills" "${detected_roots[2]}"
selected_targets=()
while IFS= read -r selected_target; do
  selected_targets+=("$selected_target")
done < <(prompt_for_targets)
assert_eq "3" "${#selected_targets[@]}"
assert_eq "$temp_home/.zshrc" "$(choose_shell_rc_file /bin/zsh)"
assert_eq "$temp_home/.bashrc" "$(choose_shell_rc_file /bin/bash)"
ensure_line_in_file "$temp_home/.zshrc" "$PATH_EXPORT_LINE"
rg -F 'export PATH="$HOME/.local/bin:$PATH"' "$temp_home/.zshrc" >/dev/null

rg -n 'install\.sh' "$repo_root/.github/workflows/release-from-artifacts.yml" >/dev/null
rg -n 'curl -fsSL .*install\.sh \| bash' "$repo_root/README.md" >/dev/null
rg -n 'curl -fsSL .*install\.sh \| bash' "$repo_root/docs/README_en.md" >/dev/null
