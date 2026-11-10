#pragma once

#include "Fusion.h"
#include "InkNet.h"
#include "StrokePreprocess.h"
#include "StrokeVariants.h"
#include "ZinniaBackend.h"

// The single recognition pipeline shared by the app, the eval tool and the CLI:
//   raw surface strokes -> crop/fit to the recognizer canvas -> preprocess -> model(s).
// Thread-safe as long as the backend outlives it (the backend serialises model access).
class Recognizer {
 public:
  struct Options {
    bool fit = true;  // crop to the ink and fit; false recognizes at the surface size
    bool dropSpecks = true;  // remove lone touch-downs / edge contacts before anything else
    int canvas = 300;
    int margin = 24;
    double maxScale = 4.0;
    int topN = 10;
    // "Both" mode: how the ja and tw lists are merged. Interleave is the original behaviour.
    fusion::Method fusion = fusion::Method::Interleave;
    fusion::Params fusionParams;
    // Stroke order/count tolerance: also try reordered/merged/split readings of the ink and
    // keep each character's best score (minus tolerancePenalty for non-original readings).
    stroke_variants::Options tolerance;
    double tolerancePenalty = 0.1;
    // Stroke-count prior: a candidate whose expected stroke count (Unihan) differs from the
    // number of strokes in the reading by more than `countBand` loses `countWeight` per extra
    // stroke. 0 weight = off. Unknown counts (kana, ASCII) are left alone.
    double countWeight = 0.0;
    int countBand = 1;  // strokes of difference that cost nothing (written order/joins vary)
    int fusionDepth = 20;  // candidates fetched from each model before fusing
    stroke_preprocess::Options preprocess;
    // Order-free neural recognizer (needs a net passed to the Recognizer; Both mode only).
    //   Only:   the net's own ranking.
    //   Hybrid: neural log-probability + neuralWeight * Zinnia score (ja and tw merged by best
    //           score; a character missing from one list gets that list's worst value).
    // Measured on 214 real dev samples: +14 top-1, +8 top-5 over Zinnia alone (docs/neural-scope.md).
    enum class Neural { Off, Only, Hybrid };
    Neural neural = Neural::Off;
    double neuralWeight = 4.0;
    int neuralDepth = 50;  // neural candidates kept before mixing
  };

  static constexpr const char* kDefaultPreprocessSpec = "resample:4+smooth:3@merge+split+count";

  // Default options (fit + default preprocessing).
  static Options defaultOptions();
  // Spec grammar: PREPROCESS[@tol] where tol is a '+'-joined subset of swap, merge, split
  // (e.g. "resample:4+smooth:3@merge+split"). A spec without '@' turns tolerance off.
  // Default options with a preprocessing spec ("none", "rdp:3", "resample:4+smooth:3", ...).
  static bool optionsFromSpec(const QString& spec, Options* out, QString* error = nullptr);

  explicit Recognizer(const ZinniaBackend* backend, const ink::InkNet* net = nullptr)
      : backend_(backend), net_(net) {}

  // Everything before the model: returns the strokes and canvas size handed to Zinnia.
  struct Prepared {
    QVector<Stroke> strokes;
    int width = 0;
    int height = 0;
  };
  static Prepared prepare(const QVector<Stroke>& raw, int surfaceWidth, int surfaceHeight,
                          const Options& options);

  RecognitionResult recognize(const QVector<Stroke>& raw, int surfaceWidth, int surfaceHeight,
                              RecognitionMode mode, const Options& options) const;

 private:
  RecognitionResult recognizeNeural(const QVector<Stroke>& raw, int surfaceWidth, int surfaceHeight,
                                    const Options& options) const;

  const ZinniaBackend* backend_;
  const ink::InkNet* net_;
};
