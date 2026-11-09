#include "Fusion.h"

#include <QCoreApplication>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

static Candidate C(const char* ch, float score, const char* src, int rank) {
  Candidate c;
  c.character = QString::fromUtf8(ch);
  c.score = score;
  c.modelSource = QString::fromLatin1(src);
  c.modelRank = rank;
  return c;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QVector<Candidate> ja = {C("A", 0.9f, "JA", 1), C("B", 0.5f, "JA", 2), C("C", 0.1f, "JA", 3)};
  const QVector<Candidate> tw = {C("D", 1.5f, "TW", 1), C("B", 1.0f, "TW", 2), C("E", 0.2f, "TW", 3)};

  auto order = [](const QVector<Candidate>& v) {
    QString s;
    for (const Candidate& c : v) s += c.character;
    return s;
  };

  expect(order(fusion::fuse(ja, tw, fusion::Method::Interleave, 10)) == QStringLiteral("ADBCE"),
         "interleave alternates and dedupes");
  expect(order(fusion::fuse(ja, tw, fusion::Method::Interleave, 3)) == QStringLiteral("ADB"), "topN respected");
  expect(order(fusion::fuse(ja, tw, fusion::Method::MaxScore, 10)) == QStringLiteral("DBAEC"),
         "max orders by raw score");
  {
    const auto r = fusion::fuse(ja, tw, fusion::Method::Rrf, 10);
    expect(order(r).startsWith(QStringLiteral("B")), "rrf boosts a character both models propose");
    expect(r[0].modelSource == QStringLiteral("JA+TW"), "agreement reported as JA+TW");
  }
  {
    fusion::Params p;
    p.jaBias = 10.0;
    expect(order(fusion::fuse(ja, tw, fusion::Method::Calibrated, 10, p)).startsWith(QStringLiteral("AB")),
           "bias pushes a model's candidates up");
  }
  expect(order(fusion::fuse({}, tw, fusion::Method::ZScore, 10)) == QStringLiteral("DBE"), "one empty list works");
  expect(fusion::fuse({}, {}, fusion::Method::Rrf, 10).isEmpty(), "both empty gives nothing");
  fusion::Method m;
  expect(fusion::parse(QStringLiteral("rrf"), &m) && m == fusion::Method::Rrf && !fusion::parse(QStringLiteral("x"), &m),
         "method names parse");

  QTextStream(stdout) << (failures == 0 ? "All fusion tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
