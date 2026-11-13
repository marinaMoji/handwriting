#pragma once

#include "InkNet.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#ifndef HANDWRITING_DEFAULT_INKNET_PATH
#define HANDWRITING_DEFAULT_INKNET_PATH "data/inknet.bin"
#endif

// Where the app looks for the InkNet weights (the order-free net that is mixed with Zinnia in
// "hybrid" mode; see docs/neural-scope.md). Weights are not committed: run scripts/fetch_inknet.sh
// or scripts/export_weights.sh. The app works without them, with Zinnia alone.
namespace neural_weights {

// HANDWRITING_NEURAL=off|0|false disables the neural half (for A/B comparison).
inline bool disabledByEnvironment() {
  const QString v = qEnvironmentVariable("HANDWRITING_NEURAL").trimmed().toLower();
  return v == QLatin1String("off") || v == QLatin1String("0") || v == QLatin1String("false") ||
         v == QLatin1String("no");
}

// First existing file among: the explicit path, $HANDWRITING_INKNET, <modelDir>/inknet.bin, the
// build-time default (data/inknet.bin in the source tree). Empty if none exists.
inline QString find(const QString& explicitPath, const QString& modelDir) {
  const QString candidates[] = {
      explicitPath,
      qEnvironmentVariable("HANDWRITING_INKNET"),
      modelDir.isEmpty() ? QString() : QDir(modelDir).filePath(QStringLiteral("inknet.bin")),
      QString::fromUtf8(HANDWRITING_DEFAULT_INKNET_PATH),
  };
  for (const QString& c : candidates) {
    if (!c.isEmpty() && QFileInfo(c).isFile()) return c;
  }
  return QString();
}

}  // namespace neural_weights
