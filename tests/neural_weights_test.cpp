#include "NeuralWeights.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

static QString touch(const QString& path) {
  QFile f(path);
  f.open(QIODevice::WriteOnly);
  f.write("x");
  return path;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir dir;
  const QString explicitFile = touch(dir.filePath(QStringLiteral("explicit.bin")));
  const QString envFile = touch(dir.filePath(QStringLiteral("env.bin")));
  QDir().mkpath(dir.filePath(QStringLiteral("models")));
  const QString modelsFile = touch(dir.filePath(QStringLiteral("models/inknet.bin")));
  const QString models = dir.filePath(QStringLiteral("models"));

  qunsetenv("HANDWRITING_INKNET");
  qunsetenv("HANDWRITING_NEURAL");
  expect(neural_weights::find(explicitFile, models) == explicitFile, "explicit path wins");
  expect(neural_weights::find(QString(), models) == modelsFile, "falls back to MODELS_DIR/inknet.bin");
  qputenv("HANDWRITING_INKNET", envFile.toUtf8());
  expect(neural_weights::find(QString(), models) == envFile, "$HANDWRITING_INKNET beats the models dir");
  expect(neural_weights::find(explicitFile, models) == explicitFile, "explicit path still beats the environment");
  expect(neural_weights::find(dir.filePath(QStringLiteral("missing.bin")), models) == envFile,
         "a missing explicit path is skipped");
  qunsetenv("HANDWRITING_INKNET");
  expect(neural_weights::find(QString(), dir.filePath(QStringLiteral("nowhere"))).isEmpty() ||
             QFileInfo(neural_weights::find(QString(), dir.filePath(QStringLiteral("nowhere")))).fileName() ==
                 QLatin1String("inknet.bin"),
         "nothing found gives empty, or only the build-time default");

  for (const char* v : {"off", "0", "FALSE", " no "}) {
    qputenv("HANDWRITING_NEURAL", v);
    expect(neural_weights::disabledByEnvironment(), "HANDWRITING_NEURAL disables the hybrid");
  }
  qputenv("HANDWRITING_NEURAL", "on");
  expect(!neural_weights::disabledByEnvironment(), "HANDWRITING_NEURAL=on leaves it enabled");
  qunsetenv("HANDWRITING_NEURAL");
  expect(!neural_weights::disabledByEnvironment(), "enabled by default");
  return failures ? 1 : 0;
}
