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
ASR_TOOL_INSTALL_SOURCE_ONLY=1 bash < "$script_path" >/dev/null

stdin_home="$(mktemp -d "${TMPDIR:-/tmp}/asr-tool-stdin-home.XXXXXX")"
stdin_status=0
stdin_output="$(HOME="$stdin_home" ASR_TOOL_RELEASE_TAG=vtest bash < "$script_path" 2>&1)" || stdin_status=$?
rm -rf "$stdin_home"
assert_eq "1" "$stdin_status"
[[ "$stdin_output" == *'[INFO] Latest release: vtest'* ]] || {
  echo "stdin execution did not enter main"
  echo "$stdin_output"
  exit 1
}
[[ "$stdin_output" == *'No supported skill roots detected'* ]] || {
  echo "stdin execution did not reach skill-root detection"
  echo "$stdin_output"
  exit 1
}
[[ "$stdin_output" != *'Ensured PATH contains'* ]] || {
  echo "stdin execution continued after target detection failure"
  echo "$stdin_output"
  exit 1
}
[[ "$stdin_output" != *'unbound variable'* ]] || {
  echo "stdin execution hit an unbound variable after target detection failure"
  echo "$stdin_output"
  exit 1
}

legacy_root="$(mktemp -d "${TMPDIR:-/tmp}/asr-tool-legacy.XXXXXX")"
legacy_skill_dir="$legacy_root/asr-tool"
unrelated_dir="$legacy_root/unrelated"
mkdir -p "$legacy_skill_dir/src" "$unrelated_dir"
touch "$legacy_skill_dir/SKILL.md" "$legacy_skill_dir/README.md" "$legacy_skill_dir/CMakeLists.txt" "$legacy_skill_dir/src/main.cpp"
looks_like_legacy_skill_dir "$legacy_skill_dir"
! looks_like_legacy_skill_dir "$unrelated_dir"
legacy_backup_dir="$(backup_legacy_skill_dir "$legacy_skill_dir")"
[[ ! -e "$legacy_skill_dir" ]] || {
  echo "legacy skill dir was not moved away"
  exit 1
}
[[ -f "$legacy_backup_dir/SKILL.md" ]] || {
  echo "legacy skill backup missing expected contents"
  exit 1
}
rm -rf "$legacy_root"

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
