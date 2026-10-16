"""Adapters for developer-only model comparisons, not production backends."""

import math
import re


def validate_qwen_log(log):
    if any(warning in log for warning in ("Truncating audio placeholders", "Result is truncated",
                                         "decode collapsed into a repetition loop", "leaves no room for audio",
                                         "prompt scaffold exceeds max_total_len")):
        raise ValueError("Candidate truncated audio or output; not a complete model comparison")


def validate_nano_log(log, raw):
    validate_qwen_log(log)
    if any(warning in log for warning in ("Falling back to keep last", "Invalid max_seq_len",
                                         "Unexpected logits rank", "Invalid logits shape",
                                         "ApplyKvDeltaInplace: pos0+S exceeds")):
        raise ValueError("Nano candidate failed or truncated context; not a complete model comparison")
    capacities = {int(value) for value in re.findall(r"(?m)^max_total_len=(\d+)\s*$", log)}
    if len(capacities) != 1 or min(capacities) <= 0:
        raise ValueError("Nano context capacity is not verified")
    capacity = next(iter(capacities))
    limit = raw["settings"]["max_new_tokens"]
    if not isinstance(limit, int) or limit <= 0:
        raise ValueError("Invalid Nano output token limit")
    # The native API does not expose termination reasons. Pair debug records
    # with the original tokens and conservatively reject either exhausted budget.
    records = re.findall(r"GenerateText: (?:starting prefill with context_len=(\d+),|generated (\d+) tokens:)", log)
    if len(records) != 2 * len(raw["windows"]):
        raise ValueError("Missing Nano generation diagnostics")
    for i, window in enumerate(raw["windows"]):
        context, unused = records[2 * i]
        unused_context, generated = records[2 * i + 1]
        if not context or unused or unused_context or not generated:
            raise ValueError("Unpaired Nano generation diagnostics")
        count = int(generated)
        if count >= limit or int(context) + count >= capacity:
            raise ValueError("Nano output may be truncated by the token/context limit")
        tokens = window["result"].get("tokens")
        if not isinstance(tokens, list) or len(tokens) != count:
            raise ValueError("Nano generation diagnostics and original tokens differ")
        for token in tokens:
            validate_text(token)
    return capacity


def validate_text(text):
    if not isinstance(text, str):
        raise ValueError("Candidate text must be a string")
    if "\ufffd" in text:
        raise ValueError("Candidate text contains a replacement character")
    try:
        text.encode("utf-8")
    except UnicodeEncodeError as error:
        raise ValueError("Candidate text is not valid Unicode") from error


def validate_firered_aed_log(log, raw):
    capacities = {int(value) for value in re.findall(r"(?m)^max_len=(\d+)\s*$", log)}
    if len(capacities) != 1 or min(capacities) < 2:
        raise ValueError("FireRed decoder capacity is not verified")
    capacity = next(iter(capacities))
    if raw["settings"].get("max_token_per_second") != 8:
        raise ValueError("Unverified FireRed token rate")
    for window in raw["windows"]:
        samples = window.get("samples")
        if type(samples) is not int or samples < 400:
            raise ValueError("FireRed input sample count is not verified")
        if abs(samples / 16000 - (window["end"] - window["start"])) > 2 / 16000:
            raise ValueError("FireRed input samples and audio window differ")
        # sherpa-onnx v1.13.8: 25 ms frames, 10 ms shift, snip_edges=true;
        # the AED loop is bounded by both frame duration and half max_len.
        frames = 1 + (samples - 400) // 160
        budget = min(int(frames / 100.0 * 8), capacity // 2)
        tokens = window["result"].get("tokens")
        if not isinstance(tokens, list):
            raise ValueError("Missing FireRed original tokens")
        if len(tokens) >= budget:
            raise ValueError("FireRed output may be truncated by the decoder limit")
        for token in tokens:
            validate_text(token)
    return capacity


def native_hypothesis(raw, candidate="qwen"):
    duration = raw["duration"]
    if not math.isfinite(duration) or duration <= 0:
        raise ValueError("Invalid candidate audio duration")
    segments = []
    languages = set()
    previous_end = 0
    for window in raw["windows"]:
        start, end = window["start"], window["end"]
        if not math.isfinite(start) or not math.isfinite(end) or start < previous_end or end <= start or end > duration + .02:
            raise ValueError("Candidate windows must be finite, ordered, and non-overlapping")
        previous_end = end
        result = window["result"]
        text = result["text"]
        validate_text(text)
        language = (result.get("lang") or "auto").lower()
        languages.add({"chinese": "zh", "english": "en", "<|zh|>": "zh", "<|en|>": "en",
                       "<|ja|>": "ja", "<|ko|>": "ko", "<|yue|>": "yue"}.get(language, language))
        if not text:
            continue
        # Only audio-window boundaries are known. Never invent token alignment.
        segments.append({"start": start, "end": min(end, duration),
                         "text": (" " if segments else "") + text,
                         "speaker": None, "confidence": 0, "confidence_available": False,
                         "needs_review": True, "review_reasons": ["timing_approximate"]})
    expected_model = {"qwen": "sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25",
                      "sensevoice": "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17",
                      "funasr-nano": "sherpa-onnx-funasr-nano-int8-2025-12-30",
                      "firered-ctc": "sherpa-onnx-fire-red-asr2-ctc-zh_en-int8-2026-02-25",
                      "firered-aed": "sherpa-onnx-fire-red-asr2-zh_en-int8-2026-02-26",
                      "firered-aed-fp32": "sherpa-onnx-fire-red-asr2-zh_en-2026-02-26"}[candidate]
    if raw.get("model", expected_model) != expected_model:
        raise ValueError("Native candidate model identity mismatch")
    return {"duration": duration, "language": next(iter(languages)) if len(languages) == 1 else "mixed",
            "model": expected_model,
            "text": "".join(s["text"] for s in segments), "segments": segments,
            "decoding_settings": raw["settings"]}
