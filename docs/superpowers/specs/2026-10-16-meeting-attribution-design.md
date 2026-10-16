# Meeting Accuracy Follow-up

Continue the approved local accuracy work with the existing Whisper Turbo and
ERes2Net models. Keep the file CLI unchanged. The user-supplied transcript is
evaluation data only; neither its words nor its speaker identities may enter
inference. Keep private artifacts outside version control.

## Scope

1. Use supported, non-overlapping speaker transitions as optional ASR window
   boundaries, retaining enough context and exactly-once audio ownership.
   Uncertain transitions and brief interjections do not force new windows.
2. Attribute tokens spanning a short pause when acoustic speech on both sides
   supports the same speaker. Do not fill wholly silent intervals, bridge a
   different/unknown speaker, or suppress overlap.
3. Reassess ambiguous diarization turns with the existing embedding model and
   clean examples from the same recording. Build frozen prototypes from at least
   three distinct clean turns per speaker, with at most eight crops per speaker.
   Require leave-one-out consistency before using a prototype. Reassess weak
   turns with 1.5-second windows, using overlapping pairs for long turns and at
   most eight queries per turn. Change only intersections supported by two
   agreeing windows. Strong contrary evidence can veto a change even when its
   identity is unreliable. Preserve overlap and unsampled gaps.
   A 512-embedding preflight budget or any extraction failure retains the exact
   original timeline. Changes retain the prior label in `original_speaker` and
   use the existing unavailable-confidence sentinel, not a fabricated silhouette.
   Freeze ASR windows before refinement to isolate text and speaker changes.

No model or threshold sweep, reference-driven speaker count, text-based
relabeling, or forced identity is part of this implementation.

Changing models, globally removing text history, and making beam search the
default were already evaluated with regressions. They are outside this change.

## Validation

Add focused tests for context coverage, speaker transitions, short pauses,
uncertain speakers, and overlap. Run the existing CTest suite. Compare the same
complete private recording using the unchanged keyword list and normalization.
Report text edit counts, assigned-speaker text error, unattributed text, and
matched-text conflicts separately. The supplied transcript is not verified
human ground truth and cannot establish DER.

Use existing public development recordings with manual speaker references to
check text and attribution regressions. Never select output using reference
answers. Retain unsuccessful experiments in local reports, and describe this
meeting as development data after using it to guide implementation.

## Automatic Selection And Direct Delivery

Continue under the user's requirement that quality improvements must not add
configuration, candidate picking, or manual correction steps. Keep the same
file command and optional terminology prompt. The program selects one result.

Keep raw token probabilities separate from timestamp alignment, which may
combine several tokens. In addition to the existing whole-window trigger,
retry a window when three consecutive token scores have a geometric mean below
0.45. A local replacement may pass when its differing region improves by more
than 0.08, reaches 0.65, and replaces an original region below 0.75. The complete
window score must not fall. Existing edit-size, number, negation, and repetition
guards still apply. Pure insertion/deletion is not local replacement evidence.
Finish the complete initial file transcript and its history before starting
the additional local retries. The pinned Whisper beam sampler changes shared
random state; inline retries were observed to change later first-pass text even
when no candidate was accepted. Delaying extra retries isolates that state
without loading another model. The existing initial low-window retry stays
unchanged. Live sample review retains its existing path so an extra retry
cannot change the next utterance. Rejected candidates do not make a strong
original transcript less reliable.
These are model-score heuristics, not probabilities of correctness; retain
them only after paired audio regression checks.

Default text joins adjacent segments from the same speaker into paragraphs of
at most 30 seconds, separated by pauses over 1.5 seconds. Preserve all words,
speaker changes, unknown labels, and overlap. Internal review flags remain in
JSON and do not become manual tasks in the text or completion message. JSON
also records decoding/retry counts so tests can confirm that the new path runs.

Validate low-score replacement, deletion/insertion rejection, invalid scores,
number/negation protection, multilingual spacing, and speaker boundaries.
Compare public meetings and Mandarin/mixed-language development replays before
the private meeting. Record rejected/interrupted experiments separately from
the final evaluated build. Do not describe presentation changes as ASR gains.

## Review Evidence

Continue the approved optimization with the literature assessment in
`docs/RESEARCH-2026-10-16.md`. Record each existing retry's original and candidate
text, model scores, selection result, and all failed guards in JSON. Separate
protected-content rejection from excessive edit distance. This instrumentation
must leave the actual decoding, selected text, speaker attribution, and default
human-readable output unchanged. Use paired public replay to verify that
invariant before inspecting the authorized private meeting's rejection cases.
Keep candidate text out of human-readable transcripts; it is development
evidence, not a new user decision step. Do not weaken protection based only on
a higher model score.
