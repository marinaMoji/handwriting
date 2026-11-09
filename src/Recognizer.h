#pragma once

#include "Fusion.h"
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
    int fusionDepth = 20;  // candidates fetched from each model before fusing
    stroke_preprocess::Options preprocess;
  };

  static constexpr const char* kDefaultPreprocessSpec = "resample:4+smooth:3@merge+split";

  // Default options (fit + default preprocessing).
  static Options defaultOptions();
  // Spec grammar: PREPROCESS[@tol] where tol is a '+'-joined subset of swap, merge, split
  // (e.g. "resample:4+smooth:3@merge+split"). A spec without '@' turns tolerance off.
  // Default options with a preprocessing spec ("none", "rdp:3", "resample:4+smooth:3", ...).
  static bool optionsFromSpec(const QString& spec, Options* out, QString* error = nullptr);

  explicit Recognizer(const ZinniaBackend* backend) : backend_(backend) {}

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
  const ZinniaBackend* backend_;
};
