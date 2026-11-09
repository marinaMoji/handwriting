// Offline evaluation: replay labeled samples through pipeline variants and report
// top-N accuracy and latency.
//
//   handwriting_eval [--samples DIR] [--synthetic N] [--models DIR]
//                    [--modes ja,tw,both] [--variants raw,none,rdp:3,smooth:2+rdp:3]
//                    [--misses] [--csv FILE] [--save-synthetic DIR]
//                    [--step PX] [--noise X] [--geom 0|1] [--margin N] [--maxscale X]
//
// --noise scales synthetic wobble/jitter (0 = clean templates); --geom 0 disables random
// scale/rotation/translation. --margin/--maxscale tune the crop/fit step.
//
// Variant "raw" skips the crop/fit step; every other variant is fit + that
// preprocess spec (see StrokePreprocess.h).

#include "SampleIO.h"
#include "StrokeMapping.h"
#include "StrokePreprocess.h"
#include "SyntheticCorpus.h"
#include "KanjiVgCorpus.h"
#include "Recognizer.h"
#include "ZinniaBackend.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QStringConverter>
#include <QTextStream>

#ifndef HANDWRITING_DEFAULT_MODEL_DIR
#define HANDWRITING_DEFAULT_MODEL_DIR "models"
#endif
#ifndef HANDWRITING_DEFAULT_SAMPLES_DIR
#define HANDWRITING_DEFAULT_SAMPLES_DIR "samples"
#endif

namespace {

struct Stats {
  int n = 0, top1 = 0, top5 = 0, top10 = 0;
  double rrSum = 0.0;
  double latencySum = 0.0;
  QStringList misses;
};

int rankOf(const RecognitionResult& r, const QString& ch) {
  for (int i = 0; i < r.candidates.size(); ++i) {
    if (r.candidates[i].character == ch) return i + 1;
  }
  return 0;
}

int gMargin = 24;
double gMaxScale = 4.0;

Recognizer::Options optionsFor(const QString& variant) {
  Recognizer::Options o;
  if (variant == QLatin1String("raw")) {
    o.fit = false;  // recognized at the surface size, no crop/fit, no preprocessing
    return o;
  }
  Recognizer::optionsFromSpec(variant, &o);
  o.margin = gMargin;
  o.maxScale = gMaxScale;
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  out.setEncoding(QStringConverter::Utf8);
  QTextStream err(stderr);
  err.setEncoding(QStringConverter::Utf8);

  QString samplesDir = QString::fromUtf8(HANDWRITING_DEFAULT_SAMPLES_DIR);
  QString modelDir = QString::fromUtf8(HANDWRITING_DEFAULT_MODEL_DIR);
  QString csvPath, saveSyntheticDir, kvgPath, kvgChars;
  int kvgMax = 0;
  unsigned seed = 1234;
  int synthetic = 0;
  double noise = 1.0;
  double step = 2.5;
  bool geom = true;
  bool showMisses = false;
  bool samplesGiven = false;
  fusion::Params fparams;
  QStringList fusions = {QStringLiteral("interleave")};
  QStringList modes = {QStringLiteral("ja"), QStringLiteral("tw"), QStringLiteral("both")};
  QStringList variants = {QStringLiteral("raw"), QStringLiteral("none"), QStringLiteral("rdp:3"),
                          QStringLiteral("smooth:2"), QStringLiteral("smooth:2+rdp:3")};

  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i) {
    const QString& a = args[i];
    auto next = [&]() { return i + 1 < args.size() ? args[++i] : QString(); };
    if (a == QLatin1String("--samples")) { samplesDir = next(); samplesGiven = true; }
    else if (a == QLatin1String("--models")) modelDir = next();
    else if (a == QLatin1String("--synthetic")) synthetic = next().toInt();
    else if (a == QLatin1String("--modes")) modes = next().split(',', Qt::SkipEmptyParts);
    else if (a == QLatin1String("--variants")) variants = next().split(',', Qt::SkipEmptyParts);
    else if (a == QLatin1String("--fusions")) fusions = next().split(',', Qt::SkipEmptyParts);
    else if (a == QLatin1String("--rrf-k")) fparams.rrfK = next().toDouble();
    else if (a == QLatin1String("--ja-bias")) fparams.jaBias = next().toDouble();
    else if (a == QLatin1String("--tw-bias")) fparams.twBias = next().toDouble();
    else if (a == QLatin1String("--csv")) csvPath = next();
    else if (a == QLatin1String("--save-synthetic")) saveSyntheticDir = next();
    else if (a == QLatin1String("--kanjivg")) kvgPath = next();
    else if (a == QLatin1String("--kvg-chars")) kvgChars = next();
    else if (a == QLatin1String("--kvg-max")) kvgMax = next().toInt();
    else if (a == QLatin1String("--seed")) seed = next().toUInt();
    else if (a == QLatin1String("--misses")) showMisses = true;
    else if (a == QLatin1String("--noise")) noise = next().toDouble();
    else if (a == QLatin1String("--step")) step = next().toDouble();
    else if (a == QLatin1String("--geom")) geom = next().toInt() != 0;
    else if (a == QLatin1String("--margin")) gMargin = next().toInt();
    else if (a == QLatin1String("--maxscale")) gMaxScale = next().toDouble();
    else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
      out << "usage: handwriting_eval [--samples DIR] [--synthetic N] [--models DIR]\n"
             "       [--modes ja,tw,both] [--variants raw,none,rdp:3,smooth:2+rdp:3]\n"
             "       [--misses] [--csv FILE] [--save-synthetic DIR]\n"
             "       [--fusions interleave,rrf,max,z,cal] [--rrf-k K] [--ja-bias B] [--tw-bias B]\n"
             "       [--kanjivg FILE [--kvg-chars STR] [--kvg-max N]] [--seed N]\n";
      return 0;
    } else {
      err << "unknown argument: " << a << "\n";
      return 2;
    }
  }

  for (const QString& f : fusions) {
    fusion::Method fm;
    if (!fusion::parse(f, &fm)) {
      err << "bad fusion '" << f << "' (interleave, rrf, max, z, cal)\n";
      return 2;
    }
  }
  for (const QString& v : variants) {
    stroke_preprocess::Options o;
    QString e;
    if (v != QLatin1String("raw") && !stroke_preprocess::parseSpec(v, &o, &e)) {
      err << "bad variant '" << v << "': " << e << "\n";
      return 2;
    }
  }

  ZinniaBackend backend;
  const Recognizer recognizer(&backend);
  QString loadError;
  if (!backend.loadModels(modelDir, &loadError)) {
    err << "Failed to load models from " << modelDir << ": " << loadError << "\n";
    return 1;
  }

  // Collect samples.
  QVector<Sample> samples;
  const QStringList files = sample_io::findSamples(samplesDir);
  int skipped = 0;
  for (const QString& f : files) {
    Sample s;
    QString e;
    if (sample_io::loadSample(f, &s, &e)) samples.push_back(s);
    else { ++skipped; err << "skip " << f << ": " << e << "\n"; }
  }
  const int realCount = samples.size();
  if (!kvgPath.isEmpty() && synthetic <= 0) synthetic = 1;
  if (synthetic > 0) {
    synthetic::Params sp;
    sp.stepPx = step;
    sp.wobbleAmp *= noise;
    sp.jitterSigma *= noise;
    if (!geom) {
      sp.scaleMin = sp.scaleMax = 1.0;
      sp.rotateDeg = 0.0;
      sp.noTranslate = true;
      sp.noAspectJitter = true;
    }
    QVector<Sample> syn;
    if (kvgPath.isEmpty()) {
      syn = synthetic::generate(synthetic, seed, sp);
    } else {
      QString kerr;
      QVector<synthetic::Template> tpls = kanjivg::loadTemplates(kvgPath, &kerr, kvgChars);
      if (tpls.isEmpty()) {
        err << "KanjiVG: " << (kerr.isEmpty() ? QStringLiteral("no usable characters") : kerr) << "\n";
        return 1;
      }
      tpls = kanjivg::subsample(tpls, kvgMax, seed);
      syn = synthetic::generateFrom(tpls, synthetic, seed, sp);
    }
    if (!saveSyntheticDir.isEmpty()) {
      for (const Sample& s : syn) {
        QString p, e;
        sample_io::saveSample(saveSyntheticDir, s, &p, &e);
      }
    }
    samples += syn;
  }
  if (samples.isEmpty()) {
    err << "No samples. Collect some with the app's \"Save sample\" button"
        << (samplesGiven ? "" : QStringLiteral(" (looked in %1)").arg(samplesDir))
        << ", or pass --synthetic N.\n";
    return 1;
  }
  out << "samples: " << realCount << " real, " << (samples.size() - realCount) << " synthetic"
      << (skipped ? QStringLiteral(", %1 unreadable").arg(skipped) : QString()) << "\n\n";

  QFile csv(csvPath);
  QTextStream csvOut(&csv);
  if (!csvPath.isEmpty() && csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
    csvOut.setEncoding(QStringConverter::Utf8);
    csvOut << "variant,mode,n,top1,top5,top10,mrr,mean_latency_ms\n";
  }

  auto modeOf = [](const QString& m) {
    if (m == QLatin1String("ja")) return RecognitionMode::Japanese;
    if (m == QLatin1String("tw")) return RecognitionMode::TraditionalChinese;
    return RecognitionMode::Both;
  };

  out << QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8\n")
             .arg(QStringLiteral("variant"), -18).arg(QStringLiteral("mode"), -11)
             .arg(QStringLiteral("n"), 5).arg(QStringLiteral("top1"), 7)
             .arg(QStringLiteral("top5"), 7).arg(QStringLiteral("top10"), 7)
             .arg(QStringLiteral("mrr"), 6).arg(QStringLiteral("ms"), 6);

  for (const QString& variant : variants) {
    // "both" expands to one row per fusion method (plain "both" = interleave).
    QStringList rows;
    for (const QString& m0 : modes) {
      if (m0 == QLatin1String("both")) {
        for (const QString& f : fusions) {
          rows << (f == QLatin1String("interleave") ? m0 : QStringLiteral("both/") + f);
        }
      } else {
        rows << m0;
      }
    }
    for (const QString& m : rows) {
      Stats st;
      Recognizer::Options ropts = optionsFor(variant);
      if (m.startsWith(QLatin1String("both/"))) {
        fusion::parse(m.mid(5), &ropts.fusion);
        ropts.fusionParams = fparams;
      }
      for (const Sample& s : samples) {
        QElapsedTimer t;
        t.start();
        const RecognitionResult r = recognizer.recognize(s.strokes, s.surfaceWidth, s.surfaceHeight,
                                                         modeOf(m.left(m.indexOf('/') < 0 ? m.size() : m.indexOf('/'))),
                                                         ropts);
        st.latencySum += t.nsecsElapsed() / 1e6;
        const int rank = rankOf(r, s.character);
        ++st.n;
        if (rank == 1) ++st.top1;
        if (rank >= 1 && rank <= 5) ++st.top5;
        if (rank >= 1) ++st.top10;
        if (rank >= 1) st.rrSum += 1.0 / rank;
        if (rank != 1) {
          st.misses << QStringLiteral("%1 -> %2").arg(s.character, r.candidates.isEmpty() ? QStringLiteral("(none)") : r.candidates[0].character);
        }
      }
      auto pct = [&](int k) { return 100.0 * k / qMax(1, st.n); };
      out << QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8\n")
                 .arg(variant, -18).arg(m, -11).arg(st.n, 5)
                 .arg(pct(st.top1), 6, 'f', 1).arg(pct(st.top5), 6, 'f', 1)
                 .arg(pct(st.top10), 6, 'f', 1).arg(st.rrSum / qMax(1, st.n), 6, 'f', 3)
                 .arg(st.latencySum / qMax(1, st.n), 6, 'f', 2);
      if (csv.isOpen()) {
        csvOut << variant << ',' << m << ',' << st.n << ',' << pct(st.top1) << ',' << pct(st.top5)
               << ',' << pct(st.top10) << ',' << st.rrSum / qMax(1, st.n) << ','
               << st.latencySum / qMax(1, st.n) << '\n';
      }
      if (showMisses && !st.misses.isEmpty()) {
        out << "    misses: " << st.misses.join(QStringLiteral(", ")) << "\n";
      }
    }
    out << "\n";
  }
  return 0;
}
