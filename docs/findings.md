# Handwriting prototype findings

**Status:** macOS arm64 build + headless smoke succeeded. Interactive Phase D benchmark not yet completed.

## Build / smoke

| Check | Result |
|-------|--------|
| Zinnia builds on macOS arm64 | Yes (AppleClang, Qt 6.12) |
| Japanese model loads | Yes (`handwriting-ja.model`, ~25 MB) |
| Traditional Chinese model loads | Yes (`handwriting-zh_TW.model`, ~50 MB) |
| Headless smoke candidates printed | Yes — both models + Both mode |

Hardcoded Zinnia SDK sample strokes (9 strokes) returned ranked UTF-8 candidates. Example top hits: JA `#1 春`, TW `#1 酋` / `#2 春`. Both mode interleaved with model provenance. Latencies on this machine were ~11 ms (JA), ~22 ms (TW), ~38 ms (Both) for that sample.

Linux build not yet verified on this workstation (same CMake project; install Qt6 + cmake per README).

## Recognition quality (to fill in)

Metrics: exact hit at rank 1 / top 5 / top 10; related-variant hit recorded separately.

| Category | n | top-1 exact | top-5 exact | top-10 exact | related variant in top-10 | notes |
|----------|---|-------------|-------------|--------------|---------------------------|-------|
| Simple | | | | | | |
| Common Japanese | | | | | | |
| Traditional forms | | | | | | |
| Variant pairs | | | | | | |
| Similar shapes | | | | | | |
| Complex | | | | | | |
| Historical / rare | | | | | | |

Soft floor for common characters: ≥85% top-5 exact. Traditional-form usefulness is the scholarly gate.

Seed list: [`tests/samples.json`](../tests/samples.json).

## Decision

_Pending Phase D interactive testing with the GUI (`./build/handwriting_app`)._

Question to answer: Is Zinnia/Tegaki good enough as marinaMoji’s first handwriting backend, or should we investigate a modern neural alternative?
