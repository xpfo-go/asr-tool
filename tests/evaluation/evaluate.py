"""Developer-only, local meeting regression set. No production dependency."""

import argparse
import importlib.metadata
import io
import json
import math
from pathlib import Path
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

import pyarrow.parquet as pq
from pyannote.core import Annotation, Segment, Timeline
from pyannote.metrics.diarization import DiarizationErrorRate, JaccardErrorRate
import soundfile as sf

from metrics import correction_scores, covered_seconds, error_counts, normalize, speaker_text_scores, summarize
from provenance import (audio_frontend_fingerprint, finalize_resources, model_fingerprint,
                        run_measured, sha256, validate_case)

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / ".cache/evaluation"
MAX_RSS_BYTES = 8 * 1024**3
SOURCES = {
    "ami-test-00.parquet": (
        "https://huggingface.co/datasets/diarizers-community/ami/resolve/"
        "8cdaae2eaf968f3b000b6eb1204ab9b8db006ed0/sdm/test-00000-of-00003.parquet",
        "36f277c695a47e4f28b8f73060df611d89fa32deb0a095aca79330eb5d58249c"),
    "ami-annotations.zip": (
        "https://groups.inf.ed.ac.uk/ami/AMICorpusAnnotations/ami_public_manual_1.6.2.zip",
        "b56e5babb2496b8795deeeda7e71178d7fbc9963f94276cf2a3f4b56ebbc9f9d"),
    "ali-test-00.parquet": (
        "https://huggingface.co/datasets/argmaxinc/ali-meetings/resolve/"
        "67b7506f0db35d211aca5d0fae7ff946439f1bea/data/test-00000-of-00020.parquet",
        "6568e322bd58cbe7148f9b43e0c94ea23c91f0c65ae101cfbb5ed65ff5516af3"),
    "Eval_Ali.tar.gz": (
        "https://speech-lab-share-data.oss-cn-shanghai.aliyuncs.com/AliMeeting/openlr/Eval_Ali.tar.gz",
        "dc47343b2474b5ebcf458927e878155f6ddeb59c85e685b3645c32a1f9578d92"),
    "ascend-validation.parquet": (
        "https://huggingface.co/datasets/CAiRE/ASCEND/resolve/"
        "737e9800ae31be9932ba8464c80366559bd28424/main/validation-00000-of-00001.parquet",
        "3bdec53d2abfd3dd4f0d86a6df4e27e60f20660edc9b66055ae0ef8ec05cf7e2"),
    "ascend-test.parquet": (
        "https://huggingface.co/datasets/CAiRE/ASCEND/resolve/"
        "737e9800ae31be9932ba8464c80366559bd28424/main/test-00000-of-00001.parquet",
        "a4c81d2b5ed6124f052089a695972808c16e0ce0c365ec9773c5d1a8fcf043a7"),
    "fleurs-cmn-validation.parquet": (
        "https://huggingface.co/datasets/google/fleurs/resolve/"
        "70bb2e84b976b7e960aa89f1c648e09c59f894dd/"
        "parquet-data/cmn_hans_cn/validation-00000-of-00001.parquet",
        "18698ffdd46c36c54af641821684d3f0313a7b64e0a49615597ec61a98f2b57e"),
}


def source(name, download):
    path = CACHE / "sources" / name
    url, checksum = SOURCES[name]
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and sha256(path) == checksum:
        return path
    if not download:
        raise ValueError(f"Missing or invalid source: {path}; run prepare --download")
    partial = path.with_suffix(path.suffix + ".part")
    # A fresh invocation resumes the preserved partial. Curl's automatic retry
    # can truncate an initially empty output after a timed-out first transfer.
    subprocess.run(["curl", "--fail", "--location",
                    "--silent", "--show-error", "--speed-limit", "1024", "--speed-time", "60",
                    "--continue-at", "-", "--max-time", "1800",
                    "--output", str(partial), url], check=True)
    if sha256(partial) != checksum:
        raise ValueError(f"Checksum mismatch: {partial}")
    partial.replace(path)
    return path


def ami_words(archive, meeting, start, end):
    words = []
    with zipfile.ZipFile(archive) as package:
        meetings = ET.fromstring(package.read("corpusResources/meetings.xml"))
        entry = next(m for m in meetings if m.get("observation") == meeting)
        for speaker in entry:
            agent = speaker.attrib["nxt_agent"]
            root = ET.fromstring(package.read(f"words/{meeting}.{agent}.words.xml"))
            for word in root.findall("w"):
                if word.get("punc") == "true" or not word.text:
                    continue
                a, b = float(word.get("starttime", "nan")), float(word.get("endtime", "nan"))
                if math.isfinite(a) and math.isfinite(b) and start <= (a + b) / 2 < end:
                    words.append({"start": max(0, a - start), "end": min(end - start, b - start),
                                  "text": word.text, "speaker": speaker.attrib["global_name"]})
    return sorted(words, key=lambda w: (w["start"], w["end"], w["speaker"]))


def prepare(args):
    if args.corpus == "nonspeech-controls":
        from fixtures import prepare_nonspeech_controls
        return prepare_nonspeech_controls(CACHE)
    if args.corpus == "fleurs-mandarin":
        from fixtures import prepare_fleurs_mandarin
        return prepare_fleurs_mandarin(CACHE, source("fleurs-cmn-validation.parquet", args.download),
                                      *SOURCES["fleurs-cmn-validation.parquet"])
    if args.corpus == "ascend-mixed":
        from fixtures import ASCEND_SPLITS, prepare_ascend_mixed
        return prepare_ascend_mixed(CACHE, {
            split: (source(f"ascend-{split}.parquet", args.download), *SOURCES[f"ascend-{split}.parquet"])
            for split in ASCEND_SPLITS})
    if args.corpus == "ali-manual":
        from fixtures import prepare_alimeeting_manual
        return prepare_alimeeting_manual(CACHE, source("Eval_Ali.tar.gz", args.download),
                                         *SOURCES["Eval_Ali.tar.gz"])
    if args.corpus == "ami-expanded":
        from fixtures import prepare_ami_expanded
        return prepare_ami_expanded(CACHE, source("ami-test-00.parquet", args.download),
                                    source("ami-annotations.zip", args.download), SOURCES, ami_words)
    name = "ami-test-00.parquet" if args.corpus == "ami" else "ali-test-00.parquet"
    parquet = source(name, args.download)
    archive = source("ami-annotations.zip", args.download) if args.corpus == "ami" else None
    table = pq.read_table(parquet)
    # Legacy filenames retained, but every previously inspected case is development data.
    choices = [(1, "dev"), (2, "holdout"), (5, "blind")] if archive else [(0, "holdout")]
    prepared = []
    for index, split in choices:
        row = table.slice(index, 1).to_pylist()[0]
        meeting = Path(row["audio"]["path"]).name.split(".")[0]
        start, end = 180.0, 480.0
        case = CACHE / "cases" / f"{args.corpus}-{meeting}-{split}"
        case.mkdir(parents=True, exist_ok=True)
        with sf.SoundFile(io.BytesIO(row["audio"]["bytes"])) as audio:
            if audio.samplerate != 16000 or audio.frames < end * audio.samplerate:
                raise ValueError("Fixture requires >=480 seconds at 16 kHz")
            audio.seek(int(start * audio.samplerate))
            samples = audio.read(int((end - start) * audio.samplerate), dtype="float32", always_2d=True)
            channels = audio.channels
        # One physical microphone, not headsets or a beamformed mixture.
        sf.write(case / "audio.wav", samples[:, 0], 16000, subtype="PCM_16")
        turns = []
        for a, b, speaker in zip(row["timestamps_start"], row["timestamps_end"], row["speakers"], strict=True):
            if b > start and a < end:
                turns.append({"start": max(a, start) - start, "end": min(b, end) - start, "speaker": speaker})
        words = ami_words(archive, meeting, start, end) if archive else []
        reference = {"id": case.name, "split": "dev-regression", "duration": end - start,
                     "recording": meeting, "corpus": args.corpus,
                     "domain": "english-meeting" if archive else "mandarin-far-field",
                     "text_timing": "word" if archive else None,
                     "language": "en" if archive else "zh", "turns": turns,
                     "words": words, "text": " ".join(w["text"] for w in words) if words else None,
                     "source": SOURCES[name][0], "source_sha256": SOURCES[name][1],
                     "annotations_sha256": SOURCES["ami-annotations.zip"][1] if archive else None,
                     "license": "CC-BY-4.0" if archive else "CC-BY-SA-4.0",
                     "attribution": "AMI Consortium" if archive else "AliMeeting / Alibaba Speech Lab",
                     "source_interval": [start, end], "source_channels": channels,
                     "selected_channel": 0, "audio_sha256": sha256(case / "audio.wav")}
        (case / "reference.json").write_text(json.dumps(reference, ensure_ascii=False, indent=2) + "\n")
        prepared.append({"id": case.name, "speakers": len({t["speaker"] for t in turns}),
                         "words": len(words), "seconds": end - start})
    print(json.dumps(prepared, indent=2))


def annotation(turns, duration):
    result = Annotation()
    for i, turn in enumerate(turns):
        a, b = turn["start"], turn["end"]
        if not math.isfinite(a) or not math.isfinite(b) or a < 0 or b <= a or b > duration + .02:
            raise ValueError(f"Invalid timeline interval: {turn}")
        # Abstentions are missing speech, never removed from the scoring region.
        if turn["speaker"] is not None:
            result[Segment(a, min(b, duration)), i] = turn["speaker"]
    return result


def diarization_scores(reference, turns, duration):
    ref, hyp = annotation(reference, duration), annotation(turns, duration)
    uem = Timeline([Segment(0, duration)])
    exact = DiarizationErrorRate(collar=0.0, skip_overlap=False)(ref, hyp, uem=uem, detailed=True)
    forgiving = DiarizationErrorRate(collar=.25, skip_overlap=False)(ref, hyp, uem=uem)
    return {"der": exact["diarization error rate"], "der_collar_250ms": forgiving,
            "jer": JaccardErrorRate(collar=0.0, skip_overlap=False)(ref, hyp, uem=uem) if ref.labels() else None,
            "reference_speaker_seconds": exact["total"],
            "missed_seconds": exact["missed detection"],
            "false_alarm_seconds": exact["false alarm"],
            "confusion_seconds": exact["confusion"],
            "hypothesis_speakers": len(hyp.labels())}


def score(reference, hypothesis, speakers=True):
    duration = reference["duration"]
    if abs(hypothesis["duration"] - duration) > .05:
        raise ValueError("Reference and hypothesis durations differ")
    if not math.isfinite(duration) or duration <= 0 or not math.isfinite(hypothesis["duration"]):
        raise ValueError("Invalid reference or hypothesis duration")
    if "words" in reference:
        annotation(reference["words"], duration)
    segments = hypothesis["segments"]
    if "".join(s["text"] for s in segments) != hypothesis["text"]:
        raise ValueError("Segment text does not reconstruct full text")
    result = {"id": reference["id"], "split": reference["split"],
              "duration": duration,
              "domain": reference.get("domain") or "meeting-" + (reference.get("language") or hypothesis["language"] or "unknown"),
              "segment_count": len(segments), "speaker_scoring_applicable": speakers,
              "review_reasons": {}, "language": hypothesis["language"]}
    if speakers:
        result["attributed_timeline"] = diarization_scores(reference["turns"], segments, duration)
    result["review_seconds"] = covered_seconds(segments, lambda s: s.get("needs_review", False))
    result["unattributed_seconds"] = covered_seconds(segments, lambda s: s["speaker"] is None)
    result["overlap_seconds"] = annotation(reference["turns"], duration).get_overlap().duration()
    result["corrections"] = correction_scores(reference, hypothesis.get("corrections", []))
    for segment in segments:
        for reason in segment.get("review_reasons", []):
            result["review_reasons"][reason] = result["review_reasons"].get(reason, 0) + 1
    if speakers and "speaker_turns" in hypothesis:
        result["raw_diarization"] = diarization_scores(reference["turns"], hypothesis["speaker_turns"], duration)
    if reference["text"] is not None:
        words = error_counts(reference["text"], hypothesis["text"])
        chars = error_counts(reference["text"], hypothesis["text"], characters=True)
        result.update(chronological_wer=words["error_rate"], cer=chars["error_rate"],
                      reference_words=words["length"], word_errors=words["errors"],
                      reference_characters=chars["length"], character_errors=chars["errors"])
        if speakers and "words" in reference:
            result["speaker_text"] = speaker_text_scores(reference["words"], segments)
    return result


def score_diarization(reference, hypothesis):
    duration = hypothesis["duration"]
    if not math.isfinite(duration) or duration <= 0 or abs(duration - reference["duration"]) > .05:
        raise ValueError("Invalid diarization hypothesis duration")
    if reference.get("speaker_scoring_applicable") is False:
        raise ValueError("Fixture cannot establish diarization accuracy")
    return {"id": reference["id"], "split": reference["split"], "duration": reference["duration"],
            "domain": reference["domain"], "speaker_scoring_applicable": True,
            "raw_diarization": diarization_scores(reference["turns"], hypothesis["speaker_turns"],
                                                 reference["duration"])}


def run(args):
    if args.candidate in ("diarization", "sortformer") and (args.no_speakers or args.prompt):
        raise ValueError("Diarization-only evaluation requires speakers and no text prompt")
    if args.candidate not in ("whisper", "diarization", "sortformer") and not args.no_speakers:
        raise ValueError("Native candidates have no verified acoustic timestamps; use --no-speakers")
    binary = args.binary.resolve()
    binary_checksum = sha256(binary)
    model_dir = args.model_dir.resolve()
    models = model_fingerprint(model_dir)
    if Path(args.label).name != args.label or args.label in (".", ".."):
        raise ValueError("Evaluation label must be a single directory name")
    if args.deny_network and sys.platform != "darwin":
        raise ValueError("This network-denied runner requires macOS sandbox-exec")
    output = CACHE / args.label
    output.mkdir(parents=True, exist_ok=True)
    cases = sorted((CACHE / "cases").glob(args.cases))
    if not cases:
        raise ValueError("No cases prepared")
    references = {}
    identities = {}
    for case in cases:
        reference = json.loads((case / "reference.json").read_text())
        if (reference["split"] == "acceptance") != args.acceptance:
            raise ValueError("Keep acceptance separate: use --acceptance with only reserved cases")
        if reference.get("speaker_scoring_applicable") is False and not args.no_speakers:
            raise ValueError("Synthetic clip replay cannot establish diarization accuracy; use --no-speakers")
        expected_checksum = None
        if "-full-" in case.name or reference["corpus"] in ("ascend", "fleurs"):
            selection_name = {"ami": "ami-expanded-selection.json", "ali": "ali-manual-selection.json",
                              "ascend": "ascend-selection.json",
                              "fleurs": "fleurs-mandarin-selection.json"}[reference["corpus"]]
            selection = json.loads((CACHE / selection_name).read_text())
            expected_checksum = selection["reference_checksums"][case.name]
        identities[case.name] = validate_case(case, reference, expected_checksum)
        references[case.name] = reference
    manifest = {"binary_sha256": binary_checksum, "models": models,
                "audio_frontend": audio_frontend_fingerprint(),
                "resource_limits": {"max_rss_bytes": MAX_RSS_BYTES, "timeout_seconds": 3600,
                                    "stop_on_macos_memory_pressure": True},
                "references": identities, "network_denied": args.deny_network,
                "requested_settings": {"candidate": args.candidate, "language": "auto", "prompt": args.prompt,
                                       "speakers": not args.no_speakers},
                "platform": platform.platform(),
                "scoring_packages": {name: importlib.metadata.version(name)
                                     for name in ("jiwer", "meeteval", "pyannote.metrics", "praatio", "psutil")},
                "scoring_sources": {name: sha256(Path(__file__).with_name(name))
                                    for name in ("evaluate.py", "metrics.py", "provenance.py", "candidates.py", "fixtures.py")}}
    manifest_path = output / "manifest.json"
    if manifest_path.exists() and json.loads(manifest_path.read_text()) != manifest:
        raise ValueError("Evaluation identity changed; use a new label for a complete run")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    manifest_checksum = sha256(manifest_path)
    reports = []
    for case in cases:
        reference = references[case.name]
        validate_case(case, reference, identities[case.name])
        destination = output / (case.name + ".json")
        command = [str(binary), str(case / "audio.wav"), "-m", str(model_dir),
                   "-f", "json", "-o", str(destination)]
        if args.prompt:
            command += ["-p", args.prompt]
        if args.no_speakers:
            command += ["--no-speakers"]
        if args.deny_network:
            command = ["/usr/bin/sandbox-exec", "-p", "(version 1)(allow default)(deny network*)"] + command
        log_path = output / (case.name + ".log")
        with log_path.open("w") as log:
            resources = run_measured(command, log, timeout=3600, max_rss_bytes=MAX_RSS_BYTES,
                                     stop_on_memory_pressure=True)
        if sha256(binary) != binary_checksum:
            raise ValueError("Binary changed during evaluation; rebuild first and rerun the complete set")
        if model_fingerprint(model_dir) != models:
            raise ValueError("Model files changed during evaluation; rerun with a new label")
        if audio_frontend_fingerprint() != manifest["audio_frontend"]:
            raise ValueError("FFmpeg changed during evaluation; rerun with a new label")
        validate_case(case, reference, identities[case.name])
        hypothesis = json.loads(destination.read_text())
        if args.candidate not in ("whisper", "diarization", "sortformer"):
            from candidates import native_hypothesis, validate_firered_aed_log, validate_nano_log, validate_qwen_log
            if args.candidate == "qwen":
                validate_qwen_log(log_path.read_text())
            elif args.candidate == "funasr-nano":
                hypothesis["settings"]["model_context_capacity"] = validate_nano_log(log_path.read_text(), hypothesis)
            elif args.candidate in ("firered-aed", "firered-aed-fp32"):
                hypothesis["settings"]["decoder_max_len"] = validate_firered_aed_log(log_path.read_text(), hypothesis)
            hypothesis = native_hypothesis(hypothesis, args.candidate)
        if args.candidate in ("diarization", "sortformer"):
            report = score_diarization(reference, hypothesis)
            hypothesis["decoding_settings"] = hypothesis["settings"]
        else:
            report = score(reference, hypothesis, speakers=not args.no_speakers)
        resources = finalize_resources(resources, log_path.read_text())
        report.update(elapsed_seconds=resources["elapsed_seconds"], resources=resources,
                      binary_sha256=binary_checksum, manifest_sha256=manifest_checksum,
                      network_denied=args.deny_network, audio_sha256=reference["audio_sha256"],
                      reference_sha256=identities[case.name], output_sha256=sha256(destination),
                      log_sha256=sha256(log_path), model=hypothesis.get("model"),
                      decoding_settings=hypothesis.get("decoding_settings"))
        reports.append(report)
        (output / "report.json").write_text(json.dumps(reports, indent=2) + "\n")
        (output / "summary.json").write_text(json.dumps(summarize(reports), indent=2) + "\n")
        print(json.dumps(report), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prep = commands.add_parser("prepare")
    prep.add_argument("--corpus", choices=["ami", "ami-expanded", "ali", "ali-manual", "ascend-mixed",
                                          "fleurs-mandarin", "nonspeech-controls"], default="ami")
    prep.add_argument("--download", action="store_true")
    prep.set_defaults(action=prepare)
    runner = commands.add_parser("run")
    runner.add_argument("--binary", type=Path, required=True)
    runner.add_argument("--model-dir", type=Path, default=Path.home() / ".cache/whisper")
    runner.add_argument("--prompt", default="")
    runner.add_argument("--no-speakers", action="store_true")
    runner.add_argument("--candidate", choices=["whisper", "qwen", "sensevoice", "funasr-nano",
                                               "firered-ctc", "firered-aed", "firered-aed-fp32",
                                               "diarization", "sortformer"], default="whisper")
    runner.add_argument("--acceptance", action="store_true")
    runner.add_argument("--label", required=True)
    runner.add_argument("--cases", default="*")
    runner.add_argument("--deny-network", action="store_true")
    runner.set_defaults(action=run)
    args = parser.parse_args()
    args.action(args)


if __name__ == "__main__":
    main()
