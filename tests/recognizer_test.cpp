#include "Recognizer.h"
#include "StrokeMapping.h"

#include <QCoreApplication>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

static Stroke line(int x0, int y0, int x1, int y1, int n = 20) {
  Stroke s;
  for (int i = 0; i <= n; ++i) s.push_back({x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n});
  return s;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // A small cross drawn off-center on a wide, pad-shaped surface.
  const QVector<Stroke> raw = {line(300, 50, 300, 150), line(250, 100, 350, 100)};

  // Default options fit to the 300x300 canvas.
  {
    const Recognizer::Options o = Recognizer::defaultOptions();
    const Recognizer::Prepared p = Recognizer::prepare(raw, 480, 300, o);
    expect(p.width == 300 && p.height == 300, "fit uses the recognizer canvas size");
    expect(p.strokes.size() == 2, "stroke count preserved");
    int minX = 1 << 30, maxX = -(1 << 30);
    for (const Stroke& s : p.strokes) for (const StrokePoint& pt : s) { minX = qMin(minX, pt.x); maxX = qMax(maxX, pt.x); }
    expect(minX >= 0 && maxX <= 300, "fitted ink lies inside the canvas");
    expect(minX < 60 && maxX > 240, "small ink is scaled up toward the canvas");
  }

  // fit=false keeps coordinates and the surface size.
  {
    Recognizer::Options o;
    o.fit = false;
    const Recognizer::Prepared p = Recognizer::prepare(raw, 480, 300, o);
    expect(p.width == 480 && p.height == 300, "fit=false keeps the surface size");
    expect(p.strokes.size() == 2 && p.strokes[0].first().x == 300, "fit=false keeps coordinates");
  }

  // prepare() == fitToCanvas + preprocess, i.e. the documented pipeline.
  {
    Recognizer::Options o = Recognizer::defaultOptions();
    const Recognizer::Prepared p = Recognizer::prepare(raw, 480, 300, o);
    const QVector<Stroke> expected = stroke_preprocess::apply(
        stroke_mapping::fitToCanvas(raw, 300, 300, o.margin, o.maxScale), o.preprocess);
    bool same = expected.size() == p.strokes.size();
    for (int i = 0; same && i < expected.size(); ++i) {
      same = expected[i].size() == p.strokes[i].size();
      for (int j = 0; same && j < expected[i].size(); ++j)
        same = expected[i][j].x == p.strokes[i][j].x && expected[i][j].y == p.strokes[i][j].y;
    }
    expect(same, "prepare equals fit + preprocess");
  }

  // Spec parsing.
  {
    Recognizer::Options o;
    QString err;
    expect(Recognizer::optionsFromSpec(QStringLiteral("none"), &o, &err) && o.preprocess.isNone(),
           "spec 'none' disables preprocessing");
    expect(!Recognizer::optionsFromSpec(QStringLiteral("bogus:1"), &o, &err) && !err.isEmpty(),
           "bad spec is rejected with an error");
    expect(!Recognizer::defaultOptions().preprocess.isNone(), "default preprocessing is on");
  }

  // Empty input does not crash.
  {
    const Recognizer::Prepared p = Recognizer::prepare({}, 300, 300, Recognizer::defaultOptions());
    expect(p.strokes.isEmpty(), "empty input stays empty");
  }

  QTextStream(stdout) << (failures == 0 ? "All recognizer tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
