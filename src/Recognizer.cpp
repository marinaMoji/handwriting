#include "Recognizer.h"

#include "StrokeCount.h"
#include "StrokeMapping.h"

#include <QElapsedTimer>
#include <QHash>
#include <algorithm>

Recognizer::Options Recognizer::defaultOptions() {
  Options o;
  stroke_preprocess::parseSpec(QStringLiteral("resample:4+smooth:3"), &o.preprocess);
  o.tolerance.merge = true;  // measured: +10 points top-5 on held-out real samples (docs/findings.md)
  o.tolerance.split = true;
  o.countWeight = 0.15;  // measured: small but consistent gain on both halves (docs/findings.md)
  return o;
}

bool Recognizer::optionsFromSpec(const QString& fullSpec, Options* out, QString* error) {
  Options o = defaultOptions();
  o.tolerance = stroke_variants::Options();  // a spec states tolerance explicitly
  o.countWeight = 0.0;
  QString spec = fullSpec;
  const int at = fullSpec.indexOf(QLatin1Char('@'));
  if (at >= 0) {
    spec = fullSpec.left(at);
    for (const QString& t : fullSpec.mid(at + 1).split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
      if (t == QLatin1String("swap")) o.tolerance.swap = true;
      else if (t == QLatin1String("merge")) o.tolerance.merge = true;
      else if (t == QLatin1String("split")) o.tolerance.split = true;
      else if (t == QLatin1String("count")) o.countWeight = 0.15;
      else {
        if (error) *error = QStringLiteral("bad tolerance '%1' (swap, merge, split, count)").arg(t);
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
    const int written = variants[vi].size();
    for (Candidate c : r.candidates) {
      if (vi > 0) c.score -= static_cast<float>(options.tolerancePenalty);
      if (options.countWeight > 0.0) {
        const int expected = stroke_count::expected(c.character);
        if (expected > 0) {
          const int extra = qMax(0, qAbs(written - expected) - options.countBand);
          c.score -= static_cast<float>(options.countWeight * extra);
        }
      }
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
  if (options.neural != Options::Neural::Off && net_ && net_->loaded() && mode == RecognitionMode::Both &&
      options.fit) {
    return recognizeNeural(raw, surfaceWidth, surfaceHeight, options);
  }
  const Prepared p = prepare(raw, surfaceWidth, surfaceHeight, options);
  if (options.tolerance.any() || options.countWeight > 0.0) {
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

RecognitionResult Recognizer::recognizeNeural(const QVector<Stroke>& rawInput, int surfaceWidth,
                                              int surfaceHeight, const Options& options) const {
  QElapsedTimer timer;
  timer.start();
  // The net sees the ink as a picture: specks dropped and fitted like Zinnia's input, but not
  // smoothed or resampled (that is what it was trained on).
  const QVector<Stroke> raw = options.dropSpecks ? stroke_preprocess::dropSpecks(rawInput) : rawInput;
  const QVector<Stroke> fitted =
      stroke_mapping::fitToCanvas(raw, options.canvas, options.canvas, options.margin, options.maxScale);
  std::vector<ink::StrokeD> strokes;
  for (const Stroke& s : fitted) {
    ink::StrokeD d;
    for (const StrokePoint& pt : s) d.emplace_back(pt.x, pt.y);
    strokes.push_back(std::move(d));
  }
  const std::vector<uint8_t> image = ink::render(strokes);
  const auto top = net_->top(image.data(), qMax(options.neuralDepth, options.topN));

  RecognitionResult out;
  QHash<QString, float> neuralScore;
  for (const auto& [idx, lp] : top) neuralScore.insert(QString::fromStdString(net_->className(idx)), lp);

  if (options.neural == Options::Neural::Only) {
    for (const auto& [idx, lp] : top) {
      Candidate c;
      c.character = QString::fromStdString(net_->className(idx));
      c.score = lp;
      c.modelSource = QStringLiteral("NN");
      out.candidates.append(c);
      if (out.candidates.size() >= options.topN) break;
    }
    for (int i = 0; i < out.candidates.size(); ++i) out.candidates[i].modelRank = i + 1;
    out.latencyMs = timer.nsecsElapsed() / 1e6;
    return out;
  }

  const Prepared p = prepare(rawInput, surfaceWidth, surfaceHeight, options);
  const int depth = qMax(options.fusionDepth, options.topN);
  const RecognitionResult ja = recognizeVariants(backend_, p, RecognitionMode::Japanese, options, depth);
  const RecognitionResult tw = recognizeVariants(backend_, p, RecognitionMode::TraditionalChinese, options, depth);
  out.strokeCount = ja.strokeCount;
  QHash<QString, Candidate> z;
  float zFloor = 0.0f;
  bool zAny = false;
  for (const RecognitionResult* r : {&ja, &tw}) {
    for (const Candidate& c : r->candidates) {
      auto it = z.find(c.character);
      if (it == z.end() || c.score > it.value().score) z.insert(c.character, c);
      zFloor = zAny ? qMin(zFloor, c.score) : c.score;
      zAny = true;
    }
  }
  float nFloor = 0.0f;
  bool nAny = false;
  for (auto it = neuralScore.cbegin(); it != neuralScore.cend(); ++it) {
    nFloor = nAny ? qMin(nFloor, it.value()) : it.value();
    nAny = true;
  }
  QHash<QString, Candidate> merged = z;
  for (auto it = neuralScore.cbegin(); it != neuralScore.cend(); ++it) {
    if (!merged.contains(it.key())) {
      Candidate c;
      c.character = it.key();
      c.modelSource = QStringLiteral("NN");
      merged.insert(it.key(), c);
    }
  }
  QVector<Candidate> list;
  for (auto it = merged.begin(); it != merged.end(); ++it) {
    Candidate c = it.value();
    const float zs = z.contains(c.character) ? z.value(c.character).score : zFloor;
    const float ns = neuralScore.contains(c.character) ? neuralScore.value(c.character) : nFloor;
    c.score = ns + static_cast<float>(options.neuralWeight) * zs;
    list.append(c);
  }
  std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) {
    return a.score != b.score ? a.score > b.score : a.character < b.character;
  });
  if (list.size() > options.topN) list.resize(options.topN);
  for (int i = 0; i < list.size(); ++i) list[i].modelRank = i + 1;
  out.candidates = list;
  out.latencyMs = ja.latencyMs + tw.latencyMs + timer.nsecsElapsed() / 1e6;
  if (out.candidates.isEmpty()) out.error = !ja.error.isEmpty() ? ja.error : tw.error;
  return out;
}
