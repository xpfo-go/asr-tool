"""Developer-only speech coverage evaluation, without speaker identity scoring."""

import argparse
import json
import math
from pathlib import Path
import sys

from evaluate import CACHE, annotation
from provenance import audio_frontend_fingerprint, finalize_resources, run_measured, sha256, validate_case


def speech_coverage(reference, detected, duration):
    if not math.isfinite(duration) or duration <= 0:
        raise ValueError("Invalid speech coverage duration")
    def timeline(spans):
        return annotation([dict(span, speaker="speech") for span in spans], duration).get_timeline().support()
    ref, hyp = timeline(reference), timeline(detected)
    speech = ref.duration()
    emitted = hyp.duration()
    covered = ref.crop(hyp).support().duration()
    false_alarm = max(0.0, emitted - covered)
    nonspeech = duration - speech
    return {"reference_speech_seconds": speech, "detected_seconds": emitted,
            "covered_speech_seconds": covered, "missed_speech_seconds": max(0.0, speech - covered),
            "nonspeech_seconds": nonspeech, "false_alarm_seconds": false_alarm,
            "recall": covered / speech if speech else None,
            "precision": covered / emitted if emitted else None,
            "nonspeech_coverage": false_alarm / nonspeech if nonspeech else None}


def run(args):
    if Path(args.label).name != args.label or args.label in (".", ".."):
        raise ValueError("VAD label must be a single directory name")
    if not math.isfinite(args.threshold) or not 0 < args.threshold < 1:
        raise ValueError("VAD threshold must be between zero and one")
    if args.deny_network and sys.platform != "darwin":
        raise ValueError("Network-denied VAD evaluation is implemented only on macOS")
    binary, model = args.binary.resolve(), args.model.resolve()
    cases = sorted((CACHE / "cases").glob(args.cases))
    if not cases:
        raise ValueError("No VAD development cases matched")
    identities = {}
    for case in cases:
        ref = json.loads((case / "reference.json").read_text())
        if ref["split"] == "acceptance":
            raise ValueError("Do not use acceptance recordings for VAD development")
        expected = None
        if "-full-" in case.name or ref["corpus"] == "ascend":
            selection_name = {"ami": "ami-expanded-selection.json", "ali": "ali-manual-selection.json",
                              "ascend": "ascend-selection.json"}[ref["corpus"]]
            expected = json.loads((CACHE / selection_name).read_text())["reference_checksums"][case.name]
        identities[case.name] = validate_case(case, ref, expected)
    manifest = {"binary_sha256": sha256(binary), "model_sha256": sha256(model),
                "audio_frontend": audio_frontend_fingerprint(),
                "model_bytes": model.stat().st_size, "backend": args.backend,
                "threshold": args.threshold,
                "references": identities, "network_denied": args.deny_network,
                "scoring_sources": {name: sha256(Path(__file__).with_name(name))
                                    for name in ("vad.py", "evaluate.py", "provenance.py")}}
    output = CACHE / args.label
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / "manifest.json"
    if manifest_path.exists() and json.loads(manifest_path.read_text()) != manifest:
        raise ValueError("VAD evaluation identity changed; use a new label")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    reports = []
    for case in cases:
        reference = json.loads((case / "reference.json").read_text())
        validate_case(case, reference, identities[case.name])
        destination = output / (case.name + ".json")
        command = [str(binary), str(case / "audio.wav"), str(model), args.backend, str(destination)]
        command.append(str(args.threshold))
        if args.deny_network:
            command = ["/usr/bin/sandbox-exec", "-p", "(version 1)(allow default)(deny network*)"] + command
        log_path = output / (case.name + ".log")
        with log_path.open("w") as log:
            resources = run_measured(command, log, timeout=600)
        resources = finalize_resources(resources, log_path.read_text())
        if sha256(binary) != manifest["binary_sha256"] or sha256(model) != manifest["model_sha256"]:
            raise ValueError("VAD binary/model changed during evaluation")
        if audio_frontend_fingerprint() != manifest["audio_frontend"]:
            raise ValueError("FFmpeg changed during VAD evaluation")
        validate_case(case, reference, identities[case.name])
        hypothesis = json.loads(destination.read_text())
        if abs(hypothesis["duration"] - reference["duration"]) > .05:
            raise ValueError("VAD fixture and output durations differ")
        report = {"id": case.name, "domain": reference.get("domain"),
                  "speech": speech_coverage(reference["turns"], hypothesis["speech"], reference["duration"]),
                  "windows": speech_coverage(reference["turns"], hypothesis["windows"], reference["duration"]),
                  "settings": hypothesis["settings"], "resources": resources,
                  "output_sha256": sha256(destination), "log_sha256": sha256(log_path)}
        reports.append(report)
        (output / "report.json").write_text(json.dumps(reports, indent=2) + "\n")
        print(json.dumps(report), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--backend", choices=["whisper", "onnx"], required=True)
    parser.add_argument("--cases", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--deny-network", action="store_true")
    parser.add_argument("--threshold", type=float, default=.5)
    run(parser.parse_args())
