#include "KanjiVgCorpus.h"

#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // Absolute and relative lines, implicit lineto after M, H/V.
  {
    const Stroke s = kanjivg::flattenPath(QStringLiteral("M10,10 L20,10 h10 v10"));
    expect(s.size() == 4, "M L h v yield four points");
    expect(s.first().x == qRound(10 * 300.0 / 109) && s.last().y == qRound(20 * 300.0 / 109), "scaled to 300 box");
  }
  // Cubic is flattened to many points and ends at its endpoint.
  {
    const Stroke s = kanjivg::flattenPath(QStringLiteral("M0,0c10,0,20,10,20,20"));
    expect(s.size() == 11, "cubic flattened to 10 segments");
    expect(s.last().x == qRound(20 * 300.0 / 109) && s.last().y == qRound(20 * 300.0 / 109), "cubic ends at its endpoint");
  }
  // Smooth cubic reflects the previous control point; numbers glued by signs parse.
  {
    const Stroke s = kanjivg::flattenPath(QStringLiteral("M10,10c5,0,10,5,10,10s-5,10-10,10"));
    expect(s.size() == 1 + 10 + 10, "S after c adds a second cubic");
    expect(s.last().x == qRound(10 * 300.0 / 109) && s.last().y == qRound(30 * 300.0 / 109), "glued numbers parse");
  }
  // Garbage does not crash.
  expect(kanjivg::flattenPath(QStringLiteral("")).isEmpty() && kanjivg::flattenPath(QStringLiteral("1,1")).isEmpty(),
         "empty and command-less paths give nothing");

  // File loading: keeps plain code points in the wanted ranges, skips variants.
  {
    QTemporaryFile f;
    f.open();
    f.write(R"(<?xml version="1.0"?>
<kanjivg xmlns:kvg='http://kanjivg.tagaini.net'>
<kanji id="kvg:kanji_00031"><g><path d="M1,1L2,2"/></g></kanji>
<kanji id="kvg:kanji_04e00"><g><path d="M10,54L99,54"/></g></kanji>
<kanji id="kvg:kanji_04e8c"><g><path d="M20,30L90,30"/><path d="M10,80L100,80"/></g></kanji>
<kanji id="kvg:kanji_04e8c-Kaisho"><g><path d="M1,1L2,2"/></g></kanji>
</kanjivg>)");
    f.flush();
    QString err;
    const auto all = kanjivg::loadTemplates(f.fileName(), &err);
    expect(all.size() == 2, "digit and variant ids are skipped");
    expect(all.size() == 2 && all[0].character == QStringLiteral("一") && all[1].strokes.size() == 2,
           "kanji loaded with stroke order");
    const auto only = kanjivg::loadTemplates(f.fileName(), &err, QStringLiteral("二"));
    expect(only.size() == 1 && only[0].character == QStringLiteral("二"), "--kvg-chars style filter");
    const auto sub = kanjivg::subsample(all, 1, 7);
    expect(sub.size() == 1, "subsample limits the count");
    expect(kanjivg::subsample(all, 1, 7)[0].character == sub[0].character, "subsample is deterministic");
    const auto samples = synthetic::generateFrom(all, 3, 99);
    expect(samples.size() == 6, "generateFrom makes N samples per template");
  }

  QTextStream(stdout) << (failures == 0 ? "All kanjivg tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
