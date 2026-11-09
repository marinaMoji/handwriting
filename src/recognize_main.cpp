// handwriting_recognize: recognize saved sample JSON files from the command line.
//   handwriting_recognize [--models DIR] [--mode ja|tw|both] [--preprocess SPEC] [--top N] FILE...
// Prints the ranked candidates for each file; the expected character (if the sample has one)
// is marked with its rank.
#include "Recognizer.h"
#include "SampleIO.h"

#include <QCoreApplication>
#include <QStringConverter>
#include <QTextStream>

#ifndef HANDWRITING_DEFAULT_MODEL_DIR
#define HANDWRITING_DEFAULT_MODEL_DIR "models"
#endif

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  out.setEncoding(QStringConverter::Utf8);
  QTextStream err(stderr);
  err.setEncoding(QStringConverter::Utf8);

  QString models = QStringLiteral(HANDWRITING_DEFAULT_MODEL_DIR);
  QString modeName = QStringLiteral("both");
  QString spec = QString::fromLatin1(Recognizer::kDefaultPreprocessSpec);
  int top = 10;
  QStringList files;
  const QStringList args = app.arguments().mid(1);
  for (int i = 0; i < args.size(); ++i) {
    const QString& a = args[i];
    auto next = [&](QString* dst) {
      if (i + 1 >= args.size()) {
        err << "missing value for " << a << "\n";
        return false;
      }
      *dst = args[++i];
      return true;
    };
    QString v;
    if (a == QLatin1String("--models")) {
      if (!next(&models)) return 2;
    } else if (a == QLatin1String("--mode")) {
      if (!next(&modeName)) return 2;
    } else if (a == QLatin1String("--preprocess")) {
      if (!next(&spec)) return 2;
    } else if (a == QLatin1String("--top")) {
      if (!next(&v)) return 2;
      top = qBound(1, v.toInt(), 50);
    } else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
      out << "usage: handwriting_recognize [--models DIR] [--mode ja|tw|both] "
             "[--preprocess SPEC] [--top N] FILE...\n";
      return 0;
    } else {
      files << a;
    }
  }
  if (files.isEmpty()) {
    err << "no sample files given (see --help)\n";
    return 2;
  }

  RecognitionMode mode = RecognitionMode::Both;
  if (modeName == QLatin1String("ja")) mode = RecognitionMode::Japanese;
  else if (modeName == QLatin1String("tw")) mode = RecognitionMode::TraditionalChinese;
  else if (modeName != QLatin1String("both")) {
    err << "bad --mode '" << modeName << "'\n";
    return 2;
  }

  Recognizer::Options options;
  QString perr;
  if (!Recognizer::optionsFromSpec(spec, &options, &perr)) {
    err << "bad --preprocess: " << perr << "\n";
    return 2;
  }
  options.topN = top;

  ZinniaBackend backend;
  QString lerr;
  if (!backend.loadModels(models, &lerr)) {
    err << "cannot load models from " << models << ": " << lerr << "\n";
    return 1;
  }
  const Recognizer recognizer(&backend);

  int status = 0;
  for (const QString& path : files) {
    Sample s;
    QString serr;
    if (!sample_io::loadSample(path, &s, &serr)) {
      err << path << ": " << serr << "\n";
      status = 1;
      continue;
    }
    const RecognitionResult r =
        recognizer.recognize(s.strokes, s.surfaceWidth, s.surfaceHeight, mode, options);
    out << path << "  expected=" << (s.character.isEmpty() ? QStringLiteral("?") : s.character)
        << "  strokes=" << s.strokes.size() << "  " << r.latencyMs << " ms\n";
    if (!r.error.isEmpty()) {
      out << "  error: " << r.error << "\n";
      status = 1;
      continue;
    }
    for (int i = 0; i < r.candidates.size(); ++i) {
      const Candidate& c = r.candidates[i];
      out << QStringLiteral("  %1 %2  %3 %4#%5%6\n")
                 .arg(i + 1, 2).arg(c.character).arg(c.score, 8, 'f', 2).arg(c.modelSource)
                 .arg(c.modelRank).arg(c.character == s.character ? QStringLiteral("  <== expected")
                                                                  : QString());
    }
  }
  return status;
}
