#include "StrokePreprocess.h"

#include <QCoreApplication>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace stroke_preprocess;

  // dedupe
  {
    Stroke s{{1, 1}, {1, 1}, {2, 2}, {2, 2}, {2, 2}, {3, 3}};
    expect(dedupe(s).size() == 3, "dedupe drops consecutive duplicates");
  }

  // RDP: collinear points collapse to the endpoints.
  {
    Stroke line;
    for (int i = 0; i <= 50; ++i) line.push_back({i * 2, i * 2});
    const Stroke r = simplifyRdp(line, 1.0);
    expect(r.size() == 2 && r.first().x == 0 && r.last().x == 100, "RDP collapses a straight line");
  }
  // RDP: a real corner is preserved.
  {
    Stroke l;
    for (int i = 0; i <= 20; ++i) l.push_back({i * 5, 0});
    for (int i = 1; i <= 20; ++i) l.push_back({100, i * 5});
    const Stroke r = simplifyRdp(l, 3.0);
    bool corner = false;
    for (const StrokePoint& p : r) corner |= (p.x == 100 && p.y == 0);
    expect(r.size() == 3 && corner, "RDP keeps the corner of an L");
  }
  // RDP: small jitter is removed, large deviation is kept.
  {
    Stroke j{{0, 0}, {10, 1}, {20, -1}, {30, 1}, {40, 0}};
    expect(simplifyRdp(j, 2.0).size() == 2, "RDP removes jitter under epsilon");
    Stroke bump{{0, 0}, {20, 15}, {40, 0}};
    expect(simplifyRdp(bump, 2.0).size() == 3, "RDP keeps deviation over epsilon");
  }
  // RDP never changes endpoints and never leaves fewer than endpoints.
  {
    Stroke s{{5, 5}, {80, 90}};
    const Stroke r2 = simplifyRdp(s, 50.0);
    expect(r2.size() == 2 && r2[0].x == 5 && r2[1].y == 90, "two-point stroke is unchanged");
    Stroke dot{{7, 7}};
    expect(simplifyRdp(dot, 5.0).size() == 1, "single point stroke survives");
  }
  // smoothing keeps endpoints and reduces zigzag amplitude.
  {
    Stroke z{{0, 0}, {10, 10}, {20, 0}, {30, 10}, {40, 0}};
    const Stroke m = smooth(z, 1);
    expect(m.first().x == 0 && m.last().x == 40 && m.first().y == 0 && m.last().y == 0,
           "smooth keeps endpoints");
    expect(m[1].y < 10 && m[2].y > 0, "smooth reduces zigzag amplitude");
  }

  // resample: equidistant, endpoints kept, speed-independent.
  {
    Stroke sparse{{0, 0}, {100, 0}};                       // 2 points, 100 px apart
    Stroke dense;
    for (int i = 0; i <= 100; ++i) dense.push_back({i, 0});  // 101 points, 1 px apart
    const Stroke a = resample(sparse, 10.0);
    const Stroke b = resample(dense, 10.0);
    expect(a.size() == 11 && b.size() == 11, "resample gives same point count regardless of input density");
    bool same = a.size() == b.size();
    for (int i = 0; same && i < a.size(); ++i) same = a[i].x == b[i].x && a[i].y == b[i].y;
    expect(same, "resample output independent of input sampling");
    expect(a.first().x == 0 && a.last().x == 100, "resample keeps endpoints");
    Stroke L{{0, 0}, {50, 0}, {50, 50}};
    const Stroke rl = resample(L, 7.0);
    bool corner = false;
    for (int i = 1; i + 1 < rl.size(); ++i) corner |= (rl[i].x == 50 && rl[i].y <= 10);
    expect(rl.last().x == 50 && rl.last().y == 50 && !rl.isEmpty(), "resample keeps final point of an L");
    Stroke dot{{9, 9}};
    expect(resample(dot, 5.0).size() == 1, "resample leaves a dot alone");
    Stroke tiny{{0, 0}, {2, 0}};
    expect(resample(tiny, 10.0).size() == 2, "stroke shorter than spacing keeps both endpoints");
    Q_UNUSED(corner);
  }
  // Smoothing after resampling rounds corners by about the same amount at any speed.
  {
    Stroke fast{{0, 0}, {60, 0}, {60, 60}};
    Stroke slow;
    for (int i = 0; i <= 60; ++i) slow.push_back({i, 0});
    for (int i = 1; i <= 60; ++i) slow.push_back({60, i});
    Options o;
    parseSpec("resample:3+smooth:3", &o);
    const QVector<Stroke> f = apply({fast}, o), sl = apply({slow}, o);
    auto cornerGap = [](const Stroke& st) {
      double best = 1e9;
      for (const StrokePoint& p : st) best = std::min(best, std::hypot(p.x - 60.0, p.y - 0.0));
      return best;
    };
    expect(std::fabs(cornerGap(f[0]) - cornerGap(sl[0])) < 2.0,
           "corner rounding is the same for sparse and dense sampling");
  }

  // spec parsing
  {
    Options o;
    expect(parseSpec("none", &o) && o.isNone(), "spec: none");
    expect(parseSpec("", &o) && o.isNone(), "spec: empty is none");
    expect(parseSpec("smooth:2+rdp:3", &o) && o.steps.size() == 2 &&
               o.steps[0].kind == Step::Kind::Smooth && o.steps[1].kind == Step::Kind::Rdp &&
               o.steps[1].value == 3.0,
           "spec: smooth then rdp, in order");
    QString err;
    expect(parseSpec("resample:3+smooth:3", &o) && o.steps.size() == 2 &&
               o.steps[0].kind == Step::Kind::Resample,
           "spec: resample step");
    expect(!parseSpec("blur:3", &o, &err) && !err.isEmpty(), "spec: unknown step rejected");
    expect(!parseSpec("rdp", &o), "spec: missing value rejected");
    expect(!parseSpec("rdp:-1", &o), "spec: negative value rejected");
  }

  // apply never drops or empties a stroke.
  {
    Options o;
    parseSpec("smooth:3+rdp:50", &o);
    QVector<Stroke> in{{{1, 1}}, {{0, 0}, {5, 5}, {10, 10}}};
    const QVector<Stroke> out = apply(in, o);
    expect(out.size() == 2 && !out[0].isEmpty() && !out[1].isEmpty(), "apply keeps every stroke non-empty");
  }

  QTextStream(stdout) << (failures == 0 ? "All stroke_preprocess tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
