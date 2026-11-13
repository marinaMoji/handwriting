// The optional alternative Japanese model: loaded beside the default, switched on and off.
// Needs the production models (scripts/fetch_models.sh); skips without them.
#include "ZinniaBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#ifndef HANDWRITING_DEFAULT_MODEL_DIR
#define HANDWRITING_DEFAULT_MODEL_DIR "models"
#endif

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QString dir = QString::fromUtf8(HANDWRITING_DEFAULT_MODEL_DIR);
  ZinniaBackend backend;
  QString err;
  if (!backend.loadModels(dir, &err)) {
    QTextStream(stdout) << "SKIP models not downloaded: " << err << "\n";
    return 0;
  }
  const QString ja = QDir(dir).filePath(QStringLiteral("handwriting-ja.model"));
  const QString tw = QDir(dir).filePath(QStringLiteral("handwriting-zh_TW.model"));

  expect(!backend.hasAlternativeJapanese(), "no alternative by default");
  backend.useAlternativeJapanese(true);
  expect(!backend.usingAlternativeJapanese(), "cannot switch to an alternative that is not loaded");

  expect(!backend.loadAlternativeJapanese(dir + QStringLiteral("/nope.model"), &err), "missing file refused");
  expect(!backend.hasAlternativeJapanese(), "failed load leaves no alternative");

  // The Traditional Chinese file is a valid Zinnia model, so it serves as a distinguishable stand-in.
  expect(backend.loadAlternativeJapanese(tw, &err), "alternative loads");
  expect(backend.hasAlternativeJapanese() && backend.alternativeJapanesePath() == tw, "alternative path recorded");
  expect(backend.japaneseModelPath() == ja, "default model still active after loading the alternative");

  const QVector<Stroke> cross = {{{150, 40}, {150, 260}}, {{40, 150}, {260, 150}}};
  const RecognitionResult before = backend.recognize(cross, RecognitionMode::Japanese);
  backend.useAlternativeJapanese(true);
  expect(backend.usingAlternativeJapanese() && backend.japaneseModelPath() == tw, "switch on selects the alternative");
  const RecognitionResult alt = backend.recognize(cross, RecognitionMode::Japanese);
  expect(!alt.candidates.isEmpty(), "alternative recognizes");
  const RecognitionResult twOnly = backend.recognize(cross, RecognitionMode::TraditionalChinese);
  expect(!alt.candidates.isEmpty() && !twOnly.candidates.isEmpty() &&
             alt.candidates[0].character == twOnly.candidates[0].character,
         "with the alternative on, the Japanese slot answers like that model");
  backend.useAlternativeJapanese(false);
  const RecognitionResult after = backend.recognize(cross, RecognitionMode::Japanese);
  expect(!backend.usingAlternativeJapanese() && backend.japaneseModelPath() == ja, "switch off restores the default");
  expect(!before.candidates.isEmpty() && !after.candidates.isEmpty() &&
             before.candidates[0].character == after.candidates[0].character &&
             before.candidates[0].score == after.candidates[0].score,
         "default answers are unchanged by the round trip");
  return failures ? 1 : 0;
}
