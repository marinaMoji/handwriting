#include "InkNet.h"
#include "MainWindow.h"
#include "NeuralWeights.h"
#include "ZinniaBackend.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QStringList>

#ifndef HANDWRITING_DEFAULT_MODEL_DIR
#define HANDWRITING_DEFAULT_MODEL_DIR "models"
#endif

// Usage: handwriting_app [MODELS_DIR] [--inknet FILE] [--no-neural]
//   MODELS_DIR  holds handwriting-ja.model and handwriting-zh_TW.model (default: models/)
//   --inknet    InkNet weights for hybrid recognition (default: $HANDWRITING_INKNET,
//               MODELS_DIR/inknet.bin, then data/inknet.bin)
//   --no-neural Zinnia only (same as HANDWRITING_NEURAL=off)
int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("handwriting"));
  app.setOrganizationName(QStringLiteral("marinaMoji"));

  QString modelDir = QString::fromUtf8(HANDWRITING_DEFAULT_MODEL_DIR);
  QString inknetPath;
  bool noNeural = neural_weights::disabledByEnvironment();
  const QStringList args = app.arguments().mid(1);
  bool haveModelDir = false;
  for (int i = 0; i < args.size(); ++i) {
    if (args[i] == QLatin1String("--inknet") && i + 1 < args.size()) {
      inknetPath = args[++i];
    } else if (args[i] == QLatin1String("--no-neural")) {
      noNeural = true;
    } else if (!haveModelDir && !args[i].startsWith(QLatin1String("--"))) {
      modelDir = args[i];
      haveModelDir = true;
    }
  }

  ZinniaBackend backend;
  QString error;
  if (!backend.loadModels(modelDir, &error)) {
    QMessageBox::critical(
        nullptr, QObject::tr("Model load failed"),
        QObject::tr("%1\n\nPass a models directory as the first argument, or run "
                    "scripts/fetch_models.sh.")
            .arg(error));
    return 1;
  }

  // The order-free net is optional: without weights the app falls back to Zinnia alone.
  ink::InkNet net;
  QString neuralNote;
  if (noNeural) {
    neuralNote = QObject::tr("Neural hybrid off");
  } else {
    const QString path = neural_weights::find(inknetPath, modelDir);
    if (path.isEmpty()) {
      neuralNote = QObject::tr("No InkNet weights found (run scripts/fetch_inknet.sh); using Zinnia only");
    } else {
      QElapsedTimer t;
      t.start();
      std::string nerr;
      if (net.load(path.toStdString(), &nerr)) {
        qInfo("InkNet weights: %s (%d classes, %lld ms)", qPrintable(path), net.numClasses(),
              static_cast<long long>(t.elapsed()));
      } else {
        neuralNote = QObject::tr("Could not load InkNet weights %1: %2; using Zinnia only")
                         .arg(path, QString::fromStdString(nerr));
        qWarning("%s", qPrintable(neuralNote));
      }
    }
  }

  MainWindow window(&backend, net.loaded() ? &net : nullptr, neuralNote);
  window.show();
  return app.exec();
}
