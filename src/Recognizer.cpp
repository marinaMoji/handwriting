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
  return backend_->recognize(p.strokes, mode, p.width, p.height, options.topN);
}
