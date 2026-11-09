#pragma once

#include "ZinniaBackend.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>

// Merging the Japanese and Traditional Chinese candidate lists in "Both" mode.
// The two Zinnia models produce uncalibrated scores, so several strategies are
// provided and measured against each other in handwriting_eval.
//
//   Interleave  JA#1, TW#1, JA#2, TW#2 ... (the original behaviour; ignores scores)
//   Rrf         reciprocal rank fusion: sum of 1/(k+rank); characters both models
//               propose get a boost
//   MaxScore    raw score, highest first
//   ZScore      scores standardised per list, then highest first
//   Calibrated  raw score plus a fixed per-model offset (jaBias / twBias)
//
// A character proposed by both models is reported once with source "JA+TW".

namespace fusion {

enum class Method { Interleave, Rrf, MaxScore, ZScore, Calibrated };

struct Params {
  double rrfK = 10.0;
  double jaBias = 0.0;
  double twBias = 0.0;
};

inline QString name(Method m) {
  switch (m) {
    case Method::Interleave: return QStringLiteral("interleave");
    case Method::Rrf: return QStringLiteral("rrf");
    case Method::MaxScore: return QStringLiteral("max");
    case Method::ZScore: return QStringLiteral("z");
    case Method::Calibrated: return QStringLiteral("cal");
  }
  return QStringLiteral("interleave");
}

inline bool parse(const QString& s, Method* out) {
  for (Method m : {Method::Interleave, Method::Rrf, Method::MaxScore, Method::ZScore,
                   Method::Calibrated}) {
    if (name(m) == s) {
      *out = m;
      return true;
    }
  }
  return false;
}

namespace detail {

struct Entry {
  Candidate best;      // the stronger of the proposals, used for display
  double fused = 0.0;  // the value we sort by
  bool fromJa = false, fromTw = false;
  int firstSeen = 0;  // tie-break: earlier in (JA then TW) order wins
};

inline void meanStd(const QVector<Candidate>& l, double* mean, double* sd) {
  *mean = 0.0;
  *sd = 1.0;
  if (l.isEmpty()) return;
  double s = 0.0;
  for (const Candidate& c : l) s += c.score;
  *mean = s / l.size();
  double v = 0.0;
  for (const Candidate& c : l) v += (c.score - *mean) * (c.score - *mean);
  const double d = std::sqrt(v / l.size());
  *sd = d > 1e-9 ? d : 1.0;
}

}  // namespace detail

inline QVector<Candidate> fuse(const QVector<Candidate>& ja, const QVector<Candidate>& tw,
                               Method method, int topN, const Params& params = {}) {
  QVector<Candidate> out;
  if (method == Method::Interleave) {
    QHash<QString, bool> seen;
    const int maxRank = qMax(ja.size(), tw.size());
    for (int i = 0; i < maxRank && out.size() < topN; ++i) {
      for (const QVector<Candidate>* l : {&ja, &tw}) {
        if (i < l->size() && out.size() < topN && !seen.contains((*l)[i].character)) {
          seen.insert((*l)[i].character, true);
          out.push_back((*l)[i]);
        }
      }
    }
    return out;
  }

  double jaMean = 0, jaSd = 1, twMean = 0, twSd = 1;
  detail::meanStd(ja, &jaMean, &jaSd);
  detail::meanStd(tw, &twMean, &twSd);

  QHash<QString, detail::Entry> byChar;
  int order = 0;
  auto add = [&](const QVector<Candidate>& list, bool isJa) {
    for (int i = 0; i < list.size(); ++i) {
      const Candidate& c = list[i];
      const int rank = c.modelRank > 0 ? c.modelRank : i + 1;
      double value = 0.0;
      switch (method) {
        case Method::Rrf: value = 1.0 / (params.rrfK + rank); break;
        case Method::MaxScore: value = c.score; break;
        case Method::ZScore:
          value = isJa ? (c.score - jaMean) / jaSd : (c.score - twMean) / twSd;
          break;
        case Method::Calibrated: value = c.score + (isJa ? params.jaBias : params.twBias); break;
        case Method::Interleave: break;
      }
      auto it = byChar.find(c.character);
      if (it == byChar.end()) {
        detail::Entry e;
        e.best = c;
        e.fused = value;
        e.fromJa = isJa;
        e.fromTw = !isJa;
        e.firstSeen = order++;
        byChar.insert(c.character, e);
      } else {
        detail::Entry& e = it.value();
        (isJa ? e.fromJa : e.fromTw) = true;
        if (method == Method::Rrf) {
          e.fused += value;  // agreement adds up
        } else if (value > e.fused) {
          e.fused = value;
          e.best = c;
        }
      }
    }
  };
  add(ja, true);
  add(tw, false);

  QVector<detail::Entry> entries = byChar.values().toVector();
  std::sort(entries.begin(), entries.end(), [](const detail::Entry& a, const detail::Entry& b) {
    if (a.fused != b.fused) return a.fused > b.fused;
    return a.firstSeen < b.firstSeen;
  });
  for (const detail::Entry& e : entries) {
    if (out.size() >= topN) break;
    Candidate c = e.best;
    if (e.fromJa && e.fromTw) c.modelSource = QStringLiteral("JA+TW");
    out.push_back(c);
  }
  return out;
}

}  // namespace fusion
