"""Preparation of expanded public fixtures; never run a recognizer here."""

import io
import json
import math
from pathlib import Path
import shutil
import tarfile
import tempfile

import numpy as np
from praatio import textgrid
import pyarrow.parquet as pq
import soundfile as sf

from provenance import sha256

ALI_MANUAL_CHOICES = {
    "R8001_M8004_MS801": "dev",
    "R8003_M8001_MS801": "acceptance",
    "R8007_M8010_MS803": "dev",
    "R8007_M8011_MS806": "dev",
    "R8008_M8013_MS807": "acceptance",
    "R8009_M8018_MS809": "dev",
    "R8009_M8019_MS810": "dev",
    "R8009_M8020_MS810": "dev",
}

ASCEND_SPLITS = {"validation": "dev", "test": "acceptance"}
ASCEND_SPEAKERS = {"validation": {7, 12, 20}, "test": {3, 17}}


def alimeeting_textgrid(path, duration):
    grid = textgrid.openTextgrid(str(path), includeEmptyIntervals=False, reportingMode="error")
    units = []
    for name in grid.tierNames:
        tier = grid.getTier(name)
        if not isinstance(tier, textgrid.IntervalTier):
            raise ValueError(f"Unexpected point tier in meeting reference: {name}")
        for start, end, label in tier.entries:
            text = label.strip()
            if not text:
                continue
            if not math.isfinite(start) or not math.isfinite(end) or start < 0 or end <= start or end > duration + .02:
                raise ValueError(f"Invalid manual text interval: {path}: {start}, {end}")
            units.append({"start": start, "end": min(end, duration), "text": text, "speaker": name})
    return sorted(units, key=lambda w: (w["start"], w["end"], w["speaker"]))


def validate_selection(references, require_disjoint_speakers=True):
    development = [r for r in references if r["split"].startswith("dev")]
    acceptance = [r for r in references if r["split"] == "acceptance"]
    recordings = {r["recording"] for r in development} & {r["recording"] for r in acceptance}
    if recordings:
        raise ValueError(f"Recording leakage in selection: {sorted(recordings)}")
    dev_speakers = {(r["corpus"], t["speaker"]) for r in development for t in r["turns"]}
    accept_speakers = {(r["corpus"], t["speaker"]) for r in acceptance for t in r["turns"]}
    shared = dev_speakers & accept_speakers
    if shared and require_disjoint_speakers:
        raise ValueError(f"Speaker leakage in selection: {sorted(shared)}")
    return {"audio_seconds": sum(r["duration"] for r in references),
            "development_recordings": sorted(r["recording"] for r in development),
            "acceptance_recordings": sorted(r["recording"] for r in acceptance),
            "shared_speakers": sorted(shared)}


def write_case(cache, reference, samples):
    case = cache / "cases" / reference["id"]
    case.mkdir(parents=True, exist_ok=True)
    sf.write(case / "audio.wav", samples, 16000, subtype="PCM_16")
    reference["audio_sha256"] = sha256(case / "audio.wav")
    (case / "reference.json").write_text(json.dumps(reference, ensure_ascii=False, indent=2) + "\n")
    return reference


def prepare_nonspeech_controls(cache, duration=60):
    if not isinstance(duration, int) or duration <= 0:
        raise ValueError("Control duration must be a positive integer")
    count = duration * 16000
    rng = np.random.Generator(np.random.PCG64(20261015))
    noise = rng.standard_normal(count)
    noise /= np.sqrt(np.mean(noise ** 2))
    time = np.arange(count) / 16000
    signals = {"silence": np.zeros(count), "quiet-noise": noise * .005,
               "loud-noise": noise * .05, "tone": .2 * np.sin(2 * np.pi * 5274 * time)}
    prepared = []
    for name, samples in signals.items():
        reference = {"id": f"control-{name}-dev", "recording": f"synthetic-{name}",
                     "corpus": "control", "split": "dev-control", "domain": "nonspeech-control",
                     "duration": duration, "language": None, "turns": [], "words": [], "text": "",
                     "text_timing": "word", "speaker_scoring_applicable": False,
                     "synthetic_control": True, "source_channels": 1, "selected_channel": 0,
                     "generator": {"kind": name, "seed": 20261015, "algorithm": "PCG64",
                                   "numpy_version": np.__version__, "sample_rate": 16000,
                                   "source_sha256": sha256(Path(__file__))}}
        prepared.append(write_case(cache, reference, samples.astype(np.float32)))
    print(json.dumps([{k: r[k] for k in ("id", "duration", "audio_sha256")}
                      for r in prepared], indent=2))
    return prepared


def prepare_fleurs_mandarin(cache, parquet, url, checksum):
    """Replay the complete official validation split; no speaker IDs are provided."""
    if sha256(parquet) != checksum:
        raise ValueError("FLEURS source checksum mismatch")
    rows = pq.read_table(parquet).to_pylist()
    if not rows:
        raise ValueError("No FLEURS validation clips")
    paths = [Path(r["path"]).name for r in rows]
    if len(set(paths)) != len(paths):
        raise ValueError("Duplicate FLEURS clip paths")
    rows.sort(key=lambda r: Path(r["path"]).name)
    references = []
    original_frames = 0
    for offset in range(0, len(rows), 24):
        group = offset // 24
        recording = f"cmn-validation-replay{group:02}"
        blocks = [np.zeros(16000, dtype=np.float32)]
        frames = 16000
        units, clips = [], []
        for row in rows[offset:offset + 24]:
            text = row["raw_transcription"]
            if not isinstance(text, str) or not text.strip():
                raise ValueError("Missing human transcript in FLEURS")
            with sf.SoundFile(io.BytesIO(row["audio"]["bytes"])) as audio:
                if audio.samplerate != 16000 or audio.channels != 1 or audio.frames <= 0:
                    raise ValueError("FLEURS requires nonempty mono 16 kHz clips")
                if audio.frames != row["num_samples"]:
                    raise ValueError("FLEURS sample count mismatch")
                samples = audio.read(dtype="float32")
            start, end = frames / 16000, (frames + len(samples)) / 16000
            units.append({"start": start, "end": end, "text": text, "speaker": None})
            clips.append({"id": row["id"], "path": Path(row["path"]).name,
                          "replay_interval": [start, end], "num_samples": len(samples)})
            blocks.extend((samples, np.zeros(16000, dtype=np.float32)))
            frames += len(samples) + 16000
            original_frames += len(samples)
        reference = {"id": f"fleurs-cmn-{group:02}-replay-dev", "recording": recording,
                     "split": "dev", "corpus": "fleurs", "domain": "mandarin-read-replay",
                     "duration": frames / 16000, "language": "zh", "text_timing": "utterance",
                     "text": " ".join(w["text"] for w in units), "words": units, "turns": [],
                     "source": url, "source_sha256": checksum, "upstream_split": "validation",
                     "reference_field": "raw_transcription", "source_clips": clips,
                     "source_channels": 1, "selected_channel": 0,
                     "synthetic_replay": True, "replay_gap_seconds": 1,
                     "speaker_scoring_applicable": False, "speaker_ids_available": False,
                     "selection_manifest": "fleurs-mandarin-selection.json",
                     "license": "CC-BY-4.0", "attribution": "Google FLEURS / Conneau et al. (2022)"}
        references.append(write_case(cache, reference, np.concatenate(blocks)))
    selection = validate_selection(references)
    selection.update(clips=len(rows), original_audio_seconds=original_frames / 16000,
                     speaker_disjointness_verified=False,
                     reference_checksums={r["id"]: sha256(cache / "cases" / r["id"] / "reference.json")
                                          for r in references})
    (cache / "fleurs-mandarin-selection.json").write_text(json.dumps(selection, indent=2) + "\n")
    print(json.dumps(selection, indent=2))
    return references


def prepare_ami_expanded(cache, parquet, archive, sources, read_words):
    table = pq.read_table(parquet)
    # EN2002 speakers have not appeared in the development recordings. The two
    # other sessions intentionally stay in development beside their old clips.
    choices = {"EN2002b": "acceptance", "ES2004b": "dev", "TS3003b": "dev"}
    references = []
    for row in table.to_pylist():
        meeting = Path(row["audio"]["path"]).name.split(".")[0]
        if meeting not in choices:
            continue
        with sf.SoundFile(io.BytesIO(row["audio"]["bytes"])) as audio:
            if audio.samplerate != 16000:
                raise ValueError("AMI fixture must be 16 kHz")
            duration = audio.frames / audio.samplerate
            samples = audio.read(dtype="float32", always_2d=True)[:, 0]
            channels = audio.channels
        words = read_words(archive, meeting, 0, duration)
        if not words:
            raise ValueError(f"No manual AMI text for {meeting}")
        turns = [{"start": max(0, a), "end": min(duration, b), "speaker": speaker}
                 for a, b, speaker in zip(row["timestamps_start"], row["timestamps_end"], row["speakers"], strict=True)
                 if b > 0 and a < duration]
        reference = {"id": f"ami-{meeting}-full-{choices[meeting]}", "split": choices[meeting],
                     "recording": meeting, "corpus": "ami", "domain": "english-meeting",
                     "duration": duration, "language": "en", "text_timing": "word",
                     "turns": turns, "words": words, "text": " ".join(w["text"] for w in words),
                     "source": sources["ami-test-00.parquet"][0],
                     "source_sha256": sources["ami-test-00.parquet"][1],
                     "annotations_sha256": sources["ami-annotations.zip"][1],
                     "license": "CC-BY-4.0", "attribution": "AMI Consortium",
                     "source_interval": [0, duration], "source_channels": channels, "selected_channel": 0}
        references.append(write_case(cache, reference, samples))
    if {r["recording"] for r in references} != set(choices):
        raise ValueError("Pinned AMI source does not contain the selected recordings")
    selection = validate_selection(references)
    selection["reference_checksums"] = {r["id"]: sha256(cache / "cases" / r["id"] / "reference.json")
                                       for r in references}
    (cache / "ami-expanded-selection.json").write_text(json.dumps(selection, indent=2) + "\n")
    print(json.dumps(selection, indent=2))


def prepare_alimeeting_manual(cache, archive, url, checksum):
    if sha256(archive) != checksum:
        raise ValueError("AliMeeting archive checksum mismatch")
    references = []
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ali-manual-", dir=cache) as temporary:
        extracted = Path(temporary)
        expected = {}
        for recording in ALI_MANUAL_CHOICES:
            expected[f"Eval_Ali/Eval_Ali_far/audio_dir/{recording}.wav"] = extracted / (recording + ".wav")
            meeting = recording.rsplit("_", 1)[0]
            expected[f"Eval_Ali/Eval_Ali_far/textgrid_dir/{meeting}.TextGrid"] = extracted / (meeting + ".TextGrid")
        found = set()
        # Copy only exact allowlisted regular members; never extract archive paths.
        with tarfile.open(archive, "r|gz") as package:
            for member in package:
                if member.name not in expected:
                    continue
                if not member.isfile() or member.name in found:
                    raise ValueError(f"Invalid or duplicate source member: {member.name}")
                found.add(member.name)
                with package.extractfile(member) as source, expected[member.name].open("wb") as target:
                    shutil.copyfileobj(source, target)
        if found != set(expected):
            raise ValueError(f"Missing official far-field source members: {sorted(set(expected) - found)}")
        for recording, split in ALI_MANUAL_CHOICES.items():
            audio_path = extracted / (recording + ".wav")
            grid_path = extracted / (recording.rsplit("_", 1)[0] + ".TextGrid")
            case_id = f"ali-{recording}-full-{split}"
            case = cache / "cases" / case_id
            case.mkdir(parents=True, exist_ok=True)
            with sf.SoundFile(audio_path) as audio, sf.SoundFile(
                    case / "audio.wav", "w", samplerate=16000, channels=1, subtype="PCM_16") as target:
                if audio.samplerate != 16000 or audio.frames <= 0:
                    raise ValueError("Official AliMeeting fixture requires nonempty 16 kHz audio")
                duration = audio.frames / audio.samplerate
                channels = audio.channels
                for block in audio.blocks(blocksize=16000 * 10, dtype="float32", always_2d=True):
                    target.write(block[:, 0])
            units = alimeeting_textgrid(grid_path, duration)
            if not units:
                raise ValueError(f"Missing manual transcript for {recording}")
            reference = {"id": case_id, "split": split, "recording": recording, "corpus": "ali",
                         "domain": "mandarin-far-field", "duration": duration, "language": "zh",
                         "text_timing": "utterance", "words": units,
                         "turns": [{k: u[k] for k in ("start", "end", "speaker")} for u in units],
                         "text": " ".join(u["text"] for u in units),
                         "source": url, "source_sha256": checksum,
                         "source_audio_sha256": sha256(audio_path), "annotations_sha256": sha256(grid_path),
                         "license": "CC-BY-SA-4.0", "attribution": "AliMeeting / Alibaba Speech Lab",
                         "source_interval": [0, duration], "source_channels": channels, "selected_channel": 0,
                         "audio_sha256": sha256(case / "audio.wav")}
            (case / "reference.json").write_text(json.dumps(reference, ensure_ascii=False, indent=2) + "\n")
            references.append(reference)
    selection = validate_selection(references)
    # Old public snippets have also been used for development. Include their
    # speaker identities in the leakage audit, without counting their audio twice.
    legacy = []
    for path in (cache / "cases").glob("ali-*/reference.json"):
        reference = json.loads(path.read_text())
        if reference["id"] not in {r["id"] for r in references}:
            legacy.append(dict(reference, corpus="ali", split="dev-regression",
                               recording=reference.get("recording", reference["id"])))
    validate_selection(references + legacy)
    selection["reference_checksums"] = {r["id"]: sha256(cache / "cases" / r["id"] / "reference.json")
                                       for r in references}
    (cache / "ali-manual-selection.json").write_text(json.dumps(selection, indent=2) + "\n")
    print(json.dumps(selection, indent=2))


def prepare_ascend_mixed(cache, sources):
    """Replay all manually labelled mixed-language clips, not continuous meetings."""
    references = []
    for upstream_split, split in ASCEND_SPLITS.items():
        parquet, url, checksum = sources[upstream_split]
        if sha256(parquet) != checksum:
            raise ValueError("ASCEND source checksum mismatch")
        rows = pq.read_table(parquet).to_pylist()
        if {r["original_speaker_id"] for r in rows} != ASCEND_SPEAKERS[upstream_split]:
            raise ValueError("Pinned ASCEND speaker identities changed")
        if len({r["id"] for r in rows}) != len(rows):
            raise ValueError("Duplicate ASCEND source IDs")
        groups = {}
        for row in rows:
            if row["language"] == "mixed":
                key = (row["original_speaker_id"], row["session_id"])
                groups.setdefault(key, []).append(row)
        if not groups:
            raise ValueError("No human-labelled mixed-language ASCEND clips")
        for (speaker_id, session_id), clips in sorted(groups.items()):
            speaker = f"ASCEND-spk{speaker_id}"
            recording = f"spk{speaker_id}-ses{session_id}"
            blocks = [np.zeros(16000, dtype=np.float32)]
            frames = 16000
            units, source_clips = [], []
            # Row IDs specify a reproducible replay order, not original turn timing.
            for row in sorted(clips, key=lambda r: r["id"]):
                with sf.SoundFile(io.BytesIO(row["audio"]["bytes"])) as audio:
                    if audio.samplerate != 16000 or audio.channels != 1 or audio.frames <= 0:
                        raise ValueError("ASCEND requires nonempty mono 16 kHz clips")
                    samples = audio.read(dtype="float32")
                duration = len(samples) / 16000
                if not math.isfinite(row["duration"]) or abs(duration - row["duration"]) > .003:
                    raise ValueError("ASCEND clip duration mismatch")
                text = row["transcription"]
                if not isinstance(text, str) or not text.strip():
                    raise ValueError("Missing human ASCEND transcript")
                start = frames / 16000
                end = start + duration
                units.append({"start": start, "end": end, "text": text, "speaker": speaker})
                source_clips.append({"id": row["id"], "path": Path(row["path"]).name,
                                     "replay_interval": [start, end]})
                blocks.extend((samples, np.zeros(16000, dtype=np.float32)))
                frames += len(samples) + 16000
            reference = {"id": f"ascend-{recording}-mixed-{split}", "split": split,
                         "recording": recording, "corpus": "ascend", "domain": "code-switch-replay",
                         "duration": frames / 16000, "language": "mixed", "text_timing": "utterance",
                         "words": units, "turns": [{k: u[k] for k in ("start", "end", "speaker")} for u in units],
                         "text": " ".join(u["text"] for u in units),
                         "source": url, "source_sha256": checksum, "upstream_split": upstream_split,
                         "source_clips": source_clips, "source_channels": 1, "selected_channel": 0,
                         "synthetic_replay": True, "replay_gap_seconds": 1,
                         "speaker_scoring_applicable": False, "selection_manifest": "ascend-selection.json",
                         "license": "CC-BY-SA-4.0", "attribution": "CAiRE / ASCEND"}
            references.append(write_case(cache, reference, np.concatenate(blocks)))
    selection = validate_selection(references)
    selection["mixed_clips"] = sum(len(r["source_clips"]) for r in references)
    selection["reference_checksums"] = {r["id"]: sha256(cache / "cases" / r["id"] / "reference.json")
                                       for r in references}
    (cache / "ascend-selection.json").write_text(json.dumps(selection, indent=2) + "\n")
    print(json.dumps(selection, indent=2))
