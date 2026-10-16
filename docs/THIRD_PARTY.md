# Third-Party Components

The project links these components; model files are downloaded separately during
explicit preparation and are not included in the repository.

| Component | Version/model | License | Source |
|---|---|---|---|
| whisper.cpp / ggml | v1.8.3 | MIT | https://github.com/ggml-org/whisper.cpp |
| sherpa-onnx | v1.13.8 | Apache-2.0 | https://github.com/k2-fsa/sherpa-onnx |
| ONNX Runtime | v1.28.2 | MIT | https://github.com/microsoft/onnxruntime |
| Whisper | large-v3-turbo | MIT | https://huggingface.co/openai/whisper-large-v3-turbo |
| Silero VAD | v6.2.0 | MIT | https://github.com/snakers4/silero-vad |
| Pyannote segmentation export | segmentation-3-0 | MIT (distributed export) | https://github.com/k2-fsa/sherpa-onnx/releases/tag/speaker-segmentation-models |
| WeSpeaker | VoxCeleb ResNet34 LM, 16kHz | Apache-2.0 | https://github.com/wenet-e2e/wespeaker |
| 3D-Speaker / ERes2Net | Base, zh-cn, 3D-Speaker, 16kHz | Apache-2.0 (upstream project) | https://github.com/modelscope/3D-Speaker |
| hclust-cpp / fastcluster | 2026-02-25 | BSD-2-Clause | https://github.com/csukuangfj/hclust-cpp |
| kaldi-decoder | v0.3.0 | Apache-2.0 | https://github.com/k2-fsa/kaldi-decoder |
| kaldifst / OpenFst | v1.8.0 / v1.8.5-2026-07-09 | Apache-2.0 | https://github.com/k2-fsa/kaldifst |
| kaldi-native-fbank | v1.22.3 | Apache-2.0 | https://github.com/csukuangfj/kaldi-native-fbank |
| kissfft | febd4caeed32e33ad8b2e0bb5ea77542c40f18ec | BSD-3-Clause | https://github.com/mborgerding/kissfft |
| simple-sentencepiece | v0.7 | Apache-2.0 | https://github.com/pkufool/simple-sentencepiece |

The Pyannote export includes a LICENSE, preserved as `segmentation-LICENSE` in
the model directory. Voice embeddings are computed locally and are not saved.
Full license texts, copyrights, and ONNX Runtime's dependency notices are in
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt). CMake installs that file next
to the executable. Linux and Windows archives include it; the macOS binary has
a companion release asset which the installer downloads alongside it.

## Local Live Model

The live recognizer downloads five files directly from
[`csukuangfj/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20`](https://huggingface.co/csukuangfj/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20)
at revision `98590b7ed6443e77b714204da2757d75e1a642f4`: the int8 encoder and
joiner, FP32 decoder, `tokens.txt`, and `bpe.vocab`. It uses the existing native
sherpa runtime; no model weights are included in the binary or Git release.
Evaluation manifests fingerprint the actual files. Preparation checks sizes
and uses HTTPS, but does not provide cryptographic integrity verification.

The runtime's Apache-2.0 license must not be interpreted as a grant for all
separately downloaded weights. The export's model-card/license could not be
retrieved during the RC audit, so its commercial weight-redistribution terms
remain unverified. This RC does not claim those rights or redistribute the
weights. Model provenance and redistribution clearance remain release work
before a commercial deployment.

## Developer Experiments

These assets stay in the ignored evaluation cache. They are not prepared by the
installer, installed by CMake, or selected by normal transcription.

The native Sortformer v2.1 probe uses the ONNX export
`cgus/diar_streaming_sortformer_4spk-v2.1-onnx` at
`6418561696a11506e38eb0babd64d7d1081554d5`. The 492,242,946-byte model's SHA-256,
verified against the repository's LFS object, is
`82b9c735e1cfc6b36b4ff8a994d9a0573e922d0e80a58a8553b2c58f7aff0c00`.
The export card attributes conversion to Altunenes/parakeet-rs. The upstream
NVIDIA model card at `cd03eee90fbec18297ac31b8c21546e596b7f71c` declares the
[NVIDIA Open Model License](https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-open-model-license/).
It is a four-speaker model, not a solution for unrestricted meeting headcounts.
The initial diagnostics used an existing local ONNX Runtime 1.17.1 library.
A subsequent file-profile build uses the product-pinned 1.28.2 static library
and reproduces the complete AMI screen's score. The product pin is unchanged.

The C++ cache implementation adapts NVIDIA NeMo's Apache-2.0
`nemo/collections/asr/modules/sortformer_modules.py`, retrieved on 2026-10-16
from the upstream main branch through jsDelivr. Its inspected SHA-256 is
`ead9804248b45153a1abfbe54ba67fab3c1d418da904f025ade5500168e7ee7c`.
The inspected feature reference, `nemo/collections/asr/parts/preprocessing/features.py`,
has SHA-256 `5cbc7b5eff0c2e57c4d9e548e5abc33b8c20078e48661faba9f5872c123d4321`.
These content hashes identify the inspected sources; the upstream branch is
mutable. Stable C++ tie-breaking can differ from Torch top-k on equal scores.
The export concatenates physical cache tensors; it does not implement the
newer NeMo export's packing of padded asynchronous inputs.

The eight-speaker candidate uses NVIDIA's official `Nemotron-3-Diarization`
`q8_0` GGUF at `f667ed73aee57d40cc39428eb768b4fd87a0a29e`, governed by the
[OpenMDW 1.1 license](https://openmdw.ai/license/1-1/). Its 107,012,128 bytes
match publisher LFS SHA-256
`08456d9e22cd9a323c0364d98375f3746d6e68507ebb705cd46438c534c7a3a1`.
The separate Apache-2.0 native runtime is
[`NVIDIA/NeMo-Speech.cpp`](https://github.com/NVIDIA/NeMo-Speech.cpp)
at `083861c241061562b8ec29eb06d7f6cdc3af8775`, with its pinned llama.cpp/ggml
commit `bd4f514db14d87fded667787a7a963bfbaa98e89` (MIT) and SentencePiece
commit `31646a467d2051eb904e0b45de3a73e91fe1c1e3` (Apache-2.0 and bundled
third-party notices). The diagnostic Metal build disables the optional ggml
patch series, ASR CLI, microphone capture, HTTP server, and TTS. These source
trees, SDK libraries, and weights remain in the ignored evaluation cache.

The rejected ASR screen uses `nvidia/nemotron-3.5-asr-streaming-0.6b`, revision
`ea30d66debe3740a08b573244286791d423d6b3e`, file
`nemotron-3.5-asr-streaming-0.6b.q8_0.gguf` under OpenMDW 1.1. Its 742,090,464
bytes match publisher LFS SHA-256
`3fc991d3badad7277c11030a7519832cddaf2057aafed6d4b25147e953a070b1`.
It uses the same pinned native runtime above. The library includes the ASR
C ABI even with its optional ASR command-line application disabled. No weights,
runtime library, or configuration for this rejected candidate is installed.

The optional native spectral evaluation targets use Eigen 5.0.1 with
`EIGEN_MPL2_ONLY`, pinned to the upstream archive SHA-256
`e9c326dc8c05cd1e044c71f30f1b2e34a6161a3b6ecf445d56b53ff1669e3dec`.
The unmodified header library is MPL-2.0; source and license are available at
<https://gitlab.com/libeigen/eigen/-/tree/5.0.1>. It is fetched only when
`ASR_BUILD_SPECTRAL_EXPERIMENT=ON` and is not added to the installed product.

The spectral graph follows WeSpeaker's Apache-2.0 implementation at
`9fecd6cb4f47475d01761d87c826298dff4ef18c`, file
`wespeaker/diar/spectral_clusterer.py` (Copyright 2022 Xu Xiang). The optional
normalized maximum eigengap search follows Park et al.,
[Auto-Tuning Spectral Clustering for Speaker Diarization](https://arxiv.org/abs/2003.02405)
and NeMo's `nemo/collections/asr/parts/utils/offline_clustering.py` at
`1f73a910bc0f5ab774d100706329730b81457591` (Copyright 2021 NVIDIA Corporation
and affiliates, Apache-2.0). The inspected source's Git blob is
`42bded47af0641a215f0f45057ccc55270f75841`, verified from the GitHub API content;
SHA-256 is `c2fcc816454612ae12f22230385d8d34de58c9ee8f650629d4936d54e0d3d515`.
Both algorithm references retain their copyright attribution in the native
implementation. This is a C++ adaptation, not a claim of bit-identical NeMo
or scikit-learn output; differences are listed in `EVALUATION.md`. No PyTorch,
Python process, model weights, or new product configuration are required by
these native candidates.

The acoustic-quality selection probe compares spectral and complete-linkage
partitions using mean cosine silhouette, without reference labels. Its native
complete-linkage implementation is the same hclust-cpp revision already used
by sherpa; archive SHA-256 is
`8f14e024c709d73afb40ae69cb22de4b73dba67cbce40f2e518813da8139ab56`.

The overlap-mask probe compiles a generated, minimally modified copy of sherpa's
Apache-2.0 diarization header (original SHA-256
`c3d1ad13e0c893abc12ad422d1c84efb53b886269579104fbce9545b869c5fef`). Its runtime
libraries stay pinned; header archives for kaldi-decoder 0.3.0, kaldifst 1.8.0,
and OpenFst 1.8.5-2026-07-09 use the same hashes as sherpa's dependency recipes.
The fallback pattern was checked against pyannote.audio tag `3.3.2`, file
`pyannote/audio/pipelines/speaker_diarization.py`, Git blob
`edfa5966cf78ad8af9fe8c1ab379e5901a0d48be`. The candidate uses the native
extractor's existing ten-frame cutoff rather than pyannote's model-specific
minimum-sample calculation. It does not add pyannote as a runtime dependency.

| Candidate | Provenance | Licensing/adoption status |
|---|---|---|
| Full Whisper large-v3 | OpenAI weights, native GGML/CoreML conversion | MIT; rejected as the English default by local evaluation |
| SenseVoice int8 2024-07-17 | sherpa-onnx ASR release; upstream `FunAudioLLM/SenseVoiceSmall`, revision `3847d57b6bdf2dd8875cb1508d2af43d80a16bf7` | Upstream model card declares `license: other` and links FunASR `MODEL_LICENSE`; the export contains only a license link. Not Apache-2.0 model weights. Commercial terms have not been cleared; not adopted. |
| Qwen3-ASR 0.6B int8 2026-03-25 | sherpa-onnx ASR release; export references `zengshuishui/Qwen3-ASR-onnx` and `Wasser1462/Qwen3-ASR-onnx` | Export archive includes no license text; conversion/distribution provenance and model terms require verification. Decoder stability also fails the development screen; not adopted. |
| Fun-ASR-Nano int8 2025-12-30 | `csukuangfj/sherpa-onnx-funasr-nano-int8-2025-12-30`, revision `6f16bd378457e13f36ccf3910df9017f96c346fb`; export README references `zengshuishui/FunASR-nano-onnx` and `Wasser1462/FunASR-nano-onnx` | Upstream `FunAudioLLM/Fun-ASR-Nano-2512` card at revision `272c57b82523ada6fd87095e955f8e29100979ab` declares Apache-2.0. Export licensing and correspondence to upstream weights still require verification; developer-only, not adopted. |
| Silero ONNX v5 / v6.2 | upstream `snakers4/silero-vad`; native sherpa-onnx coverage probes | MIT; no production backend switch based on coverage alone |
| FireRedASR2 CTC int8 | `csukuangfj2/sherpa-onnx-fire-red-asr2-ctc-zh_en-int8-2026-02-25`, revision `423be1acfdc5d8aaa5a485fb6263fe4ea8570b06` | Export README identifies the encoder and CTC branch of `FireRedTeam/FireRedASR2-AED`. Upstream card at revision `2304afed56eacfee6256dee5937ed22ffa0b64ec` declares Apache-2.0. Export contains no standalone license text; redistribution review remains open. Developer-only. |
| FireRedASR2 AED int8 | `csukuangfj2/sherpa-onnx-fire-red-asr2-zh_en-int8-2026-02-26`, revision `374cff185e952c40fcf2f6da972a3b6cf340608d` | Native encoder/decoder candidate from the same upstream model family; not installed or selected by the product. Export correspondence and redistribution notices require verification before adoption. |

The SenseVoice archive SHA-256 is
`7d1efa2138a65b0b488df37f8b89e3d91a60676e416f515b952358d83dfd347e`;
Qwen's is `393f8a14e2f5fb96746aaab342997a40641001fbd5bf9592a080a8329178ee96`.
ONNX Silero v5 SHA-256 is
`6b99cbfd39246b6706f98ec13c7c50c6b299181f2474fa05cbc8046acc274396`;
v6.2 is `1a153a22f4509e292a94e67d6f9b85e8deb25b4988682b7e174c65279d8788e3`.
Each inference manifest additionally fingerprints extracted files. Native runtime
licenses do not grant rights to unrelated model weights or benchmark audio.

Fun-ASR-Nano's pinned export was obtained through `hf-mirror.com`, with file
hashes checked against the pinned repository's LFS identities. The tokenizer
merges file also matches Git blob `31349551d90c7606f325fe0f11bbb8bd5fa0d7c7`.

| File | SHA-256 |
|---|---|
| `llm.int8.onnx` | `dfbf9aa3be41bccc257587f151e15c63fbe1b549f2b517f5ccd5bdce3bf4322a` |
| `encoder_adaptor.int8.onnx` | `f36dea2e30fbc33b5db1d7a7265cc976c5e5586c77b042d5adb1ad27c72db422` |
| `embedding.int8.onnx` | `95e61cd0c9c3b9543339a4cf973c95c116815e745ccc1e0285cbd81f76d18644` |
| `Qwen3-0.6B/tokenizer.json` | `aeb13307a71acd8fe81861d94ad54ab689df773318809eed3cbe794b4492dae4` |
| `Qwen3-0.6B/vocab.json` | `ca10d7e9fb3ed18575dd1e277a2579c16d108e32f27439684afa0e10b1440910` |
| `Qwen3-0.6B/merges.txt` | `8831e4f1a044471340f7c0a83d7bd71306a5b867e95fd870f74d0c5308a904d5` |

Published accuracy tables and model-card tags are not local validation evidence.
The Nano card itself lists acoustic timestamps and diarization as unfinished;
the candidate cannot claim either from approximate native window timing.

FireRed export files are fetched through TLS-verified `hf-mirror.com` at fixed
revisions, with LFS hashes checked before inference. The common `tokens.txt`
matches upstream AED `dict.txt` Git blob `50ac9cbd43d638b58f875e90b118ad8c9be51718`.
Expected SHA-256 identities from the pinned repositories:

| File | SHA-256 |
|---|---|
| CTC `model.int8.onnx` | `ca3dbabd82170110cc0b343c2890866d449984bc9cd92b9a18371ff80a81bb99` |
| AED `encoder.int8.onnx` | `54048d66b6e8f3c80ea7ce95efe794587b0fd81d7271651d0decd3803852ae82` |
| AED `decoder.int8.onnx` | `b840ce7196ae4a14d05ae84bbf56082b6b61ccec5610fda907dddbcea37354ff` |
| Common `tokens.txt` | `1bc613de2112d257e61a349c3e72d1b1a9cf19c33d3ca954197ad2171e5ea07b` |

The upstream published AED/LLM benchmark numbers are not scores for the CTC
branch, the quantized exports, this machine, or this project's replay protocol.

A possible unquantized comparison is published as
`csukuangfj2/sherpa-onnx-fire-red-asr2-zh_en-2026-02-26`, revision
`54e7d658026fff7805acc5a7ad7ee7dd4ca35072`. Repository metadata lists these
LFS identities, now matched against all three downloaded files before any
inference. The files remain in the ignored evaluation cache:

| File | Bytes | Expected SHA-256 |
|---|---:|---|
| `encoder.onnx` | 696607 | `26b73ad336ef17ec79a778d5e5021c2d40d246a497fc603b0e092a5a5cbe2fb9` |
| `encoder.weights` | 3103167616 | `6774fd9b68017f4ddd3dcf3bbb34c791f5825cfa04f306cd0b120223c2c13053` |
| `decoder.onnx` | 1584394636 | `c67df92a5dcccec7d0f6b1527706c9f08d21ca18ca37b7b29f32cc68801c6c51` |

Its token table matches the int8 export's SHA-256. ONNX metadata identifies
FireRedASR2-AED, PyTorch 2.8.0/opset 17, float32 weights, and a dynamic cache
length. The encoder's external-data location is `encoder.weights`. The README
identifies `https://www.modelscope.cn/models/FireRedTeam/FireRedASR2-AED` as the
conversion source. The native width-three FP32 target completed the first
FLEURS development group with network denied, 6.55% CER, and 4.90 GiB sampled
peak process-tree RSS. This screen regressed versus int8; broader accuracy,
sustained memory pressure, and export redistribution review remain open.
It is not a product download asset.

The developer-only ERes2NetV2 speaker comparison uses the 71,441,526-byte ONNX
asset identified by sherpa-onnx release asset `198893092`. Its GitHub transfer
was too slow; the first 4,307,395 downloaded bytes match a complete mirror at
`moeru-ai/sherpaw-eres2netv2-zh-cn`, revision
`f835806fb35ad2a8987826b8dd3c87429b02afa9`, file `install/bin/wasm/preload.data`.
The complete file matches the mirror's pinned LFS SHA-256 and source manifest:
`bf1a75b9930474cf3389ef415e6e5d38ca96fea4a3a00f7e301d080a58ee2239`.
The mirror declares unchanged ONNX bytes and Apache-2.0, referencing
`iic/speech_eres2netv2_sv_zh-cn_16k-common`. ONNX metadata independently names
that upstream model, 16 kHz audio, 192-dimensional output, and global-mean
feature normalization. GitHub provides no asset digest; the complete official
file has not been independently checked. This is a pinned developer experiment,
not a product download or cleared redistribution asset.

The ignored ITN probe uses `kaldifst==1.8.0` in a separate virtual environment.
`itn_zh_number.fst` is GitHub release asset `174211236` in
`k2-fsa/sherpa-onnx` (`asr-models`), observed SHA-256
`d2a2858bca72c9ba8ab940adad27454f4c686f4b8c5ac46ce274420dcb8c8185`.
Its generator is `sherpa-onnx/itn_zh_number.ipynb` in `k2-fsa/colab`, Git blob
`ad621e8e06c8d7b67b1a9c0068c3b42ac48824ad`. The publisher exposes no release
digest; this observed hash records identity, not independent publisher verification.

The WeTextProcessing probe uses the upstream Apache-2.0 project's release tag
`1.0.4`, commit `a8efdf731b0153edb97e2a1a73268f3b2d38e901`, with
`release-graph-v1.0.4.1.zip` downloaded from the official GitHub release over
verified TLS. Observed SHA-256 identities (no publisher digest supplied):

| Probe asset | SHA-256 |
|---|---|
| Release archive | `883a2db3c0fe58f2b3c9b44b3acd392c0580c9e1f860ea931bbada78017b788b` |
| `zh_itn_tagger.fst` | `4983ab817f3f3b206516d576b54d6f925c3a52c58c8348d3ae98e826b4f6d49d` |
| `zh_itn_verbalizer.fst` | `a475d151fce5b365265b04e61c785898158cbac21f5a953723b230c5e4639f6f` |

Both rule sets remain developer probes after ordinary-word counterexamples;
neither is installed, bundled, or used to rescore the ASR comparisons.

The developer native beam experiment additionally uses ONNX Runtime 1.28.2
headers from `csukuangfj/onnxruntime-libs`, release asset
`onnxruntime-osx-arm64-static_lib-1.28.2.zip`. The archive's verified SHA-256 is
`bf6e35dc9440d2899c3d21f52b1045fb99176a6ae822281b7245c5418cc338ab`, matching
both the publisher's release digest and the pinned sherpa build configuration.
Only headers are extracted for the experiment; the existing pinned static
runtime remains linked. Search behavior follows the Apache-2.0 upstream
`FireRedTeam/FireRedASR2S` `transformer_decoder.py` at commit
`4e7d9aaf4482a47cec1724807026b9b151926eb5`. These experimental targets add no
runtime download, Python service, or product installation asset.
The adapted pruning/ranking header retains the upstream copyright
`Copyright 2026 Xiaohongshu (Author: Kaituo Xu)` and Apache-2.0 identifier;
the full Apache-2.0 text is included in `THIRD_PARTY_NOTICES.txt`.
