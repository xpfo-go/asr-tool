#!/usr/bin/env bash
set -euo pipefail

REPO_OWNER="xpfo-go"
REPO_NAME="asr-tool"
REPO_URL="https://github.com/${REPO_OWNER}/${REPO_NAME}.git"
LATEST_RELEASE_URL="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/latest"
LATEST_RELEASE_API="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases/latest"
MODEL_NAME="ggml-large-v3-turbo-q8_0.bin"
MODEL_GLOB="ggml-large-v3*.bin"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${MODEL_NAME}"
PATH_EXPORT_LINE='export PATH="$HOME/.local/bin:$PATH"'

log() {
  printf '[INFO] %s\n' "$*"
}

warn() {
  printf '[WARN] %s\n' "$*" >&2
}

error() {
  printf '[ERROR] %s\n' "$*" >&2
}

die() {
  error "$*"
  exit 1
}

command_exists() {
  command -v "$1" >/dev/null 2>&1
}

uname_s() {
  if [[ -n "${ASR_TOOL_TEST_OS:-}" ]]; then
    printf '%s\n' "$ASR_TOOL_TEST_OS"
    return
  fi
  uname -s
}

uname_m() {
  if [[ -n "${ASR_TOOL_TEST_ARCH:-}" ]]; then
    printf '%s\n' "$ASR_TOOL_TEST_ARCH"
    return
  fi
  uname -m
}

is_windows() {
  case "$(uname_s)" in
    MINGW*|MSYS*|CYGWIN*) return 0 ;;
    *) return 1 ;;
  esac
}

normalized_os() {
  case "$(uname_s)" in
    Darwin) printf '%s\n' 'macOS' ;;
    Linux) printf '%s\n' 'Linux' ;;
    MINGW*|MSYS*|CYGWIN*) printf '%s\n' 'Windows' ;;
    *) die "Unsupported operating system: $(uname_s)" ;;
  esac
}

normalized_arch() {
  case "$(uname_m)" in
    arm64|aarch64) printf '%s\n' 'arm64' ;;
    x86_64|amd64) printf '%s\n' 'x86_64' ;;
    *) printf '%s\n' "$(uname_m)" ;;
  esac
}

detect_os_arch() {
  local os_name arch_name
  os_name="$(normalized_os)"
  arch_name="$(normalized_arch)"

  case "${os_name}/${arch_name}" in
    macOS/arm64|Linux/x86_64|Windows/x86_64)
      printf '%s %s\n' "$os_name" "$arch_name"
      ;;
    *)
      die "Unsupported platform: ${os_name}/${arch_name}"
      ;;
  esac
}

release_asset_name() {
  case "$1/$2" in
    macOS/arm64) printf '%s\n' 'asr-tool-macos-arm64' ;;
    Linux/x86_64) printf '%s\n' 'asr-tool-linux-x86_64.zip' ;;
    Windows/x86_64) printf '%s\n' 'asr-tool-windows-x86_64.zip' ;;
    *) die "No release asset available for $1/$2" ;;
  esac
}

release_binary_name() {
  case "$1" in
    Windows) printf '%s\n' 'asr-tool.exe' ;;
    *) printf '%s\n' 'asr-tool' ;;
  esac
}

binary_install_dir() {
  printf '%s\n' "$HOME/.local/bin"
}

binary_release_marker() {
  printf '%s\n' "$(binary_install_dir)/.asr-tool-release"
}

model_cache_dir() {
  printf '%s\n' "$HOME/.cache/whisper"
}

model_target_path() {
  printf '%s\n' "$(model_cache_dir)/${MODEL_NAME}"
}

choose_shell_rc_file() {
  local shell_path shell_name
  shell_path="${1:-${SHELL:-/bin/bash}}"
  shell_name="$(basename "$shell_path")"

  case "$shell_name" in
    zsh) printf '%s\n' "$HOME/.zshrc" ;;
    bash) printf '%s\n' "$HOME/.bashrc" ;;
    *) printf '%s\n' "$HOME/.profile" ;;
  esac
}

path_contains_dir() {
  case ":${PATH:-}:" in
    *":$1:"*) return 0 ;;
    *) return 1 ;;
  esac
}

ensure_line_in_file() {
  local file_path line
  file_path="$1"
  line="$2"

  mkdir -p "$(dirname "$file_path")"
  touch "$file_path"
  if ! grep -Fqx "$line" "$file_path"; then
    printf '\n%s\n' "$line" >> "$file_path"
  fi
}

release_download_url() {
  local release_tag asset_name
  release_tag="$1"
  asset_name="$2"
  printf 'https://github.com/%s/%s/releases/download/%s/%s\n' \
    "$REPO_OWNER" "$REPO_NAME" "$release_tag" "$asset_name"
}

resolve_latest_release_tag() {
  local effective_url tag_name

  if [[ -n "${ASR_TOOL_RELEASE_TAG:-}" ]]; then
    printf '%s\n' "$ASR_TOOL_RELEASE_TAG"
    return
  fi

  effective_url="$({ curl -fsSIL -o /dev/null -w '%{url_effective}' "$LATEST_RELEASE_URL"; } 2>/dev/null || true)"
  tag_name="${effective_url##*/}"
  if [[ -n "$tag_name" && "$tag_name" == v* ]]; then
    printf '%s\n' "$tag_name"
    return
  fi

  tag_name="$({
    curl -fsSL "$LATEST_RELEASE_API" |
      sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' |
      head -n 1
  } 2>/dev/null || true)"

  [[ -n "$tag_name" ]] || die "Failed to resolve the latest release tag"
  printf '%s\n' "$tag_name"
}

detect_skill_roots() {
  local candidates entry
  candidates=(
    "Claude|$HOME/.claude/skills"
    "Codex|$HOME/.codex/skills"
    "Agents|$HOME/.agents/skills"
    "Gemini|$HOME/.gemini/skills"
    "OpenClaw|$HOME/.openclaw/skills"
    "Hermes|$HOME/.hermes/skills"
  )

  for entry in "${candidates[@]}"; do
    if [[ -d "${entry#*|}" ]]; then
      printf '%s\n' "$entry"
    fi
  done
}

prompt_input_path() {
  if [[ -n "${ASR_TOOL_PROMPT_INPUT:-}" ]]; then
    printf '%s\n' "$ASR_TOOL_PROMPT_INPUT"
    return
  fi

  if { true < /dev/tty; } 2>/dev/null; then
    printf '%s\n' '/dev/tty'
  fi
}

prompt_output_path() {
  if [[ -n "${ASR_TOOL_PROMPT_OUTPUT:-}" ]]; then
    printf '%s\n' "$ASR_TOOL_PROMPT_OUTPUT"
    return
  fi

  if { true > /dev/tty; } 2>/dev/null; then
    printf '%s\n' '/dev/tty'
  fi
}

all_selected_indexes() {
  local count index selected
  count="$1"
  selected=","

  for ((index = 0; index < count; index++)); do
    selected="${selected}${index},"
  done

  printf '%s\n' "$selected"
}

selection_contains() {
  local index selected_indexes
  index="$1"
  selected_indexes="$2"
  [[ "$selected_indexes" == *",$index,"* ]]
}

toggle_selection_index() {
  local index selected_indexes
  index="$1"
  selected_indexes="$2"

  if selection_contains "$index" "$selected_indexes"; then
    selected_indexes="${selected_indexes//,$index,/,}"
    printf '%s\n' "$selected_indexes"
    return
  fi

  printf '%s%s,\n' "$selected_indexes" "$index"
}

toggle_all_selection_indexes() {
  local count selected_indexes
  count="$1"
  selected_indexes="$2"

  if [[ "$selected_indexes" == "$(all_selected_indexes "$count")" ]]; then
    printf '%s\n' ','
    return
  fi

  all_selected_indexes "$count"
}

prompt_for_targets() {
  local -a detected
  local selection prompt_in prompt_out current_index key key_2 key_3
  local selected_indexes status_message menu_lines
  local label root_dir marker cursor_prefix
  local index

  while IFS= read -r selection; do
    detected+=("$selection")
  done < <(detect_skill_roots)
  if (( ${#detected[@]} == 0 )); then
    die "No supported skill roots detected under ~/.claude/skills, ~/.codex/skills, ~/.agents/skills, ~/.gemini/skills, ~/.openclaw/skills, or ~/.hermes/skills"
  fi

  if [[ "${ASR_TOOL_ASSUME_YES:-0}" == "1" ]]; then
    printf '%s\n' "${detected[@]}"
    return
  fi

  prompt_in="$(prompt_input_path)"
  prompt_out="$(prompt_output_path)"
  if [[ -z "$prompt_in" || -z "$prompt_out" ]]; then
    printf '%s\n' "${detected[@]}"
    return
  fi

  current_index=0
  selected_indexes="$(all_selected_indexes "${#detected[@]}")"
  menu_lines=$((${#detected[@]} + 2))

  exec 3< "$prompt_in"
  exec 4> "$prompt_out"

  while true; do
    printf '\r' >&4
    printf '\033[J' >&4
    printf 'Detected skill roots (default: all)\n' >&4
    for index in "${!detected[@]}"; do
      label="${detected[index]%%|*}"
      root_dir="${detected[index]#*|}"
      if selection_contains "$index" "$selected_indexes"; then
        marker='x'
      else
        marker=' '
      fi
      if (( index == current_index )); then
        cursor_prefix='>'
      else
        cursor_prefix=' '
      fi
      printf '%s [%s] %s (%s)\n' "$cursor_prefix" "$marker" "$label" "$root_dir" >&4
    done
    if [[ -n "$status_message" ]]; then
      printf '%s\n' "$status_message" >&4
    else
      printf 'Use Up/Down to move, Space to toggle, a to toggle all, Enter to confirm.\n' >&4
    fi

    if ! IFS= read -r -s -n1 -u 3 key; then
      break
    fi

    status_message=''
    case "$key" in
      ''|$'\n'|$'\r')
        if [[ "$selected_indexes" == ',' ]]; then
          status_message='Select at least one target before confirming.'
        else
          break
        fi
        ;;
      ' ')
        selected_indexes="$(toggle_selection_index "$current_index" "$selected_indexes")"
        ;;
      'a'|'A')
        selected_indexes="$(toggle_all_selection_indexes "${#detected[@]}" "$selected_indexes")"
        ;;
      'j')
        current_index=$(((current_index + 1) % ${#detected[@]}))
        ;;
      'k')
        current_index=$(((current_index + ${#detected[@]} - 1) % ${#detected[@]}))
        ;;
      $'\033')
        if IFS= read -r -s -n1 -u 3 key_2 && [[ "$key_2" == '[' ]] && \
          IFS= read -r -s -n1 -u 3 key_3; then
          case "$key_3" in
            'A') current_index=$(((current_index + ${#detected[@]} - 1) % ${#detected[@]})) ;;
            'B') current_index=$(((current_index + 1) % ${#detected[@]})) ;;
          esac
        fi
        ;;
    esac

    printf '\033[%dA' "$menu_lines" >&4
  done

  printf '\n' >&4
  exec 3<&-
  exec 4>&-

  for index in "${!detected[@]}"; do
    if selection_contains "$index" "$selected_indexes"; then
      printf '%s\n' "${detected[index]}"
    fi
  done
}

managed_remote_matches() {
  case "$1" in
    "https://github.com/${REPO_OWNER}/${REPO_NAME}"|\
    "https://github.com/${REPO_OWNER}/${REPO_NAME}.git"|\
    "git@github.com:${REPO_OWNER}/${REPO_NAME}.git"|\
    "ssh://git@github.com/${REPO_OWNER}/${REPO_NAME}.git")
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

ensure_clean_managed_repo() {
  local repo_dir remote_url
  repo_dir="$1"

  git -C "$repo_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1 || \
    die "$repo_dir exists but is not a git repository"

  remote_url="$(git -C "$repo_dir" remote get-url origin 2>/dev/null || true)"
  managed_remote_matches "$remote_url" || \
    die "$repo_dir exists but origin is not ${REPO_URL}"

  git -C "$repo_dir" update-index -q --refresh
  if [[ -n "$(git -C "$repo_dir" status --porcelain)" ]]; then
    die "$repo_dir has local changes; please clean it before running install.sh"
  fi
}

looks_like_legacy_skill_dir() {
  local skill_dir
  skill_dir="$1"

  [[ -f "$skill_dir/SKILL.md" ]] || return 1
  [[ -f "$skill_dir/README.md" ]] || return 1
  [[ -f "$skill_dir/CMakeLists.txt" ]] || return 1
  [[ -d "$skill_dir/src" || -d "$skill_dir/bin" ]] || return 1
  return 0
}

backup_legacy_skill_dir() {
  local skill_dir backup_dir stamp suffix
  skill_dir="$1"
  stamp="$(date +%Y%m%d%H%M%S 2>/dev/null || printf '%s' "$$")"
  backup_dir="${skill_dir}.backup-${stamp}"
  suffix=0

  while [[ -e "$backup_dir" ]]; do
    suffix=$((suffix + 1))
    backup_dir="${skill_dir}.backup-${stamp}-${suffix}"
  done

  mv "$skill_dir" "$backup_dir"
  printf '%s\n' "$backup_dir"
}

install_or_update_skill() {
  local label root_dir release_tag skill_dir current_tag backup_dir
  label="$1"
  root_dir="$2"
  release_tag="$3"
  skill_dir="$root_dir/$REPO_NAME"

  if [[ -e "$skill_dir" && ! -d "$skill_dir" ]]; then
    die "$skill_dir exists but is not a directory"
  fi

  if [[ -d "$skill_dir" ]] && ! git -C "$skill_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if looks_like_legacy_skill_dir "$skill_dir"; then
      backup_dir="$(backup_legacy_skill_dir "$skill_dir")"
      warn "$skill_dir is a legacy non-git install; backed it up to $backup_dir"
    else
      die "$skill_dir exists but is not a git repository"
    fi
  fi

  if [[ ! -d "$skill_dir" ]]; then
    log "Installing ${label} Skill into $skill_dir"
    git -c advice.detachedHead=false clone --depth 1 --branch "$release_tag" --single-branch \
      "$REPO_URL" "$skill_dir"
    return
  fi

  ensure_clean_managed_repo "$skill_dir"
  current_tag="$(git -C "$skill_dir" describe --tags --exact-match HEAD 2>/dev/null || true)"
  if [[ "$current_tag" == "$release_tag" ]]; then
    log "${label} Skill already at ${release_tag}"
    return
  fi

  log "Updating ${label} Skill to ${release_tag}"
  git -C "$skill_dir" fetch --force --tags origin
  git -c advice.detachedHead=false -C "$skill_dir" checkout --force "$release_tag"
  git -C "$skill_dir" reset --hard "$release_tag"
}

download_file() {
  local url destination
  url="$1"
  destination="$2"

  command_exists curl || die 'curl is required to download installer assets'
  curl --fail --location --retry 3 --silent --show-error \
    -o "$destination" "$url"
}

to_windows_path() {
  if command_exists cygpath; then
    cygpath -w "$1"
    return
  fi

  case "$1" in
    /[A-Za-z]/*)
      printf '%s\n' "$(printf '%s' "$1" | sed -E 's#^/([A-Za-z])/#\1:/#; s#/#\\\\#g')"
      ;;
    *)
      die "Unable to convert path to Windows format: $1"
      ;;
  esac
}

extract_zip_archive() {
  local archive_path destination_dir win_archive win_destination
  archive_path="$1"
  destination_dir="$2"

  mkdir -p "$destination_dir"
  if command_exists unzip; then
    unzip -oq "$archive_path" -d "$destination_dir"
    return
  fi

  if command_exists python3; then
    python3 - "$archive_path" "$destination_dir" <<'PY'
import sys
import zipfile

archive_path, destination_dir = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(archive_path) as archive:
    archive.extractall(destination_dir)
PY
    return
  fi

  if is_windows && command_exists powershell.exe; then
    win_archive="$(to_windows_path "$archive_path")"
    win_destination="$(to_windows_path "$destination_dir")"
    ASR_TOOL_WIN_ARCHIVE="$win_archive" \
    ASR_TOOL_WIN_DESTINATION="$win_destination" \
      powershell.exe -NoProfile -NonInteractive -Command '
        Expand-Archive -Path $env:ASR_TOOL_WIN_ARCHIVE -DestinationPath $env:ASR_TOOL_WIN_DESTINATION -Force
      '
    return
  fi

  if command_exists bsdtar; then
    bsdtar -xf "$archive_path" -C "$destination_dir"
    return
  fi

  die "Need unzip, python3, powershell, or bsdtar to extract $archive_path"
}

remove_old_binary_payload() {
  local install_dir
  install_dir="$1"
  mkdir -p "$install_dir"

  find "$install_dir" -maxdepth 1 -type f \
    \( -name 'asr-tool' -o -name 'asr-tool.exe' -o \
       -name 'libwhisper*.so*' -o -name 'libggml*.so*' -o \
       -name 'whisper*.dll' -o -name 'ggml*.dll' \) -delete
}

write_windows_shim() {
  local install_dir shim_path
  install_dir="$1"
  shim_path="$install_dir/asr-tool"

  cat > "$shim_path" <<'SHIM'
#!/usr/bin/env bash
exec "$(dirname "$0")/asr-tool.exe" "$@"
SHIM
  chmod +x "$shim_path"
}

install_or_update_binary() {
  local os_name arch_name asset_name binary_name install_dir marker_path marker_tag
  local temp_dir archive_path extract_dir download_url

  read -r os_name arch_name < <(detect_os_arch)
  asset_name="$(release_asset_name "$os_name" "$arch_name")"
  binary_name="$(release_binary_name "$os_name")"
  install_dir="$(binary_install_dir)"
  marker_path="$(binary_release_marker)"
  marker_tag="$(cat "$marker_path" 2>/dev/null || true)"

  if [[ "$marker_tag" == "$1" && -f "$install_dir/$binary_name" ]]; then
    if [[ "$os_name" == 'Windows' ]]; then
      write_windows_shim "$install_dir"
    fi
    log "Shared binary already at $1"
    return
  fi

  temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/asr-tool-installer.XXXXXX")"
  trap 'rm -rf "$temp_dir"' RETURN
  download_url="$(release_download_url "$1" "$asset_name")"
  mkdir -p "$install_dir"
  remove_old_binary_payload "$install_dir"

  if [[ "$asset_name" == *.zip ]]; then
    archive_path="$temp_dir/$asset_name"
    extract_dir="$temp_dir/extracted"
    log "Downloading ${asset_name}"
    download_file "$download_url" "$archive_path"
    extract_zip_archive "$archive_path" "$extract_dir"
    cp -fR "$extract_dir"/. "$install_dir"/
    if [[ "$os_name" == 'Windows' ]]; then
      write_windows_shim "$install_dir"
    else
      chmod +x "$install_dir/asr-tool"
    fi
  else
    log "Downloading ${asset_name}"
    download_file "$download_url" "$install_dir/asr-tool"
    chmod +x "$install_dir/asr-tool"
  fi

  printf '%s\n' "$1" > "$marker_path"
  trap - RETURN
  rm -rf "$temp_dir"
  log "Installed shared binary into $install_dir"
}

find_existing_model() {
  local model_dir
  model_dir="$(model_cache_dir)"
  mkdir -p "$model_dir"
  find "$model_dir" -maxdepth 1 -type f -name "$MODEL_GLOB" | head -n 1
}

ensure_model() {
  local existing_model model_path
  existing_model="$(find_existing_model || true)"
  if [[ -n "$existing_model" ]]; then
    log "Model already available: $existing_model"
    return
  fi

  model_path="$(model_target_path)"
  log "Downloading Whisper model to $model_path"
  download_file "$MODEL_URL" "$model_path"
}

run_with_privilege() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
    return
  fi

  if command_exists sudo; then
    sudo "$@"
    return
  fi

  "$@"
}

install_ffmpeg_macos() {
  command_exists brew || return 1
  brew install ffmpeg
}

install_ffmpeg_linux() {
  if command_exists apt-get; then
    run_with_privilege apt-get update
    run_with_privilege env DEBIAN_FRONTEND=noninteractive apt-get install -y ffmpeg
    return
  fi

  if command_exists apt; then
    run_with_privilege apt update
    run_with_privilege env DEBIAN_FRONTEND=noninteractive apt install -y ffmpeg
    return
  fi

  return 1
}

install_ffmpeg_windows() {
  if command_exists winget; then
    winget install --id Gyan.FFmpeg -e \
      --accept-package-agreements --accept-source-agreements
    return
  fi

  if command_exists choco; then
    choco install ffmpeg -y
    return
  fi

  return 1
}

ensure_ffmpeg() {
  local os_name arch_name
  if command_exists ffmpeg; then
    log 'ffmpeg already installed'
    return
  fi

  read -r os_name arch_name < <(detect_os_arch)
  log 'ffmpeg not found; attempting automatic installation'
  case "$os_name" in
    macOS)
      install_ffmpeg_macos || \
        warn 'Unable to install ffmpeg automatically. Please install it manually.'
      ;;
    Linux)
      install_ffmpeg_linux || \
        warn 'Unable to install ffmpeg automatically. Please install it manually.'
      ;;
    Windows)
      install_ffmpeg_windows || \
        warn 'Unable to install ffmpeg automatically. Please install it manually.'
      ;;
  esac

  if command_exists ffmpeg; then
    log 'ffmpeg is ready'
  else
    warn 'Continuing without ffmpeg. Install it manually before transcribing audio.'
  fi
}

ensure_windows_user_path() {
  local install_dir windows_install_dir
  install_dir="$1"
  windows_install_dir="$(to_windows_path "$install_dir")"

  if ! command_exists powershell.exe; then
    warn 'powershell.exe not found; cannot persist PATH for Windows shells'
    return
  fi

  ASR_TOOL_WINDOWS_BIN_DIR="$windows_install_dir" \
    powershell.exe -NoProfile -NonInteractive -Command '
      $bin = $env:ASR_TOOL_WINDOWS_BIN_DIR
      $current = [Environment]::GetEnvironmentVariable("Path", "User")
      $parts = @()
      if ($current) {
        $parts += ($current -split ";")
      }
      if (-not ($parts -contains $bin)) {
        $newPath = (@($bin) + $parts | Where-Object { $_ -and $_.Trim() -ne "" } | Select-Object -Unique) -join ";"
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
      }
    '

  ensure_line_in_file "$HOME/.bashrc" "$PATH_EXPORT_LINE"
}

ensure_path_configuration() {
  local install_dir rc_file
  install_dir="$(binary_install_dir)"

  if ! path_contains_dir "$install_dir"; then
    export PATH="$install_dir:${PATH:-}"
  fi

  if is_windows; then
    ensure_windows_user_path "$install_dir"
    log 'Ensured PATH contains ~/.local/bin for Git Bash, PowerShell, and cmd'
    return
  fi

  rc_file="$(choose_shell_rc_file)"
  ensure_line_in_file "$rc_file" "$PATH_EXPORT_LINE"
  log "Ensured PATH contains ~/.local/bin via $rc_file"
}

main() {
  local latest_tag prompt_output target_entry label root_dir
  local -a targets

  command_exists git || die 'git is required to install or update Skill directories'
  command_exists curl || die 'curl is required to download release assets'

  latest_tag="$(resolve_latest_release_tag)"
  log "Latest release: $latest_tag"

  prompt_output="$(prompt_for_targets)"
  while IFS= read -r target_entry; do
    [[ -n "$target_entry" ]] || continue
    targets+=("$target_entry")
  done <<< "$prompt_output"
  ensure_path_configuration
  ensure_ffmpeg

  for target_entry in "${targets[@]}"; do
    label="${target_entry%%|*}"
    root_dir="${target_entry#*|}"
    install_or_update_skill "$label" "$root_dir" "$latest_tag"
  done

  install_or_update_binary "$latest_tag"
  ensure_model

  printf '\nInstall complete.\n'
  printf '  Skills installed: %d\n' "${#targets[@]}"
  printf '  Shared binary: %s\n' "$(binary_install_dir)/asr-tool"
  printf '  Shared model dir: %s\n' "$(model_cache_dir)"
  printf 'Open a new terminal or source your shell rc file if the PATH change is not visible yet.\n'
}

if [[ -z "${ASR_TOOL_INSTALL_SOURCE_ONLY:-}" ]]; then
  if ! (return 0 2>/dev/null); then
    main "$@"
  fi
fi
