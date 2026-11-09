#pragma once

#include "ZinniaBackend.h"

#include <QString>
#include <QStringList>
#include <QVector>

// Labeled handwriting samples for offline evaluation. One JSON file per sample:
//
//   { "version": 1, "character": "大", "surface": [480, 300],
//     "source": "hold-to-write", "timestamp": "2026-11-09T12:00:00",
//     "note": "", "strokes": [ [[x, y], [x, y], ...], ... ] }
//
// Strokes are the raw ink in drawing-surface coordinates (before crop/fit and
// preprocessing), so the eval tool can replay them through any pipeline variant.

struct Sample {
  QString character;  // the character the writer intended
  QVector<Stroke> strokes;
  int surfaceWidth = 300;
  int surfaceHeight = 300;
  QString source;  // "hold-to-write", "click-drag", "mixed", "synthetic", ...
  QString timestamp;
  QString note;
};

namespace sample_io {

// Writes <dir>/<U+XXXX>_<timestamp>.json, creating dir if needed.
bool saveSample(const QString& dir, const Sample& sample, QString* pathOut, QString* error);
bool loadSample(const QString& path, Sample* sample, QString* error);
// Recursively lists *.json sample files, sorted by path.
QStringList findSamples(const QString& dir);

QByteArray toJson(const Sample& sample);
bool fromJson(const QByteArray& json, Sample* sample, QString* error);

}  // namespace sample_io
