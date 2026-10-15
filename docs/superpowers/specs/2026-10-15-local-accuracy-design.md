# Local Accuracy and Live Transcription

## Product Contract

All audio recognition, speaker analysis, and correction run on the user's
machine. Network access is limited to explicit model/data preparation.
The primary target is an Apple M2 Pro with 16 GiB unified memory. NVIDIA
support remains a subsequent portability target, not a verified claim.

File transcription remains `asr-tool recording.mp4`, with an optional `-p`
terminology prompt. Model choices, thresholds, and scheduling are internal;
users do not edit configuration files. Accuracy takes priority over file
processing speed. Normal transcription must not download missing models.

"Online" means local live input, not a cloud meeting service. The intended
live command is `asr-tool --live`, using the default microphone. Do not start
microphone capture during development: use paced public audio replay instead.
Live output is provisional first, then revised from the original audio by
a stronger local recognizer. Revisions replace an identified utterance rather
than appending duplicate text. JSON live output uses explicit revision and
finalization events. File JSON and subtitle formats retain their contracts.

## Existing Evidence

The four old five-minute samples are development regression cases, even where
their directory names say holdout or blind. All have already informed previous
experiments. Current English chronological WER is 22.01-31.79%; attributed DER
is 27.40-57.67% on AMI and 52.06% on the Chinese AliMeeting sample. The latter
has no text reference in the current parquet source. These results do not
establish commercial readiness or Chinese text accuracy.

## Evaluation Design

1. Obtain public, manually transcribed Chinese speech and far-field meetings,
   English meetings, and genuine mixed-language recordings where accessible.
   Record upstream revisions, licenses, checksums, recording channels, and
   fixed intervals. Never fabricate references with an ASR model.
2. Split by original recording and, where possible, speaker identity. Reserve
   new recordings for final acceptance before model comparisons. The old
   samples remain regression cases. Public data may have been in model
   training; disclose this limitation rather than calling it unseen training
   data. At least three hours across multiple recordings are required for
   the initial product evaluation, not a universal accuracy guarantee.
3. Measure normalized text CER/WER, speaker-attributed text error with a
   single global speaker permutation, raw and attributed DER, abstentions,
   overlap, correction regressions, review workload, elapsed time, and peak
   memory. Unknown speakers and difficult spans remain in denominators.
   Report per-recording results and the worst recording, not only averages.
4. Retain existing chronological WER as a regression metric, not a standard
   overlap-aware leaderboard score. Use an established scoring library for
   speaker-attributed text rather than implementing a new metric.
5. Run the tested process with network denied. Fingerprint the executable,
   model weights, fixture audio, annotations, and effective decoding settings.
   Reject mixed-version evaluation reports.

## Model and Pipeline Selection

Compare the current Whisper Turbo against full Whisper large-v3 and a
Chinese/multilingual local candidate supported by the existing native runtime.
Qwen3-ASR and FunASR/SenseVoice are candidates, not committed product choices.
Check weight licenses, native API availability, timestamp support, memory,
and actual performance before adoption. Do not add a Python service or cloud
dependency just to follow a published model benchmark.

Hold segmentation, prompts, scoring, and recording intervals constant for the
first model comparisons. Then isolate changes to VAD/window boundaries,
speaker clustering, and acoustic alignment. Accept defaults only after an
independent acceptance run. Do not tune on the acceptance results repeatedly;
a failed run becomes development data and requires fresh acceptance data.

Speaker assignment must follow acoustic evidence, not the meaning of an
utterance or meeting minutes. Preserve uncertain and overlapping speech
instead of confidently attributing it to the wrong person. Better text alone
does not prove better viewpoint attribution.

Corrections must be based on re-decoding the original audio. Preserve the
original hypothesis and the revision reason. Protect numbers and negation;
measure harms separately from beneficial edits. Do not let a language model
rewrite statements simply because they sound more fluent. Existing `-p`
terminology input is the only user-facing vocabulary mechanism.

## Live Scheduling

A lightweight local first pass owns the latency-sensitive path. A stronger
local second pass receives completed audio spans through a bounded queue.
Audio ingestion and first-pass decoding must not wait for correction or
speaker clustering. Keep at most 20 seconds of pending correction audio in
memory; overload leaves provisional text explicitly unverified rather than
silently losing input or growing memory without bound. Recorded audio can
still be processed in accurate file mode afterwards.

Prefer a small native streaming recognizer that can run alongside a quantized
review model. If measured memory pressure or contention makes the combination
unusable, serialize expensive work and disclose slower finalization. Do not
claim real-time correction from first-pass throughput alone.

## Acceptance Gates

These are initial engineering targets, not achieved results or industry-wide
definitions of commercial accuracy:

| Condition | Target |
| --- | --- |
| Clear Mandarin | CER <= 5% |
| Far-field Mandarin meeting | CER <= 10% |
| English meeting | WER <= 15% |
| Meeting speaker timeline, overlap included, zero collar | DER <= 15% |
| Runtime privacy | No network during inference or correction |
| Peak process memory on 16 GiB Mac | <= 8 GiB, plus observed system pressure |
| Live provisional update, warmed models | p95 <= 1.5 s after audio availability |
| Live finalization after endpoint, warmed models | p95 <= 8 s |
| Live pending review audio | <= 20 s |

Text targets apply per domain; also report speaker-attributed text error and
mixed-language results. No gate can be passed by removing uncertain output,
overlap, numbers, or silence hallucinations. Resource measurements include
both recognition passes together and a paced sustained replay, not only an
isolated fast model. Failures remain open work with evidence in the report.
Only after the public gates are met should private meeting validation resume.

## Implementation Sequence

1. Expand and test public references/scoring; freeze a new acceptance manifest.
2. Benchmark compatible local model candidates and reject regressions.
3. Integrate the measured file-mode winner and improve acoustic attribution.
4. Add local first-pass/live revision scheduling with bounded resources.
5. Verify unit tests, complete public acceptance, sustained replay, offline
   behavior, installation, and simple CLI usage. Keep private data out of Git.

No unrelated refactors, generated model/audio assets, private recordings, or
private transcripts are part of the committed source changes.
