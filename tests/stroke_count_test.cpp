#include "StrokeCount.h"

#include <QCoreApplication>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  expect(stroke_count::expected(QStringLiteral("一")) == 1, "一 has 1 stroke");
  expect(stroke_count::expected(QStringLiteral("十")) == 2, "十 has 2 strokes");
  expect(stroke_count::expected(QStringLiteral("大")) == 3, "大 has 3 strokes");
  expect(stroke_count::expected(QStringLiteral("日")) == 4, "日 has 4 strokes");
  expect(stroke_count::expected(QStringLiteral("國")) == 11, "國 has 11 strokes");
  expect(stroke_count::expected(QStringLiteral("龍")) == 16, "龍 has 16 strokes");
  expect(stroke_count::expected(QStringLiteral("𠮷")) == 0, "non-BMP 𠮷 is not in the models, so unknown (0)");
  expect(stroke_count::expected(QStringLiteral("あ")) == 0, "kana is unknown (0)");
  expect(stroke_count::expected(QStringLiteral("4")) == 0, "ASCII is unknown (0)");
  expect(stroke_count::expected(QString()) == 0, "empty string is unknown (0)");
  expect(stroke_count::expected(0x10FFFF) == 0, "out of table is unknown (0)");
  QTextStream(stdout) << (failures == 0 ? "All stroke_count tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
