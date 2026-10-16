# Local Accuracy Implementation Plan

Approved design: `../specs/2026-10-15-local-accuracy-design.md`.

## 1. Public Evaluation

- Pin structured TextGrid parsing and established speaker-text scoring tools.
- Test global speaker permutation, abstentions, merged/split speakers,
  overlap, silence insertions, and correction benefit versus harm.
- Validate audio/reference identities; fingerprint model directories and
  binaries; measure peak memory and elapsed time with inference network denied.
- Prepare official AliMeeting human transcripts and new AMI recordings.
  Freeze recording-level development/acceptance selections before inference.
- Verification: Python regression tests, reproducible fixture checksums,
  disjoint recording identities, and at least three hours of public cases.

## 2. File Accuracy

- Compare Turbo and full Whisper large-v3 on identical development audio.
- Compare a native Chinese/multilingual candidate if needed; verify licensing,
  timestamps, runtime support, memory, and errors before integration.
- Isolate segmentation and acoustic speaker-attribution changes. Keep numbers,
  negation, original hypotheses, uncertain output, and overlap visible.
- Verification: per-domain/per-recording text and speaker scores, correction
  harms, peak memory, offline execution, and existing native regression tests.
  Do not change defaults based on a small or contaminated acceptance sample.

## 3. Local Live Transcription

- Add a native low-latency first pass and bounded local audio-review queue.
- Use stable utterance IDs with revision/finalization events; expose overload
  as unverified text and never block ingestion on review or speaker clustering.
- Keep the public interface to `--live` and optional `-p`; no configuration
  file. Model preparation remains an explicit, separate network operation.
- Verification: deterministic event/queue tests and sustained paced public
  replay, with no microphone capture during development.

## 4. Acceptance

- Run frozen acceptance recordings once; report every gate and worst recording.
- Check sustained warm provisional/final latency, both models' memory together,
  bounded pending audio, denied network, build/install, and CLI contracts.
- Resume private meeting checks only after public gates pass. Do not include
  private data, downloaded audio, model weights, or generated transcripts in Git.

## Progress

2026-10-16 release decision: the user selected **v2.0.0-rc** after public-source
and product-value review. Stop expanding model experiments. Ship the local
workflow/usability improvements as a prerelease with explicit accuracy failures;
do not resume private meetings. See `docs/PRODUCT_DIRECTIONS.md` and
`docs/releases/v2.0.0-rc.md`.

RC execution completed three reserved full meetings (101.57 minutes), all seven
reserved ASCEND replay groups (32.25 minutes), and three paced live development
replays. Meeting/mixed accuracy gates remain failed. One mixed run was stopped
by host memory pressure; completed cases were retained and remaining cases
completed unchanged after pressure normalized. Native/Python/installation checks
pass. Remote CI, publication, and installed-Skill updates still require successful
GitHub access. Execution completion is not a passed accuracy gate.

- [x] Written design reviewed and approved by the user.
- [x] Public annotation and full Whisper download sources verified.
- [x] Evaluation metrics, provenance, and resource tests.
- [x] Full public meetings and human-labelled mixed replay; frozen split.
- [x] Clear-Mandarin fixture and complete model comparisons.
- [x] Mixed-language baseline and correction-guard regression check.
- [ ] File text and speaker accuracy meeting the approved gates.
- [x] Local live scheduling and paced public replay verification.
- [ ] Independent public acceptance and product verification.

Evaluation infrastructure passes 61 Python tests. Native checks cover decoding,
window coverage, meeting output, spectral clustering, and overlap masks.
Three full AMI and eight official AliMeeting recordings total 358.0 minutes.
ASCEND adds 586 original human-labelled mixed-language clips in 18 replay groups
(51.63 minutes including gaps), with no synthetic diarization claims. Recording
and speaker splits are frozen; no reserved recording has been used for inference.

Turbo completed two full English development meetings with network denied:
chronological WER 28.38%, raw DER 19.47%, attributed DER 34.79%. Full Whisper
large-v3 regressed to 30.31% WER, took 1123.5 seconds versus 517.5, and peaked at
4.58 GiB sampled process-tree RSS. It is rejected as the English default.
Increasing the speaker clustering threshold also worsened raw DER, merging
distinct voices; the threshold remains unchanged.

The isolated upstream silhouette-confidence mutation experiment completed both
full English meetings. Attributed DER changed only from 33.75% to 33.66% on one
recording; no production dependency patch is adopted.

All six complete Chinese development meetings finished with network denied:
CER 37.94%, raw DER 28.37%, attributed DER 54.65%. Full Whisper on R8001 improved
CER from 63.47% to 59.98% but regressed attributed DER. Original SenseVoice int8
scored 53.37% CER on the same recording in text-only mode, with unsupported
terminology prompts and unresolved commercial model-license terms. Qwen's
baseline window run hit repeated-decoder-loop termination and was rejected.
A developer-only 12-second Qwen target completed R8001 at 50.15% CER but
emitted invalid UTF-8 on the complete R8009_M8020 recording. Its failed run
is not scored, and Qwen remains excluded from the product.

Native ONNX and GGML VAD coverage probes now separate union speech recall from
non-speech coverage. GGML threshold 0.3 raises quiet R8001 planned-window recall
from 84.50% to 94.14%, but coverage is not text accuracy. The complete eight-meeting
comparison is finished: Chinese CER changed from 37.94% to 37.89% with two
recording regressions, and English WER improved from 28.38% to 27.51%. The mixed
results do not justify a unified default change; product threshold remains 0.5.
Four deterministic non-speech controls emitted no text under both thresholds.
These controls do not establish real-room noise robustness.

All 409 FLEURS validation clips in 18 development replays are scored. Turbo
has 8.10% CER, full Whisper 9.33%, and SenseVoice 9.80%. The developer-only
Fun-ASR-Nano pause-aware candidate has 8.68% CER (1279/14729 characters),
12.21% worst replay, 767.4 seconds runtime, and 2.66 GiB sampled peak RSS.
It is not a better default. The original 26-second Nano candidate exhausted
the export's context capacity; hard 12-second splitting then produced a tiny
tail without generation diagnostics. Those incomplete runs are not ranked.
The pause-aware planner retains all coverage and real short speech, but its
ASCEND run subsequently hit a 256-token repetition and was rejected too.

Turbo completed all 213 ASCEND development clips in 11 replay groups:
24.34% CER, with 53.95% on the worst group. After protecting common English
number words, additional negations, curly-apostrophe contractions, and Chinese
financial numerals, the rebuilt product produced identical output hashes and
errors on all groups. One beneficial correction remained; one scored example
does not establish correction precision.

An isolated experiment disabling automatic text history kept user terminology,
language selection, windows, and VAD unchanged. All 11 ASCEND development groups
completed, but CER regressed from 24.34% to 28.42% despite a better worst group.
The default retains history. Its effective state is now recorded in output JSON
for reproducible comparisons without adding a user option.

The final product rebuild completed all 11 ASCEND development groups again
with network denied. Apart from the new history-state metadata, each JSON is
identical to the correction-guard run. Native and Python tests and the temporary
build/install check pass; this does not close the accuracy acceptance gate.

FireRedASR2 native CTC and AED developer targets are now available. The CTC
int8 export completed all 11 ASCEND development groups with 21.29% CER versus
Turbo's 24.34%, and worst-group CER improved from 53.95% to 27.50%. Six groups
improved and five regressed. Runtime was 318.51 seconds for 1163.08 seconds of
replay and sampled peak RSS 1.40 GiB. The measured p95 decode time of 6.61 seconds
uses windows up to 26 seconds and is not a live first-text latency result.
Broader Chinese/meeting checks and terminology support remain open; no product
switch follows this screen alone.

The CTC screen on the first FLEURS group regressed from 5.26% to 8.99% CER.
On complete AliMeeting R8009_M8020, it improved CER from 14.52% to 11.21%,
still short of the 10% meeting target. This is one lower-overlap recording,
not the full Chinese meeting selection. Both AED weight files were downloaded
and verified against their pinned LFS hashes before comparison.

AED completed all 11 ASCEND development groups at 13.58% CER (864/6361), versus
Turbo's 24.34% (1548/6361), with nine groups improving and two regressing.
The worst group improved to 21.21%. Runtime was 434.67 seconds and peak sampled
RSS 2.13 GiB. All 59 windows passed the known generation-limit guards. Window
p95 decoding took 9.06 seconds, so this supports investigating a slower local
review model, not claiming a working real-time first pass. Broader domains,
speaker alignment, terminology support, and concurrent live scheduling remain
unverified; the goal and accuracy gates remain open.

AED's first FLEURS group initially regressed to 16.94% CER because complete
sentences after long pauses were omitted. A developer-only planner that splits
at detected gaps of at least 1.5 seconds, preserving all parent audio, reduced
that same group to 6.29%. It completed the full ASCEND development selection at
13.65% CER: four more character errors than unchanged-window AED, still well
below Turbo's 24.34%. Ten groups were unchanged. Its 447.52-second runtime and
1.95 GiB sampled peak RSS remain file-mode measurements, not concurrent live
latency. Full Mandarin-meeting development comparisons remain required before
product integration.

The fixed AED gap candidate completed all 18 FLEURS development groups at
8.91% CER (1312/14729), worse than Turbo's 8.10%, with a worst group of 18.12%.
Seven groups improved, nine regressed, and two tied. Peak sampled RSS was
2.09 GiB. Group 00's native/system clocks disagree materially with the outer
timer, so this run cannot establish runtime or live performance. The evaluator
now retains both durations and flags inconsistent/unchecked timing in summaries;
a retrospective audit also invalidated full Whisper's FLEURS runtime total.
Accuracy scores and original reports are preserved.

A separate restart planner removes obsolete baseline cuts after long pauses,
retains all covered audio, and keeps a 26-second maximum with no artificial
tiny tails. Native coverage/ownership tests pass. On the diagnosed FLEURS group
08 it reduced errors from 181 to 166 without changing audio coverage. Broader
regression testing is still required. Neither planner changes product defaults.

Two isolated Chinese number-rule probes found ordinary-word counterexamples,
including converting `十分重要` to `10分重要`. Neither rule set is adopted or
used to rescore ASR. Acoustic number errors remain distinct from formatting.

Upstream FireRedASR2 uses optional CTC forced alignment of AED output and beam
search. Both remain missing from the current native AED API; the separate native
beam experiment below addresses search only. A separate CTC hypothesis cannot
supply defensible timestamps for different AED text. Forced alignment remains
a concrete follow-up for speaker accuracy, not a validated product capability.

The fixed AED gap candidate completed full AliMeeting R8009_M8020 with network
denied: CER 10.23% (872/8528), versus Turbo 14.52% and CTC 11.21%. It took
736.60 seconds for 1910.44 seconds of audio with 2.28 GiB sampled peak RSS;
system and outer clocks agreed. Substitutions fell from 348 to 126, deletions
from 844 to 687, while insertions rose from 46 to 59. Deletions remain the main
error source. This one lower-overlap meeting still misses the <=10% target and
does not prove improved speaker attribution or live latency. Next: complete
the fixed six-meeting comparison, test restart segmentation on the complete
development replays, and investigate upstream decoding/alignment capabilities
without using reserved or private recordings.

The six-meeting AED gap run subsequently failed on its first recording,
R8001_M8004: eight of 88 windows exhausted generation budgets with repeated
output. The evaluator rejected the run; no aggregate score or completeness
claim is made, and the other five cases were not run in that invocation.
Reliability on difficult audio must be addressed before broad adoption.

A separate native C++ beam-search experiment now reuses the pinned runtime's
model and feature extractor. Width one reproduced all 26 result objects and
token sequences of the first FLEURS replay exactly. Width three (upstream
smoothing 1.25, length penalty 0.6) produced identical text on that same replay,
with runtime increasing from 70.49 to 93.88 seconds. Both timings are consistent.
Search tests cover path recovery, finished hypotheses, cache-parent selection,
legitimate repetitions, length normalization, numeric stability, and failure on
unfinished output. No product default or public CLI option changed.

The width-three difficult-meeting check completed all 88 R8001_M8004 windows,
including all eight previously exhausted cases. CER was 50.81% versus Turbo's
63.47%; substitutions fell from 1363 to 547, deletions from 5390 to 4823, and
insertions increased from 48 to 74. System and outer clocks agreed at 767.76
seconds, with 2.26 GiB sampled peak RSS. The failed greedy AED run is not ranked.
This establishes one reliability improvement, not commercial accuracy or better
speaker attribution.

The complete ASCEND width-three regression finished at 12.64% CER (804/6361),
versus greedy gap 13.65% and Turbo 24.34%, with worst-group CER 18.18%.
Six groups improved versus greedy, three regressed, and two tied; all 60 audio
windows and sample counts were identical. All generation checks passed and
timing clocks agreed. Runtime was 648.70 seconds and sampled peak RSS 2.29 GiB;
some building/coverage checks overlapped, so this is not isolated throughput.
The improved text score does not establish speaker or live accuracy.

Coverage inspection found 73 complete reference utterances / 646 normalized
characters outside all decoded windows, with early speech around -46 to -49
dBFS RMS. Separate excluded leveling targets now reuse FFmpeg's bounded
`dynaudnorm` filter with unchanged VAD thresholds and beam settings. This is
an offline experiment, not a live frontend; coverage, text errors, non-speech
controls, and duration preservation must be checked before any adoption.
The coverage screen raised R8001 planned speech recall from 84.50% to 94.60%,
with non-speech coverage increasing from 28.10 to 58.21 seconds. All five other
Chinese development meetings and four non-speech controls also completed;
durations were retained and the controls emitted no speech windows.

The full leveled R8001 text comparison completed all 93 windows at 43.24% CER
(4633/10715), versus the identical decoder without leveling at 50.81%.
Deletions fell by 972, but substitutions rose by 97 and insertions by 64.
Runtime was 947.02 seconds with consistent clocks, and sampled peak RSS was
2.22 GiB. All four non-speech ASR controls emitted no text. This is one difficult
recording, still far above the meeting target; the combined effect on VAD and
ASR is not a proven universal correction. Cross-recording regressions remain
required before adoption.

ASR and VAD manifests now fingerprint FFmpeg's resolved path, executable hash,
and full version output, rejecting frontend drift before scoring. New tests
cover missing identification, same-version binary replacement, and runner
rejection before a report is written. The actual non-speech ASR/VAD controls
pass through both updated runners; older reports are preserved unmodified.

The product rebuild is byte-identical to the earlier final ASCEND binary
(`2d19fa490d31b52415a5d0fa7417db7c532b995bfa281fc4e89dab0cb2851a00`).
All three native tests and the temporary build/install check pass. Experimental
filters are excluded from regular builds and installation.

Next file-accuracy checks: complete fixed-profile width-three Chinese meeting
and clear-Mandarin development regressions; compare leveling across recordings
and restart segmentation independently; investigate acoustic alignment and
overlap omissions. An unquantized native AED export was located and its expected
hashes recorded in `docs/THIRD_PARTY.md`. All files are now downloaded and match
those hashes; ONNX metadata and external-weight references have been inspected.
Separate excluded width-three FP32 and restart targets build successfully.
The width-three restart screen on FLEURS group 08 reduced errors from 152 to
142 (15.22% to 14.21%), retaining all 247.34 seconds of coverage. It ran
concurrently with the full FLEURS comparison, so its 162.47-second duration
does not establish isolated speed. Sampled peak RSS was 1.82 GiB. Full restart
regressions remain open.
FP32 broader accuracy is not yet evaluated. Any comparison must measure the 16 GiB host's memory pressure and
retain the same references and scoring. Do not resume private recordings or
move to acceptance/live claims while these gates remain open.

The complete fixed width-three FLEURS development regression finished under
`fleurs-firered-aed-beam3-dev`: 8.67% CER (1277/14729), versus greedy gap 8.91%
and Turbo 8.10%. All 429 windows/sample counts matched greedy gap. Nine groups
improved versus greedy, two regressed, and seven tied; worst CER was 15.22%.
Clocks agreed across all groups at 1781.84 seconds total, with 2.20 GiB sampled
peak RSS. Some builds/downloads and the restart screen overlapped, so this is
not isolated throughput. It is not a clear-Mandarin default improvement.

After that run completed, the evaluator gained a distinct FP32 model adapter
and a fixed 8 GiB sampled process-tree RSS budget. Exceeding it terminates the
process group, records the diagnostic, and prevents scoring the failed case.
Tests verify termination, guard forwarding, and int8/FP32 identity separation.
The first FP32 screen completed serially on FLEURS group 00 with network denied:
6.55% CER (51/779), versus int8 6.29% (49/779). All windows were unchanged;
only one foreign-name transcription changed. Runtime was 100.83 seconds and
peak sampled RSS 4.90 GiB, below the guard. Pressure snapshots stayed normal,
but system swap rose by about 623 MiB and an intrusive `vmmap` check observed
a 4.4 GiB physical footprint. Neither speed nor sustained memory behavior is
qualified by this screen. The FP32 ASCEND attempt completed one group, then
was manually interrupted in the second when host pressure reached warning
level 2 and system swap reached 4418.50 MiB. The process group terminated and
pressure returned to normal. This is not a complete mixed-language result;
FP32 is not adopted. The evaluator now samples macOS host pressure every
second and automatically stops at warning/critical levels, independently of
the 8 GiB RSS budget. Tests cover below-budget termination and failed readings.

A short public int8 replay passed the actual pressure-monitored runner with
network denied, unchanged text, and six normal pressure samples. Product
binary identity remains unchanged.

The excluded diarization-only driver builds and shares the existing evaluator,
model fingerprints, offline enforcement, and resource guards. Tests ensure
overlap/unknown speakers remain scored and no text/attribution score is
fabricated. Full R8001 baseline parity reproduced all 461 speaker labels and
timestamps within 0.5 ms rounding, with 35.1572% raw DER and 0.43 GiB sampled
RSS. The pinned ERes2NetV2 model-only screen regressed raw DER to 38.32% with
identical settings: fewer speaker labels (seven) but more confusion (433.54
versus 361.41 speaker-seconds). Runtime was 146.49 seconds and peak RSS 0.70 GiB;
all 140 pressure samples stayed normal. The direct swap is rejected. Provenance
and mirror verification limits are recorded in `docs/THIRD_PARTY.md`.

Native average-linkage probes at 0.7 and 0.8 thresholds regressed R8001 raw DER
to 39.32% and 46.94%. Neither is adopted. A separate pinned WeSpeaker spectral
clustering probe improved the same full meeting from 35.16% to 24.12%, reducing
confusion from 361.41 to 141.86 speaker-seconds and estimating four speakers
without reference-count input. It used 0.62 GiB sampled process-tree RSS and
74.19 seconds with normal pressure. Its Python helper is developer-only,
receives embeddings via pipes, and runs within inference network denial.
No production Python dependency was added. The complete eight-meeting fixed
profile comparison finished under `diarization-spectral-full-dev`: Chinese DER
21.35%, English 30.38%. The English regression rejects a universal replacement.

Native C++ spectral clustering now has optional, reproducible CMake targets,
pinned Eigen headers, deterministic initialization, original-distance confidence,
and an explicit 4096-embedding limit. No Python subprocess or hard-coded runtime
path is needed. The complete eight-meeting native fixed-neighbor run scored
21.54% Chinese / 30.48% English DER. Automatic normalized-eigengap neighbor
selection scored 21.56% / 28.84%; it also remains a rejected universal default.
Both completed offline below 0.51 GiB sampled RSS and normal memory pressure.
Synthetic and sanitizer checks cover deterministic partitions, degenerate input,
subsampled neighbor search, and the fixed graph's known over-splitting failure.

The difficult R8007_M8010 recording has 1046.74 seconds of overlapping speech.
Changing minimum on/off durations to 0.1/0.1 seconds improved DER only from
35.36% to 35.25%. A separate native overlap-mask candidate now tests retaining
otherwise discarded local speakers when only overlapping frames are available.
The full-recording screen restored 268 local masks, but DER regressed from
35.36% to 37.09%, with more misses and identity confusion. It is rejected and
remains isolated from production. Reference counts and transcript semantics
are not supplied to any candidate.

An additional existing-WeSpeaker model comparison regressed both English
meetings (weighted DER 42.17%) and is rejected. A native acoustic-quality
selector then compared fixed spectral clustering with original complete linkage
using mean unmodified cosine silhouette. It selected complete linkage on the
TS3003b regression without knowing its language or true speaker count.
The complete eight-meeting run (`diarization-native-hybrid-full-dev`) retained
all six Chinese improvements and improved ES2004b, with TS3003b effectively
unchanged. Weighted DER is 21.54% Chinese and 16.96% English; all inference was
offline, peak RSS 0.50 GiB, and memory pressure normal. The tested binary is
preserved beside its fingerprinted report. The quality rule was developed on
these recordings and still misses the accuracy gates, so it remains an excluded
candidate. Production defaults, reserved data, and private meetings stay untouched.

Five native tests, 59 Python tests, ASan/UBSan checks for the new clustering and
mask code, and temporary build/install validation pass. A fresh configuration
with the experiment disabled contains no spectral/overlap/hybrid targets or
new experimental dependencies. No user-facing configuration or parameters were
added. Remaining priorities are overlapping-speech detection/separation, text
accuracy and acoustic text alignment, followed by independent acceptance.

The Sortformer v2.1 adapter now supplies unpadded cache tensors to the actual
ONNX export, refreshes uncompressed cache probabilities, and preserves short
first/final chunks. Independent FFT/Mel parity, exact output coverage, and
sanitizer checks pass. Corrected file-profile DER is 33.85% on the five-minute
AMI regression and 21.40% on full R8001. It is not a better universal default.

The official eight-speaker Nemotron model now has an excluded native Metal
candidate with pinned weights/runtime and no Python inference dependency.
All eight full development meetings complete: weighted Chinese DER improves
from the previous hybrid candidate's 21.54% to 12.62%; all six Chinese meetings
improve. English regresses from 16.96% to 19.05%, and the worst Chinese meeting
remains at 18.24%. Normal memory pressure and 0.356 GiB sampled peak RSS were
observed. Five uniform segmentation profiles reproduce the original default
and show that lowering thresholds trades Chinese gains for English regression.
No new default is adopted. Frame coverage now rejects missing audio tails.

Nemotron 3.5 multilingual ASR q8_0 is rejected after unchanged-window screens:
Mandarin replay CER 26.83% versus Turbo 5.26%; mixed-language replay 31.52%
versus 17.57%; the two complete English meetings have weighted WER 29.86%
versus 28.38%. Published model results are not treated as local product results.
Exact case scores, postprocessing settings, and limitations are in EVALUATION.md.

Fresh product and optional Nemotron builds now use restored, checksum-verified
pinned sherpa libraries/headers in the ignored cache, rather than missing
temporary dependencies. All 61 Python tests, four freshly built default native
tests, and the temporary build/install check pass. The new frame guard passes
five tiny/tail audio inputs, four 60-second non-speech controls, and all eight
complete meetings; speaker intervals and DER remain exactly unchanged.

The full public accuracy gate remains open. No reserved recording, private
audio, or microphone has been used for these comparisons. File text accuracy,
overlapping speech, and acoustic text attribution remain priorities before
accuracy promotion and private validation.


The local live path is now integrated into the product target. `asr-tool
--live` captures the default macOS microphone through AVAudioEngine, resamples
to mono 16 kHz, uses a bounded two-second capture queue, emits JSONL revision
events, and flushes the local review queue on Ctrl-C. Three paced ASCEND
development replays improved reference-weighted CER from 27.96% to 23.46%
and WER from 65.52% to 57.24% after local review. One case regressed on CER,
so this validates the scheduling path and a promising review strategy while
leaving the accuracy gate open. p95 input lag was 0.07-0.08 seconds, p95
endpoint finalization was 2.06-2.22 seconds, and peak sampled process-tree RSS
was 1.68 GiB.
