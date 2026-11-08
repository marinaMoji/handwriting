#include "MainWindow.h"
#include "ZinniaBackend.h"

#include <QApplication>
#include <QMessageBox>

#ifndef HANDWRITING_DEFAULT_MODEL_DIR
#define HANDWRITING_DEFAULT_MODEL_DIR "models"
#endif

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("handwriting"));
  app.setOrganizationName(QStringLiteral("marinaMoji"));

  QString modelDir = QString::fromUtf8(HANDWRITING_DEFAULT_MODEL_DIR);
  if (argc > 1) {
    modelDir = QString::fromLocal8Bit(argv[1]);
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

  MainWindow window(&backend);
  window.show();
  return app.exec();
}
