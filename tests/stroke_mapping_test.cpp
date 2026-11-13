#include "HoldKey.h"
#include "StrokeMapping.h"

#include <QCoreApplication>
#include <QTextStream>
#include <cmath>

static int failures = 0;

static void expect(bool cond, const char* msg) {
  QTextStream out(stdout);
  if (cond) {
    out << "OK  " << msg << "\n";
  } else {
    out << "FAIL " << msg << "\n";
    ++failures;
  }
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace stroke_mapping;

  // Pad -> surface: corners map to corners, no cropping.
  {
    const StrokePoint tl = fromNormalizedTrackpad(QPointF(0.0, 0.0), 480, 300);
    const StrokePoint br = fromNormalizedTrackpad(QPointF(1.0, 1.0), 480, 300);
    const StrokePoint c = fromNormalizedTrackpad(QPointF(0.5, 0.5), 480, 300);
    expect(tl.x == 0 && tl.y == 0, "pad top-left -> surface origin");
    expect(br.x == 479 && br.y == 299, "pad bottom-right -> surface corner");
    expect(std::abs(c.x - 240) <= 2 && std::abs(c.y - 150) <= 2, "pad center -> surface center");
    expect(surfaceWidthFor(300, 1.6) == 480, "surface width follows aspect");
  }

  // fitToCanvas: ink in a corner at small size is centered and enlarged.
  {
    QVector<Stroke> in{{{400, 20}, {440, 20}}, {{420, 20}, {420, 60}}};  // 40x40 box, far right/top
    const QVector<Stroke> out = fitToCanvas(in, 300, 300, 24, 100.0);
    int minX = 999, maxX = -1, minY = 999, maxY = -1;
    for (const Stroke& s : out)
      for (const StrokePoint& p : s) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
      }
    expect(minX == 24 && maxX == 276 && minY == 24 && maxY == 276,
           "small corner ink scales to fill canvas minus margin");
    expect(out[1][0].x == 150, "stroke geometry preserved (vertical bar stays centered)");
  }
  // Non-square ink keeps aspect and centers.
  {
    QVector<Stroke> in{{{0, 100}, {200, 100}}};  // horizontal line, zero height
    const QVector<Stroke> out = fitToCanvas(in, 300, 300);
    expect(out[0][0].y == 150 && out[0][1].y == 150, "flat line is vertically centered");
    expect(out[0][0].x == 24 && out[0][1].x == 276, "flat line spans the margin box");
  }
  // Scale cap: a dot is not blown up.
  {
    QVector<Stroke> in{{{10, 10}, {12, 12}}};
    const QVector<Stroke> out = fitToCanvas(in, 300, 300, 24, 4.0);
    expect(std::abs(out[0][1].x - out[0][0].x) <= 8, "tiny ink respects max scale");
  }
  expect(fitToCanvas({}, 300, 300).isEmpty(), "empty input stays empty");

  // Jump detection.
  {
    StrokePoint a{10, 10};
    StrokePoint nearPt{20, 15};
    StrokePoint farPt{100, 10};
    expect(!isHoldJump(a, nearPt, 40), "small move is not a jump");
    expect(isHoldJump(a, farPt, 40), "large move is a jump");
  }

  // Hold key: layout-independent matching.
  {
    expect(hold_key::isHoldToWriteKey(Qt::Key_QuoteLeft, 0), "US backtick is hold key");
    expect(hold_key::isHoldToWriteKey(Qt::Key_twosuperior, 0), "AZERTY ² is hold key");
    expect(hold_key::isHoldToWriteKey(Qt::Key_F12, 0), "F12 is a second hold key");
    expect(!hold_key::isHoldToWriteKey(Qt::Key_F11, 0), "F11 is not a hold key");
    expect(!hold_key::isHoldToWriteKey(Qt::Key_Space, 0), "Space is not hold key");
    expect(!hold_key::isHoldToWriteKey(Qt::Key_A, 0), "letters are not hold key");
#ifdef Q_OS_MACOS
    expect(hold_key::isHoldToWriteKey(Qt::Key_twosuperior, hold_key::kMacGraveVirtualKey),
           "mac grave key code matches regardless of layout character");
    expect(hold_key::isHoldToWriteKey(Qt::Key_F12, hold_key::kMacF12VirtualKey), "mac F12 key code matches");
    expect(!hold_key::isHoldToWriteKey(Qt::Key_QuoteLeft, 0x31),
           "mac key code wins over character when present");
#elif defined(Q_OS_WIN)
    expect(hold_key::isHoldToWriteKey(Qt::Key_A, 0, hold_key::kWinGraveScanCode),
           "win scan 0x29 matches regardless of layout character");
    expect(hold_key::isHoldToWriteKey(Qt::Key_A, 0, hold_key::kWinF12ScanCode), "win scan 0x58 (F12) matches");
    expect(!hold_key::isHoldToWriteKey(Qt::Key_QuoteLeft, 0, 0x1E),
           "win scan code wins over character when present");
#elif defined(Q_OS_LINUX)
    expect(hold_key::isHoldToWriteKey(Qt::Key_A, 0, hold_key::kX11GraveKeycode),
           "X11 keycode 49 matches regardless of layout character");
    expect(hold_key::isHoldToWriteKey(Qt::Key_A, 0, hold_key::kX11F12Keycode), "X11 keycode 96 (F12) matches");
    expect(!hold_key::isHoldToWriteKey(Qt::Key_QuoteLeft, 0, 30),
           "X11 keycode wins over character when present");
#endif
  }

  QTextStream out(stdout);
  out << (failures == 0 ? "All stroke_mapping tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
