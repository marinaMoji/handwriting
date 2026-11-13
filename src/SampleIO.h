#pragma once

#include "ZinniaBackend.h"

#include <QString>
#include <QStringList>
#include <QVector>

// Labeled handwriting samples for offline evaluation. One JSON file per sample:
//
//   { "version": 2, "character": "大", "surface": [480, 300],
//     "source": "hold-to-write", "timestamp": "2026-11-09T12:00:00",
//     "note": "", "strokes": [ [[x, y], [x, y], ...], ... ],
//     "writer": "w1", "handedness": "left", "device": "trackpad",
//     "set": "test", "session": "20261120-101500", "prompted": true }
//
// Version 1 files (no writer metadata) are still read; the metadata fields are then empty.
// "set" is "dev" (tuning), "test" (held-out, never tuned on) or "textbook"; "prompted" is true
// when the app showed the character (Practice mode) and false when the writer chose it.
// "writer" is a pseudonym, never a name.
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
  QString writer;      // pseudonym of the person writing ("" in version 1 files)
  QString handedness;  // "left", "right" or ""
  QString device;      // "trackpad", "mouse", "stylus" or ""
  QString set;         // "dev", "test", "textbook" or ""
  QString session;     // one Practice or capture session, e.g. "20261120-101500"
  bool prompted = false;
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
