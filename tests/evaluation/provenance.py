"""Developer evaluation fingerprints and sampled process-tree resources."""

import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

import psutil


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def audio_frontend_fingerprint():
    executable = shutil.which("ffmpeg")
    if executable is None:
        raise ValueError("FFmpeg is required for evaluation audio conversion")
    path = Path(executable).resolve()
    version = subprocess.run([str(path), "-version"], check=True, capture_output=True,
                             text=True, timeout=10).stdout.strip()
    if not version.startswith("ffmpeg version "):
        raise ValueError("Unrecognized FFmpeg version output")
    return {"path": str(path), "sha256": sha256(path), "version": version}


def model_fingerprint(directory):
    files = {}

    def visit(path, ancestors):
        resolved = path.resolve()
        if path.is_dir():
            if resolved in ancestors:
                raise ValueError(f"Model directory contains a symlink cycle: {path}")
            for child in sorted(path.iterdir()):
                visit(child, ancestors | {resolved})
        elif path.is_file():
            if path.suffix == ".part":
                raise ValueError(f"Model download is incomplete: {path}")
            files[path.relative_to(directory).as_posix()] = {"bytes": path.stat().st_size,
                                                            "sha256": sha256(path)}
        else:
            raise ValueError(f"Invalid model file: {path}")

    directory = Path(directory).resolve()
    if not directory.is_dir():
        raise ValueError(f"Missing model directory: {directory}")
    visit(directory, set())
    if not any(name.endswith((".bin", ".onnx", ".gguf")) for name in files):
        raise ValueError("No model weight files in evaluation directory")
    return files


def validate_case(case, reference, expected_reference_sha256=None):
    checksum = sha256(case / "reference.json")
    if expected_reference_sha256 is not None and checksum != expected_reference_sha256:
        raise ValueError(f"Fixture reference changed: {case}")
    if reference["id"] != case.name:
        raise ValueError("Fixture directory and reference identity differ")
    if sha256(case / "audio.wav") != reference["audio_sha256"]:
        raise ValueError(f"Fixture audio changed: {case}")
    if not isinstance(reference["duration"], (int, float)) or not 0 < reference["duration"] < float("inf"):
        raise ValueError("Invalid fixture duration")
    if reference.get("text") is not None:
        from metrics import normalize
        if normalize(" ".join(w["text"] for w in reference["words"])) != normalize(reference["text"]):
            raise ValueError("Manual text units do not reconstruct reference text")
    return checksum


def memory_pressure_level():
    if sys.platform != "darwin":
        return None
    result = subprocess.run(["/usr/sbin/sysctl", "-n", "kern.memorystatus_vm_pressure_level"],
                            check=True, capture_output=True, text=True, timeout=2)
    level = int(result.stdout.strip())
    if level not in (1, 2, 4):
        raise ValueError("Unrecognized macOS memory pressure level")
    return level


def run_measured(command, log, timeout, max_rss_bytes=None, stop_on_memory_pressure=False):
    measured = command
    if sys.platform == "darwin":
        measured = ["/usr/bin/time", "-l"] + command
    started = time.monotonic()
    peak = 0
    samples = 0
    pressure_peak = None
    pressure_samples = 0
    next_pressure_check = started
    with subprocess.Popen(measured, stdout=log, stderr=log, start_new_session=True) as process:
        root = psutil.Process(process.pid)
        try:
            while process.poll() is None:
                rss = 0
                try:
                    processes = [root] + root.children(recursive=True)
                except psutil.NoSuchProcess:
                    processes = []
                for child in processes:
                    try:
                        rss += child.memory_info().rss
                    except psutil.NoSuchProcess:
                        pass
                peak = max(peak, rss)
                samples += 1
                if stop_on_memory_pressure and time.monotonic() >= next_pressure_check:
                    level = memory_pressure_level()
                    next_pressure_check = time.monotonic() + 1
                    if level is not None:
                        pressure_samples += 1
                        pressure_peak = max(pressure_peak or level, level)
                        if level != 1:
                            message = (f"macOS host memory pressure level {level} is elevated; "
                                       "stopping evaluation to preserve host responsiveness")
                            print(message, file=log, flush=True)
                            raise MemoryError(message)
                if max_rss_bytes is not None and rss > max_rss_bytes:
                    message = (f"Sampled process-tree RSS {rss} bytes exceeds evaluation "
                               f"memory budget of {max_rss_bytes} bytes")
                    print(message, file=log, flush=True)
                    raise MemoryError(message)
                if time.monotonic() - started > timeout:
                    raise subprocess.TimeoutExpired(command, timeout)
                time.sleep(.05)
            if process.returncode:
                raise subprocess.CalledProcessError(process.returncode, command)
        except BaseException:
            # FFmpeg and runtime children must not survive a failed evaluation.
            import os
            import signal
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise
    return {"elapsed_seconds": time.monotonic() - started,
            "peak_sampled_tree_rss_bytes": peak, "sample_interval_seconds": .05,
            "max_rss_bytes": max_rss_bytes,
            "memory_pressure_peak_level": pressure_peak, "memory_pressure_samples": pressure_samples,
            "memory_pressure_interval_seconds": 1 if stop_on_memory_pressure else None,
            "samples": samples,
            "memory_scope": "sampled process tree RSS; excludes driver/ANE allocations"}


def kernel_peak_rss(log_text):
    for line in log_text.splitlines():
        if line.strip().endswith("maximum resident set size"):
            return int(line.split()[0])
    return None


def finalize_resources(resources, log_text):
    records = re.findall(r"(?m)^\s*(\d+(?:\.\d+)?)\s+real\s+"
                         r"\d+(?:\.\d+)?\s+user\s+\d+(?:\.\d+)?\s+sys\s*$", log_text)
    if len(records) > 1:
        raise ValueError("Ambiguous system timing records")
    monotonic_seconds = resources["elapsed_seconds"]
    system_seconds = float(records[0]) if records else None
    result = dict(resources, monotonic_elapsed_seconds=monotonic_seconds,
                  system_elapsed_seconds=system_seconds, timing_consistent=None,
                  kernel_peak_process_rss_bytes=kernel_peak_rss(log_text))
    if system_seconds is not None:
        # Host suspension can affect these clocks differently. Keep both and
        # invalidate performance claims when they materially disagree.
        result["elapsed_seconds"] = system_seconds
        tolerance = max(1.0, .01 * max(system_seconds, monotonic_seconds))
        result["timing_consistent"] = abs(system_seconds - monotonic_seconds) <= tolerance
    return result
