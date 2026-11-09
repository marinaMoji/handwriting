#pragma once

#include "SyntheticCorpus.h"

#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <random>

// Loads clean stroke templates (correct stroke order) from a KanjiVG XML file
// (https://kanjivg.tagaini.net, CC BY-SA 3.0: fetched on demand, never committed) so the
// synthetic corpus can cover thousands of characters instead of 15 hand-made ones.
// Only plain code points are used (variant ids such as "kvg:kanji_0f9a8-Kaisho" are skipped).

namespace kanjivg {

// Flatten one SVG path "d" attribute (M C S L H V Z, absolute and relative) to polylines,
// scaled from KanjiVG's 109x109 box to 300x300. Returns one polyline (a path is one stroke;
// extra subpaths are appended to the same stroke).
inline Stroke flattenPath(const QString& d, int curveSegments = 10) {
  static const QRegularExpression token(
      QStringLiteral("[A-Za-z]|[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?"));
  QStringList tok;
  for (auto it = token.globalMatch(d); it.hasNext();) tok << it.next().captured(0);

  constexpr double kScale = 300.0 / 109.0;
  Stroke out;
  double cx = 0, cy = 0, sx = 0, sy = 0;  // current point, subpath start
  double lastC2x = 0, lastC2y = 0;        // previous cubic's 2nd control point (for S)
  bool prevCubic = false;
  QChar cmd;
  int i = 0;
  auto isCmd = [&](int k) { return k < tok.size() && tok[k][0].isLetter(); };
  auto num = [&](double* v) {
    if (i >= tok.size() || isCmd(i)) return false;
    *v = tok[i++].toDouble();
    return true;
  };
  auto add = [&](double x, double y) {
    StrokePoint p;
    p.x = static_cast<int>(std::lround(x * kScale));
    p.y = static_cast<int>(std::lround(y * kScale));
    out.push_back(p);
  };
  auto cubic = [&](double x1, double y1, double x2, double y2, double x, double y) {
    const double x0 = cx, y0 = cy;
    for (int k = 1; k <= curveSegments; ++k) {
      const double t = static_cast<double>(k) / curveSegments, u = 1 - t;
      add(u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x,
          u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y);
    }
    lastC2x = x2; lastC2y = y2; prevCubic = true;
    cx = x; cy = y;
  };

  while (i < tok.size()) {
    if (isCmd(i)) cmd = tok[i++][0];
    else if (cmd.isNull()) return {};
    const bool rel = cmd.isLower();
    const QChar c = cmd.toUpper();
    double a[6];
    if (c == QLatin1Char('Z')) {
      cx = sx; cy = sy; prevCubic = false;
      continue;
    }
    if (c == QLatin1Char('M')) {
      if (!num(&a[0]) || !num(&a[1])) break;
      cx = rel ? cx + a[0] : a[0];
      cy = rel ? cy + a[1] : a[1];
      sx = cx; sy = cy;
      add(cx, cy);
      cmd = rel ? QLatin1Char('l') : QLatin1Char('L');  // implicit lineto after moveto
      prevCubic = false;
    } else if (c == QLatin1Char('L')) {
      if (!num(&a[0]) || !num(&a[1])) break;
      cx = rel ? cx + a[0] : a[0];
      cy = rel ? cy + a[1] : a[1];
      add(cx, cy);
      prevCubic = false;
    } else if (c == QLatin1Char('H')) {
      if (!num(&a[0])) break;
      cx = rel ? cx + a[0] : a[0];
      add(cx, cy);
      prevCubic = false;
    } else if (c == QLatin1Char('V')) {
      if (!num(&a[0])) break;
      cy = rel ? cy + a[0] : a[0];
      add(cx, cy);
      prevCubic = false;
    } else if (c == QLatin1Char('C')) {
      bool ok = true;
      for (int k = 0; k < 6 && ok; ++k) ok = num(&a[k]);
      if (!ok) break;
      const double ox = rel ? cx : 0, oy = rel ? cy : 0;
      cubic(a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy, a[4] + ox, a[5] + oy);
    } else if (c == QLatin1Char('S')) {
      bool ok = true;
      for (int k = 0; k < 4 && ok; ++k) ok = num(&a[k]);
      if (!ok) break;
      const double ox = rel ? cx : 0, oy = rel ? cy : 0;
      const double x1 = prevCubic ? 2 * cx - lastC2x : cx;
      const double y1 = prevCubic ? 2 * cy - lastC2y : cy;
      cubic(x1, y1, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy);
    } else {
      break;  // arcs / quadratics are not used by KanjiVG stroke paths
    }
  }
  return out;
}

inline bool wanted(uint cp) {
  return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3041 && cp <= 0x3096) ||
         (cp >= 0x30A1 && cp <= 0x30FA);
}

// All templates in the file, in code point order. Optionally keep only `onlyChars`.
inline QVector<synthetic::Template> loadTemplates(const QString& path, QString* error,
                                                  const QString& onlyChars = QString()) {
  QVector<synthetic::Template> out;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = QStringLiteral("cannot open %1").arg(path);
    return out;
  }
  QVector<uint> only;
  for (const QChar* p = onlyChars.constData(); p < onlyChars.constData() + onlyChars.size(); ++p) {
    if (p->isHighSurrogate() && p + 1 < onlyChars.constData() + onlyChars.size()) {
      only << QChar::surrogateToUcs4(*p, *(p + 1));
      ++p;
    } else {
      only << p->unicode();
    }
  }

  QXmlStreamReader xml(&f);
  uint cp = 0;
  bool in = false;
  QVector<Stroke> strokes;
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement()) {
      if (xml.name() == QLatin1String("kanji")) {
        const QString id = xml.attributes().value(QStringLiteral("id")).toString();
        static const QRegularExpression re(QStringLiteral("^kvg:kanji_([0-9a-f]{5})$"));
        const auto m = re.match(id);
        in = false;
        if (m.hasMatch()) {
          cp = m.captured(1).toUInt(nullptr, 16);
          in = wanted(cp) && (only.isEmpty() || only.contains(cp));
          strokes.clear();
        }
      } else if (in && xml.name() == QLatin1String("path")) {
        const Stroke s = flattenPath(xml.attributes().value(QStringLiteral("d")).toString());
        if (s.size() >= 2) strokes.push_back(s);
      }
    } else if (xml.isEndElement() && xml.name() == QLatin1String("kanji") && in) {
      if (!strokes.isEmpty()) {
        synthetic::Template t;
        t.character = QString::fromUcs4(&cp, 1);
        t.strokes = strokes;
        out.push_back(t);
      }
      in = false;
    }
  }
  if (xml.hasError() && error) *error = xml.errorString();
  return out;
}

// Seeded, deterministic subsample of at most `maxChars` templates (0 = all).
inline QVector<synthetic::Template> subsample(QVector<synthetic::Template> all, int maxChars,
                                              unsigned seed) {
  if (maxChars <= 0 || all.size() <= maxChars) return all;
  std::mt19937 rng(seed);
  std::shuffle(all.begin(), all.end(), rng);
  all.resize(maxChars);
  std::sort(all.begin(), all.end(), [](const synthetic::Template& a, const synthetic::Template& b) {
    return a.character < b.character;
  });
  return all;
}

}  // namespace kanjivg
