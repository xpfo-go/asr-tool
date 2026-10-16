# Public Evaluation

This repository keeps developer-only public evaluations for local ASR and
speaker diarization. Data, models, generated transcripts, and reports stay in
the ignored `.cache/evaluation/` directory. No private meeting recording is
needed for this stage. The application has no Python evaluation dependency.

The v2.0.0-rc file candidate has now run the frozen meeting acceptance split.
Commercial-grade accuracy remains a long-term goal, not a release requirement
for this version. The numerical targets below guide future optimization.
Product direction is recorded in [PRODUCT_DIRECTIONS.md](PRODUCT_DIRECTIONS.md):
retain the measured default, improve usability, and publish the observed results
with the prerelease. No private recording or microphone is used in this run.

## RC Meeting Acceptance (2026-10-16)

The unchanged product candidate processed all three reserved full meetings,
101.57 minutes, with network denied, no prompt, automatic language selection,
Turbo q8, ERes2Net, and acoustic timestamps. Results include overlap at zero
collar; unknown assignments remain in the denominator.

| Reserved recording | Text error | Raw DER | Attributed DER | Speaker text error including unknown |
|---|---:|---:|---:|---:|
| AliMeeting R8003 | CER 40.12% | 21.44% | 46.09% | CER 66.14% |
| AliMeeting R8008 | CER 84.76% | 24.53% | 83.29% | CER 90.20% |
| AMI EN2002b | Chronological WER 39.99% | 35.99% | 61.56% | WER 86.40% |

Reference-weighted Mandarin CER is **60.98%**, raw DER **23.01%**, and
attributed DER **65.03%**. The worst Mandarin recording is retained, not dropped.
Chronological WER is this project's regression metric, not an overlap-aware
leaderboard score. The speaker-text column uses the documented global
permutation and adds unattributed units; it is not interchangeable with DER.

Elapsed inference time totals 655.67 seconds; maximum sampled process-tree RSS
is 1.49 GiB, excluding ANE/driver memory. Memory pressure remained normal and
both elapsed clocks agreed. These are file-mode observations. Build/tooling
checks also ran on this host, so this is not a controlled speed comparison.

Reports: `.cache/evaluation/v2.0.0-rc-meetings-acceptance/`, including the
manifest, per-recording JSON/logs, `report.json`, and `summary.json`.
Product executable SHA-256:
`f912f92c0140da26297f325c809bee2aac323ab367e5256dc5410bcdba297e8a`.
Manifest SHA-256:
`e6f55491fd37e78d4cf5b38d01c47d73d51fb89c4a912ba680ef0f847d5d0fef`.

This split was reserved before model selection and is now consumed. No
thresholds, models, or segmentation were tuned on these results. Future tuning
on these recordings makes them development data and requires a new acceptance
split. Private meeting validation remains paused for **v2.0.0-rc**.

## RC Mixed-Language Acceptance (2026-10-16)

All seven reserved ASCEND speaker/session groups completed in file mode with
the same product binary, models, audio frontend, decoding settings, and scoring
sources as recorded in their manifests. These are 373 human-labelled clips,
32.25 replay minutes including silent gaps; no speaker accuracy is inferred.

| Group | CER | Character errors / reference characters |
|---|---:|---:|
| spk17-ses1 | 31.18% | 106 / 340 |
| spk17-ses2 | 50.32% | 158 / 314 |
| spk17-ses3 | 22.14% | 263 / 1188 |
| spk17-ses4 | 16.25% | 222 / 1366 |
| spk3-ses1 | 11.78% | 392 / 3327 |
| spk3-ses2 | 9.01% | 105 / 1165 |
| spk3-ses3 | 10.10% | 194 / 1920 |
| Reference-weighted total | **14.97%** | **1440 / 9620** |

The 50.32% worst group remains a substantial product limitation. This is a
different split from the 24.34% development result; the difference is not an
accuracy improvement attributable to the release.

The first run stopped during spk17-ses3 when macOS reported elevated memory
pressure (level 2). It produced no score for that incomplete case. After pressure
returned to normal, the five remaining groups ran with unchanged settings;
the two already completed groups were not rerun. Completed reports contain each
reserved case exactly once. Successful inference totaled 133.63 seconds, with
1.07 GiB maximum sampled process-tree RSS and consistent elapsed clocks. This
runtime total excludes the aborted attempt; the interruption is not erased.

Reports and logs are retained under `.cache/evaluation/`:
`v2.0.0-rc-mixed-acceptance`, `v2.0.0-rc-mixed-remaining17`, and
`v2.0.0-rc-mixed-remaining3`. Cross-report executable, model, frontend, settings,
and scoring fingerprints were checked before aggregation. These reserved groups
are now consumed, just like the meeting split; no tuning followed the scores.

## Data

The evaluator downloads fixed public sources only when explicitly requested:

- AMI meeting audio and manual word annotations, CC BY 4.0.
- AliMeeting far-field meeting audio and official manual TextGrids, CC BY-SA 4.0.
- ASCEND spontaneous Chinese-English clips with original human transcripts,
  CC BY-SA 4.0. Do not substitute ASR-rewritten `ASCEND_CLEAN` references.
- Google FLEURS official Mandarin validation recordings and human read-speech
  transcripts, CC BY 4.0.

Source URLs, commit/revision identifiers, SHA-256 checksums, selected intervals,
and attribution are recorded in `tests/evaluation/evaluate.py` and each prepared
case's `reference.json`. Only one physical far-field microphone channel is
selected; headset/near-field audio and multi-channel beamforming are not used.
Official AliMeeting utterance boundaries are not word timestamps.

All four old five-minute cases are `dev-regression`, even if their legacy
folder names contain `holdout` or `blind`. They have already informed tuning.
Full AMI ES2004b and TS3003b are development cases; EN2002b is reserved for
acceptance with different speakers. These three recordings total 105.7 minutes.
The official AliMeeting selection is declared in `fixtures.py` before inference:
R8003 and R8008 are reserved; R8001, R8007, and R8009 stay in development.
Preparation rejects development/acceptance speaker leakage, including the old
Chinese snippet. Selection manifests pin every prepared reference checksum.
Eight full AliMeeting recordings total 252.3 minutes. Together with full AMI,
the frozen meeting selection contains 11 recordings and 358.0 minutes.

ASCEND uses revision `737e9800ae31be9932ba8464c80366559bd28424` and every
human-labelled mixed-language clip in its official validation/test sources.
Validation speakers 7, 12, and 20 are development; test speakers 3 and 17 are
reserved for acceptance. The 586 clips are replayed in 18 original
speaker/session groups, with a one-second silent gap between unchanged clips.
Development has 213 clips (15.65 minutes of original speech); acceptance has
373 clips (25.92 minutes). Replay duration including gaps is 51.63 minutes.
These are synthetic replay sequences, not continuous meetings or the official
ASCEND leaderboard protocol. They require `--no-speakers` and cannot establish
diarization quality, clear-Mandarin accuracy, or meeting accuracy.

FLEURS uses revision `70bb2e84b976b7e960aa89f1c648e09c59f894dd`, locale
`cmn_hans_cn`, and all 409 official validation clips. It preserves
`raw_transcription`, including numbers and original wording. Sorting by clip
filename and grouping 24 unchanged clips with one-second gaps creates 18
development replays: 76.27 minutes of original audio, 83.38 minutes with gaps.
The source SHA-256 is
`18698ffdd46c36c54af641821684d3f0313a7b64e0a49615597ec61a98f2b57e`;
this run used the checksum-identical `hf-mirror.com` transport. FLEURS supplies
no speaker IDs; speaker disjointness is not verified and there are no reserved
FLEURS acceptance cases. These replays require `--no-speakers`. They measure
Mandarin read-speech, not continuous meetings, diarization, or the official
clip-by-clip FLEURS protocol. Duplicate sentence IDs are valid; duplicate audio
filenames, missing human text, and inconsistent sample counts are rejected.

Public benchmarks may have been in a model's training data. "Acceptance" here
means not used for this project's tuning, not a claim of unseen training data.

Prepare the Python environment and fixtures:

```bash
python3 -m venv .cache/evaluation/venv
.cache/evaluation/venv/bin/pip install -r tests/evaluation/requirements.txt
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py prepare --corpus ami --download
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py prepare --corpus ami-expanded --download
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py prepare --corpus ali --download
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py prepare --corpus ali-manual --download
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py prepare --corpus ascend-mixed --download
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py prepare --corpus fleurs-mandarin --download
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py prepare --corpus nonspeech-controls
```

Downloads are explicit and checksum-verified. A failed transfer preserves its
partial file; invoke preparation again to resume. Do not use automatic curl
retries that can truncate the original partial on timeout.

Run development only, with network access denied to the tested binary:

```bash
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py run \
  --binary build-meeting/asr-tool \
  --label local-public-dev \
  --cases 'ami-*-full-dev' \
  --deny-network
```

Normal transcription never downloads models. The evaluator uses public data
only during the explicit `prepare --download` step; model files remain in the
user's local cache.

`--model-dir` isolates candidates without changing product defaults. The
manifest fingerprints all model/CoreML files (including directory symlinks),
the binary, references, scoring source and packages, requested prompt, and
speaker mode. Incomplete downloads are rejected. The runner revalidates the
binary, models, audio, and references after every case and refuses mixed-version
results under one label. Network denial is enforced with macOS `sandbox-exec`;
other platforms are not represented as network-denied runs by this runner.

An explicit `--cases` selection is important: the default `*` can include
reserved cases and is rejected if it mixes development and acceptance. Reserved
cases require `--acceptance`; a run must contain only that split. Expanded case
references are checked against their frozen selection manifest before execution.
Do not evaluate acceptance while selecting models or tuning.

New ASR and VAD manifests also record the resolved FFmpeg executable, its
SHA-256, and full `-version` output (including library versions). These are
checked again after each case, before scoring. A changed frontend rejects the
run and requires a new label. This is especially relevant for audio-filter
experiments. It is not a hash of every dynamically loaded library. Older
reports remain unchanged and lack this additional frontend fingerprint.

New ASR runs record a fixed 8 GiB sampled process-tree RSS budget in the
manifest and resource report. The runner terminates the process group at the
first sample exceeding that budget, retaining the observed bytes and limit in
the log; a failed case receives no accuracy report. Sampling every 50 ms is
not an OS-enforced allocation limit and excludes driver/ANE allocations.
On macOS, the runner also samples host memory pressure once per second and
terminates when it reaches warning (2) or critical (4), even below the RSS
budget. Successful reports retain the highest level and number of samples.
An unavailable pressure reading fails the monitored run rather than declaring
it normal. This is a shared-host measurement, not proof the candidate alone
caused pressure. These developer guards add no product CLI option.

## Metrics

- `CER` and `chronological_wer` compare the complete recognized text with the
  public reference, ordered by word start time. Concurrent words have an
  arbitrary order; this is a regression metric, not a standard ASR leaderboard
  comparison or a speaker-attributed text score.
- `speaker_text` uses meeteval cpWER and character-token cpCER with one global
  speaker permutation per full recording. It reports assigned-text errors,
  substitutions/deletions/insertions, merged/split speakers, and unattributed
  text units. Unknown text cannot freely match a reference speaker. The
  supplementary `error_rate_including_unattributed` also counts its emitted
  units as insertions; it is conservative and is not standard cpWER. This
  supplementary score is the summary's `speaker_text_wer/cer.error_rate`;
  `assigned_cp_error_rate` is reported separately. Neither hides abstentions.
- `DER` and `JER` measure speaker diarization. Speaker IDs are permuted by the
  metric, so anonymous label names are not penalized. Unknown speaker output is
  counted as missed speech, not removed from scoring.
- The report contains both `raw_diarization` and `attributed_timeline` when
  available. The former evaluates the standalone speaker timeline;
  `attributed_timeline` evaluates the speaker labels attached to ASR segments.
- `review_reasons` counts uncertainty signals such as approximate timing,
  overlap, low ASR confidence, repetition, and uncertain speakers. It is a
  review workload signal, not an error rate.
- `review_seconds`, `unattributed_seconds`, and reference `overlap_seconds`
  report duration rather than just segment counts. Per-domain summaries use
  reference-unit weighting and report worst-case text and speaker errors.
- `corrections` separates beneficial, harmful, neutral, and unscored changes.
  A change crossing a manual utterance boundary is unscored rather than given
  fabricated word timing. Zero scored changes do not establish safe correction.
- `resources` includes elapsed time, sampled process-tree RSS, and macOS
  kernel peak process RSS. These do not include all driver/ANE allocations or
  prove acceptable system pressure. Concurrent downloads/other workloads are
  not an isolated latency benchmark.
  New runs retain both the outer monotonic duration and `/usr/bin/time` real
  duration, using the latter for elapsed time when available. A discrepancy
  exceeding the greater of one second or 1% marks timing inconsistent; missing
  independent timing is unchecked. Domain summaries list both kinds of cases.
  Such runs retain their accuracy scores but cannot establish latency gates.
- ASR segment `confidence` is a token score, not calibrated accuracy.
  `speaker_turns[].confidence` is an upstream silhouette score in `[-1, 1]`,
  or `-2` when no score was computed. Negative computed scores are treated as
  ambiguous during text attribution; `-2` does not indicate high confidence.

## Native Candidate Comparison

The developer-only `qwen_candidate` target uses sherpa-onnx Qwen3-ASR 0.6B int8
on CPU. It reuses the production Silero VAD parameters and speech-window planner,
without adding a Python service or downloading models during inference. Build
it explicitly; it is excluded from normal builds and installation:

```bash
cmake --build build-meeting --target qwen_candidate -j 6
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py run \
  --binary build-meeting/qwen_candidate --candidate qwen --no-speakers \
  --model-dir /absolute/path/to/local/qwen-model-directory \
  --label qwen-public-dev --cases 'ali-*-full-dev' --deny-network
```

The model directory must contain the ONNX frontend, int8 encoder/decoder,
tokenizer, and the same `ggml-silero-v6.2.0.bin` as the Whisper baseline.
The candidate saves the SDK's original JSON per audio window. Since native
Qwen acoustic token timing is not verified, the adapter marks window timing
approximate and emits no speaker claims. Speaker scoring is explicitly
inapplicable for `--no-speakers`; it is not interpreted as a diarization gain.
An ordinary speaker-enabled run still scores missing/uncertain labels as errors.
This target is an experiment, not an adopted product model.
Runs that report audio-placeholder truncation, output truncation, repetition-loop
termination, or a prompt leaving no audio space are rejected rather than ranked
as complete comparisons. The current upstream API does not expose every decoder
termination reason; model context limits also need verification before adoption.

`sensevoice_candidate` uses the original 2024 multilingual SenseVoice int8
export, not the separately distributed 2025 Cantonese model. Its model directory
requires `model.int8.onnx`, `tokens.txt`, and the baseline GGML VAD. Run it with
`--candidate sensevoice --no-speakers`. It does not support `-p`; ITN is disabled
to retain the model's literal hypothesis. Native token timings are retained in
the original window JSON but are not yet verified for acoustic attribution.
The exported weights are not Apache-2.0 just because sherpa-onnx is; see
`docs/THIRD_PARTY.md` before any product adoption.

The excluded `qwen_short_candidate` target isolates 12-second audio windows
instead of the baseline's 26-second limit, with the same VAD and no history
prompt. It records the limit in JSON. It is a stability experiment following
a rejected repeated-decoder-loop run, not a fair unchanged-segmentation model
comparison or a user-facing setting.

`funasr_nano_candidate` uses the pinned 2025-12-30 int8 Fun-ASR-Nano export
through the same native runtime, CPU with four threads, baseline VAD and
26-second windows, without history or ITN. Its directory requires
`encoder_adaptor.int8.onnx`, `embedding.int8.onnx`, `llm.int8.onnx`,
`Qwen3-0.6B/{tokenizer.json,vocab.json,merges.txt}`, and the baseline GGML VAD.
It accepts the existing optional `-p` as hotwords; no new product option is added.

```bash
cmake --build build-meeting --target funasr_nano_candidate -j 6
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py run \
  --binary build-meeting/funasr_nano_candidate --candidate funasr-nano --no-speakers \
  --model-dir /absolute/path/to/local/nano-model-directory \
  --label nano-public-dev --cases 'fleurs-*-replay-dev' --deny-network
```

The adapter validates paired native generation diagnostics and the original
token counts, rejecting missing diagnostics, exhausted context/output budgets,
known audio truncation, runtime logits failures, and invalid Unicode. It does
not silently repair text. Native token timestamps are estimated, not verified
acoustic alignment; no speaker score or viewpoint-attribution gain is claimed.
License and export provenance checks remain separate adoption requirements.

`funasr_nano_short_candidate` isolates hard 12-second windows. The excluded
`funasr_nano_pause_candidate` instead balances continuous speech into windows
of at most 12 seconds, preferring VAD pauses after six seconds that leave at
least two seconds of remaining audio. Both are developer experiments. The
pause-aware planner preserves every parent-window interval exactly once and
retains genuine short isolated speech. It prevents artificial tiny tails from
hard subdivision; it does not discard or pad speech. Native tests cover these
properties. Neither planner changes the product's normal segmentation.

`firered_ctc_candidate` and `firered_aed_candidate` compare pinned FireRedASR2
int8 exports using the same native runtime, four CPU threads, baseline VAD,
26-second window planner, and no history prompt. Use `--candidate firered-ctc`
or `--candidate firered-aed`, always with `--no-speakers`. The CTC directory
contains `model.int8.onnx`; AED contains `encoder.int8.onnx` and
`decoder.int8.onnx`. Both require `tokens.txt` and the baseline GGML VAD.
They reject `-p` because this native adapter has no verified terminology-bias
implementation. They are excluded from normal builds and installation.

Native CTC timestamps remain in the original result without an acoustic
alignment or speaker claim. The AED adapter explicitly sets eight tokens per
second. Its evaluator verifies encoder `max_len` metadata and the original
sample count, then rejects exhausted duration/context budgets using the pinned
sherpa-onnx v1.13.8 decoder's limits. Missing tokens, invalid Unicode, and
unverified capacities are errors, not repaired hypotheses. This guards known
termination limits; it does not establish recognition accuracy.
Unicode checks apply to the SDK-returned text and AED tokens. The pinned SDK's
text-normalization path itself removes invalid UTF-8 sequences; validation
after that path cannot prove that every internal byte was preserved.

Native candidate JSON also records model-load time and per-window decode time
using a monotonic clock. A window timing includes stream creation, waveform
acceptance, decoding, and result retrieval; it excludes VAD, model loading,
output serialization, and any wait for live audio. The outer runner still
measures complete process elapsed time and sampled memory. These timings alone
do not prove live first-text latency or contention between two active models.

The excluded `firered_aed_gap_candidate` isolates another AED behavior: it
restarts decoding at the midpoint of VAD gaps of at least 1.5 seconds inside
each baseline window. All parent-window audio is retained exactly once, short
isolated speech remains, and the 26-second maximum is unchanged. It uses only
detected audio pauses, not human reference boundaries. JSON records
`window_policy=vad_gap_split` and the gap threshold; native tests cover coverage,
short speech, and a pause exactly on an existing window boundary.

The excluded `firered_aed_restart_candidate` removes obsolete baseline cuts
inside contiguous audio before applying the same long-gap rule. Each resulting
span gets a fresh 26-second budget; longer spans use balanced subdivision and
prefer detected pauses after 18 seconds. It retains the same union of audio,
including real short speech, and does not fill excluded silence. This addresses
an observed FLEURS sentence split into a long part and an artificial two-second
tail after a preceding pause. JSON records `window_policy=vad_gap_restart`.
Native tests verify coverage, ownership, maximum duration, and short-tail
behavior. Text accuracy must still be measured separately.

### Native AED Beam Experiment

On macOS, setting the developer CMake path `ASR_EVALUATION_ONNX_INCLUDE_DIR`
to the pinned ONNX Runtime 1.28.2 headers enables the excluded targets
`firered_aed_beam1_candidate` and `firered_aed_beam3_candidate`. They use the
existing native model and feature extractor directly; no Python runtime is
added to transcription. They are not installed or selected by the product.
Use the existing evaluator's `--candidate firered-aed --no-speakers` adapter.

The implementation follows FireRedASR2S's beam pruning and final GNMT length
normalization, keeping completed hypotheses without accumulating extra EOS
penalties. Width three uses the upstream example's smoothing 1.25 and length
penalty 0.6; width one uses smoothing 1 and penalty 0 to verify greedy parity.
The candidates keep baseline VAD, gap splitting, CPU four-thread inference,
eight tokens per second, and the same decoder capacity. Branches are evaluated
sequentially and share completed cache states. No repetition is removed or
invented; the selected hypothesis must terminate before the generation limit.
Invalid logits, unknown tokens, and unfinished output fail the run. JSON records
the search profile; raw output bypasses the SDK's invalid-UTF-8 removal step.
Terminology bias and acoustic alignment remain unavailable.

The native `token_beam` test checks recovery of a better complete path from a
weaker first token, cache-parent selection, finished-path retention, legitimate
repeats, length normalization, incomplete output, and invalid/large logits.
These tests establish search behavior, not speech accuracy.

Two further excluded width-three targets isolate follow-up comparisons.
`firered_aed_beam3_restart_candidate` uses the existing restart window planner
with the same int8 weights and search profile. `firered_aed_beam3_fp32_candidate`
retains gap splitting and loads `encoder.onnx`, `encoder.weights`, and
`decoder.onnx` from the separately pinned unquantized export. Its JSON uses
the distinct non-int8 model identity. Neither target enables leveling or changes
product defaults. Build success is not an accuracy or memory qualification.
The FP32 target requires the distinct evaluator adapter
`--candidate firered-aed-fp32 --no-speakers`; its model identity cannot be
interchanged with the int8 adapter. Both use the same generation-limit guards.

The separate `firered_aed_beam3_level_candidate` keeps width-three decoding
and adds FFmpeg's `dynaudnorm=f=500:g=31:p=0.95:m=10` during audio conversion.
It uses the existing FFmpeg dependency, caps amplification at 10x, and records
the filter in JSON. `vad_level_candidate` isolates coverage with the same
filter. This is an offline, noncausal experiment with a 31-frame smoothing
window, not a live frontend or a product default. Original fixture audio and
references remain unchanged. The initial environment uses FFmpeg 7.1.1;
noise controls and complete text comparisons are required before adoption.

## Speaker Model Experiments

The excluded `diarization_candidate` target runs only the existing native
speaker pipeline, with CPU two-thread inference, 0.5 window shift, automatic
speaker count, clustering threshold 1.05, and the product's speaker-label
normalization. It accepts `segmentation.onnx` and `embedding.onnx` in an
isolated model directory. Use the evaluator with `--candidate diarization`,
without `--no-speakers` or a text prompt. All model files, audio, annotations,
binary, frontend, and settings are fingerprinted. Resource and offline guards
are shared with ASR runs. No voice embeddings are stored.

This mode reports raw DER only, including overlap and abstentions. It cannot
establish text, correction, or viewpoint-attribution accuracy. The baseline
screen on full R8001 (`diarization-base-parity-dev`) reproduces all 461 product
turn labels, with at most 0.5 ms timestamp differences due to the product's
JSON rounding. Raw DER is 35.1572% versus 35.1574%, with nine hypothesized
speakers. Runtime was 74.49 seconds, sampled peak RSS 0.43 GiB, and all pressure
samples were normal. Binary SHA-256:
`903f9cb0819f371424f94a5bedb57c44da5f48df16d2c8f8a536843358c906e9`.
This verifies the isolated driver before a model swap; it is not an accuracy
improvement. Product defaults and user installations remain unchanged.

Replacing only the embedding with ERes2NetV2 regressed this same full recording
(`diarization-eres2netv2-screen-dev`): raw DER **38.32%** versus **35.16%**.
Hypothesized speakers fell from nine to seven, but confusion increased from
361.41 to 433.54 speaker-seconds. Misses changed from 240.90 to 234.80 seconds
and false alarms from 118.14 to 116.98. A closer speaker count alone is not
better attribution. Runtime was 146.49 seconds, sampled peak RSS 0.70 GiB,
and all 140 pressure samples stayed normal. This fixed-threshold direct swap
is rejected; it does not rank every possible calibration of the new embedding.
No text or attributed-DER improvement is claimed.

An isolated native average-linkage probe also failed the same R8001 screen.
It recompiled the pinned sherpa `fast-clustering.cc` against its pinned
hclust-cpp (2026-02-25) and Eigen (5.0.1) headers, changing complete linkage to
the library's average linkage and preserving original distances for silhouette
computation. The speaker model, segmentation, and label normalization remained
unchanged. Thresholds 0.7 and 0.8 gave **39.32%** and **46.94%** DER, with 14
and 10 hypothesized speakers respectively, versus the 35.16% baseline.
Reports are `diarization-average070-screen-dev` and
`diarization-average080-screen-dev`; neither is adopted. Both timing clocks
agreed (73.32 / 72.15 seconds), with normal host pressure throughout.

Binary SHA-256 identities are
`172ab2f9a777232ebcc3bdaab7b2ba3bf077bedde24319e08271e3c8bffde6ef` and
`afe48e2b44f2c40519a8d1ac1eac6ab3e5933d02e1f656f9a4b7465ae77027bc`.
The patched clustering source SHA-256 is
`4daec6b07f67c8ea61eab162c535e2a8271bc3ee43a55dc306dadfac1324f5ab`.
A three-vector check distinguishes average from complete linkage, and a
two-cluster check verifies silhouette distances were not mutated. These
establish the probe's behavior, not production accuracy or a full threshold
calibration. Failed probes remain outside the build and product sources.

A separate spectral-clustering experiment follows WeSpeaker's
`wespeaker/diar/spectral_clusterer.py` at commit
`9fecd6cb4f47475d01761d87c826298dff4ef18c`: cosine affinity, nearest-neighbor
pruning (`p=0.01`), symmetric Laplacian, eigengap speaker count from 1 to 20,
and ten k-means initializations. The probe fixes the upstream's unspecified
random seed to 42. It reuses existing acoustic segmentation/embeddings, keeps
original distances for silhouette confidence, and does not supply the reference
speaker count. The Python clustering helper is an isolated evaluation child,
not a product runtime. Embeddings pass through local pipes and are not saved;
the child's memory and network denial are included in the measured process tree.
The helper lives in the fingerprinted experiment model directory. NumPy 2.2.6,
SciPy 1.15.3, and scikit-learn 1.9.1 are recorded in its run log.

The full R8001 screen (`diarization-spectral-screen-dev`) improved raw DER from
**35.16% to 24.12%**, with four hypothesized speakers. Misses were 235.34,
false alarms 117.07, and confusion 141.86 speaker-seconds; baseline confusion
was 361.41. Runtime was 74.19 seconds with consistent clocks, peak process-tree
RSS 0.62 GiB, and 71 normal host-pressure samples. This remains above the 15%
gate and says nothing about text attribution. The complete eight-meeting run
finished with the same frozen profile: Chinese weighted DER **21.35%** versus
**28.37%** baseline, but English regressed to **30.38%** from **19.47%**.
TS3003b was split into seven speakers and regressed from 17.43% to 47.20%.
The profile is rejected as a universal default.

Probe binary SHA-256:
`540204fbf799caa1489ddcdecfd377b3264cc42ca47957d9b673f33338ed1784`;
native adapter source:
`b572c7549a7211466b310e701857daa737a0a5b182cd7361686ad794591de457`;
Python helper:
`36e0c7d3ba6154db59429b1d7a087f75eb539cff88e5bd461cf99c0545cbc9b2`.
A synthetic three-cluster check matched the pinned upstream's partition and
verified deterministic pipe transport under network denial.

### Native Spectral Candidates

`diarization_spectral_candidate` removes the Python child and uses pinned Eigen
5.0.1, deterministic C++ k-means++ (seed 42, ten starts, 300 iterations), and
unmodified cosine distances for confidence. It keeps the WeSpeaker neighbor
recipe and estimates 1-20 speakers from the Laplacian eigengap. Equal similarities
use an index tie-break, normalization is double precision, and initialization
differs from scikit-learn; identical labels are not assumed. Empty speech stays
empty; identical embeddings stay one cluster; invalid/zero embeddings and more
than 4096 embeddings fail explicitly. Embeddings are not written to disk.

`diarization_nme_candidate` instead selects the neighbor count automatically
using normalized maximum eigengap analysis. The fixed search uses up to 30
counts within 15% of the embedding count, with a 512-row subsampling target.
The final graph and speaker count use all embeddings. Compared with the NeMo
reference, it chooses the first connected searched graph (or the largest if
none connects), recomputes count on the full graph, and uses this project's
deterministic k-means. It does not use reference speaker counts, languages,
transcripts, or recording-specific parameters.

Both are excluded developer targets with a macOS static-library adapter. They
replace only clustering inside the pinned sherpa pipeline; acoustic segmentation,
embedding weights, normalization of final speakers, and scoring stay unchanged.
Normal builds, installation, and the user CLI do not select either candidate.
Example (after preparing the existing model/data fixtures):

```sh
cmake -S . -B build-meeting -DASR_BUILD_SPECTRAL_EXPERIMENT=ON
cmake --build build-meeting --target diarization_spectral_candidate diarization_nme_candidate spectral_clustering_test -j 2
ctest --test-dir build-meeting -R spectral_clustering --output-on-failure
.cache/evaluation/venv/bin/python tests/evaluation/evaluate.py run \
  --binary build-meeting/diarization_nme_candidate \
  --model-dir .cache/evaluation/models/diarization-base --candidate diarization \
  --cases '*-full-dev' --deny-network --label diarization-native-nme-full-dev
```

The synthetic suite checks empty/short input, invalid dimensions and embeddings,
determinism, speaker-label permutation, original-distance confidence, and a
documented graph limitation. On a three-group fixture with small directional
perturbations, both the Python reference and fixed-neighbor native algorithm
estimate 12 groups; automatic neighbor selection recovers three. This is an
algorithm regression fixture, not evidence of meeting accuracy.

Complete native development results (zero collar, overlap and unknown speakers
included; lower is better):

| Recording | Product baseline DER | Native fixed neighbors | Native automatic neighbors | Acoustic quality selection |
|---|---:|---:|---:|---:|
| Ali R8001_M8004 | 35.16% | 25.75% | 25.89% | 25.75% |
| Ali R8007_M8010 | 41.36% | 35.20% | 35.36% | 35.20% |
| Ali R8007_M8011 | 28.88% | 23.44% | 23.43% | 23.44% |
| Ali R8009_M8018 | 15.26% | 14.08% | 13.82% | 14.08% |
| Ali R8009_M8019 | 23.54% | 10.70% | 10.70% | 10.70% |
| Ali R8009_M8020 | 13.88% | 8.27% | 8.27% | 8.27% |
| AMI ES2004b | 21.13% | 16.59% | 16.66% | 16.59% |
| AMI TS3003b | 17.43% | 47.53% | 43.78% | 17.43% |
| Chinese weighted | 28.37% | 21.54% | 21.56% | 21.54% |
| English weighted | 19.47% | 30.48% | 28.84% | 16.96% |

The runs cover 256.44 minutes of original public recordings. Both native
candidates completed with inference network denied, normal host memory pressure,
and peak sampled process-tree RSS below 0.51 GiB. Results live under
`diarization-native-spectral-full-v2-dev` and `diarization-native-nme-full-dev`.
The earlier native fixed-neighbor invocation was interrupted after a synthetic
test failed and has no complete score. Its failure led to the documented
reference-algorithm limitation above; it is not mixed into the completed run.
Neither complete candidate passes the 15% gate or qualifies as a universal
replacement. No ASR or speaker-attributed-text improvement is claimed.

A model-only comparison using the existing WeSpeaker embedding with the native
automatic-neighbor candidate regressed the two English meetings to 31.68%
and 55.03% DER (weighted 42.17%). The corresponding ERes2Net scores were 16.66%
and 43.78%. `diarization-native-nme-wespeaker-english-dev` retains the complete
offline reports; this model swap is rejected.

`diarization_hybrid_candidate` compares fixed-neighbor spectral clustering with
the original complete-linkage threshold (1.05). It selects the partition with
the higher mean original-distance silhouette, preferring complete linkage on
ties. Both partitions label the same complete set of embeddings. The
score is an acoustic diagnostic, not a calibrated probability or a substitute
for evaluation. The complete-linkage branch retains its original unconstrained
speaker count; the 20-speaker limit belongs only to the spectral branch. In the
first hybrid reports this field was named `max_speakers`; subsequent binaries
name it `spectral_max_speakers` to make the distinction explicit. This metadata
rename does not change decoding or the recorded hypotheses.

The complete TS3003b screen selected complete linkage (mean silhouette 0.3411
versus 0.2020) and reproduced baseline DER at 17.43%, avoiding the 47.53%
spectral regression. This screen motivated the full eight-recording comparison;
it does not establish the selection rule's reliability on unseen recordings.

The full comparison is complete in `diarization-native-hybrid-full-dev`.
Acoustic quality selection chose spectral clustering for six Chinese meetings
and ES2004b, and complete linkage for TS3003b. Chinese weighted DER is **21.54%**
and English **16.96%**, with seven recordings improving and TS3003b effectively
unchanged from baseline (sub-millisecond output rounding). No recording-specific
choice was supplied. All inference ran with network denied and normal host
memory pressure; peak sampled RSS was **0.50 GiB**. The tested binary is preserved
as `candidate-macos-arm64` beside the report, SHA-256
`e8f78ef74942a9ae5cb5a591da72ea6e29520eb8ef472bcddd7aef35ac3281bf`.

This is the strongest complete native clustering comparison so far, but the
35.20% worst recording and both domain averages still miss the 15% gate. The
rule was designed after inspecting development failures, so this is development
evidence only. Product defaults remain unchanged; reserved and private recordings
are not used. ASR text, text-to-speaker alignment, and live transcription need
their own subsequent validation. Seven native tests, 59 Python tests, the new
clustering/mask sanitizer checks, and temporary build/install validation pass.

### Boundary And Overlap Probes

`diarization_nme_turns_candidate` changes only the minimum on/off durations from
0.2/0.3 seconds to 0.1/0.1 seconds. On complete R8007_M8010, DER changed from
35.36% to 35.25%: false alarms fell from 90.81 to 83.88 speaker-seconds, but
misses rose from 619.00 to 620.76. The small net change does not address the
main failure or justify a product change.

`diarization_nme_overlap_candidate` tests a different failure mode: sherpa drops
a local speaker when fewer than ten non-overlapping frames remain for its
embedding. The candidate restores that speaker's original mask only if it
contains at least ten frames; all other masks and clustering settings stay
unchanged. This follows pyannote's clean-mask/full-mask fallback pattern, using
the pinned native extractor's existing ten-frame requirement. It is not voice
separation: mixed voice embeddings may increase confusion.

This target requires the existing ONNX header option as well as the spectral
option. It uses hash-pinned Kaldi/OpenFst headers matching the linked runtime
and a generated copy of sherpa's diarization header. The original dependency
is unchanged, and CMake rejects an unexpected source hash before applying the
single call-site change. Mask tests cover clean speech, overlap-only speech,
insufficient speech, empty input, and repeated application.

The complete R8007_M8010 overlap screen restored 268 local speaker masks
(691 to 959 embeddings), but DER regressed from 35.36% to 37.09%. Misses rose
from 619.00 to 622.54 speaker-seconds and confusion from 440.01 to 492.50;
estimated speakers increased from four to five. The probe is rejected. It
demonstrates why retaining mixed embeddings is not a substitute for separating
overlapping voices. The source change remains isolated from the product.

Two additional overlap probes remain developer-only. `powerset_decode` replaces
the segmentation model's per-frame winning-class argmax with a softmax marginal
decision constrained to the same single/two-speaker powerset. On the difficult
R8007_M8010 screen it produced 35.19% DER versus 35.20% for the hybrid baseline,
with more missed speech, so the change is rejected. `diarization_hybrid_dense_candidate`
reduces the segmentation window shift from 50% to 25%. It reduced that screen to
34.08% DER, but increased elapsed time from about 65 seconds to 130 seconds for
the 30.9-minute recording and did not approach the 15% gate. It remains an
evaluation candidate until cross-recording quality and sustained throughput are
shown.

`diarization_hybrid_constrained_candidate` applies a joint cosine-centroid
assignment to local speakers after clustering, ensuring distinct local voices do
not greedily collide on one global centroid when enough clusters exist. The
eight-recording run completed offline with a peak sampled process-tree RSS of
about 0.51 GiB. It scored 21.53% weighted Chinese DER and 17.82% weighted
English DER; the Chinese result was effectively tied with hybrid, while English
regressed from 16.96%. It is rejected. The assignment uses acoustic embeddings
only and emits an abstention when a local window has fewer global clusters.
The generated pipeline overrides are excluded from normal builds and installation.

## Speech Coverage Experiments

The excluded `vad_candidate` target compares GGML Silero and native sherpa-onnx
ONNX Silero without ASR. `vad.py` reports detector and planned-window recall,
missed speech, and non-speech coverage. Speech is the union of manual intervals,
not the sum of concurrent speaker durations. Example:

```bash
cmake --build build-meeting --target vad_candidate -j 6
.cache/evaluation/venv/bin/python tests/evaluation/vad.py \
  --binary build-meeting/vad_candidate \
  --model /absolute/path/to/ggml-silero-v6.2.0.bin --backend whisper \
  --threshold 0.3 --cases 'ali-*-full-dev' \
  --label vad-coverage-dev --deny-network
```

Thresholds belong to developer probes only. The product still uses 0.5.
`whisper_vad_candidate` runs the full product pipeline with a compile-time 0.3
threshold and records it in `decoding_settings.vad_threshold`. Neither probe is
installed. New run labels are required when the binary or model changes.

The excluded `whisper_no_history_candidate` isolates removal of the previous
window's recognized-text prompt. User terminology from `-p`, language selection,
VAD, windows, and correction rules remain unchanged. JSON now records
`decoding_settings.history_prompt` for both this candidate and normal Whisper
runs. The product default is `true`; no new CLI option or configuration file
is added.

Planned-window speech recall on the complete development recordings:

| Recording | GGML v6.2, 0.5 | ONNX v5, 0.5 | GGML v6.2, 0.3 | GGML v6.2, 0.15 |
|---|---:|---:|---:|---:|
| Ali R8001_M8004_MS801 | 84.50% | 93.54% | 94.14% | 99.54% |
| Ali R8007_M8010_MS803 | 100% | 100% | 100% | 100% |
| Ali R8007_M8011_MS806 | 100% | 100% | 100% | 100% |
| Ali R8009_M8018_MS809 | about 100% | about 100% | about 100% | about 100% |
| Ali R8009_M8019_MS810 | 99.26% | 98.60% | 99.76% | 99.81% |
| Ali R8009_M8020_MS810 | about 100% | 100% | about 100% | 100% |
| AMI ES2004b | 97.14% | 95.33% | 98.08% | 98.52% |
| AMI TS3003b | 95.63% | 93.81% | 98.79% | 99.00% |

Lowering the threshold also includes more non-speech; at 0.15, planned windows
cover 64.11% and 64.67% of annotated non-speech in the two AMI recordings.
ONNX v5 improves the quiet R8001 recording but regresses English recall. The
same-version ONNX v6.2 detector is not a universal improvement either. These
are coverage scores, not ASR or speaker-accuracy gains. R8007 has high text
error despite essentially complete audio coverage, so VAD alone cannot fix it.

`nonspeech-controls` generates four deterministic 60-second fixtures: silence,
Gaussian white noise at RMS 0.005 and 0.05, and a 5274 Hz sine wave at amplitude
0.2. NumPy PCG64 seed 20261015, generator version, and fixture hashes are recorded.
Their empty references are known by construction, not pseudo-labels. Evaluate
with `--cases 'control-*-dev' --no-speakers`; emitted text counts as insertions,
not a zero error rate with an empty denominator. These controls are not a public
human corpus and cannot establish speech-recognition accuracy or real-noise
robustness.

All four controls produced zero detected/planned speech at GGML thresholds
0.5, 0.3, and 0.15 (`vad-controls-th05-dev`, `vad-controls-th03-dev`, and
`vad-controls-th015-dev`). This is a detector result; the complete application
control runs are recorded separately below. Real room noise, music, and
non-speech vocalizations are not represented by these four signals.

Complete application runs with network denied also emitted zero segments and
zero inserted characters/words on all four controls, at both the product's 0.5
threshold and the 0.3 candidate. Reports are in `nonspeech-baseline-v2-dev` and
`nonspeech-th03-v2-dev`. Empty-reference error rates remain undefined; insertion
counts, not a fabricated 0% CER, establish this limited control result. The
original baseline label failed in the scorer on a null reference language;
a regression test and fallback domain fix precede both version-2 runs.

## Current local result

The current macOS Apple Silicon run uses whisper.cpp `large-v3-turbo-q8`,
sherpa-onnx `v1.13.8`, Pyannote segmentation-3-0, and the local ERes2Net
speaker model, with acoustic token alignment. On 2026-10-15 the four fixed
300-second cases produced these results with network access denied:

| Case | Language | Attributed DER | Raw DER | WER | CER |
|---|---:|---:|---:|---:|---:|
| AMI ES2004c holdout | English | 28.70% | 22.26% | 22.01% | 16.69% |
| AMI TS3003a dev | English | 27.40% | 11.73% | 31.36% | 22.54% |
| AMI IS1009a blind | English | 57.67% | 37.68% | 31.79% | 23.91% |
| AliMeeting R8002 holdout | Chinese | 52.06% | 26.57% | not scored | not scored |

Local report: `.cache/evaluation/final-local/report.json`. All four cases use
binary SHA-256 `cf15bae235a788fdaaefea504f8b7bf51ee0e2bcb8106c888772a0ad4dc57dd6`.
The evaluator rejects a binary change during a run. Fixtures and detailed
outputs remain local; source checksums are in the evaluator. Each case took
35-49 seconds on an Apple M2 Pro while another meeting transcription was
running, so these timings are not an isolated performance benchmark.

These are one-machine regression observations, not product guarantees. The
Chinese fixture has no word reference in the public slice used here. Attributed
DER includes abstentions: overlapping speech, uncertain clusters, missing ASR
coverage, and uncertain alignment all contribute missed speaker time. A low
raw DER therefore does not establish correct attribution of the actual text.

The expanded Turbo development baseline completed two full AMI recordings on
2026-10-15 with network denied and unchanged decoding behavior:

| Full recording | Minutes | WER | Raw DER | Attributed DER |
|---|---:|---:|---:|---:|
| ES2004b | 39.09 | 26.75% | 21.13% | 35.64% |
| TS3003b | 36.84 | 30.67% | 17.43% | 33.75% |
| Weighted total | 75.93 | 28.38% | 19.47% | 34.79% |

Local reports: `.cache/evaluation/expanded-turbo-dev/`. Binary SHA-256:
`d9dd6d4657ffadbf9daec8f3d9c6280251ed93725f5a9e33b89f0c4c1a14f551`.
Sampled peak process-tree RSS was about 1.23 GiB; total runtime was 517.5 seconds.
This establishes a harder baseline, not an accuracy improvement. EN2002b has
not been used for model comparison.

Full Whisper large-v3 was tested on the same two development recordings, with
the same segmentation, automatic language selection, prompts, and speaker models:

| Full recording | Turbo WER | Full Whisper WER | Full Attributed DER |
|---|---:|---:|---:|
| ES2004b | 26.75% | 30.68% | 37.17% |
| TS3003b | 30.67% | 29.80% | 32.42% |
| Weighted total | 28.38% | 30.31% | 35.04% |

Local reports: `.cache/evaluation/expanded-full-whisper-dev/`. Full Whisper
took 1123.5 seconds, versus Turbo's 517.5 seconds, with sampled peak process-tree
RSS of 4.58 GiB. Raw DER stayed at 19.47%; changing the ASR model did not improve
the acoustic speaker timeline. One accepted correction was neutral. The full
model is rejected as the English default because aggregate text accuracy
regressed. Larger weights alone are not evidence of a product improvement.

The Turbo baseline also completed all six official Chinese development
meetings (180.51 minutes) with the same binary and network denied:

| Full recording | CER | Raw DER | Attributed DER | Overlap / recording |
|---|---:|---:|---:|---:|
| R8001_M8004_MS801 | 63.47% | 35.16% | 69.78% | 29.00% |
| R8007_M8010_MS803 | 55.32% | 41.36% | 79.96% | 56.39% |
| R8007_M8011_MS806 | 34.81% | 28.88% | 56.36% | 23.79% |
| R8009_M8018_MS809 | 17.57% | 15.26% | 28.14% | 7.03% |
| R8009_M8019_MS810 | 23.19% | 23.54% | 42.55% | 9.49% |
| R8009_M8020_MS810 | 14.52% | 13.88% | 27.12% | 6.56% |
| Reference-weighted total | 37.94% | 28.37% | 54.65% | not averaged |

Local reports: `.cache/evaluation/ali-manual-turbo-dev/`. Total runtime was
1312.55 seconds and sampled peak process-tree RSS was about 1.48 GiB. Literal
CER preserves numeral and script differences. Chinese WER is not meaningful
here because the official references are unsegmented whole utterances; it is
not used for ranking. Overlap in the table is union overlap duration divided
by recording duration, not speaker seconds.

The standalone diarizer also oversplits voices: R8001 has four reference
speakers but nine hypotheses, R8007_M8010 has four but fifteen, and the R8009
meetings have two but three or four. Increasing the global clustering threshold
is not adopted: complete English development comparisons worsened by merging
distinct voices. Neither meeting minutes nor semantic viewpoint matching is
used to repair acoustic labels.

The isolated threshold-0.3 candidate completed the same six Chinese meetings:

| Full recording | Baseline CER | 0.3 CER | Baseline attributed DER | 0.3 attributed DER |
|---|---:|---:|---:|---:|
| R8001_M8004_MS801 | 63.47% | 62.03% | 69.78% | 67.78% |
| R8007_M8010_MS803 | 55.32% | 57.04% | 79.96% | 80.39% |
| R8007_M8011_MS806 | 34.81% | 32.48% | 56.36% | 53.04% |
| R8009_M8018_MS809 | 17.57% | 17.38% | 28.14% | 28.19% |
| R8009_M8019_MS810 | 23.19% | 23.07% | 42.55% | 41.45% |
| R8009_M8020_MS810 | 14.52% | 16.40% | 27.12% | 27.57% |
| Reference-weighted total | 37.94% | 37.89% | 54.65% | 53.76% |

Reports: `.cache/evaluation/whisper-vad-th03-complete-dev/`. Raw DER is unchanged
because the acoustic diarizer is unchanged. The worst CER improves slightly,
but two recordings regress and the worst attributed DER worsens. One accepted
correction crosses manual utterance boundaries and is unscored; no correction
precision is established. Better VAD recall is therefore insufficient evidence
to change the product threshold. These results remain development evidence,
not an acceptance pass.

The same candidate completed the two full English development meetings:

| Full recording | Baseline WER | 0.3 WER | Baseline attributed DER | 0.3 attributed DER |
|---|---:|---:|---:|---:|
| ES2004b | 26.75% | 26.47% | 35.64% | 34.37% |
| TS3003b | 30.67% | 28.97% | 33.75% | 30.15% |
| Reference-weighted total | 28.38% | 27.51% | 34.79% | 32.47% |

Conservative speaker-text WER, including unattributed insertions, improved from
51.59% to 49.52%; Chinese speaker-text CER changed from 77.19% to 77.04%.
The candidate took 530.83 seconds for English and 1421.63 seconds for Chinese,
with sampled peak RSS of 1.37 and 2.35 GiB respectively. Raw DER stayed at
19.47% and 28.37%. Binary SHA-256:
`423741168696b636b097171a3b2bdad39d08b22992928e3257f33b5e715c50da`.
The mixed Chinese results and small aggregate text gains do not justify a
unified default change; the product threshold stays at 0.5. Neither the
synthetic control pass nor improved English scores constitute acceptance.

Initial unchanged-window model screens on the complete R8001 recording:

| Model | CER | Attributed DER | Runtime | Outcome |
|---|---:|---:|---:|---|
| Whisper Turbo | 63.47% | 69.78% | 211.21 s | baseline |
| Full Whisper large-v3 | 59.98% | 70.74% | 454.45 s | insufficient improvement |
| SenseVoice 2024 int8 | 53.37% | not applicable | 54.56 s | text-only experiment |
| Qwen3-ASR 0.6B int8 | not ranked | not applicable | 260.75 s | repeated decoder loops |

Reports remain in `ali-manual-full-screen-dev`,
`ali-manual-sensevoice-screen-dev`, and `ali-manual-qwen-screen-dev` under
`.cache/evaluation/`. SenseVoice used four CPU threads and about 0.70 GiB sampled
RSS. It emitted no speaker claims and has no terminology prompt support, so the
screen does not establish better attribution or a product replacement. Qwen's
repetition termination after 113 and 123 tokens invalidates its comparison;
silently treating truncated output as complete would hide a reliability fault.

The 12-second Qwen stability experiment completed R8001 without a known
truncation/repetition-loop warning and scored 50.15% CER, versus 63.47% for
Turbo and 53.37% for unchanged-window SenseVoice. It took 307.69 seconds with
2.78 GiB sampled peak RSS (`ali-manual-qwen-short-screen-v2-dev`). Different
window segmentation prevents an unchanged-window model comparison. Timing
remains approximate and no speaker improvement is established.

On the complete lower-overlap R8009_M8020 recording, the same short-window
Qwen binary completed all 202 windows but emitted invalid UTF-8 in recognized
text at 1155.825-1167.825 seconds (`ali-manual-qwen-short-low-overlap-dev`).
Strict JSON loading rejected the output; no CER is ranked for that failed run.
Invalid bytes are not silently replaced or removed to manufacture a score.
Shorter windows therefore do not resolve candidate reliability. Qwen remains
excluded from the product.

## Mandarin Read-Speech Comparison

All three models completed the same 409 official FLEURS Mandarin validation
clips in 18 fixed development replay groups, with network denied, no speaker
claims, unchanged human references, no terminology prompt, and baseline VAD.
Scores are weighted by all 14,729 normalized reference characters, retaining
numbers, repetitions, negation, foreign names, and parenthetical English text.
No reference edits were made to improve a candidate's score.

| Model | CER | Worst replay CER | Runtime | Peak sampled RSS |
|---|---:|---:|---:|---:|
| Whisper Turbo | 8.10% | 13.41% | 396.98 s | 1.10 GiB |
| Full Whisper large-v3 | 9.33% | 15.82% | Timing inconsistent | 3.42 GiB |
| SenseVoice 2024 int8 | 9.80% | 13.76% | 184.65 s | 0.73 GiB |

Reports: `.cache/evaluation/fleurs-turbo-dev/`, `fleurs-full-whisper-dev/`,
and `fleurs-sensevoice-dev/`. Error counts are 1193, 1374, and 1444 respectively.
Full Whisper's binary SHA-256 is
`3870bbaed54787ac55e92941e96ad9c58ce31a8f60a6233d3eb1fef324ac66be`;
SenseVoice's is
`d4fcf08e4e5701226841409def1e2939df5cf2c85ec54afb866aafe822d06026`.
Both alternatives regress overall text accuracy and are rejected as defaults.
None meets the initial <=5% clear-Mandarin target. The word-space-based
`chronological_wer` in these reports is not a meaningful Chinese word-error
benchmark; CER is the text selection metric here.

A later clock audit found that full Whisper group 00 logged 259.46 seconds in
`/usr/bin/time` but 134.07 seconds in the outer timer. The original report's
1168.21-second total is not reliable wall-clock performance evidence; system
logs total 1292.76 seconds. Original artifacts are preserved. The accuracy
result and rejection as a default are unchanged.

Replay groups are not continuous meetings, the official clip-by-clip protocol,
or independent acceptance data. These runs do not prove diarization accuracy,
real-time performance, correction precision, or peak driver/ANE memory.

### Nano Reliability and Segmentation

The original 26-second Nano screen exhausted the pinned export's 512-token
context capacity: one window used 462 context tokens and generated 50 tokens.
The SDK returned success, but the evaluator rejected the potentially truncated
output (`fleurs-nano-screen-dev`). Hard 12-second splitting then failed on
replay group 02: an artificial 425 ms tail returned empty text/tokens without
the required generation diagnostic (`fleurs-nano-short-dev`). The failed
comparisons have no ranked aggregate CER. Missing diagnostics are not ignored,
and empty windows are not silently removed.

The pause-aware Nano candidate completed all 18 FLEURS replays with the same
human references and network denied:

| Pipeline | CER | Worst replay CER | Runtime | Peak sampled RSS |
|---|---:|---:|---:|---:|
| Whisper Turbo baseline | 8.10% | 13.41% | 396.98 s | 1.10 GiB |
| Nano, pause-aware 12 s | 8.68% | 12.21% | 767.40 s | 2.66 GiB |

Nano made 1279 errors over 14729 reference characters, versus Turbo's 1193.
Report: `.cache/evaluation/fleurs-nano-pause-dev/`. Binary SHA-256:
`e823d7d6654597ea4ba5aa9b9fa2c74e27f62a9a6eb2fe044cf43d9019923967`.
The improved worst group does not offset the worse aggregate CER. Segmentation
also differs, so this compares pipelines rather than isolating model weights.
Other local work overlapped portions of this run; timing is not an isolated
performance benchmark. Nano is not adopted as the default.

## Mixed-Language Development

Turbo completed all 213 original human-labelled ASCEND development clips in
11 speaker/session replays, with network denied, no terminology prompt, and no
speaker scoring. The same fixtures were rerun after the correction guard fix:

| Replay group | Turbo CER | CER after guard fix |
|---|---:|---:|
| spk12-ses1 | 17.57% | 17.57% |
| spk12-ses2 | 16.98% | 16.98% |
| spk12-ses3 | 15.73% | 15.73% |
| spk12-ses4 | 32.20% | 32.20% |
| spk20-ses1 | 22.18% | 22.18% |
| spk20-ses2 | 22.65% | 22.65% |
| spk20-ses3 | 17.66% | 17.66% |
| spk20-ses4 | 5.90% | 5.90% |
| spk7-ses1 | 45.35% | 45.35% |
| spk7-ses2 | 53.95% | 53.95% |
| spk7-ses3 | 41.12% | 41.12% |
| Reference-weighted total | 24.34% | 24.34% |

Both runs have 1548 errors over 6361 normalized reference characters, and all
11 output JSON hashes match. Original speech totals 15.65 minutes; replay
duration including gaps is 1163.08 seconds. Reports are in
`.cache/evaluation/ascend-turbo-dev/` and `ascend-guard-dev/`. The baseline took
95.32 seconds with 1.18 GiB sampled peak RSS; the rebuilt product took 96.19
seconds with 1.15 GiB. The latter binary SHA-256 is
`5d6a5660b4d49a7a2e2765c900deaffc554cf6ddbae06b58b8099053a124003b`.
The word-space-based `chronological_wer` is not used to rank mixed Chinese text.

One accepted correction in spk12-ses2 reduced errors from 62 to 37 over 161
reference characters in both runs. Zero harmful accepted corrections were
observed, but one example cannot establish correction precision. The guard
now protects common English number words, additional English negation terms,
curly-apostrophe contractions, and Chinese financial numerals. Focused native
tests first reproduced missing protection before the fix. This is conservative
lexical protection, not a guarantee of semantic equivalence or correct numbers.

The pause-aware Nano candidate failed on spk12-ses4 after three completed
groups (`ascend-nano-pause-dev`). A window with 201 context tokens repeatedly
generated the same phrase until the configured 256-token output limit. The
strict evaluator rejected the run rather than ranking partial output or
removing repetitions. Its successful FLEURS run therefore does not establish
reliability on spontaneous mixed speech. No aggregate Nano ASCEND CER or
speaker improvement is claimed.

### Automatic History Prompt

Manual inspection of the existing mixed-language development outputs found
Chinese speech rendered in English and a repeated previous sentence in an
English-led window. This motivated an isolated no-history experiment, without
changing references or forcing a source language. The complete 11-group
comparison rejected globally disabling history:

| Pipeline | CER | Errors / reference characters | Worst group CER |
|---|---:|---:|---:|
| Turbo, default history | 24.34% | 1548 / 6361 | 53.95% |
| Turbo, no automatic text history | 28.42% | 1808 / 6361 | 50.00% |

Five groups improved and six regressed. The better worst group does not justify
the worse aggregate. Report: `.cache/evaluation/ascend-no-history-dev/`; binary
SHA-256 `1db184f9300443258f64a9e5c971dc0ec1413b8f616059e12f86388168754d1b`.
Runtime was 126.45 seconds and sampled peak RSS 2.89 GiB; other build/install
work overlapped this run, so these are not isolated performance measurements.
One correction reduced errors from 42 to 40 over 120 reference characters;
it does not establish safe automatic correction. The default retains history.

The final product rebuild was rerun on all 11 ASCEND development groups with
network denied (`ascend-final-dev`). Removing only the new `history_prompt`
metadata field makes every output JSON identical to `ascend-guard-dev`;
text, timing, corrections, and review signals are unchanged. CER remains
24.34%, runtime is 97.22 seconds, and sampled peak RSS is 1.15 GiB. Final binary
SHA-256: `2d19fa490d31b52415a5d0fa7417db7c532b995bfa281fc4e89dab0cb2851a00`.
Both native tests, 45 Python evaluation tests, shell syntax checks, and the
temporary-directory build/install check pass. Linux, Windows, and NVIDIA
runtime behavior have not been tested on this Mac.

## FireRedASR2 Comparison

The pinned CTC and AED int8 exports completed all 11 ASCEND development replays with
network denied, the same audio and human references, baseline VAD/window
planning, no terminology prompt, and no speaker claims:

| Replay group | Turbo CER | FireRed CTC CER | FireRed AED CER |
|---|---:|---:|---:|
| spk12-ses1 | 17.57% | 24.29% | 16.28% |
| spk12-ses2 | 16.98% | 20.40% | 10.41% |
| spk12-ses3 | 15.73% | 23.17% | 16.85% |
| spk12-ses4 | 32.20% | 27.50% | 21.21% |
| spk20-ses1 | 22.18% | 22.56% | 10.53% |
| spk20-ses2 | 22.65% | 15.86% | 2.91% |
| spk20-ses3 | 17.66% | 13.60% | 11.22% |
| spk20-ses4 | 5.90% | 14.67% | 6.80% |
| spk7-ses1 | 45.35% | 18.62% | 15.27% |
| spk7-ses2 | 53.95% | 20.06% | 9.60% |
| spk7-ses3 | 41.12% | 22.90% | 16.36% |
| Reference-weighted total | 24.34% | 21.29% | 13.58% |

CTC made 1354 errors over 6361 reference characters, versus Turbo's 1548.
Six groups improved and five regressed; the worst group improved from 53.95%
to 27.50%. This is useful development evidence, not a universal model win or
commercial acceptance. Manual inspection of spk7-ses2 showed preserved Chinese
where Turbo emitted English, but English phonetic/spelling errors remain.

Report: `.cache/evaluation/ascend-firered-ctc-dev/`. Binary SHA-256:
`03e37b91936a5d39ecc8a5750bc595c9a099d9ed1b7c235a5c863ab04be01f70`.
Complete-process elapsed time was 318.51 seconds for 1163.08 seconds of replay;
peak sampled process-tree RSS was 1.40 GiB. Across 59 windows, median decode
time was 5.11 seconds, nearest-rank p95 6.61 seconds, and maximum 6.75 seconds.
These windows can contain up to 26 seconds of audio. This is not a low-latency
streaming result; model-loading, audio availability, and concurrent-review
costs cannot be inferred from the window throughput. Model downloads overlapped
parts of the run, so this is not an isolated hardware performance benchmark.
CTC has no verified terminology bias or speaker attribution in this adapter.
The product default is unchanged pending broader accuracy and product checks.

The first fixed FLEURS replay, containing 24 official validation clips, scored
8.99% CER (70/779 characters), versus Turbo's 5.26% (41/779). It took 54.80
seconds for 308.18 seconds of replay (`fleurs-firered-ctc-screen-dev`). This
single-group screen is not an aggregate FLEURS score and does not establish a
clear-Mandarin improvement. It reinforces the need to check multiple domains
before changing the default for all files.

CTC also completed the full 31.84-minute AliMeeting R8009_M8020 recording with
network denied (`ali-firered-ctc-screen-dev`). CER was 11.21% (956/8528
characters), versus Turbo's 14.52% (1238/8528). Runtime was 505.33 seconds and
sampled peak RSS 1.38 GiB. This recording has only 6.56% union overlap; one
lower-overlap meeting does not represent the six-meeting development set.
The <=10% Mandarin-meeting target is still unmet on this screen. The CTC run
has no speaker scoring, so this result does not establish better attribution
or viewpoint separation.

AED made 864 errors over the same 6361 reference characters, 44.19% fewer
errors than Turbo. Nine groups improved and two regressed; the worst group
scored 21.21%. All 59 windows passed the known decoder-budget/Unicode checks
with verified native `max_len=1024`. This is the strongest mixed-language
development result so far, but it is not an independent acceptance result.
Report: `.cache/evaluation/ascend-firered-aed-dev/`. Binary SHA-256:
`43326a04e337bee3aa05e4544c7a9f2e64863473cd63b3a85127762d23564df5`.

AED took 434.67 seconds for the 1163.08-second replay and peaked at 2.13 GiB
sampled process-tree RSS. Median window decode time was 6.73 seconds,
nearest-rank p95 9.06 seconds, and maximum 10.23 seconds. Windows extend to
26 seconds, and these measurements exclude live audio collection and a second
concurrent recognizer. The result motivates an accurate local file/review
candidate; it does not meet or prove the live provisional/finalization gates.
No correction precision, terminology-bias support, acoustic alignment, or
speaker-attribution improvement follows from this text-only comparison.

The unchanged-window AED failed the first FLEURS screen: CER 16.94% (132/779),
versus Turbo 5.26% (`fleurs-firered-aed-screen-dev`). Inspection found whole
sentences omitted after long pauses inside otherwise covered audio windows.
Splitting at detected gaps reduced the same screen to 6.29% (49/779), with
unchanged 200.96 seconds of total window coverage and 69.30 seconds runtime
(`fleurs-firered-aed-gap-screen-dev`). Previously omitted sentences returned;
no reference or scoring changes were made. This is a diagnosis and development
fix, not a full FLEURS score. The gap candidate binary SHA-256 is
`68902df7b7b30b63f67738aa8977157b5c2b11139180ec40d1dc85bb99da956b`.
Residual errors include an incorrect ordinal and foreign names, alongside
spoken-number versus digit formatting differences. Normalizing formatting
alone would not establish preservation of numbers or correct content.

The gap candidate subsequently completed all 11 ASCEND development groups
(`ascend-firered-aed-gap-dev`): CER 13.65% (868/6361), worst group 21.21%,
447.52 seconds runtime, and 1.95 GiB sampled peak RSS. Ten groups retained the
same errors as unchanged-window AED; spk20-ses1 increased from 28 to 32 errors
(10.53% to 12.03%). The 60 decoded windows had p95 9.28 seconds and maximum
10.53 seconds. This preserves most of the mixed-language gain while repairing
the diagnosed read-speech omission, but it is not uniformly better.

The fixed gap candidate then completed all 18 FLEURS development groups
(`fleurs-firered-aed-gap-dev`): CER **8.91% (1312/14729)** versus Turbo's
8.10% (1193/14729). Seven groups improved, nine regressed, and two tied;
the worst group was 08 at 18.12%. Sampled peak RSS was 2.09 GiB. All 429
windows passed the generation guards, but this is not a clear-Mandarin win.
No scoring normalization or reference text was changed.

Group 00 has inconsistent timing: the outer timer recorded 97.48 seconds,
`/usr/bin/time` recorded 1823.54, and native decode durations sum to 1819.53.
The precise cause is unverified; the run cannot establish processing speed
or live latency. The original outer total is 1330.38 seconds, while system
logs total 3055.75 seconds. Reports remain unmodified for traceability, and
new runs explicitly cross-check these clocks. The other audited FireRed CTC,
AED, and gap ASCEND runs had no material clock disagreement.

Inspection of group 08 found a baseline cut at 168.20 seconds splitting one
sentence after a long pause at 153.885 seconds. The restart candidate keeps
that sentence's audio together through 170.24 seconds. It reduced this group's
errors from 181 to 166 (18.12% to 16.62%) while retaining exactly 247.34 seconds
of decoded coverage; windows decreased from 25 to 24. Report:
`fleurs-firered-aed-restart-screen-dev`, binary SHA-256
`04ba8aeea0c1ddb87be4c633c8ffac36f4b439a3e92c3a077125f9709c1f0cb9`.
Its system duration was 87.37 seconds, consistent with the outer timer, and
sampled peak RSS was 1.92 GiB. This is a targeted development screen, not a
full FLEURS score or an accepted replacement. Complete restart regressions and
Mandarin-meeting comparisons remain open; no reserved acceptance case was run.

The width-three restart screen on the same group 08 also improved over its
width-three gap control: **14.21% (142/999)** versus **15.22% (152/999)**.
Coverage stayed at 247.34 seconds and windows decreased from 25 to 24.
Report: `fleurs-firered-aed-beam3-restart-screen-dev`, binary SHA-256
`cb2b567f4adbad1f064e5c4dcb6b48242ced2d1e04dceb8119fd0ce973229ba7`.
Both clocks agreed at 162.47 seconds, with 1.82 GiB sampled peak RSS. This
screen overlapped the complete width-three FLEURS run, so it cannot establish
isolated speed. It remains a targeted segmentation result, not an aggregate
clear-Mandarin or speaker-attribution improvement.

Upstream `FireRedTeam/FireRedASR2S` at commit
`4e7d9aaf4482a47cec1724807026b9b151926eb5` uses beam search and, optionally,
CTC forced alignment of the AED token sequence via `torchaudio.functional.forced_align`.
The pinned native AED API currently supports only greedy decoding and does
not expose that alignment. These are specific remaining capability gaps;
transferring timestamps from a separately decoded CTC transcript would not
reproduce forced alignment or justify speaker attribution.

The unchanged gap candidate completed the full 31.84-minute AliMeeting
R8009_M8020 screen (`ali-firered-aed-gap-screen-dev`) with network denied:

| Pipeline | CER | Substitutions | Deletions | Insertions |
|---|---:|---:|---:|---:|
| Turbo baseline | 14.52% | 348 | 844 | 46 |
| FireRed CTC | 11.21% | 215 | 696 | 45 |
| FireRed AED gap | 10.23% | 126 | 687 | 59 |

AED made 872 errors over 8528 reference characters, 29.56% fewer than Turbo.
Substitutions improved substantially, but insertions increased and deletions
remain 78.78% of its errors. It still misses the <=10% meeting target on this
single, lower-overlap recording; no full six-meeting result is implied.
The text-only run does not score speaker identity or correction precision.
All 91 windows passed generation checks. System and outer durations agreed
at 736.60 seconds (RTF 0.386), with 2.28 GiB sampled peak RSS. Native window
p95 decoding was 10.84 seconds and maximum 11.48 seconds, on up to 26 seconds
of audio, excluding ingestion and a simultaneous first-pass recognizer.
These are file-mode measurements and do not establish live finalization latency.

The attempted full six-meeting AED gap comparison (`ali-firered-aed-gap-dev`)
failed on the first recording, R8001_M8004, after all 88 windows were decoded.
Eight windows exhausted their duration-derived generation budget, with repeated
fillers or phrases. The first was 95.935-121.935 seconds, generating 207 tokens
against a budget of 207; further failures began at 493.94, 536.575, 590.20,
706.43, 1011.38, 1169.885, and 1333.65 seconds. The evaluator rejected the run.
There is no ranked CER for this failure, and the remaining five meetings were
not run in that invocation. Successful easier screens do not establish broad
meeting reliability. Raw output and failure logs remain available for diagnosis.

The native width-one implementation reproduced all 26 complete result objects,
token arrays, sample counts, and boundaries on FLEURS group 00 exactly
(`fleurs-firered-aed-beam1-parity-dev`). Its SHA-256 is
`a2ef23061d2bb82fbc5130c60fc3350c3955464a4db5240ff0b7c749483fd905`.
This validates this integration screen, not parity on every language or model.
The width-three profile also returned identical text/tokens on this group
(`fleurs-firered-aed-beam3-screen-dev`), at 6.29% CER (49/779). System duration
increased from 70.49 to 93.88 seconds, with both outer clocks consistent.
Width-three sampled peak RSS was 1.88 GiB; no speed or accuracy advantage follows
from this screen. Its SHA-256 is
`1e60c962b09e2c1c4e810422316ead404f1df697fd70b759938eff4f0fdbdf72`.

The same width-three binary completed all 88 windows of the difficult full
R8001_M8004 recording (`ali-firered-aed-beam3-screen-dev`). All eight previously
exhausted windows now terminated with content, although finite repeated words
remain and have not been removed. CER was **50.81% (5444/10715)** versus Turbo's
63.47% (6801/10715), a 19.95% relative error reduction. This comparison changes
both recognizer and search; the failed greedy AED run is not ranked.

| Pipeline | Substitutions | Deletions | Insertions |
|---|---:|---:|---:|
| Turbo baseline | 1363 | 5390 | 48 |
| AED width three | 547 | 4823 | 74 |

Deletions are still 88.59% of the new errors. Runtime was 767.76 seconds for
1573.85 seconds of audio, with consistent clocks and 2.26 GiB sampled peak RSS.
Window decoding p95 was 17.37 seconds and maximum 20.75 seconds. These are
file-mode measurements, not live latency or a speaker-attribution result.
The <=10% meeting target is still far from met.

An amplitude/coverage audit found early human-annotated speech at roughly
-46 to -49 dBFS RMS following a much louder opening sound. Seventy-three
complete reference utterances, containing 646 normalized characters, have no
intersection with the decoded windows. This establishes a coverage problem,
not an exact attribution of all deletions: the references have utterance timing,
and overlapping speakers also contribute substantial errors. The separate
leveling experiment tests this observed failure without changing VAD thresholds
or using reference boundaries to cut input audio.

Width three also completed the full 213-clip ASCEND development selection
(`ascend-firered-aed-beam3-dev`), keeping all 60 window boundaries/sample counts
identical to greedy AED gap:

| Pipeline | CER | Errors / reference characters | Worst group CER |
|---|---:|---:|---:|
| Turbo baseline | 24.34% | 1548 / 6361 | 53.95% |
| AED gap greedy | 13.65% | 868 / 6361 | 21.21% |
| AED gap width three | 12.64% | 804 / 6361 | 18.18% |

Relative to greedy gap, six groups improved, three regressed, and two tied;
relative to Turbo, nine improved and two regressed. All generation checks
passed. Runtime was 648.70 seconds (versus greedy gap 447.52), with consistent
clocks and 2.29 GiB sampled peak RSS. Some building/coverage checks overlapped
this run, so timing is not an isolated throughput measurement. Window decoding
p95 was 15.39 seconds and maximum 17.40 seconds. Neither speaker attribution,
terminology bias, correction precision, nor concurrent live latency is verified.

The complete width-three FLEURS comparison subsequently finished all 409 clips
in 18 development replays (`fleurs-firered-aed-beam3-dev`). All 429 windows and
sample counts exactly match greedy gap, and every generation guard passed:

| Pipeline | CER | Substitutions | Deletions | Insertions | Worst group CER |
|---|---:|---:|---:|---:|---:|
| Turbo baseline | 8.10% | 499 | 620 | 74 | 13.41% |
| AED gap greedy | 8.91% | 548 | 624 | 140 | 18.12% |
| AED gap width three | 8.67% | 538 | 604 | 135 | 15.22% |

Width three reduced greedy gap's errors from 1312 to 1277 / 14729 characters;
nine groups improved, two regressed, and seven tied. Against Turbo, eight
improved, nine regressed, and one tied. It therefore remains unsuitable as a
universal replacement despite the mixed-language improvement. Runtime was
1781.84 seconds for 5003 seconds of replay, with consistent clocks in all
groups and 2.20 GiB sampled peak RSS. Some builds, downloads, and the restart
screen overlapped this run, so it is not isolated throughput. References and
normalization are unchanged; numeric expressions and foreign proper names
remain scored. This result does not establish speaker or live accuracy.

The first unquantized width-three screen completed FLEURS group 00 with all
26 windows/sample counts identical to int8 (`fleurs-firered-aed-beam3-fp32-screen-dev`).
CER was **6.55% (51/779)** versus int8 **6.29% (49/779)** and Turbo **5.26%**.
Only one window's text changed, substituting a different foreign proper name;
deletions and insertions stayed at 15 and 7. This is no accuracy win. Runtime
was 100.83 seconds with consistent clocks, load time 4.39 seconds, and peak
sampled RSS 4.90 GiB, below the new 8 GiB sampled budget. The binary SHA-256 is
`aca42ea1a8543875f488dd75db5577a8ee0433ab32f104e6914a75c526ea265d`.

No other ASR was run concurrently with this FP32 screen. Point-in-time macOS
pressure checks stayed normal (level 1); system swap increased from 2953.56
to 3576.88 MiB during the run, which cannot be attributed exclusively to this
process. An intrusive `vmmap` inspection reported a 4.4 GiB physical footprint
and 326.1 MiB swapped regions; that inspection can affect elapsed time. These
are limited host observations, not a sustained live/concurrent-model memory
qualification.

The subsequent FP32 ASCEND run (`ascend-firered-aed-beam3-fp32-dev`) completed
only the first group: 16.28% CER (63/387), 45.10 seconds, 4.91 GiB sampled RSS.
During the second group, host memory pressure reached warning level 2 and
system swap reached 4418.50 MiB. The run was manually interrupted; the runner
killed its process group and pressure returned to level 1. The partial second
output is not scored, and the one-case summary is not an ASCEND aggregate.
The automatic host-pressure guard above was added after this observation.
FP32 is not adopted on the 16 GiB host; these results do not justify its extra
resource cost. No accuracy conclusion is drawn for the uncompleted selection.

The leveling coverage screen (`vad-level-ali-screen-dev`) raised R8001 planned
speech recall from 84.50% to 94.60%, while non-speech included in windows rose
from 28.10 to 58.21 seconds. The five other development meetings completed in
`vad-level-ali-remainder-dev`: three stayed at essentially 100% recall,
R8009_M8019 improved from 99.26% to 99.81%, and R8009_M8020 stayed at 99.99%.
The four fixed non-speech controls produced no speech windows
(`vad-level-controls-dev`). All fixture durations were retained. These are
coverage checks only; actual transcription is required to measure whether
additional speech outweighs noise hallucinations or changed segmentation.

The leveled width-three candidate then completed all 93 windows of full R8001
with inference network denied (`ali-firered-aed-beam3-level-screen-dev`):

| Pipeline | CER | Substitutions | Deletions | Insertions |
|---|---:|---:|---:|---:|
| Turbo baseline | 63.47% | 1363 | 5390 | 48 |
| AED width three | 50.81% | 547 | 4823 | 74 |
| AED width three, leveled | 43.24% | 644 | 3851 | 138 |

Leveling reduced total errors from 5444 to 4633 (14.90% relative to the same
decoder, 31.88% relative to Turbo), but increased substitutions by 97 and
insertions by 64. It is not a uniformly beneficial correction. Because the
filter precedes both VAD and ASR, coverage, segmentation, and model input all
change; this screen does not isolate which accounts for the gain. All original
audio/reference identities are retained. No reference-dependent cuts or
post-hoc text deletions are used.

System and outer durations agreed at 947.02 seconds for 1573.85 seconds of
audio; sampled peak process-tree RSS was 2.22 GiB. Native window p95 decoding
was 19.64 seconds and maximum 21.85 seconds. A point-in-time macOS pressure
reading was normal, but this is not sustained concurrent-model verification.
The candidate SHA-256 is
`e3abb7d8427dcb580b08aa879ff7d85de110f7830fa2e792989d26674ef80e44`.
All four non-speech controls subsequently returned zero text/insertions under
the same candidate (`nonspeech-firered-beam3-level-dev`), with new FFmpeg
fingerprinting enabled. VAD controls reproduced the same windows under the
updated runner (`vad-level-controls-fingerprint-dev`). These deterministic
controls do not prove robustness to real-room noises. Complete cross-recording
regressions and the <=10% meeting gate remain open; no product default changed.

## Number Formatting Probe

The GitHub `asr-models` release's `itn_zh_number.fst` was tested separately with
`kaldifst==1.8.0`, without modifying models, hypotheses, references, or scoring.
It converts `一百二十三` to `123`, but also changes `大家意见一致` to
`大家意见1致`, `万一没有库存` to `万1没有库存`, and `十分重要` to
`10分重要`. Its unconstrained rewrite is rejected for product use.

The WeTextProcessing `release-graph-v1.0.4.1.zip` Chinese ITN tagger preserves
several of those ordinary words, but still classifies `十分重要` as a measure
with value `10分` followed by `重要`. It is not adopted unchanged either.
That archive requires its token-field reorder stage between tagging and
verbalization; simply chaining two FSTs is not a valid full implementation
(fractions can become empty). This is a rule-compatibility probe, not a score
or a verdict on newer WeTextProcessing releases.

Any later formatting step must preserve the acoustic hypothesis, expose its
changes, and pass ordinary-word, negation, decimal, percentage, date, and
fraction checks. Formatting is separate from acoustic correction: converting
`第一张` to digits cannot recover a misrecognized `第一千张`. Lower CER after
formatting alone would not prove better recognition or number preservation.

## Development comparisons

The initial local meeting implementation used WeSpeaker and estimated token
timing. Its attributed DER was 37.21%, 47.11%, 66.07%, and 61.61% on ES2004c,
TS3003a, IS1009a, and R8002 respectively. The current implementation improves
that metric on all four cases. This is an early development baseline, not a
released version, and several components changed together. It does not isolate
the contribution of the embedding model. Text error rates have not improved:
the initial English WER values were 21.72%, 30.76%, and 31.79%.

Estimated timestamps must not be used to invent a speaker change within an
utterance. Keeping such an utterance intact raised the Chinese attributed DER
to 81.48%; acoustic alignment reduced it to 52.06%. English changes were mixed
(27.82% to 28.70%, 21.12% to 27.40%, and 60.03% to 57.67%). Alignment is retained
to reduce the Chinese timing failure while preserving conservative attribution;
it is not a uniform accuracy improvement. Failed alignment still keeps the
whole utterance and abstains if speaker evidence is inconsistent.

An exploratory default beam-search run worsened all three old English WER values.
It was rejected. The default remains greedy decoding with a local beam retry
only for low-scoring or repetitive windows. Corrections protect numbers and
negation and preserve the original text. The expanded baseline accepted zero
corrections, so correction precision remains unestablished.

A temporary isolated fix for upstream silhouette-confidence distance mutation
was evaluated on both full AMI development recordings. Text and raw DER were
unchanged; attributed DER was unchanged on ES2004b and changed from 33.75% to
33.66% on TS3003b (weighted 34.75%). This is negligible relative to the remaining
error and is not adopted as a production dependency patch.

## Native Sortformer interface validation (2026-10-16)

The excluded `sortformer_candidate` and `sortformer_file_candidate` use the
same v2.1 ONNX weights with the model card's low-latency and file profiles.
The latter uses core/right/FIFO/update/cache lengths 340/40/40/300/188,
all in 80 ms frames. Both use Slaney Mel features and a 0.5 output threshold,
with no minimum-duration removal or gap filling. Raw model speaker channels
are retained, including short interjections.

An initial 96.88% DER diagnostic was invalid as a model comparison: the
adapter padded empty cache tensors, but this export concatenates their
physical lengths and then masks by logical length. Actual speech therefore
fell outside the attention mask. The adapter now supplies unpadded tensors,
refreshes pre-compression cache probabilities, sorts disabled slots last,
and processes shortened first/final chunks. It checks complete output-frame
coverage rather than silently dropping the tail. The earlier failed run is
not a production baseline or a complete fingerprinted report.

The C++ frontend agrees with an independent NumPy centered FFT/Slaney reference:
the 30-second public audio diagnostic has maximum error 8.59e-6. Eight real
CLI boundary checks, over both profiles and 0.37/1.0049/1.51-second signals
plus silence, pass with maximum Mel error 3.23e-5. Silence emits no turns;
frame coverage is exact. Release-mode state tests with ASan/UBSan cover
context exclusion, short tails, rejecting padded output, refreshed cache
labels, disabled-slot ordering, and repeated compression retaining speakers.

| Complete development screen | DER, zero collar/with overlap | Missed / false / confused seconds | Elapsed | Peak sampled RSS |
|---|---:|---:|---:|---:|
| AMI TS3003a, 5 minutes, low-latency | 34.15% | 92.68 / 0.29 / 1.51 | 309.28 s | 1.11 GiB |
| Same AMI interval, file profile | 33.85% | 92.76 / 0.29 / 0.59 | 14.92 s | 1.28 GiB |
| AliMeeting R8001, full 26.23 minutes, file profile | 21.40% | 375.91 / 40.03 / 22.52 | 73.80 s | 1.55 GiB |

Reports are `sortformer-unpadded-dev`, `sortformer-file-dev`, and
`sortformer-file-ali-screen-dev` under `.cache/evaluation`. Inference had
network denied and normal system memory pressure. The file-profile executable
is preserved in its report directory, SHA-256
`c3c2240815edf19379e7bdaad9c3f95e2b2f2b6f9771022c0846f9a4b7f672a0`.
The low-latency diagnostic was compiled without optimization and overlapped
brief auxiliary probes; its runtime is not a controlled profile speed comparison.
Neither profile meets the 15% DER gate. These screens do not establish text,
live, or more-than-four-speaker accuracy, and do not change product defaults.

After restoring the verified sherpa package, the file candidate was rebuilt
against the product-pinned ONNX Runtime 1.28.2. The complete five-minute AMI
regression (`sortformer-ort128-file-dev`) reproduces 33.8503% DER with all
3750 output frames, network denied, and normal memory pressure. It took
15.04 seconds with 1.35 GiB sampled peak RSS. The runtime change does not
resolve this candidate's accuracy limitation.

## Native eight-speaker candidate (2026-10-16)

`nemotron_candidate` uses the official Nemotron-3-Diarization q8_0 GGUF through
NeMo-Speech.cpp's native C ABI and Metal. It supports eight anonymous channels,
not unlimited speakers or real-person identity. Exact source/weight pins and
licenses are in `THIRD_PARTY.md`. No Python inference service is introduced.

The fixed `v3-offline` geometry runs through the streaming state machine,
including long files. `left_context_frames=-1` preserves the preset; zero is
an explicit override in this ABI. The library's default segmentation uses
onset/offset 0.641/0.561, start/end padding 0.229/0.079 seconds, minimum speech
0.511 seconds, and minimum gap 0.296 seconds. The same defaults apply before
and after the runtime's approximately 20-minute probability compaction.

All eight development meetings (256.44 minutes) completed with network denied:

| Full meeting | Previous hybrid DER | Nemotron DER |
|---|---:|---:|
| AliMeeting R8001_M8004 | 25.75% | 14.65% |
| AliMeeting R8007_M8010 | 35.20% | 18.24% |
| AliMeeting R8007_M8011 | 23.44% | 11.75% |
| AliMeeting R8009_M8018 | 14.08% | 12.65% |
| AliMeeting R8009_M8019 | 10.70% | 6.61% |
| AliMeeting R8009_M8020 | 8.27% | 7.77% |
| AMI ES2004b | 16.59% | 19.81% |
| AMI TS3003b | 17.43% | 18.12% |
| Mandarin, weighted by reference speaker-seconds | 21.54% | 12.62% |
| English, same weighting | 16.96% | 19.05% |

These are raw timelines with zero collar and overlap retained. The previous
hybrid is also an unadopted experiment, not the released product. No text or
viewpoint-attribution improvement is established by this comparison.
The report is `.cache/evaluation/nemotron3-metal-full-dev/report.json`;
its executable is retained beside it, SHA-256
`bc5c4376b88d4caadf571f4c3b7d63f1d5267065f5699d9aed44be81b04c47f7`.
Model-directory fingerprints include a `runtime` symlink to all SDK dylibs.
Peak sampled process-tree RSS was 0.356 GiB, excluding GPU driver allocations,
with normal host pressure. The six Chinese runs took 30.43 seconds and the
two English runs 12.69 seconds with a warmed Metal shader cache. These are
observations, not a controlled throughput or live-latency benchmark.

An isolated postprocessing probe uses the pinned upstream C++ segmentation
function over the complete probability timeline, with compaction disabled.
Its default profile reproduces all eight original scores. This avoids the
C ABI's trap where custom settings affect only the retained tail of long files.
Every profile uses the same settings across both languages:

| Segmentation probe | Mandarin DER | English DER |
|---|---:|---:|
| Default, minimum speech reduced to 0.1 s only | 12.48% | 18.98% |
| Threshold 0.5/0.5, pads 0.1/0.1 s, minimum speech/gap 0.1/0.2 s | 11.74% | 20.46% |
| Same, threshold 0.4/0.3 | 11.61% | 20.40% |
| Threshold 0.5/0.5, no padding, merging, or removal | 17.41% | 26.33% |

The short-turn rule has only a small effect, and lower thresholds regress
English. None is adopted. The diagnostic reports and probe source are retained
under `.cache/evaluation/nemotron3-profiles-dev`; they do not alter references.

To build the excluded candidate, prepare the pinned SDK separately, configure
with `-DASR_EVALUATION_NEMO_SDK=/absolute/path/to/nemo-sdk`, and build target
`nemotron_candidate`. Place the verified GGUF at `MODEL_DIR/model.gguf` and
link `MODEL_DIR/runtime` to the SDK's `lib` directory for complete fingerprints.
Run the existing evaluator with `--candidate diarization`, that binary/model
directory, `--cases '*-full-dev'`, a new label, and `--deny-network`.
The normal build and installer neither fetch nor install this candidate.

The latest adapter checks that every centered 10 ms frame was produced,
including a final padded frame, and records total/retained frame counts.
Missing tails fail the run even when no speech segment is emitted.
The fresh CMake-built adapter completed the same eight meetings under
`nemotron3-metal-checked-full-dev`: every speaker interval and score is identical
to the original run. Its preserved executable has SHA-256
`1ad9e796a90d9b631a89d7b2b0e983e04846ba906afd9e14c4e776f3bec832e0`.
Five real CLI tests cover 1/5920/16000/16078/24160-sample inputs; all frames are
accounted for. Four 60-second silence/noise/tone controls produce no turns.
These controls do not establish real-room noise robustness.

## Native multilingual ASR screen (2026-10-16)

Nemotron 3.5 ASR 0.6B q8_0 was screened with the same native Metal runtime,
automatic language selection, verbatim output, and unchanged baseline audio
windows capped at 26 seconds. The probe consumes only window boundaries;
human transcripts are used exclusively by scoring. All inference had network
denied and normal host memory pressure.

| Development case | Turbo baseline | Nemotron 3.5 |
|---|---:|---:|
| FLEURS Mandarin replay 00, CER | 5.26% (41/779) | 26.83% (209/779) |
| ASCEND speaker 12/session 1, CER | 17.57% (68/387) | 31.52% (122/387) |
| Full AMI ES2004b, WER | 26.75% (1809/6763) | 28.73% (1943/6763) |
| Full AMI TS3003b, WER | 30.67% (1483/4836) | 31.45% (1521/4836) |
| Both English meetings, weighted WER | 28.38% | 29.86% |

The candidate is rejected for default adoption; broader Chinese testing stops
after these regressions. Reports and diagnostic sources are retained in
`.cache/evaluation/nemotron35-text-screen-dev` and
`.cache/evaluation/nemotron35-text-english-dev`. The sampled peak RSS was
1.10 GiB. This screen establishes neither acoustic timestamp accuracy nor
streaming latency; it adds no new product dependency or user-facing setting.

## Local live replay (2026-10-16)

The initial live path was screened with paced replays of three ASCEND
development cases. The replay used the production streaming Zipformer bilingual
model for provisional text and the local `ggml-large-v3-turbo-q8_0.bin` model
for completed-utterance review. It ran with network access denied and did not
use a microphone, private recording, or reference text at runtime.

| Case | Provisional CER | Final CER | Provisional WER | Final WER |
|---|---:|---:|---:|---:|
| `ascend-spk12-ses1` | 35.66% (138/387) | 22.48% (87/387) | 65.67% (44/67) | 50.75% (34/67) |
| `ascend-spk7-ses2` | 22.88% (81/354) | 22.60% (80/354) | 58.06% (36/62) | 58.06% (36/62) |
| `ascend-spk7-ses3` | 23.83% (51/214) | 26.64% (57/214) | 93.75% (15/16) | 81.25% (13/16) |
| Reference-weighted total | 27.96% (270/955) | 23.46% (224/955) | 65.52% (95/145) | 57.24% (83/145) |

The first case improved materially, the second was effectively unchanged, and
the third regressed on CER while improving WER. This mixed result keeps the
live accuracy gate open. Across the three cases, 12 utterances were accepted
as locally reviewed; the remaining candidates were rejected by confidence,
protected-content, or bounded-edit checks. Changes crossed manual boundaries
in the replay metric, so this screen has no scored correction benefit/harm
cases. Reports are under `.cache/evaluation/live-accuracy-20261016-turbo-v3/`
and `.cache/evaluation/live-accuracy-20261016-turbo-v3-spk7/`; the replay
binary SHA-256 is
`7ecd5500ebf86e0e6ad5d84566f99d165dc5eabdc054d102ef440cddb3a8d9f7`.

The p95 input-to-provisional lag was 0.07-0.08 seconds and endpoint-to-final
delay was 2.06-2.22 seconds. Peak sampled process-tree RSS was 1.68 GiB.
Peak pending review audio was 10.4 seconds, below the 20-second bound.

The latency and resource observations meet the current engineering targets,
but the text error remains far above the approved mixed-language quality bar,
and one development case regressed after review.
This is a scheduling and offline behavior validation, not commercial accuracy
acceptance. The full 3.1 GiB Whisper model was not used for this replay because
the host memory-pressure guard stopped it before inference; the Turbo model is
the product default. A subsequent release audit found this replay driver also
rejected `correction_rejected` review reasons, while the microphone entrypoint
rejects low ASR confidence and repetition before the shared correction guard.
Those initial measurements therefore describe the stricter replay, not exact
microphone-policy parity. The RC replay driver was corrected to match the
production entrypoint; that does not change the file recognizer or the original
reports above.

## Interpretation and limits

### RC Live Replay With Matching Review Policy

The RC replay matches the microphone entrypoint's review policy and uses the
release's CoreML-enabled runtime, production streaming recognizer, and bounded
review queue. It replays three public development sequences at wall-clock speed
with network denied; it does not capture a microphone or evaluate speaker labels.

| Case | Endpoint provisional CER | Final CER | Input lag p95 | Endpoint-to-final p95 |
|---|---:|---:|---:|---:|
| spk12-ses1 | 34.88% | 24.55% | 0.079 s | 1.27 s |
| spk7-ses2 | 22.88% | 21.47% | 0.057 s | 1.84 s |
| spk7-ses3 | 23.83% | 26.64% | 0.078 s | 1.59 s |
| Reference-weighted text total | **27.96% (267/955)** | **23.87% (228/955)** | | |

Peak pending audio was 10.4 seconds; sampled process-tree RSS peaked at 2.41 GiB
and reported host memory pressure stayed normal. Input lag measures processing
behind received audio, not first-word recognition latency. The warm models'
endpoint finalization delays fit the scheduling targets on these short replays;
they do not prove sustained long-meeting behavior or hardware capture stability.

Thirteen utterances were locally reviewed. Accepted changes cross manual
annotation boundaries, so correction benefit/harm cannot be reliably assigned
per utterance. The third case's CER regression remains visible. Better aggregate
text error does not establish safe automatic correction or commercial quality.

Reports: `.cache/evaluation/v2.0.0-rc-live12/` and `v2.0.0-rc-live7/`.
Replay executable SHA-256:
`d6215a71c17f58f1673fbe40585054da7c544a6d0b482d2458f0b2a9b9d3fd73`.
The replay executable substitutes file-fed PCM for AVAudioEngine; the actual
microphone permission, resampling, and device lifecycle remain unverified.

An additional paced replay of the complete 26.23-minute R8001 development
meeting was attempted with the same binary and models. The resource guard
stopped it during warmup when macOS memory pressure reached level 2, before
the event stream began. No latency or accuracy score is assigned to this run.
The manifest and failure log are retained under
`.cache/evaluation/v2.0.0-rc-live-sustained-r8001/`. Its inference process tree
was terminated by the guard; long-session performance remains unverified.

### Remaining Limits

The remaining error is substantial. Short interjections, distant speech,
overlap, microphone placement, language switching, and specialized terms need
human review. A label count is not a reliable estimate of the actual meeting
headcount.

Further accuracy validation requires an organization-owned, consented evaluation set with
word-level transcripts, speaker time annotations, representative microphone
conditions, and fixed latency/RAM limits. Changes should be accepted only after
the same set is re-run and the text, diarization, review workload, and resource
usage are compared together. The original 20-minute screen was too small for
general claims. The expanded development split remains a limited sample and
is no longer unseen after these experiments.

The approved targets and sequence are in
`docs/superpowers/specs/2026-10-15-local-accuracy-design.md`. Full Whisper and
Chinese-native candidates must improve fixed public development cases before
changing defaults. Clear Mandarin has a measured development baseline but has
not met its target. Genuine mixed-language development results are also below
usable quality on several groups. The RC meeting acceptance above was executed
and its results are reported above. The numerical targets remain optimization goals. These are
developer evaluations; users provide only audio and an optional `-p` prompt,
not a configuration file, scoring options, or a speaker count.
