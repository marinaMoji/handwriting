#include "SampleIO.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir tmp;
  expect(tmp.isValid(), "temp dir");

  Sample s;
  s.character = QStringLiteral("大");
  s.strokes = {{{10, 100}, {200, 102}}, {{105, 20}, {60, 250}}, {{110, 120}, {190, 240}}};
  s.surfaceWidth = 480;
  s.surfaceHeight = 300;
  s.source = QStringLiteral("hold-to-write");
  s.note = QStringLiteral("test");

  QString path, err;
  expect(sample_io::saveSample(tmp.path() + "/nested", s, &path, &err), "save creates dirs and file");
  expect(QFile::exists(path) && path.contains("U+5927"), "file named by code point");

  Sample r;
  expect(sample_io::loadSample(path, &r, &err), "load round trip");
  expect(r.character == s.character && r.surfaceWidth == 480 && r.surfaceHeight == 300 &&
             r.source == s.source && r.note == s.note && !r.timestamp.isEmpty(),
         "metadata preserved");
  expect(r.strokes.size() == 3 && r.strokes[1].size() == 2 && r.strokes[1][1].y == 250 &&
             r.strokes[2][0].x == 110,
         "stroke points preserved exactly");

  // Supplementary-plane character (surrogate pair).
  Sample t = s;
  t.character = QStringLiteral("𠮷");
  QString p2;
  expect(sample_io::saveSample(tmp.path(), t, &p2, &err) && p2.contains("U+20BB7"),
         "non-BMP character saves with full code point");
  Sample r2;
  expect(sample_io::loadSample(p2, &r2, &err) && r2.character == t.character, "non-BMP round trip");

  expect(sample_io::findSamples(tmp.path()).size() == 2, "findSamples is recursive");

  // Rejections.
  Sample bad;
  expect(!sample_io::fromJson("not json", &bad, &err), "rejects invalid JSON");
  expect(!sample_io::fromJson("{\"strokes\":[[[1,2]]]}", &bad, &err), "rejects missing character");
  expect(!sample_io::fromJson("{\"character\":\"a\",\"strokes\":[]}", &bad, &err), "rejects no strokes");
  expect(!sample_io::fromJson("{\"character\":\"a\",\"strokes\":[[[1]]]}", &bad, &err), "rejects bad point");
  Sample empty;
  expect(!sample_io::saveSample(tmp.path(), empty, &p2, &err), "refuses to save an empty sample");

  QTextStream(stdout) << (failures == 0 ? "All sample_io tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
