#pragma once

#include "ZinniaBackend.h"

#include <QPointF>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

// Pure helpers: trackpad -> drawing-surface mapping, hold-to-write jump
// detection, and cropping strokes to the recognizer's canvas.
// Header-only so tests can cover them without Qt Widgets.

namespace stroke_mapping {

// Fallback pad width/height (typical MacBook) used until a backend reports the
// device's real size. The drawing surface has the pad's aspect, so the whole pad
// is usable and strokes are never stretched. HANDWRITING_PAD_ASPECT overrides it.
inline constexpr qreal kDefaultTrackpadAspect = 1.6;

inline int surfaceWidthFor(int surfaceHeight, qreal aspect) {
  return qMax(1, static_cast<int>(std::lround(surfaceHeight * aspect)));
}

// Full pad (normalized 0..1, top-left origin) -> full surface.
inline StrokePoint fromNormalizedTrackpad(QPointF normalized, int surfaceWidth, int surfaceHeight) {
  const qreal x = qBound<qreal>(0.0, normalized.x(), 1.0);
  const qreal y = qBound<qreal>(0.0, normalized.y(), 1.0);
  StrokePoint p;
  p.x = static_cast<int>(x * (surfaceWidth - 1) + 0.5);
  p.y = static_cast<int>(y * (surfaceHeight - 1) + 0.5);
  return p;
}

inline int distanceSquared(const StrokePoint& a, const StrokePoint& b) {
  const int dx = a.x - b.x;
  const int dy = a.y - b.y;
  return dx * dx + dy * dy;
}

inline bool isHoldJump(const StrokePoint& from, const StrokePoint& to, int jumpThreshold = 40) {
  return distanceSquared(from, to) >= jumpThreshold * jumpThreshold;
}

// Crop to the strokes' bounding box, scale uniformly to fit the canvas minus a
// margin, and center. Lets the user write anywhere on the surface at any size.
// Scale is capped so a tiny dot is not blown up to fill the canvas.
inline QVector<Stroke> fitToCanvas(const QVector<Stroke>& strokes,
                                   int canvasWidth,
                                   int canvasHeight,
                                   int margin = 24,
                                   qreal maxScale = 4.0) {
  bool any = false;
  int minX = 0, minY = 0, maxX = 0, maxY = 0;
  for (const Stroke& s : strokes) {
    for (const StrokePoint& p : s) {
      if (!any) {
        minX = maxX = p.x;
        minY = maxY = p.y;
        any = true;
      } else {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
      }
    }
  }
  if (!any) {
    return strokes;
  }

  const qreal bw = qMax(1, maxX - minX);
  const qreal bh = qMax(1, maxY - minY);
  const qreal availW = qMax(1, canvasWidth - 2 * margin);
  const qreal availH = qMax(1, canvasHeight - 2 * margin);
  const qreal scale = qMin(qMin(availW / bw, availH / bh), maxScale);
  const qreal cx = (minX + maxX) / 2.0;
  const qreal cy = (minY + maxY) / 2.0;

  QVector<Stroke> out;
  out.reserve(strokes.size());
  for (const Stroke& s : strokes) {
    Stroke t;
    t.reserve(s.size());
    for (const StrokePoint& p : s) {
      StrokePoint q;
      q.x = qBound(0, static_cast<int>(std::lround((p.x - cx) * scale + canvasWidth / 2.0)),
                   canvasWidth - 1);
      q.y = qBound(0, static_cast<int>(std::lround((p.y - cy) * scale + canvasHeight / 2.0)),
                   canvasHeight - 1);
      t.push_back(q);
    }
    out.push_back(t);
  }
  return out;
}

}  // namespace stroke_mapping
