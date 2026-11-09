#include "Recognizer.h"

#include "StrokeMapping.h"

Recognizer::Options Recognizer::defaultOptions() {
  Options o;
  stroke_preprocess::parseSpec(QString::fromLatin1(kDefaultPreprocessSpec), &o.preprocess);
  return o;
}

bool Recognizer::optionsFromSpec(const QString& spec, Options* out, QString* error) {
  Options o = defaultOptions();
  if (!stroke_preprocess::parseSpec(spec, &o.preprocess, error)) {
    return false;
  }
  *out = o;
  return true;
}

Recognizer::Prepared Recognizer::prepare(const QVector<Stroke>& raw, int surfaceWidth,
                                         int surfaceHeight, const Options& options) {
  Prepared p;
  if (!options.fit) {
    p.strokes = stroke_preprocess::apply(raw, options.preprocess);
    p.width = surfaceWidth;
    p.height = surfaceHeight;
    return p;
  }
  p.strokes = stroke_preprocess::apply(
      stroke_mapping::fitToCanvas(raw, options.canvas, options.canvas, options.margin,
                                  options.maxScale),
      options.preprocess);
  p.width = options.canvas;
  p.height = options.canvas;
  return p;
}

RecognitionResult Recognizer::recognize(const QVector<Stroke>& raw, int surfaceWidth,
                                        int surfaceHeight, RecognitionMode mode,
                                        const Options& options) const {
  const Prepared p = prepare(raw, surfaceWidth, surfaceHeight, options);
  if (mode == RecognitionMode::Both && options.fusion != fusion::Method::Interleave) {
    const int depth = qMax(options.fusionDepth, options.topN);
    const RecognitionResult ja = backend_->recognize(p.strokes, RecognitionMode::Japanese, p.width,
                                                     p.height, depth);
    const RecognitionResult tw = backend_->recognize(p.strokes, RecognitionMode::TraditionalChinese,
                                                     p.width, p.height, depth);
    RecognitionResult out;
    out.strokeCount = ja.strokeCount;
    out.latencyMs = ja.latencyMs + tw.latencyMs;
    out.candidates = fusion::fuse(ja.candidates, tw.candidates, options.fusion, options.topN,
                                  options.fusionParams);
    if (out.candidates.isEmpty()) {
      out.error = !ja.error.isEmpty() ? ja.error : tw.error;
    }
    return out;
  }
  return backend_->recognize(p.strokes, mode, p.width, p.height, options.topN);
}
