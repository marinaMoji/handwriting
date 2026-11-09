#pragma once

#include "ZinniaBackend.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <cmath>
#include <vector>

// Pure stroke cleanup for noisy trackpad ink. Header-only so the app, the eval
// tool and tests share one implementation.
//
// Spec strings (HANDWRITING_PREPROCESS, eval --variant): "none", "rdp:3", "smooth:2",
// "resample:3+smooth:3" (steps run in the written order). Resample first so that smoothing
// is a function of distance along the stroke, not of how fast the pen moved.

namespace stroke_preprocess {

inline Stroke dedupe(const Stroke& s) {
  Stroke out;
  out.reserve(s.size());
  for (const StrokePoint& p : s) {
    if (out.isEmpty() || out.last().x != p.x || out.last().y != p.y) {
      out.push_back(p);
    }
  }
  return out;
}

inline double perpendicularDistance(const StrokePoint& p, const StrokePoint& a, const StrokePoint& b) {
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  if (len2 == 0.0) {
    return std::hypot(p.x - a.x, p.y - a.y);
  }
  return std::fabs(dy * p.x - dx * p.y + b.x * a.y - b.y * a.x) / std::sqrt(len2);
}

// Ramer-Douglas-Peucker; keeps both endpoints.
inline Stroke simplifyRdp(const Stroke& input, double epsilon) {
  const Stroke s = dedupe(input);
  if (epsilon <= 0.0 || s.size() < 3) {
    return s;
  }
  std::vector<char> keep(static_cast<size_t>(s.size()), 0);
  keep.front() = keep.back() = 1;
  std::vector<std::pair<int, int>> stack{{0, static_cast<int>(s.size()) - 1}};
  while (!stack.empty()) {
    const auto [first, last] = stack.back();
    stack.pop_back();
    double maxDist = -1.0;
    int index = -1;
    for (int i = first + 1; i < last; ++i) {
      const double d = perpendicularDistance(s[i], s[first], s[last]);
      if (d > maxDist) {
        maxDist = d;
        index = i;
      }
    }
    if (index >= 0 && maxDist > epsilon) {
      keep[static_cast<size_t>(index)] = 1;
      stack.push_back({first, index});
      stack.push_back({index, last});
    }
  }
  Stroke out;
  for (int i = 0; i < s.size(); ++i) {
    if (keep[static_cast<size_t>(i)]) {
      out.push_back(s[i]);
    }
  }
  return out;
}

// 3-point moving average of interior points; endpoints are kept exactly.
inline Stroke smooth(const Stroke& input, int passes) {
  Stroke s = dedupe(input);
  for (int pass = 0; pass < passes && s.size() >= 3; ++pass) {
    Stroke next = s;
    for (int i = 1; i + 1 < s.size(); ++i) {
      next[i].x = static_cast<int>(std::lround((s[i - 1].x + s[i].x + s[i + 1].x) / 3.0));
      next[i].y = static_cast<int>(std::lround((s[i - 1].y + s[i].y + s[i + 1].y) / 3.0));
    }
    s = next;
  }
  return s;
}

// Equidistant points every `spacing` px along the stroke (arc length). First and last
// points are always kept, so endpoints and a single dot survive.
inline Stroke resample(const Stroke& input, double spacing) {
  const Stroke s = dedupe(input);
  if (spacing <= 0.0 || s.size() < 2) {
    return s;
  }
  Stroke out;
  out.push_back(s.first());
  double carry = 0.0;  // distance walked since the last emitted point
  for (int i = 1; i < s.size(); ++i) {
    const double x0 = s[i - 1].x, y0 = s[i - 1].y;
    const double dx = s[i].x - x0, dy = s[i].y - y0;
    const double len = std::hypot(dx, dy);
    double pos = spacing - carry;  // distance into this segment of the next emitted point
    while (pos <= len) {
      const double t = pos / len;
      StrokePoint p;
      p.x = static_cast<int>(std::lround(x0 + dx * t));
      p.y = static_cast<int>(std::lround(y0 + dy * t));
      out.push_back(p);
      pos += spacing;
    }
    carry = len - (pos - spacing);
  }
  if (out.last().x != s.last().x || out.last().y != s.last().y) {
    out.push_back(s.last());
  }
  return out;
}

struct Step {
  enum class Kind { Smooth, Rdp, Resample } kind = Kind::Rdp;
  double value = 0.0;
};

struct Options {
  QVector<Step> steps;
  bool isNone() const { return steps.isEmpty(); }
};

// Returns false (and leaves *out empty) if the spec is malformed.
inline bool parseSpec(const QString& spec, Options* out, QString* error = nullptr) {
  out->steps.clear();
  const QString trimmed = spec.trimmed().toLower();
  if (trimmed.isEmpty() || trimmed == QLatin1String("none")) {
    return true;
  }
  for (const QString& part : trimmed.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
    const QStringList kv = part.split(QLatin1Char(':'));
    bool ok = false;
    const double v = kv.size() == 2 ? kv[1].toDouble(&ok) : 0.0;
    if (!ok || v < 0.0) {
      if (error) *error = QStringLiteral("bad preprocess step '%1'").arg(part);
      out->steps.clear();
      return false;
    }
    Step step;
    step.value = v;
    if (kv[0] == QLatin1String("rdp")) {
      step.kind = Step::Kind::Rdp;
    } else if (kv[0] == QLatin1String("smooth")) {
      step.kind = Step::Kind::Smooth;
    } else if (kv[0] == QLatin1String("resample")) {
      step.kind = Step::Kind::Resample;
    } else {
      if (error) *error = QStringLiteral("unknown preprocess step '%1'").arg(kv[0]);
      out->steps.clear();
      return false;
    }
    out->steps.push_back(step);
  }
  return true;
}

inline QVector<Stroke> apply(const QVector<Stroke>& strokes, const Options& opts) {
  if (opts.isNone()) {
    return strokes;
  }
  QVector<Stroke> out;
  out.reserve(strokes.size());
  for (const Stroke& stroke : strokes) {
    Stroke s = stroke;
    for (const Step& step : opts.steps) {
      switch (step.kind) {
        case Step::Kind::Rdp: s = simplifyRdp(s, step.value); break;
        case Step::Kind::Smooth: s = smooth(s, static_cast<int>(step.value)); break;
        case Step::Kind::Resample: s = resample(s, step.value); break;
      }
    }
    // Zinnia rejects empty strokes; never drop a stroke, even a single dot.
    if (s.isEmpty() && !stroke.isEmpty()) {
      s.push_back(stroke.first());
    }
    out.push_back(s);
  }
  return out;
}

}  // namespace stroke_preprocess
