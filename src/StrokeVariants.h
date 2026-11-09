#pragma once

#include "ZinniaBackend.h"

#include <QVector>
#include <cmath>

// Alternative readings of one piece of ink, for recognizers (like Zinnia) that depend on
// stroke order and stroke count. Real handwriting reorders strokes (which inner stroke
// comes first), joins strokes cursively, and sometimes splits one stroke into two. Each
// generator proposes plausible alternatives; the original ink is always variant 0.
//
//   swap    swap two adjacent strokes (covers inner-stroke order variants)
//   merge   join stroke i and i+1 when the pen ended near where the next began
//   split   cut a stroke at its sharpest corner (a turn written without lifting the pen)

namespace stroke_variants {

struct Options {
  bool swap = false;
  bool merge = false;
  bool split = false;
  double mergeGap = 30.0;      // max endpoint distance (canvas px) to join two strokes
  double splitAngleDeg = 70.0; // min turning angle to cut a stroke
  int maxVariants = 40;        // including the original
  bool any() const { return swap || merge || split; }
};

inline double dist(const StrokePoint& a, const StrokePoint& b) {
  return std::hypot(double(a.x - b.x), double(a.y - b.y));
}

// Sharpest corner of a stroke measured over a window of ~12% of its length on each side.
// Returns the vertex index and angle in degrees (0 = straight), or -1 if too short.
inline int sharpestCorner(const Stroke& s, double* angleDeg) {
  *angleDeg = 0.0;
  const int n = s.size();
  if (n < 5) return -1;
  QVector<double> arc(n, 0.0);
  for (int i = 1; i < n; ++i) arc[i] = arc[i - 1] + dist(s[i - 1], s[i]);
  const double total = arc[n - 1];
  if (total < 20.0) return -1;
  const double win = qMax(6.0, total * 0.12);
  int best = -1;
  double bestAngle = 0.0;
  for (int i = 1; i < n - 1; ++i) {
    int a = i, b = i;
    while (a > 0 && arc[i] - arc[a] < win) --a;
    while (b < n - 1 && arc[b] - arc[i] < win) ++b;
    if (arc[i] - arc[a] < win * 0.5 || arc[b] - arc[i] < win * 0.5) continue;  // too near an end
    const double v1x = s[i].x - s[a].x, v1y = s[i].y - s[a].y;
    const double v2x = s[b].x - s[i].x, v2y = s[b].y - s[i].y;
    const double l1 = std::hypot(v1x, v1y), l2 = std::hypot(v2x, v2y);
    if (l1 < 1e-9 || l2 < 1e-9) continue;
    double c = (v1x * v2x + v1y * v2y) / (l1 * l2);
    c = qBound(-1.0, c, 1.0);
    const double ang = std::acos(c) * 180.0 / 3.14159265358979323846;
    if (ang > bestAngle) {
      bestAngle = ang;
      best = i;
    }
  }
  *angleDeg = bestAngle;
  return best;
}

inline QVector<QVector<Stroke>> generate(const QVector<Stroke>& strokes, const Options& o) {
  QVector<QVector<Stroke>> out;
  out.push_back(strokes);
  const int n = strokes.size();
  auto room = [&]() { return out.size() < o.maxVariants; };

  if (o.swap) {
    for (int i = 0; i + 1 < n && room(); ++i) {
      QVector<Stroke> v = strokes;
      std::swap(v[i], v[i + 1]);
      out.push_back(v);
    }
  }
  if (o.merge) {
    for (int i = 0; i + 1 < n && room(); ++i) {
      if (strokes[i].isEmpty() || strokes[i + 1].isEmpty()) continue;
      if (dist(strokes[i].last(), strokes[i + 1].first()) > o.mergeGap) continue;
      QVector<Stroke> v;
      for (int k = 0; k < n; ++k) {
        if (k == i) {
          Stroke joined = strokes[i];
          joined += strokes[i + 1];
          v.push_back(joined);
        } else if (k != i + 1) {
          v.push_back(strokes[k]);
        }
      }
      out.push_back(v);
    }
  }
  if (o.split) {
    for (int i = 0; i < n && room(); ++i) {
      double ang = 0.0;
      const int at = sharpestCorner(strokes[i], &ang);
      if (at < 0 || ang < o.splitAngleDeg) continue;
      QVector<Stroke> v;
      for (int k = 0; k < n; ++k) {
        if (k == i) {
          v.push_back(strokes[i].mid(0, at + 1));
          v.push_back(strokes[i].mid(at));
        } else {
          v.push_back(strokes[k]);
        }
      }
      out.push_back(v);
    }
  }
  return out;
}

}  // namespace stroke_variants
