#include "ZinniaBackend.h"

#include <QCoreApplication>
#include <QStringConverter>
#include <QTextStream>

#ifndef HANDWRITING_DEFAULT_MODEL_DIR
#define HANDWRITING_DEFAULT_MODEL_DIR "models"
#endif

// Hardcoded stroke sequence from the Zinnia SDK example (あ-like / simple kanji sample).
static QVector<Stroke> sampleStrokes() {
  QVector<Stroke> strokes;
  auto add = [&](std::initializer_list<std::pair<int, int>> points) {
    Stroke s;
    for (const auto& p : points) {
      s.push_back(StrokePoint{p.first, p.second});
    }
    strokes.push_back(s);
  };
  add({{51, 29}, {117, 41}});
  add({{99, 65}, {219, 77}});
  add({{27, 131}, {261, 131}});
  add({{129, 17}, {57, 203}});
  add({{111, 71}, {219, 173}});
  add({{81, 161}, {93, 281}});
  add({{99, 167}, {207, 167}, {189, 245}});
  add({{99, 227}, {189, 227}});
  add({{111, 257}, {189, 245}});
  return strokes;
}

static void printResult(QTextStream& out, const QString& label, const RecognitionResult& result) {
  out << "== " << label << " ==\n";
  out << "latency_ms=" << result.latencyMs << " strokes=" << result.strokeCount << "\n";
  if (!result.error.isEmpty()) {
    out << "error=" << result.error << "\n";
  }
  for (const Candidate& c : result.candidates) {
    out << c.modelSource << " #" << c.modelRank << "\t" << c.character << "\t" << c.score << "\n";
  }
  out << "\n";
}

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  out.setEncoding(QStringConverter::Utf8);

  QString modelDir = QString::fromUtf8(HANDWRITING_DEFAULT_MODEL_DIR);
  if (argc > 1) {
    modelDir = QString::fromLocal8Bit(argv[1]);
  }

  ZinniaBackend backend;
  QString error;
  if (!backend.loadModels(modelDir, &error)) {
    QTextStream err(stderr);
    err.setEncoding(QStringConverter::Utf8);
    err << "Failed to load models from " << modelDir << ": " << error << "\n";
    return 1;
  }

  out << "JA model: " << backend.japaneseModelPath() << "\n";
  out << "TW model: " << backend.traditionalModelPath() << "\n\n";

  const QVector<Stroke> strokes = sampleStrokes();
  printResult(out, QStringLiteral("Japanese"),
              backend.recognize(strokes, RecognitionMode::Japanese));
  printResult(out, QStringLiteral("Traditional Chinese"),
              backend.recognize(strokes, RecognitionMode::TraditionalChinese));
  printResult(out, QStringLiteral("Both"),
              backend.recognize(strokes, RecognitionMode::Both));

  return 0;
}
