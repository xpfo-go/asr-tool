"""Paced public replay of the actual local first-pass/review pair; no microphone."""

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import sys

from candidates import validate_text
from metrics import correction_scores, error_counts
from provenance import audio_frontend_fingerprint, model_fingerprint, run_measured, sha256, validate_case

CACHE = Path(__file__).resolve().parents[2] / ".cache/evaluation"


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)] if values else None


def score_live(reference, records):
    if len(records) < 2 or records[0].get("type") != "run_start" or records[-1].get("type") != "run_end":
        raise ValueError("Incomplete live replay")
    start, end = records[0], records[-1]
    samples = round(reference["duration"] * 16000)
    if start["sample_rate"] != 16000 or abs(start["duration"] - reference["duration"]) > 1 / 16000:
        raise ValueError("Live replay audio identity differs")
    if end["received_samples"] != samples or end["pending_review_samples"] != 0 or not 0 <= end["peak_review_samples"] <= 320000:
        raise ValueError("Incomplete audio or exceeded live review budget")
    utterances, finals, endpoints = {}, {}, {}
    lag, final_delay, first_events, changes = [], [], [], []
    previous_wall, previous_received = -1, -1
    for event in records[1:-1]:
        if event["type"] not in ("provisional", "revision", "final"):
            raise ValueError("Unknown live event")
        identity, revision = event["utterance_id"], event["revision"]
        if type(identity) is not int or identity <= 0 or type(revision) is not int or revision <= 0:
            raise ValueError("Invalid live utterance identity")
        wall, received = event["emitted_seconds"], event["received_samples"]
        if not math.isfinite(wall) or wall < previous_wall or not previous_received <= received <= samples:
            raise ValueError("Invalid live event clock")
        previous_wall, previous_received = wall, received
        a, b = event["start"], event["end"]
        if not all(math.isfinite(x) for x in (a, b)) or not 0 <= a < b <= reference["duration"] + .001 or b > received / 16000 + .001:
            raise ValueError("Event precedes its audio or has an invalid interval")
        validate_text(event["text"])
        previous = utterances.get(identity)
        if identity in finals or previous and (revision <= previous["revision"] or a != previous["start"] or b < previous["end"]):
            raise ValueError("Stale or post-final live revision")
        if previous is None and event["type"] != "provisional":
            raise ValueError("Missing provisional utterance")
        utterances[identity] = event
        if event["type"] == "provisional":
            if identity in endpoints:
                raise ValueError("Provisional changed after endpoint")
            if event["text"]:
                lag.append(wall - b)
                first_events.append(event)
            if event.get("reason") == "endpoint":
                endpoints[identity] = event
        else:
            if identity not in endpoints or (a, b) != (endpoints[identity]["start"], endpoints[identity]["end"]):
                raise ValueError("Reviewed event lacks its endpoint")
            if event["type"] == "revision":
                if not event["reviewed"]:
                    raise ValueError("Unverified replacement")
            else:
                finals[identity] = event
                final_delay.append(wall - endpoints[identity]["emitted_seconds"])
                original = endpoints[identity]["text"]
                if event["text"] != original:
                    if not event["reviewed"] or event.get("original_text") != original:
                        raise ValueError("Correction did not preserve the original")
                    changes.append({"start": a, "end": b, "original_text": original, "corrected_text": event["text"]})
    if set(finals) != set(utterances) or sorted(finals) != list(range(1, len(finals) + 1)):
        raise ValueError("Missing final utterance")
    cursor = 0
    for identity in sorted(finals):
        event = finals[identity]
        if abs(event["start"] - cursor) > .001:
            raise ValueError("Missing or duplicate audio ownership")
        cursor = event["end"]
    if abs(cursor - reference["duration"]) > .001:
        raise ValueError("Missing final audio tail")
    initial = " ".join(endpoints[i]["text"] for i in sorted(finals))
    final = " ".join(finals[i]["text"] for i in sorted(finals))
    # Clip/utterance onset is available in these references, but not acoustic
    # first-word completion. Report it separately from compute/input lag.
    onset_latencies = []
    for unit in reference.get("words", []) if reference.get("text_timing") == "utterance" else []:
        events = [e for e in first_events if unit["start"] <= e["end"] <= unit["end"] + .001]
        if events:
            onset_latencies.append(min(e["emitted_seconds"] for e in events) - unit["start"])
    return {"id": reference["id"], "domain": reference["domain"], "duration": reference["duration"],
            "provisional_cer": error_counts(reference["text"], initial, True),
            "final_cer": error_counts(reference["text"], final, True),
            "provisional_wer": error_counts(reference["text"], initial),
            "final_wer": error_counts(reference["text"], final),
            "corrections": correction_scores(reference, changes), "utterances": len(finals),
            "reviewed": sum(e["reviewed"] for e in finals.values()),
            "final_reasons": dict(Counter(e.get("reason", "") for e in finals.values())),
            "peak_review_seconds": end["peak_review_samples"] / 16000,
            "paced": start["paced"], "elapsed_seconds": end["elapsed_seconds"],
            "input_lag_p95_seconds": percentile(lag, .95) if start["paced"] else None,
            "finalization_p95_seconds": percentile(final_delay, .95) if start["paced"] else None,
            "utterance_onset_to_text_p95_seconds": percentile(onset_latencies, .95) if start["paced"] else None,
            "onsets_with_output": len(onset_latencies)}


def run(args):
    if sys.platform != "darwin":
        raise ValueError("Network-denied live evaluation currently requires macOS")
    if Path(args.label).name != args.label or args.label in (".", ".."):
        raise ValueError("Evaluation label must be a single directory name")
    cases = sorted((CACHE / "cases").glob(args.cases))
    if not cases:
        raise ValueError("No public cases selected")
    references = {p.name: json.loads((p / "reference.json").read_text()) for p in cases}
    if any(r["split"] != "dev" for r in references.values()):
        raise ValueError("Live model selection uses development data only")
    output = CACHE / args.label
    output.mkdir(exist_ok=True)
    model_dir, stream_dir, binary = args.model_dir.resolve(), args.stream_model_dir.resolve(), args.binary.resolve()
    manifests = {"binary_sha256": sha256(binary), "review_models": model_fingerprint(model_dir),
                 "streaming_models": model_fingerprint(stream_dir), "network_denied": True,
                 "paced": not args.unpaced, "prompt": args.prompt,
                 "references": {p.name: validate_case(p, references[p.name]) for p in cases},
                 "audio_frontend": audio_frontend_fingerprint(),
                 "scoring_sources": {name: sha256(Path(__file__).with_name(name))
                                     for name in ("live_evaluate.py", "metrics.py", "provenance.py")}}
    manifest = output / "manifest.json"
    if manifest.exists() and json.loads(manifest.read_text()) != manifests:
        raise ValueError("Live evaluation identity changed; use a new label")
    manifest.write_text(json.dumps(manifests, indent=2) + "\n")
    reports = []
    for case in cases:
        destination = output / (case.name + ".jsonl")
        command = ["/usr/bin/sandbox-exec", "-p", "(version 1)(allow default)(deny network*)", str(binary),
                   str(case / "audio.wav"), "-m", str(model_dir), "--stream-model-dir", str(stream_dir),
                   "-o", str(destination)]
        if args.prompt:
            command += ["-p", args.prompt]
        if args.unpaced:
            command += ["--unpaced"]
        with (output / (case.name + ".log")).open("w") as log:
            resources = run_measured(command, log, timeout=math.ceil(references[case.name]["duration"] + 900),
                                     max_rss_bytes=8 * 1024**3, stop_on_memory_pressure=True)
        if sha256(binary) != manifests["binary_sha256"] or model_fingerprint(model_dir) != manifests["review_models"] or \
                model_fingerprint(stream_dir) != manifests["streaming_models"] or audio_frontend_fingerprint() != manifests["audio_frontend"]:
            raise ValueError("Live runtime or models changed during evaluation")
        validate_case(case, references[case.name], manifests["references"][case.name])
        records = [json.loads(line) for line in destination.read_text().splitlines()]
        report = score_live(references[case.name], records)
        report.update(resources=resources, output_sha256=sha256(destination), manifest_sha256=sha256(manifest))
        reports.append(report)
        (output / "report.json").write_text(json.dumps(reports, indent=2) + "\n")
        print(json.dumps(report), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--model-dir", type=Path, required=True)
    parser.add_argument("--stream-model-dir", type=Path, required=True)
    parser.add_argument("--cases", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--prompt", default="")
    parser.add_argument("--unpaced", action="store_true")
    run(parser.parse_args())
