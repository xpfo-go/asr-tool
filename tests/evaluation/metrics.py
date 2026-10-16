"""Text attribution and review metrics for manually annotated public audio."""

from collections import defaultdict
import math
import unicodedata

import jiwer
from meeteval.wer.wer.cp import cp_word_error_rate


def normalize(text):
    text = unicodedata.normalize("NFKC", text).casefold()
    return " ".join("".join(c for c in text if c.isalnum() or c.isspace() or c == "'").split())


def error_counts(reference, hypothesis, characters=False):
    reference, hypothesis = normalize(reference), normalize(hypothesis)
    if characters:
        result = jiwer.process_characters(reference.replace(" ", ""), hypothesis.replace(" ", ""))
    else:
        result = jiwer.process_words(reference, hypothesis)
    errors = result.substitutions + result.deletions + result.insertions
    length = result.hits + result.substitutions + result.deletions
    return {"errors": errors, "length": length, "insertions": result.insertions,
            "deletions": result.deletions, "substitutions": result.substitutions,
            "error_rate": errors / length if length else None}


def speaker_streams(segments, characters=False, reference_units=False):
    streams = defaultdict(str)
    unknown = []
    for segment in sorted(segments, key=lambda s: (s["start"], s["end"])):
        if segment["speaker"] is None:
            unknown.append(segment["text"])
        else:
            streams[str(segment["speaker"])] += (" " if reference_units else "") + segment["text"]
    streams = {speaker: normalize(text) for speaker, text in streams.items()}
    unknown = normalize((" " if reference_units else "").join(unknown))
    if characters:
        streams = {speaker: " ".join(text.replace(" ", "")) for speaker, text in streams.items()}
        unknown_count = len(unknown.replace(" ", ""))
    else:
        unknown_count = len(unknown.split())
    return streams, unknown_count


def speaker_text_scores(reference, hypothesis):
    """Use one full-recording permutation per metric; unknown IDs cannot match."""
    if any(segment["speaker"] is None for segment in reference):
        raise ValueError("Manual text references must identify every speaker")
    scores = {}
    for name, characters in (("wer", False), ("cer", True)):
        ref, _ = speaker_streams(reference, characters, reference_units=True)
        hyp, unknown = speaker_streams(hypothesis, characters)
        result = cp_word_error_rate(ref, hyp, reference_sort=False, hypothesis_sort=False)
        # Unknown output has no defensible speaker mapping. Reference omissions
        # remain deletions in cpWER; its emitted text is also reported as insertions.
        scores[name] = {"assigned_cp_error_rate": result.error_rate,
                        "assigned_errors": result.errors, "reference_units": result.length,
                        "insertions": result.insertions, "deletions": result.deletions,
                        "substitutions": result.substitutions,
                        "missed_speakers": result.missed_speaker,
                        "extra_speakers": result.falarm_speaker,
                        "assignment": result.assignment,
                        "unattributed_units": unknown,
                        "errors_including_unattributed": result.errors + unknown,
                        "error_rate_including_unattributed":
                            (result.errors + unknown) / result.length if result.length else None}
    return scores


def correction_scores(reference, corrections):
    items = []
    for change in corrections:
        if not math.isfinite(change["start"]) or not math.isfinite(change["end"]) or \
                change["end"] <= change["start"] or change["start"] < 0 or change["end"] > reference["duration"] + .02:
            raise ValueError("Invalid correction interval")
        units = reference.get("words", [])
        if reference.get("text_timing") == "utterance" and any(
                w["start"] < change["end"] and w["end"] > change["start"] and
                (w["start"] < change["start"] or w["end"] > change["end"]) for w in units):
            items.append({"start": change["start"], "end": change["end"],
                          "outcome": "unscored_boundary"})
            continue
        if reference["text"] is None:
            items.append({"start": change["start"], "end": change["end"],
                          "outcome": "unscored_reference"})
            continue
        text = " ".join(w["text"] for w in units
                        if change["start"] <= (w["start"] + w["end"]) / 2 < change["end"])
        original = error_counts(text, change["original_text"], characters=True)
        revised = error_counts(text, change["corrected_text"], characters=True)
        delta = revised["errors"] - original["errors"]
        items.append({"start": change["start"], "end": change["end"],
                      "original_errors": original["errors"], "revised_errors": revised["errors"],
                      "reference_characters": original["length"],
                      "outcome": "benefit" if delta < 0 else "harm" if delta > 0 else "neutral"})
    return {"count": len(items),
            "benefit": sum(i["outcome"] == "benefit" for i in items),
            "harm": sum(i["outcome"] == "harm" for i in items),
            "neutral": sum(i["outcome"] == "neutral" for i in items),
            "unscored": sum(i["outcome"].startswith("unscored") for i in items),
            "items": items}


def covered_seconds(segments, predicate):
    intervals = sorted((s["start"], s["end"]) for s in segments if predicate(s))
    total, previous_end = 0.0, 0.0
    for start, end in intervals:
        total += max(0, end - max(start, previous_end))
        previous_end = max(previous_end, end)
    return total


def summarize(reports):
    domains = {}
    for domain in sorted({r["domain"] for r in reports}):
        cases = [r for r in reports if r["domain"] == domain]
        result = {"cases": len(cases), "audio_seconds": sum(r["duration"] for r in cases)}
        for metric, errors, length in (("cer", "character_errors", "reference_characters"),
                                       ("chronological_wer", "word_errors", "reference_words")):
            scored = [r for r in cases if length in r]
            count = sum(r[length] for r in scored)
            total = sum(r[errors] for r in scored)
            worst = max((r for r in scored if r[metric] is not None),
                        key=lambda r: r[metric], default=None)
            result[metric] = {"error_rate": total / count if count else None,
                              "errors": total, "reference_units": count,
                              "worst_case": worst["id"] if worst else None,
                              "worst_error_rate": worst[metric] if worst else None}
        for timeline in ("raw_diarization", "attributed_timeline"):
            scored = [r[timeline] for r in cases if timeline in r]
            length = sum(r["reference_speaker_seconds"] for r in scored)
            errors = sum(r["missed_seconds"] + r["false_alarm_seconds"] + r["confusion_seconds"]
                         for r in scored)
            result[timeline] = {"der": errors / length if length else None,
                                "reference_speaker_seconds": length,
                                "worst_der": max((r["der"] for r in scored), default=None)}
        for metric in ("wer", "cer"):
            scored = [r["speaker_text"][metric] for r in cases if "speaker_text" in r]
            length = sum(r["reference_units"] for r in scored)
            errors = sum(r["errors_including_unattributed"] for r in scored)
            assigned = sum(r["assigned_errors"] for r in scored)
            worst = max((r for r in cases if "speaker_text" in r and
                         r["speaker_text"][metric]["error_rate_including_unattributed"] is not None),
                        key=lambda r: r["speaker_text"][metric]["error_rate_including_unattributed"], default=None)
            result[f"speaker_text_{metric}"] = {"reference_units": length, "errors": errors,
                                                "error_rate": errors / length if length else None,
                                                "assigned_cp_error_rate": assigned / length if length else None,
                                                "worst_case": worst["id"] if worst else None,
                                                "worst_error_rate": worst["speaker_text"][metric][
                                                    "error_rate_including_unattributed"] if worst else None}
        result["peak_sampled_tree_rss_bytes"] = max(
            (r.get("resources", {}).get("peak_sampled_tree_rss_bytes", 0) for r in cases), default=0)
        result["elapsed_seconds"] = sum(r.get("elapsed_seconds", 0) for r in cases)
        result["timing_inconsistent_cases"] = [r["id"] for r in cases
            if r.get("resources", {}).get("timing_consistent") is False]
        result["timing_unchecked_cases"] = [r["id"] for r in cases
            if r.get("resources", {}).get("timing_consistent") is None]
        domains[domain] = result
    return domains
