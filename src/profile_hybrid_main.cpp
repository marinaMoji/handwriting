// Break down hybrid recognition latency on real samples.
//
//   ./build-rel/hybrid_profile [--samples DIR] [--models DIR] [--neural FILE] [--weight W] [--limit N]
//
// Build it in Release (`cmake -B build-rel -DCMAKE_BUILD_TYPE=Release`, target hybrid_profile):
// timings from an unoptimised build are several times too slow. The neural weights default to
// data/inknet.bin (see scripts/export_weights.sh).
//
// Prints mean milliseconds for: prepare, neural(render+infer), zinnia JA, zinnia TW,
// end-to-end hybrid (same path as Recognizer::recognizeNeural). The isolated Zinnia rows
// use a single reading; the end-to-end row also pays for the tolerance (merge/split) readings.

#include "InkNet.h"
#include "Recognizer.h"
#include "SampleIO.h"
#include "ZinniaBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTextStream>
#include <vector>

#ifndef HANDWRITING_DEFAULT_MODEL_DIR
#define HANDWRITING_DEFAULT_MODEL_DIR "models"
#endif
#ifndef HANDWRITING_DEFAULT_SAMPLES_DIR
#define HANDWRITING_DEFAULT_SAMPLES_DIR "samples"
#endif

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  out.setEncoding(QStringConverter::Utf8);

  QString modelDir = QString::fromUtf8(HANDWRITING_DEFAULT_MODEL_DIR);
  QString samplesDir = QString::fromUtf8(HANDWRITING_DEFAULT_SAMPLES_DIR);
  QString neuralPath = QStringLiteral("data/inknet.bin");
  double weight = 4.0;
  int limit = 0;
  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i) {
    auto next = [&]() { return i + 1 < args.size() ? args[++i] : QString(); };
    if (args[i] == QLatin1String("--models")) modelDir = next();
    else if (args[i] == QLatin1String("--samples")) samplesDir = next();
    else if (args[i] == QLatin1String("--neural")) neuralPath = next();
    else if (args[i] == QLatin1String("--weight")) weight = next().toDouble();
    else if (args[i] == QLatin1String("--limit")) limit = next().toInt();
  }

  ZinniaBackend backend;
  QString err;
  if (!backend.loadModels(modelDir, &err)) {
    out << "models: " << err << "\n";
    return 1;
  }
  ink::InkNet net;
  std::string nerr;
  if (!net.load(neuralPath.toStdString(), &nerr)) {
    out << "neural: " << QString::fromStdString(nerr) << "\n";
    return 1;
  }

  QVector<Sample> samples;
  for (const QString& f : sample_io::findSamples(samplesDir)) {
    Sample s;
    QString e;
    if (!sample_io::loadSample(f, &s, &e)) continue;
    samples.push_back(s);
    if (limit > 0 && samples.size() >= limit) break;
  }
  if (samples.isEmpty()) {
    out << "no samples in " << samplesDir << "\n";
    return 1;
  }

  Recognizer::Options opt = Recognizer::defaultOptions();
  QString specErr;
  if (!Recognizer::optionsFromSpec(QString::fromUtf8(Recognizer::kDefaultPreprocessSpec), &opt, &specErr)) {
    out << "spec: " << specErr << "\n";
    return 1;
  }
  opt.neural = Recognizer::Options::Neural::Hybrid;
  opt.neuralWeight = weight;
  const int depth = qMax(opt.fusionDepth, opt.topN);

  double prepMs = 0, neuralMs = 0, jaMs = 0, twMs = 0, hybridMs = 0;
  Recognizer recognizer(&backend, &net);

  for (const Sample& s : samples) {
    QElapsedTimer t;

    t.start();
    const Recognizer::Prepared p =
        Recognizer::prepare(s.strokes, s.surfaceWidth, s.surfaceHeight, opt);
    prepMs += t.nsecsElapsed() / 1e6;

    // Neural path pieces (the same image code recognizeNeural uses, without Zinnia).
    t.start();
    {
      const std::vector<uint8_t> image = Recognizer::neuralImage(s.strokes, opt);
      (void)net.top(image.data(), qMax(opt.neuralDepth, opt.topN));
    }
    neuralMs += t.nsecsElapsed() / 1e6;

    t.start();
    (void)backend.recognize(p.strokes, RecognitionMode::Japanese, p.width, p.height, depth);
    jaMs += t.nsecsElapsed() / 1e6;

    t.start();
    (void)backend.recognize(p.strokes, RecognitionMode::TraditionalChinese, p.width, p.height,
                            depth);
    twMs += t.nsecsElapsed() / 1e6;

    t.start();
    (void)recognizer.recognize(s.strokes, s.surfaceWidth, s.surfaceHeight, RecognitionMode::Both,
                               opt);
    hybridMs += t.nsecsElapsed() / 1e6;
  }

  const double n = samples.size();
  out << "hybrid_profile samples=" << samples.size() << " models=" << modelDir
      << " neural=" << neuralPath << " weight=" << weight << "\n";
  auto row = [&](const char* name, double sum) {
    out << QStringLiteral("%1 %2\n").arg(QString::fromUtf8(name), -28).arg(sum / n, 8, 'f', 2);
  };
  row("prepare_ms", prepMs);
  row("neural_render_infer_ms", neuralMs);
  row("zinnia_ja_ms", jaMs);
  row("zinnia_tw_ms", twMs);
  row("sum_parts_ms", prepMs + neuralMs + jaMs + twMs);
  row("hybrid_e2e_ms", hybridMs);
  // e2e runs Zinnia over every merge/split reading, the parts above use one reading each.
  row("tolerance_extra_ms", hybridMs - (prepMs + neuralMs + jaMs + twMs));
  return 0;
}
