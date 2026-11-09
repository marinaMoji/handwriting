#pragma once

#include "SampleIO.h"

#include <QString>
#include <QVector>
#include <cmath>
#include <random>

// Synthetic "trackpad-like" ink for robustness experiments when no real samples
// are available. Hand-authored clean stroke templates (correct stroke order) are
// densified and degraded with correlated wobble, per-point jitter, anisotropic
// scale, rotation and translation. This measures how much a preprocessing step
// helps with noisy input; it says nothing about real handwriting style.

namespace synthetic {

struct Template {
  QString character;
  QVector<Stroke> strokes;  // in a 300x300 box
};

inline Stroke S(std::initializer_list<std::pair<int, int>> pts) {
  Stroke s;
  for (const auto& p : pts) s.push_back(StrokePoint{p.first, p.second});
  return s;
}

inline const QVector<Template>& templates() {
  static const QVector<Template> t = {
      {"一", {S({{40, 150}, {260, 150}})}},
      {"二", {S({{70, 100}, {230, 100}}), S({{40, 200}, {260, 200}})}},
      {"三", {S({{70, 70}, {230, 70}}), S({{90, 150}, {210, 150}}), S({{40, 230}, {260, 230}})}},
      {"十", {S({{40, 150}, {260, 150}}), S({{150, 40}, {150, 260}})}},
      {"人", {S({{150, 40}, {70, 260}}), S({{140, 110}, {240, 260}})}},
      {"大", {S({{40, 110}, {260, 110}}), S({{150, 40}, {60, 260}}), S({{150, 120}, {250, 260}})}},
      {"小", {S({{150, 40}, {150, 250}, {125, 230}}), S({{90, 120}, {50, 200}}), S({{210, 120}, {250, 200}})}},
      {"山", {S({{150, 50}, {150, 220}}), S({{60, 120}, {60, 230}, {240, 230}}), S({{240, 120}, {240, 230}})}},
      {"日", {S({{80, 50}, {80, 250}}), S({{80, 50}, {220, 50}, {220, 250}}), S({{80, 150}, {220, 150}}), S({{80, 250}, {220, 250}})}},
      {"木", {S({{40, 120}, {260, 120}}), S({{150, 40}, {150, 260}}), S({{145, 150}, {60, 250}}), S({{155, 150}, {240, 250}})}},
      {"口", {S({{70, 80}, {70, 230}}), S({{70, 80}, {230, 80}, {230, 230}}), S({{70, 230}, {230, 230}})}},
      {"川", {S({{90, 50}, {70, 250}}), S({{150, 50}, {150, 250}}), S({{220, 50}, {220, 250}})}},
      {"八", {S({{130, 70}, {70, 240}}), S({{160, 90}, {240, 240}})}},
      {"上", {S({{150, 40}, {150, 230}}), S({{150, 130}, {220, 130}}), S({{50, 230}, {260, 230}})}},
      {"土", {S({{80, 110}, {220, 110}}), S({{150, 40}, {150, 230}}), S({{40, 230}, {260, 230}})}},
  };
  return t;
}

struct Params {
  double stepPx = 2.5;       // point spacing after densification (trackpads are dense)
  double wobbleAmp = 5.0;    // low-frequency lateral wobble amplitude (px)
  double jitterSigma = 1.2;  // per-point gaussian noise (px)
  double scaleMin = 0.6, scaleMax = 1.3;
  double rotateDeg = 6.0;
  bool noTranslate = false;
  bool noAspectJitter = false;
  int surfaceW = 480, surfaceH = 300;
};

// n variants per template; deterministic for a given seed.
inline QVector<Sample> generateFrom(const QVector<Template>& templates, int perTemplate,
                                    unsigned seed = 1234, const Params& p = {}) {
  std::mt19937 rng(seed);
  auto uni = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
  std::normal_distribution<double> gauss(0.0, 1.0);

  QVector<Sample> out;
  for (const Template& tpl : templates) {
    for (int v = 0; v < perTemplate; ++v) {
      const double sx = uni(p.scaleMin, p.scaleMax) * (p.noAspectJitter ? 1.0 : uni(0.9, 1.15));
      const double sy = uni(p.scaleMin, p.scaleMax);
      const double rot = uni(-p.rotateDeg, p.rotateDeg) * 3.14159265358979323846 / 180.0;
      const double tx = (p.noTranslate ? 0.0 : uni(-60, 60)) + p.surfaceW / 2.0;
      const double ty = (p.noTranslate ? 0.0 : uni(-40, 40)) + p.surfaceH / 2.0;
      const double phase = uni(0, 6.28);
      const double freq = uni(0.02, 0.06);

      Sample s;
      s.character = tpl.character;
      s.source = QStringLiteral("synthetic");
      s.surfaceWidth = p.surfaceW;
      s.surfaceHeight = p.surfaceH;
      for (const Stroke& clean : tpl.strokes) {
        Stroke noisy;
        double travelled = 0.0;
        for (int i = 1; i < clean.size(); ++i) {
          const double x0 = clean[i - 1].x, y0 = clean[i - 1].y;
          const double x1 = clean[i].x, y1 = clean[i].y;
          const double len = std::hypot(x1 - x0, y1 - y0);
          const int n = qMax(1, static_cast<int>(len / p.stepPx));
          const double nx = len > 0 ? -(y1 - y0) / len : 0.0;  // unit normal
          const double ny = len > 0 ? (x1 - x0) / len : 0.0;
          for (int k = (i == 1 ? 0 : 1); k <= n; ++k) {
            const double t = static_cast<double>(k) / n;
            double x = x0 + (x1 - x0) * t;
            double y = y0 + (y1 - y0) * t;
            const double wob = p.wobbleAmp * std::sin(phase + freq * (travelled + len * t));
            x += nx * wob + gauss(rng) * p.jitterSigma;
            y += ny * wob + gauss(rng) * p.jitterSigma;
            // center, scale, rotate, translate
            const double cx = (x - 150.0) * sx, cy = (y - 150.0) * sy;
            const double rx = cx * std::cos(rot) - cy * std::sin(rot);
            const double ry = cx * std::sin(rot) + cy * std::cos(rot);
            StrokePoint pt;
            pt.x = qBound(0, static_cast<int>(std::lround(rx + tx)), p.surfaceW - 1);
            pt.y = qBound(0, static_cast<int>(std::lround(ry + ty)), p.surfaceH - 1);
            noisy.push_back(pt);
          }
          travelled += len;
        }
        s.strokes.push_back(noisy);
      }
      out.push_back(s);
    }
  }
  return out;
}

inline QVector<Sample> generate(int perTemplate, unsigned seed = 1234, const Params& p = {}) {
  return generateFrom(templates(), perTemplate, seed, p);
}

}  // namespace synthetic
