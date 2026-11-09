#include "Recognizer.h"

#include "StrokeMapping.h"

#include <QHash>
#include <algorithm>

Recognizer::Options Recognizer::defaultOptions() {
  Options o;
  stroke_preprocess::parseSpec(QStringLiteral("resample:4+smooth:3"), &o.preprocess);
  o.tolerance.merge = true;  // measured: +10 points top-5 on held-out real samples (docs/findings.md)
  o.tolerance.split = true;
  return o;
}

bool Recognizer::optionsFromSpec(const QString& fullSpec, Options* out, QString* error) {
  Options o = defaultOptions();
  o.tolerance = stroke_variants::Options();  // a spec states tolerance explicitly
  QString spec = fullSpec;
  const int at = fullSpec.indexOf(QLatin1Char('@'));
  if (at >= 0) {
    spec = fullSpec.left(at);
    for (const QString& t : fullSpec.mid(at + 1).split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
      if (t == QLatin1String("swap")) o.tolerance.swap = true;
      else if (t == QLatin1String("merge")) o.tolerance.merge = true;
      else if (t == QLatin1String("split")) o.tolerance.split = true;
      else {
        if (error) *error = QStringLiteral("bad tolerance '%1' (swap, merge, split)").arg(t);
        return false;
      }
    }
  }
  if (!stroke_preprocess::parseSpec(spec, &o.preprocess, error)) {
    return false;
  }
  *out = o;
  return true;
}

Recognizer::Prepared Recognizer::prepare(const QVector<Stroke>& rawInput, int surfaceWidth,
                                         int surfaceHeight, const Options& options) {
  const QVector<Stroke> raw =
      options.dropSpecks ? stroke_preprocess::dropSpecks(rawInput) : rawInput;
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

namespace {

// Recognize every reading of the ink with one model and keep each character's best score.
RecognitionResult recognizeVariants(const ZinniaBackend* backend, const Recognizer::Prepared& p,
                                    RecognitionMode mode, const Recognizer::Options& options,
                                    int depth) {
  const auto variants = stroke_variants::generate(p.strokes, options.tolerance);
  QHash<QString, Candidate> best;
  RecognitionResult out;
  for (int vi = 0; vi < variants.size(); ++vi) {
    const RecognitionResult r = backend->recognize(variants[vi], mode, p.width, p.height, depth);
    if (vi == 0) {
      out.strokeCount = r.strokeCount;
      out.error = r.error;
    }
    out.latencyMs += r.latencyMs;
    for (Candidate c : r.candidates) {
      if (vi > 0) c.score -= static_cast<float>(options.tolerancePenalty);
      auto it = best.find(c.character);
      if (it == best.end() || c.score > it.value().score) best.insert(c.character, c);
    }
  }
  QVector<Candidate> list = best.values().toVector();
  std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) {
    return a.score != b.score ? a.score > b.score : a.character < b.character;
  });
  if (list.size() > depth) list.resize(depth);
  for (int i = 0; i < list.size(); ++i) list[i].modelRank = i + 1;
  out.candidates = list;
  if (!list.isEmpty()) out.error.clear();
  return out;
}

}  // namespace

RecognitionResult Recognizer::recognize(const QVector<Stroke>& raw, int surfaceWidth,
                                        int surfaceHeight, RecognitionMode mode,
                                        const Options& options) const {
  const Prepared p = prepare(raw, surfaceWidth, surfaceHeight, options);
  if (options.tolerance.any()) {
    const int depth = qMax(options.fusionDepth, options.topN);
    if (mode != RecognitionMode::Both) {
      RecognitionResult r = recognizeVariants(backend_, p, mode, options, depth);
      if (r.candidates.size() > options.topN) r.candidates.resize(options.topN);
      return r;
    }
    const RecognitionResult ja = recognizeVariants(backend_, p, RecognitionMode::Japanese, options, depth);
    const RecognitionResult tw = recognizeVariants(backend_, p, RecognitionMode::TraditionalChinese, options, depth);
    RecognitionResult out;
    out.strokeCount = ja.strokeCount;
    out.latencyMs = ja.latencyMs + tw.latencyMs;
    out.candidates = fusion::fuse(ja.candidates, tw.candidates, options.fusion, options.topN,
                                  options.fusionParams);
    if (out.candidates.isEmpty()) out.error = !ja.error.isEmpty() ? ja.error : tw.error;
    return out;
  }
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
