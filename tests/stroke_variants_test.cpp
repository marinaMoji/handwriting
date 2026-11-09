#include "StrokeVariants.h"

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
  using namespace stroke_variants;

  const QVector<Stroke> three = {line(0, 0, 100, 0), line(0, 50, 100, 50), line(0, 100, 100, 100)};

  Options none;
  expect(generate(three, none).size() == 1 && generate(three, none)[0].size() == 3, "no options -> only the original");

  Options sw; sw.swap = true;
  {
    const auto v = generate(three, sw);
    expect(v.size() == 3, "swap gives n-1 extra variants");
    expect(v[1][0][0].y == 50 && v[1][1][0].y == 0, "swap exchanges adjacent strokes");
  }

  // merge: only pairs whose end is near the next start
  Options mg; mg.merge = true; mg.mergeGap = 15;
  {
    const QVector<Stroke> s = {line(0, 0, 50, 0), line(55, 0, 100, 0), line(0, 200, 50, 200)};
    const auto v = generate(s, mg);
    expect(v.size() == 2, "merge only joins nearby endpoints");
    expect(v[1].size() == 2 && v[1][0].size() == s[0].size() + s[1].size(), "merged stroke is the concatenation");
  }

  // split: an L-shaped stroke is cut at its corner; a straight line is not
  Options sp; sp.split = true;
  {
    Stroke L = line(0, 0, 100, 0);
    L += line(100, 5, 100, 100);
    const auto v = generate({L, line(0, 200, 100, 200)}, sp);
    expect(v.size() == 2 && v[1].size() == 3, "L stroke split into two strokes");
    const int cutX = v[1][0].last().x, cutY = v[1][0].last().y;
    expect(qAbs(cutX - 100) <= 12 && cutY <= 20, "cut lands near the corner");
    expect(generate({line(0, 0, 100, 0)}, sp).size() == 1, "straight stroke is not split");
  }

  // cap
  {
    Options all; all.swap = true; all.merge = true; all.split = true; all.maxVariants = 2;
    expect(generate(three, all).size() == 2, "maxVariants caps the list");
  }
  // degenerate input
  expect(generate({}, sw).size() == 1 && generate({Stroke{}}, sw).size() == 1, "empty input is safe");

  QTextStream(stdout) << (failures == 0 ? "All stroke_variants tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
