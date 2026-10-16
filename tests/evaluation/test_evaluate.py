import unittest
from unittest import mock
from argparse import Namespace
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import tarfile

import evaluate
from candidates import native_hypothesis, validate_firered_aed_log, validate_nano_log, validate_qwen_log
from evaluate import annotation, diarization_scores, normalize, score, score_diarization
from fixtures import (ALI_MANUAL_CHOICES, ASCEND_SPEAKERS, alimeeting_textgrid,
                      prepare_alimeeting_manual, prepare_ascend_mixed, prepare_nonspeech_controls,
                      prepare_fleurs_mandarin, validate_selection)
from metrics import correction_scores, error_counts, speaker_text_scores, summarize
from provenance import (audio_frontend_fingerprint, finalize_resources, kernel_peak_rss,
                        memory_pressure_level, model_fingerprint, run_measured, sha256, validate_case)
from praatio import textgrid
import pyarrow as pa
import pyarrow.parquet as pq
import soundfile as sf


class EvaluationTest(unittest.TestCase):
    def test_diarization_only_scores_overlap_and_unknown_without_text_claims(self):
        reference = {"id": "meeting", "split": "dev", "duration": 3, "domain": "meeting",
                     "text": "not transcribed", "turns": [
                         {"start": 0, "end": 2, "speaker": "a"},
                         {"start": 1, "end": 3, "speaker": "b"}]}
        hypothesis = {"duration": 3, "speaker_turns": [
            {"start": 0, "end": 2, "speaker": "x"},
            {"start": 1, "end": 3, "speaker": None}]}
        result = score_diarization(reference, hypothesis)
        self.assertEqual(result["raw_diarization"]["der"], .5)
        self.assertEqual(result["raw_diarization"]["missed_seconds"], 2)
        for name in ("cer", "speaker_text", "attributed_timeline"):
            self.assertNotIn(name, result)
        self.assertIsNone(summarize([result])["meeting"]["cer"]["error_rate"])
        for duration in (0, float("nan"), 4):
            with self.assertRaisesRegex(ValueError, "duration"):
                score_diarization(reference, dict(hypothesis, duration=duration))
        reference["speaker_scoring_applicable"] = False
        with self.assertRaisesRegex(ValueError, "cannot establish"):
            score_diarization(reference, hypothesis)

    def test_diarization_runner_rejects_text_options(self):
        for candidate in ("diarization", "sortformer"):
            for no_speakers, prompt in ((True, ""), (False, "terms")):
                with self.subTest(candidate=candidate, no_speakers=no_speakers):
                    with self.assertRaisesRegex(ValueError, "requires speakers"):
                        evaluate.run(Namespace(candidate=candidate, no_speakers=no_speakers, prompt=prompt))

    def test_sortformer_runner_scores_timelines_without_asr_fields(self):
        with tempfile.TemporaryDirectory() as root, mock.patch("sys.stdout", new_callable=io.StringIO):
            cache = Path(root)
            case = cache / "cases/fixture"
            case.mkdir(parents=True)
            (case / "audio.wav").write_bytes(b"audio")
            turns = [{"start": 0, "end": 1, "speaker": "a"}]
            reference = {"id": case.name, "corpus": "test", "domain": "meeting-en",
                         "duration": 1, "split": "dev", "turns": turns,
                         "audio_sha256": sha256(case / "audio.wav")}
            (case / "reference.json").write_text(json.dumps(reference))
            args = Namespace(binary=Path("/usr/bin/true"), candidate="sortformer", model_dir=cache,
                             label="sortformer", cases="*", deny_network=False,
                             no_speakers=False, prompt="", acceptance=False)

            def measured(command, log, **kwargs):
                Path(command[command.index("-o") + 1]).write_text(json.dumps({
                    "duration": 1, "speaker_turns": turns, "settings": {"threshold": .5}}))
                return {"elapsed_seconds": 1}

            with mock.patch.object(evaluate, "CACHE", cache), mock.patch(
                    "evaluate.model_fingerprint", return_value={}), mock.patch(
                    "evaluate.audio_frontend_fingerprint", return_value={}), mock.patch(
                    "evaluate.run_measured", side_effect=measured):
                evaluate.run(args)
            report = json.loads((cache / args.label / "report.json").read_text())[0]
            self.assertEqual(report["raw_diarization"]["der"], 0)
            self.assertEqual(report["decoding_settings"], {"threshold": .5})
            self.assertNotIn("cer", report)

    def test_model_fingerprint_accepts_onnx_only_and_rejects_empty_weights(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            (directory / "tokens.txt").write_text("tokens")
            with self.assertRaisesRegex(ValueError, "No model weight"):
                model_fingerprint(directory)
            (directory / "embedding.onnx").write_bytes(b"model")
            self.assertEqual(model_fingerprint(directory)["embedding.onnx"]["sha256"],
                             sha256(directory / "embedding.onnx"))

    def test_model_fingerprint_accepts_gguf_and_rejects_partial_downloads(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            model = directory / "model.gguf"
            model.write_bytes(b"GGUF-test-fixture")
            self.assertEqual(model_fingerprint(directory)[model.name]["sha256"], sha256(model))
            model.rename(directory / "model.gguf.part")
            with self.assertRaisesRegex(ValueError, "incomplete"):
                model_fingerprint(directory)

    def test_fleurs_uses_all_human_clips_in_frozen_order_without_speaker_claims(self):
        with tempfile.TemporaryDirectory() as root, mock.patch("sys.stdout", new_callable=io.StringIO):
            cache = Path(root)
            audio = io.BytesIO()
            sf.write(audio, [.25] * 16000, 16000, format="WAV", subtype="PCM_16")
            rows = [{"id": i // 2, "path": f"{i:02}.wav", "num_samples": 16000,
                     "raw_transcription": f"不要{i}，repeat repeat。", "transcription": "not the raw reference",
                     "audio": {"bytes": audio.getvalue(), "path": f"{i:02}.wav"}}
                    for i in reversed(range(25))]
            parquet = cache / "fleurs.parquet"
            pq.write_table(pa.Table.from_pylist(rows), parquet)
            prepare_fleurs_mandarin(cache, parquet, "https://example.invalid/fleurs", sha256(parquet))
            selection = json.loads((cache / "fleurs-mandarin-selection.json").read_text())
            self.assertEqual(selection["clips"], 25)
            self.assertEqual(selection["acceptance_recordings"], [])
            self.assertFalse(selection["speaker_disjointness_verified"])
            self.assertEqual(len(selection["reference_checksums"]), 2)
            case = cache / "cases/fleurs-cmn-00-replay-dev"
            reference = json.loads((case / "reference.json").read_text())
            self.assertEqual(reference["source_clips"][0]["path"], "00.wav")
            self.assertEqual(reference["source_clips"][-1]["path"], "23.wav")
            self.assertEqual(reference["words"][0]["text"], "不要0，repeat repeat。")
            self.assertEqual((reference["words"][0]["start"], reference["words"][0]["end"]), (1, 2))
            self.assertEqual(reference["duration"], 49)
            self.assertEqual(reference["turns"], [])
            self.assertFalse(reference["speaker_scoring_applicable"])
            self.assertTrue(all(w["speaker"] is None for w in reference["words"]))
            validate_case(case, reference, selection["reference_checksums"][case.name])
            samples, rate = sf.read(case / "audio.wav")
            self.assertEqual(rate, 16000)
            self.assertEqual(samples[:16000].max(), 0)
            self.assertEqual(samples[16000:32000].min(), .25)
            self.assertEqual(samples[32000:48000].max(), 0)
            original = selection["reference_checksums"]
            prepare_fleurs_mandarin(cache, parquet, "https://example.invalid/fleurs", sha256(parquet))
            self.assertEqual(original, json.loads((cache / "fleurs-mandarin-selection.json").read_text())[
                "reference_checksums"])

    def test_fleurs_rejects_changed_source_duplicate_clip_and_bad_manual_reference(self):
        with tempfile.TemporaryDirectory() as root, mock.patch("sys.stdout", new_callable=io.StringIO):
            cache = Path(root)
            audio = io.BytesIO()
            sf.write(audio, [0.] * 16000, 16000, format="WAV", subtype="PCM_16")
            row = {"id": 1, "path": "clip.wav", "num_samples": 16000, "raw_transcription": "不要12",
                   "audio": {"bytes": audio.getvalue(), "path": "clip.wav"}}
            parquet = cache / "fleurs.parquet"
            pq.write_table(pa.Table.from_pylist([row]), parquet)
            with self.assertRaisesRegex(ValueError, "checksum"):
                prepare_fleurs_mandarin(cache, parquet, "source", "changed")
            for rows, message in (([row, row], "Duplicate"),
                                  ([dict(row, num_samples=32000)], "sample count"),
                                  ([dict(row, raw_transcription="")], "human transcript")):
                pq.write_table(pa.Table.from_pylist(rows), parquet)
                with self.assertRaisesRegex(ValueError, message):
                    prepare_fleurs_mandarin(cache, parquet, "source", sha256(parquet))

    def test_fleurs_prepare_cli_routes_pinned_validation_source(self):
        args = Namespace(corpus="fleurs-mandarin", download=False)
        parquet = Path("fleurs.parquet")
        with mock.patch("evaluate.source", return_value=parquet) as source, mock.patch(
                "fixtures.prepare_fleurs_mandarin") as prepare:
            evaluate.prepare(args)
        source.assert_called_once_with("fleurs-cmn-validation.parquet", False)
        prepare.assert_called_once_with(evaluate.CACHE, parquet, *evaluate.SOURCES["fleurs-cmn-validation.parquet"])

    def test_fleurs_runner_rejects_a_changed_frozen_reference(self):
        with tempfile.TemporaryDirectory() as root:
            cache = Path(root)
            case = cache / "cases/fleurs-cmn-00-replay-dev"
            case.mkdir(parents=True)
            (case / "audio.wav").write_bytes(b"audio")
            reference = {"id": case.name, "corpus": "fleurs", "duration": 1, "split": "dev",
                         "speaker_scoring_applicable": False, "audio_sha256": sha256(case / "audio.wav")}
            (case / "reference.json").write_text(json.dumps(reference))
            (cache / "fleurs-mandarin-selection.json").write_text(json.dumps({
                "reference_checksums": {case.name: "original-frozen-checksum"}}))
            args = Namespace(binary=Path("/usr/bin/true"), candidate="whisper", model_dir=cache, label="guard",
                             cases="fleurs-*", deny_network=False, no_speakers=True, prompt="", acceptance=False)
            with mock.patch.object(evaluate, "CACHE", cache), mock.patch(
                    "evaluate.model_fingerprint", return_value={}):
                with self.assertRaisesRegex(ValueError, "reference changed"):
                    evaluate.run(args)

    def test_nonspeech_controls_are_reproducible_and_have_empty_references(self):
        with tempfile.TemporaryDirectory() as root, mock.patch("sys.stdout", new_callable=io.StringIO):
            cache = Path(root)
            first = prepare_nonspeech_controls(cache, duration=1)
            second = prepare_nonspeech_controls(cache, duration=1)
            self.assertEqual([r["audio_sha256"] for r in first], [r["audio_sha256"] for r in second])
            self.assertEqual(len(first), 4)
            for reference in first:
                self.assertEqual(reference["text"], "")
                self.assertEqual(reference["turns"], [])
                self.assertFalse(reference["speaker_scoring_applicable"])
                case = cache / "cases" / reference["id"]
                validate_case(case, reference)
                samples, sample_rate = sf.read(case / "audio.wav")
                self.assertEqual((len(samples), sample_rate), (16000, 16000))
                if reference["id"] == "control-silence-dev":
                    self.assertTrue((samples == 0).all())

    def test_speaker_permutation_is_not_an_error(self):
        reference = [{"start": 0, "end": 1, "speaker": "A"},
                     {"start": 1, "end": 2, "speaker": "B"}]
        hypothesis = [{"start": 0, "end": 1, "speaker": "speaker_2"},
                      {"start": 1, "end": 2, "speaker": "speaker_1"}]
        self.assertEqual(diarization_scores(reference, hypothesis, 2)["der"], 0)

    def test_unknown_is_missed_speech(self):
        reference = [{"start": 0, "end": 2, "speaker": "A"}]
        hypothesis = [{"start": 0, "end": 2, "speaker": None}]
        self.assertEqual(diarization_scores(reference, hypothesis, 2)["der"], 1)

    def test_overlap_is_included(self):
        reference = [{"start": 0, "end": 2, "speaker": "A"},
                     {"start": 0, "end": 2, "speaker": "B"}]
        result = diarization_scores(reference, reference[:1], 2)
        self.assertEqual(result["der"], .5)
        self.assertEqual(result["missed_seconds"], 2)

    def test_malformed_times_fail(self):
        with self.assertRaises(ValueError):
            annotation([{"start": float("nan"), "end": 2, "speaker": "A"}], 2)
        with self.assertRaises(ValueError):
            annotation([{"start": 0, "end": 3, "speaker": "A"}], 2)

    def test_text_integrity_and_silence_insertions(self):
        reference = {"duration": 2, "id": "silence", "split": "dev", "turns": [], "text": ""}
        hypothesis = {"duration": 2, "language": "en", "segments": [], "text": "invented"}
        with self.assertRaises(ValueError):
            score(reference, hypothesis)
        hypothesis["segments"] = [{"start": 0, "end": 2, "speaker": None, "text": "invented"}]
        self.assertEqual(score(reference, hypothesis)["word_errors"], 1)

    def test_normalization_preserves_numbers_and_negation(self):
        self.assertEqual(normalize("NOT １２, don't 没有。"), "not 12 don't 没有")

    def test_empty_nonspeech_language_keeps_domain_and_scores_zero_insertions(self):
        reference = {"duration": 2, "id": "silence", "split": "dev", "turns": [],
                     "text": "", "language": None, "domain": "nonspeech-control"}
        hypothesis = {"duration": 2, "language": "", "segments": [], "text": ""}
        report = score(reference, hypothesis, speakers=False)
        self.assertEqual(report["domain"], "nonspeech-control")
        self.assertEqual((report["word_errors"], report["character_errors"]), (0, 0))
        del reference["domain"]
        self.assertEqual(score(reference, hypothesis, speakers=False)["domain"], "meeting-unknown")

    def test_speaker_text_global_permutation(self):
        reference = [{"start": 0, "end": 1, "speaker": "A", "text": "one two"},
                     {"start": 1, "end": 2, "speaker": "B", "text": "three four"}]
        hypothesis = [dict(reference[0], speaker="speaker_2"), dict(reference[1], speaker="speaker_1")]
        result = speaker_text_scores(reference, hypothesis)
        self.assertEqual(result["wer"]["assigned_errors"], 0)
        self.assertEqual(result["cer"]["assigned_errors"], 0)
        # Reassigning one fragment of a speaker cannot receive a new permutation.
        hypothesis.append({"start": 2, "end": 3, "speaker": "speaker_1", "text": " one two"})
        self.assertGreater(speaker_text_scores(reference, hypothesis)["wer"]["assigned_errors"], 0)

    def test_unknown_text_cannot_match_a_reference_speaker(self):
        reference = [{"start": 0, "end": 1, "speaker": "A", "text": "不要增加12"}]
        hypothesis = [dict(reference[0], speaker=None)]
        result = speaker_text_scores(reference, hypothesis)["cer"]
        self.assertEqual(result["assigned_cp_error_rate"], 1)
        self.assertEqual(result["unattributed_units"], 6)
        self.assertEqual(result["error_rate_including_unattributed"], 2)

    def test_merged_and_split_speaker_text_is_penalized(self):
        reference = [{"start": 0, "end": 1, "speaker": "A", "text": "one"},
                     {"start": 1, "end": 2, "speaker": "B", "text": "two"}]
        merged = [dict(reference[0], speaker="h"), dict(reference[1], speaker="h", text=" two")]
        self.assertGreater(speaker_text_scores(reference, merged)["wer"]["assigned_errors"], 0)
        same = [dict(reference[0], speaker="A"), dict(reference[1], speaker="A")]
        self.assertGreater(speaker_text_scores(same, reference)["wer"]["assigned_errors"], 0)

    def test_chinese_character_and_overlapping_speaker_text(self):
        reference = [{"start": 0, "end": 2, "speaker": "A", "text": "没有12"},
                     {"start": 0, "end": 2, "speaker": "B", "text": "对"}]
        result = speaker_text_scores(reference, reference[:1])["cer"]
        self.assertEqual(result["reference_units"], 5)
        self.assertEqual(result["assigned_errors"], 1)

    def test_hypothesis_token_fragments_do_not_invent_word_boundaries(self):
        reference = [{"start": 0, "end": 1, "speaker": "A", "text": "cat"}]
        hypothesis = [dict(reference[0], end=.5, text="ca"), dict(reference[0], start=.5, text="t")]
        self.assertEqual(speaker_text_scores(reference, hypothesis)["wer"]["assigned_errors"], 0)
        unknown = [dict(s, speaker=None) for s in hypothesis]
        self.assertEqual(speaker_text_scores(reference, unknown)["wer"]["unattributed_units"], 1)

    def test_silence_attribution_keeps_unknown_insertions(self):
        hypothesis = [{"start": 0, "end": 1, "speaker": None, "text": "invented"}]
        result = speaker_text_scores([], hypothesis)["wer"]
        self.assertIsNone(result["assigned_cp_error_rate"])
        self.assertEqual(result["errors_including_unattributed"], 1)
        self.assertEqual(error_counts("", "invented")["errors"], 1)

    def test_correction_benefits_and_harms_are_separate(self):
        reference = {"duration": 2, "text": "不要 12", "words": [
            {"start": 0, "end": 1, "speaker": "A", "text": "不要"},
            {"start": 1, "end": 2, "speaker": "A", "text": "12"}]}
        corrections = [{"start": 0, "end": 1, "original_text": "需要", "corrected_text": "不要"},
                       {"start": 1, "end": 2, "original_text": "12", "corrected_text": "13"}]
        result = correction_scores(reference, corrections)
        self.assertEqual((result["benefit"], result["harm"], result["unscored"]), (1, 1, 0))
        reference["text_timing"] = "utterance"
        corrections[0]["end"] = .5
        self.assertEqual(correction_scores(reference, corrections)["unscored"], 1)
        corrections[0]["start"] = float("nan")
        with self.assertRaises(ValueError):
            correction_scores(reference, corrections)

    def test_model_fingerprint_detects_drift_and_follows_coreml_symlinks(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root) / "models"
            directory.mkdir()
            (directory / "model.bin").write_bytes(b"model")
            external = Path(root) / "encoder.mlmodelc"
            external.mkdir()
            (external / "weights.bin").write_bytes(b"weights")
            (directory / "encoder.mlmodelc").symlink_to(external, target_is_directory=True)
            original = model_fingerprint(directory)
            self.assertIn("encoder.mlmodelc/weights.bin", original)
            (external / "weights.bin").write_bytes(b"changed")
            self.assertNotEqual(original, model_fingerprint(directory))
            (directory / "download.bin.part").touch()
            with self.assertRaises(ValueError):
                model_fingerprint(directory)

    def test_audio_frontend_detects_same_version_binary_replacement(self):
        with tempfile.TemporaryDirectory() as root:
            binary = Path(root) / "ffmpeg-real"
            binary.write_bytes(b"first build")
            link = Path(root) / "ffmpeg"
            link.symlink_to(binary)
            version = subprocess.CompletedProcess([], 0, stdout="ffmpeg version test\nlibavfilter 10\n")
            with mock.patch("provenance.shutil.which", return_value=str(link)), mock.patch(
                    "provenance.subprocess.run", return_value=version):
                first = audio_frontend_fingerprint()
                binary.write_bytes(b"rebuilt same version")
                second = audio_frontend_fingerprint()
            self.assertEqual(first["path"], str(binary.resolve()))
            self.assertEqual(first["version"], second["version"])
            self.assertNotEqual(first["sha256"], second["sha256"])

    def test_audio_frontend_requires_available_identified_ffmpeg(self):
        with mock.patch("provenance.shutil.which", return_value=None):
            with self.assertRaisesRegex(ValueError, "required"):
                audio_frontend_fingerprint()
        with mock.patch("provenance.shutil.which", return_value="/usr/bin/true"), mock.patch(
                "provenance.subprocess.run", return_value=subprocess.CompletedProcess([], 0, stdout="")):
            with self.assertRaisesRegex(ValueError, "Unrecognized"):
                audio_frontend_fingerprint()

    def test_runner_rejects_audio_frontend_change_before_scoring(self):
        with tempfile.TemporaryDirectory() as root:
            cache = Path(root)
            case = cache / "cases/fixture"
            case.mkdir(parents=True)
            (case / "audio.wav").write_bytes(b"audio")
            reference = {"id": case.name, "corpus": "test", "duration": 1, "split": "dev",
                         "audio_sha256": sha256(case / "audio.wav")}
            (case / "reference.json").write_text(json.dumps(reference))
            args = Namespace(binary=Path("/usr/bin/true"), candidate="whisper", model_dir=cache,
                             label="frontend-drift", cases="*", deny_network=False,
                             no_speakers=True, prompt="", acceptance=False)
            with mock.patch.object(evaluate, "CACHE", cache), mock.patch(
                    "evaluate.model_fingerprint", return_value={}), mock.patch(
                    "evaluate.audio_frontend_fingerprint", side_effect=[{"sha256": "a"}, {"sha256": "b"}]), mock.patch(
                    "evaluate.run_measured", return_value={}) as measured, mock.patch("evaluate.score") as score_case:
                with self.assertRaisesRegex(ValueError, "FFmpeg changed"):
                    evaluate.run(args)
                score_case.assert_not_called()
                self.assertEqual(measured.call_args.kwargs["max_rss_bytes"], 8 * 1024**3)
                self.assertTrue(measured.call_args.kwargs["stop_on_memory_pressure"])
            self.assertFalse((cache / args.label / "report.json").exists())

    def test_reference_identity_and_checksum_drift(self):
        with tempfile.TemporaryDirectory() as root:
            case = Path(root) / "case"
            case.mkdir()
            (case / "audio.wav").write_bytes(b"audio")
            reference = {"id": "case", "duration": 1, "audio_sha256": sha256(case / "audio.wav")}
            (case / "reference.json").write_text(json.dumps(reference))
            checksum = validate_case(case, reference)
            (case / "reference.json").write_text(json.dumps(reference) + "\n")
            with self.assertRaises(ValueError):
                validate_case(case, reference, checksum)
            (case / "audio.wav").write_bytes(b"different")
            with self.assertRaises(ValueError):
                validate_case(case, reference)

    def test_sampled_resources_and_failure_exit(self):
        with tempfile.TemporaryFile(mode="w+") as log:
            result = run_measured(["/bin/sleep", "0.1"], log, timeout=2)
            self.assertGreater(result["samples"], 0)
            self.assertGreater(result["peak_sampled_tree_rss_bytes"], 0)
            with self.assertRaises(subprocess.CalledProcessError):
                run_measured(["/usr/bin/false"], log, timeout=2)
            with self.assertRaises(subprocess.TimeoutExpired):
                run_measured(["/bin/sleep", "5"], log, timeout=.1)
        self.assertEqual(kernel_peak_rss("   12345  maximum resident set size\n"), 12345)

    def test_memory_budget_terminates_process_and_keeps_diagnostic(self):
        with tempfile.TemporaryFile(mode="w+") as log, mock.patch("os.killpg", wraps=os.killpg) as kill:
            with self.assertRaisesRegex(MemoryError, "exceeds.*1 bytes"):
                run_measured(["/bin/sleep", "5"], log, timeout=2, max_rss_bytes=1)
            kill.assert_called_once()
            with self.assertRaises(ProcessLookupError):
                os.kill(kill.call_args.args[0], 0)
            log.seek(0)
            self.assertIn("exceeds evaluation memory budget of 1 bytes", log.read())

    def test_host_memory_pressure_stops_below_rss_limit(self):
        with tempfile.TemporaryFile(mode="w+") as log, mock.patch(
                "provenance.memory_pressure_level", return_value=2), mock.patch("os.killpg", wraps=os.killpg) as kill:
            with self.assertRaisesRegex(MemoryError, "host memory pressure level 2"):
                run_measured(["/bin/sleep", "5"], log, timeout=2,
                             max_rss_bytes=8 * 1024**3, stop_on_memory_pressure=True)
            kill.assert_called_once()
            with self.assertRaises(ProcessLookupError):
                os.kill(kill.call_args.args[0], 0)
            log.seek(0)
            self.assertIn("host memory pressure level 2", log.read())

    def test_memory_pressure_measurement_is_explicit(self):
        with mock.patch("provenance.sys.platform", "darwin"), mock.patch("provenance.subprocess.run") as run:
            for level in (1, 2, 4):
                run.return_value.stdout = f"{level}\n"
                self.assertEqual(memory_pressure_level(), level)
            run.return_value.stdout = "0\n"
            with self.assertRaisesRegex(ValueError, "Unrecognized"):
                memory_pressure_level()
            run.side_effect = subprocess.CalledProcessError(1, "sysctl")
            with self.assertRaises(subprocess.CalledProcessError):
                memory_pressure_level()
        with mock.patch("provenance.sys.platform", "linux"), mock.patch("provenance.subprocess.run") as run:
            self.assertIsNone(memory_pressure_level())
            run.assert_not_called()
        with tempfile.TemporaryFile(mode="w+") as log, mock.patch(
                "provenance.memory_pressure_level", return_value=1):
            result = run_measured(["/bin/sleep", "0.1"], log, timeout=2, stop_on_memory_pressure=True)
            self.assertEqual(result["memory_pressure_peak_level"], 1)
            self.assertGreater(result["memory_pressure_samples"], 0)

    def test_resource_timing_disagreement(self):
        measured = {"elapsed_seconds": 97.48, "peak_sampled_tree_rss_bytes": 12345}
        log = "  1823.54 real 275.38 user 3.77 sys\n  12345 maximum resident set size\n"
        result = finalize_resources(measured, log)
        self.assertEqual(result["elapsed_seconds"], 1823.54)
        self.assertEqual(result["monotonic_elapsed_seconds"], 97.48)
        self.assertEqual(result["kernel_peak_process_rss_bytes"], 12345)
        self.assertFalse(result["timing_consistent"])
        self.assertEqual(measured["elapsed_seconds"], 97.48)
        self.assertTrue(finalize_resources(measured, "97.40 real 275.38 user 3.77 sys\n")[
            "timing_consistent"])
        self.assertIsNone(finalize_resources(measured, "")["timing_consistent"])
        with self.assertRaises(ValueError):
            finalize_resources(measured, log + log)

    def test_summary_retains_timing_anomalies(self):
        cases = [{"id": "a", "domain": "zh", "duration": 1, "elapsed_seconds": 20,
                  "resources": {"timing_consistent": False}},
                 {"id": "b", "domain": "zh", "duration": 1, "elapsed_seconds": 2}]
        result = summarize(cases)["zh"]
        self.assertEqual(result["elapsed_seconds"], 22)
        self.assertEqual(result["timing_inconsistent_cases"], ["a"])
        self.assertEqual(result["timing_unchecked_cases"], ["b"])

    def test_summary_weights_by_reference_units(self):
        cases = [{"id": "a", "domain": "zh", "duration": 1, "cer": .1,
                  "reference_characters": 10, "character_errors": 1},
                 {"id": "b", "domain": "zh", "duration": 1, "cer": .5,
                  "reference_characters": 2, "character_errors": 1}]
        result = summarize(cases)["zh"]["cer"]
        self.assertEqual(result["error_rate"], 2 / 12)
        self.assertEqual(result["worst_case"], "b")

    def test_textgrid_preserves_speaker_text_and_overlap(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "manual.TextGrid"
            grid = textgrid.Textgrid()
            grid.addTier(textgrid.IntervalTier("speaker-A", [(0, 1, "不要12"), (1, 2, "")], 0, 2))
            grid.addTier(textgrid.IntervalTier("speaker-B", [(.5, 1.5, "yes")], 0, 2))
            grid.save(str(path), format="long_textgrid", includeBlankSpaces=True)
            units = alimeeting_textgrid(path, 2)
            self.assertEqual([w["text"] for w in units], ["不要12", "yes"])
            self.assertEqual({w["speaker"] for w in units}, {"speaker-A", "speaker-B"})
            self.assertEqual(diarization_scores(units, units, 2)["der"], 0)
            with self.assertRaises(ValueError):
                alimeeting_textgrid(path, .5)

    def test_frozen_selection_rejects_recording_and_speaker_leakage(self):
        reference = {"recording": "r1", "corpus": "ami", "duration": 1, "split": "dev",
                     "turns": [{"start": 0, "end": 1, "speaker": "A"}]}
        acceptance = dict(reference, split="acceptance")
        with self.assertRaises(ValueError):
            validate_selection([reference, acceptance])
        acceptance["recording"] = "r2"
        with self.assertRaises(ValueError):
            validate_selection([reference, acceptance])
        acceptance["turns"] = [{"start": 0, "end": 1, "speaker": "B"}]
        self.assertEqual(validate_selection([reference, acceptance])["shared_speakers"], [])

    def test_candidate_keeps_audio_window_timing_without_speaker_claims(self):
        raw = {"duration": 2, "settings": {"acoustic_timestamps": False}, "windows": [
            {"start": 0, "end": 1, "result": {"text": "不要12", "lang": "Chinese"}},
            {"start": 1, "end": 2, "result": {"text": "yes", "lang": "English"}}]}
        hypothesis = native_hypothesis(raw)
        self.assertEqual(hypothesis["text"], "不要12 yes")
        self.assertEqual(hypothesis["language"], "mixed")
        self.assertTrue(all(s["speaker"] is None for s in hypothesis["segments"]))
        reference = {"id": "candidate", "split": "dev", "duration": 2, "turns": [], "text": "不要12 yes"}
        report = score(reference, hypothesis, speakers=False)
        self.assertEqual(report["cer"], 0)
        self.assertNotIn("attributed_timeline", report)
        self.assertNotIn("speaker_text", report)
        raw["windows"][1]["start"] = .5
        with self.assertRaises(ValueError):
            native_hypothesis(raw)

    def test_failed_download_keeps_partial_for_a_fresh_resume(self):
        with tempfile.TemporaryDirectory() as root:
            cache = Path(root)
            partial = cache / "sources" / "source.bin.part"
            partial.parent.mkdir()
            partial.write_bytes(b"partial")
            with mock.patch.object(evaluate, "CACHE", cache), mock.patch.dict(
                    evaluate.SOURCES, {"source.bin": ("https://example.invalid/source", "not-used")}), mock.patch(
                    "evaluate.subprocess.run", side_effect=subprocess.CalledProcessError(28, "curl")) as run:
                with self.assertRaises(subprocess.CalledProcessError):
                    evaluate.source("source.bin", True)
                self.assertEqual(partial.read_bytes(), b"partial")
                command = run.call_args.args[0]
                self.assertIn("--continue-at", command)
                self.assertNotIn("--retry", command)

    def test_candidate_empty_language_and_truncation_guard(self):
        raw = {"duration": 1, "settings": {}, "windows": [
            {"start": 0, "end": 1, "result": {"text": "yes", "lang": ""}}]}
        self.assertEqual(native_hypothesis(raw)["language"], "auto")
        validate_qwen_log("normal decoding output")
        for warning in ("Truncating audio placeholders", "Result is truncated",
                        "decode collapsed into a repetition loop", "leaves no room for audio",
                        "prompt scaffold exceeds max_total_len"):
            with self.assertRaisesRegex(ValueError, "truncated"):
                validate_qwen_log(warning)

    def test_sensevoice_identity_and_language_tags(self):
        raw = {"duration": 1, "settings": {"use_itn": False}, "windows": [
            {"start": 0, "end": 1, "result": {"text": "不要十二", "lang": "<|zh|>"}}]}
        hypothesis = native_hypothesis(raw, "sensevoice")
        self.assertEqual(hypothesis["language"], "zh")
        self.assertEqual(hypothesis["text"], "不要十二")
        self.assertFalse(hypothesis["decoding_settings"]["use_itn"])
        self.assertIsNone(hypothesis["segments"][0]["speaker"])
        raw["model"] = "some-other-model"
        with self.assertRaisesRegex(ValueError, "identity"):
            native_hypothesis(raw, "sensevoice")

    def test_nano_keeps_original_text_without_speaker_or_alignment_claims(self):
        raw = {"duration": 1, "settings": {"max_new_tokens": 256, "use_itn": False}, "windows": [
            {"start": 0, "end": 1, "result": {"text": "不要十二 repeat repeat", "tokens": ["不要", "十二"]}}]}
        log = "max_total_len=1024\nGenerateText: starting prefill with context_len=100,\nGenerateText: generated 2 tokens:"
        self.assertEqual(validate_nano_log("qwen3_asr(max_total_len=2048)\n" + log, raw), 1024)
        hypothesis = native_hypothesis(raw, "funasr-nano")
        self.assertEqual(hypothesis["text"], "不要十二 repeat repeat")
        self.assertEqual(hypothesis["language"], "auto")
        self.assertIsNone(hypothesis["segments"][0]["speaker"])
        self.assertEqual(hypothesis["segments"][0]["review_reasons"], ["timing_approximate"])
        raw["model"] = "unverified-export"
        with self.assertRaisesRegex(ValueError, "identity"):
            native_hypothesis(raw, "funasr-nano")

    def test_nano_rejects_exhausted_output_or_context_budget(self):
        for context, count in ((100, 256), (1022, 2)):
            raw = {"settings": {"max_new_tokens": 256}, "windows": [
                {"result": {"tokens": ["x"] * count}}]}
            log = f"max_total_len=1024\nGenerateText: starting prefill with context_len={context},\nGenerateText: generated {count} tokens:"
            with self.assertRaisesRegex(ValueError, "truncated"):
                validate_nano_log(log, raw)

    def test_nano_rejects_missing_mismatched_or_unpaired_diagnostics(self):
        raw = {"settings": {"max_new_tokens": 256}, "windows": [{"result": {"tokens": ["x"]}}]}
        prefix = "max_total_len=1024\n"
        context = "GenerateText: starting prefill with context_len=100,\n"
        output = "GenerateText: generated 1 tokens:\n"
        for log, message in ((context + output, "capacity"), (prefix + context, "Missing"),
                             (prefix + output + context, "Unpaired"),
                             (prefix + context + output.replace("1 tokens", "2 tokens"), "differ")):
            with self.assertRaisesRegex(ValueError, message):
                validate_nano_log(log, raw)
        validate_nano_log(prefix, {"settings": {"max_new_tokens": 256}, "windows": []})

    def test_nano_rejects_native_truncation_and_failed_logits(self):
        for warning in ("Truncating audio placeholders", "Falling back to keep last",
                        "Invalid max_seq_len", "Unexpected logits rank", "Invalid logits shape",
                        "ApplyKvDeltaInplace: pos0+S exceeds"):
            with self.assertRaisesRegex(ValueError, "truncated"):
                validate_nano_log(warning, {})

    def test_candidate_rejects_invalid_unicode_without_silent_repair(self):
        for text in ("bad\ufffd", "bad\ud800"):
            raw = {"duration": 1, "settings": {}, "windows": [
                {"start": 0, "end": 1, "result": {"text": text}}]}
            with self.assertRaises(ValueError):
                native_hypothesis(raw)
        raw = {"settings": {"max_new_tokens": 256}, "windows": [{"result": {"tokens": ["\ufffd"]}}]}
        log = "max_total_len=1024\nGenerateText: starting prefill with context_len=100,\nGenerateText: generated 1 tokens:"
        with self.assertRaisesRegex(ValueError, "replacement"):
            validate_nano_log(log, raw)

    def test_firered_precision_identity_cannot_be_mislabeled(self):
        identities = {"firered-aed": "sherpa-onnx-fire-red-asr2-zh_en-int8-2026-02-26",
                      "firered-aed-fp32": "sherpa-onnx-fire-red-asr2-zh_en-2026-02-26"}
        for candidate, model in identities.items():
            raw = {"duration": 1, "settings": {}, "model": model, "windows": [
                {"start": 0, "end": 1, "result": {"text": "不要十二 repeat repeat"}}]}
            self.assertEqual(native_hypothesis(raw, candidate)["model"], model)
            other = next(name for name in identities if name != candidate)
            with self.assertRaisesRegex(ValueError, "identity"):
                native_hypothesis(raw, other)

    def test_firered_rejects_duration_and_context_exhaustion(self):
        raw = {"settings": {"max_token_per_second": 8}, "windows": [
            {"start": 0, "end": 1, "samples": 16000,
             "result": {"tokens": ["x"] * 6}}]}
        self.assertEqual(validate_firered_aed_log("max_len=1024\n", raw), 1024)
        with self.assertRaisesRegex(ValueError, "truncated"):
            validate_firered_aed_log("max_len=12\n", raw)
        raw["windows"][0]["result"]["tokens"].append("x")
        with self.assertRaisesRegex(ValueError, "truncated"):
            validate_firered_aed_log("max_len=1024\n", raw)

    def test_firered_rejects_unverified_frame_and_token_inputs(self):
        raw = {"settings": {"max_token_per_second": 8}, "windows": [
            {"start": 1, "end": 2, "samples": 16000,
             "result": {"tokens": ["不要", "twelve"]}}]}
        for log in ("", "max_len=0\n", "max_len=12\nmax_len=1024\n"):
            with self.assertRaisesRegex(ValueError, "capacity"):
                validate_firered_aed_log(log, raw)
        for samples in (None, True, 399, 32000):
            raw["windows"][0]["samples"] = samples
            with self.assertRaises(ValueError):
                validate_firered_aed_log("max_len=1024\n", raw)
        raw["windows"][0]["samples"] = 16000
        raw["windows"][0]["result"]["tokens"] = ["\ufffd"]
        with self.assertRaisesRegex(ValueError, "replacement"):
            validate_firered_aed_log("max_len=1024\n", raw)

    def test_official_ali_prepare_cli_routes_pinned_source(self):
        args = Namespace(corpus="ali-manual", download=False)
        archive = Path("official.tar.gz")
        with mock.patch("evaluate.source", return_value=archive) as source, mock.patch(
                "fixtures.prepare_alimeeting_manual") as prepare:
            evaluate.prepare(args)
        source.assert_called_once_with("Eval_Ali.tar.gz", False)
        prepare.assert_called_once_with(evaluate.CACHE, archive, *evaluate.SOURCES["Eval_Ali.tar.gz"])

    def test_ascend_replay_uses_human_mixed_clips_and_disjoint_speakers(self):
        with tempfile.TemporaryDirectory() as root:
            cache = Path(root)
            audio = io.BytesIO()
            sf.write(audio, [.25] * 16000, 16000, format="WAV", subtype="PCM_16")
            sources = {}
            for split, speakers in ASCEND_SPEAKERS.items():
                rows = []
                for speaker in sorted(speakers):
                    for language, text in (("zh", "不能删掉"), ("mixed", "不要12 repeat repeat")):
                        rows.append({"id": str(len(rows)), "path": f"ses1_spk{speaker}_{language}.wav",
                                     "original_speaker_id": speaker, "session_id": 1,
                                     "duration": 1., "language": language, "transcription": text,
                                     "audio": {"bytes": audio.getvalue(), "path": "clip.wav"}})
                parquet = cache / (split + ".parquet")
                pq.write_table(pa.Table.from_pylist(rows), parquet)
                sources[split] = (parquet, "https://example.invalid/" + split, sha256(parquet))
            prepare_ascend_mixed(cache, sources)
            selection = json.loads((cache / "ascend-selection.json").read_text())
            self.assertEqual(selection["mixed_clips"], 5)
            self.assertEqual(selection["shared_speakers"], [])
            case = cache / "cases/ascend-spk7-ses1-mixed-dev"
            reference = json.loads((case / "reference.json").read_text())
            self.assertTrue(reference["synthetic_replay"])
            self.assertFalse(reference["speaker_scoring_applicable"])
            self.assertEqual(reference["text"], "不要12 repeat repeat")
            self.assertEqual(reference["words"][0]["start"], 1)
            self.assertEqual(reference["words"][0]["end"], 2)
            self.assertEqual(reference["duration"], 3)
            samples, rate = sf.read(case / "audio.wav")
            self.assertEqual(rate, 16000)
            self.assertEqual(samples[:16000].max(), 0)
            self.assertEqual(samples[16000:32000].min(), .25)
            self.assertEqual(samples[32000:].max(), 0)
            validate_case(case, reference, selection["reference_checksums"][case.name])
            sources["validation"] = (*sources["validation"][:2], "invalid-checksum")
            with self.assertRaisesRegex(ValueError, "checksum"):
                prepare_ascend_mixed(cache, sources)

    def test_replay_cannot_be_reported_as_a_speaker_benchmark(self):
        with tempfile.TemporaryDirectory() as root:
            cache = Path(root)
            case = cache / "cases/ascend-example-dev"
            case.mkdir(parents=True)
            (case / "reference.json").write_text(json.dumps({"id": case.name, "split": "dev",
                                                             "speaker_scoring_applicable": False}))
            args = Namespace(candidate="whisper", no_speakers=False, binary=Path("/usr/bin/true"),
                             model_dir=cache, deny_network=False, label="replay", cases="ascend-*", acceptance=False)
            with mock.patch.object(evaluate, "CACHE", cache), mock.patch(
                    "evaluate.model_fingerprint", return_value={}):
                with self.assertRaisesRegex(ValueError, "cannot establish diarization"):
                    evaluate.run(args)

    def test_official_ali_preparation_uses_only_far_field_and_freezes_split(self):
        with tempfile.TemporaryDirectory() as root:
            cache = Path(root)
            archive = cache / "official.tar.gz"
            audio = io.BytesIO()
            sf.write(audio, [[0, .25]] * 16000, 16000, format="WAV", subtype="PCM_16")
            with tarfile.open(archive, "w:gz") as package:
                for i, recording in enumerate(ALI_MANUAL_CHOICES):
                    grid_path = cache / "manual.TextGrid"
                    grid = textgrid.Textgrid()
                    grid.addTier(textgrid.IntervalTier(f"N_SPK{i}", [(0, 1, "不要12")], 0, 1))
                    grid.save(str(grid_path), format="long_textgrid", includeBlankSpaces=True)
                    for name, data in [
                            (f"Eval_Ali/Eval_Ali_far/audio_dir/{recording}.wav", audio.getvalue()),
                            (f"Eval_Ali/Eval_Ali_far/textgrid_dir/{recording.rsplit('_', 1)[0]}.TextGrid", grid_path.read_bytes()),
                            (f"Eval_Ali/Eval_Ali_near/audio_dir/{recording}.wav", b"not audio")]:
                        member = tarfile.TarInfo(name)
                        member.size = len(data)
                        package.addfile(member, io.BytesIO(data))
            prepare_alimeeting_manual(cache, archive, "https://example.invalid/official", sha256(archive))
            selection = json.loads((cache / "ali-manual-selection.json").read_text())
            self.assertEqual(selection["audio_seconds"], 8)
            self.assertEqual(len(selection["acceptance_recordings"]), 2)
            recording = next(iter(ALI_MANUAL_CHOICES))
            case = cache / "cases" / f"ali-{recording}-full-dev"
            reference = json.loads((case / "reference.json").read_text())
            self.assertEqual(reference["text_timing"], "utterance")
            self.assertEqual(reference["source_channels"], 2)
            samples, _ = sf.read(case / "audio.wav")
            self.assertEqual(samples.max(), 0)
            self.assertEqual(reference["text"], "不要12")

    def test_runner_rejects_accidental_acceptance_and_changed_frozen_reference(self):
        with tempfile.TemporaryDirectory() as root:
            cache = Path(root)
            binary = cache / "binary"
            binary.write_bytes(b"binary")
            models = cache / "models"
            models.mkdir()
            (models / "model.bin").write_bytes(b"model")
            case = cache / "cases" / "ami-test-full-acceptance"
            case.mkdir(parents=True)
            (case / "audio.wav").write_bytes(b"audio")
            reference = {"id": case.name, "corpus": "ami", "duration": 1, "split": "acceptance",
                         "audio_sha256": sha256(case / "audio.wav")}
            (case / "reference.json").write_text(json.dumps(reference))
            (cache / "ami-expanded-selection.json").write_text(json.dumps({
                "reference_checksums": {case.name: "original-frozen-checksum"}}))
            args = Namespace(binary=binary, candidate="whisper", model_dir=models, label="guard-test",
                             cases="*", deny_network=False, no_speakers=False, prompt="", acceptance=False)
            with mock.patch.object(evaluate, "CACHE", cache):
                with self.assertRaisesRegex(ValueError, "Keep acceptance separate"):
                    evaluate.run(args)
                args.acceptance = True
                with self.assertRaisesRegex(ValueError, "reference changed"):
                    evaluate.run(args)


if __name__ == "__main__":
    unittest.main()
