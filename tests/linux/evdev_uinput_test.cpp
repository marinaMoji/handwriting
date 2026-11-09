// Linux integration test (issue #4): drive EvdevTrackpadSource from a virtual
// type-B pad created with /dev/uinput. Skips cleanly when uinput is unavailable
// (typical unprivileged CI runners).

#include "UinputTouchpad.h"
#include "linux/EvdevTrackpadSource.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointF>
#include <QTextStream>
#include <QThread>
#include <QVector>

#include <cmath>
#include <functional>

#include <linux/input.h>

namespace {

int failures = 0;
void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) {
    ++failures;
  }
}

constexpr int kSkip = 77;  // ctest SKIP_RETURN_CODE

bool pumpUntil(QCoreApplication& app, const std::function<bool()>& done, int timeoutMs = 1000) {
  QElapsedTimer t;
  t.start();
  while (!done() && t.elapsed() < timeoutMs) {
    app.processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
  }
  return done();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  UinputTouchpad pad;
  const QString err = pad.create();
  if (!err.isEmpty()) {
    QTextStream(stdout) << "SKIP " << err << "\n";
    return kSkip;
  }
  QTextStream(stdout) << "uinput fixture at " << pad.eventPath() << "\n";
  qputenv("HANDWRITING_EVDEV_DEVICE", pad.eventPath().toLocal8Bit());
  qputenv("HANDWRITING_PAD_EDGE", "0");
  qunsetenv("HANDWRITING_EVDEV_GRAB");

  EvdevTrackpadSource src;
  expect(src.isAvailable(), "fixture touchpad is discovered");
  if (!src.isAvailable()) {
    return failures == 0 ? 0 : 1;
  }

  int began = 0;
  int moved = 0;
  int ended = 0;
  int problems = 0;
  QVector<QPointF> beganAt;
  QPointF lastMove;
  QObject::connect(&src, &TrackpadSource::strokeBegan, &app, [&](QPointF p) {
    ++began;
    beganAt.push_back(p);
    lastMove = p;
  });
  QObject::connect(&src, &TrackpadSource::strokeMoved, &app, [&](QPointF p) {
    ++moved;
    lastMove = p;
  });
  QObject::connect(&src, &TrackpadSource::strokeEnded, &app, [&] { ++ended; });
  QObject::connect(&src, &TrackpadSource::problem, &app, [&](const QString& m) {
    ++problems;
    QTextStream(stdout) << "  problem: " << m << "\n";
  });

  src.setActive(true);
  expect(src.isActive(), "backend activates on the fixture");

  // --- Basic stroke: down, move, up -----------------------------------------
  began = moved = ended = 0;
  beganAt.clear();
  pad.fingerDown(0, 10, 100, 100);
  expect(pumpUntil(app, [&] { return began >= 1; }), "strokeBegan after finger down");
  expect(began == 1 && !beganAt.isEmpty() && std::fabs(beganAt[0].x() - 0.1) < 0.02 &&
             std::fabs(beganAt[0].y() - 0.1) < 0.02,
         "began near normalized (0.1, 0.1)");

  pad.fingerMove(0, 400, 200);
  expect(pumpUntil(app, [&] { return moved >= 1; }), "strokeMoved after finger move");
  expect(std::fabs(lastMove.x() - 0.4) < 0.02 && std::fabs(lastMove.y() - 0.2) < 0.02,
         "move near normalized (0.4, 0.2)");

  pad.fingerUp(0);
  expect(pumpUntil(app, [&] { return ended >= 1; }), "strokeEnded after finger up");

  // --- Two-finger palm handoff ----------------------------------------------
  began = moved = ended = 0;
  beganAt.clear();
  pad.fingerDown(0, 20, 200, 200);  // writing finger
  expect(pumpUntil(app, [&] { return began >= 1; }), "palm test: writing finger begins");
  // Palm appears in another slot; writing finger must stay tracked.
  pad.setSlot(1);
  pad.emitEvent(EV_ABS, ABS_MT_TRACKING_ID, 21);
  pad.emitEvent(EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PALM);
  pad.emitEvent(EV_ABS, ABS_MT_POSITION_X, 800);
  pad.emitEvent(EV_ABS, ABS_MT_POSITION_Y, 800);
  pad.emitEvent(EV_ABS, ABS_MT_PRESSURE, 80);
  pad.syn();
  pad.fingerMove(0, 300, 250);
  expect(pumpUntil(app, [&] { return moved >= 1; }), "palm test: move still tracked on finger 0");
  expect(ended == 0, "palm test: palm does not end the stroke");

  // Writing finger lifts; palm remains → stroke ends, await clear pad.
  const int endedBefore = ended;
  pad.slotUp(0);
  expect(pumpUntil(app, [&] { return ended > endedBefore; }),
         "palm test: lifting tracked finger ends stroke while palm remains");
  const int beganBefore = began;
  pad.setSlot(1);
  pad.emitEvent(EV_ABS, ABS_MT_POSITION_X, 810);
  pad.syn();
  app.processEvents();
  QThread::msleep(30);
  app.processEvents();
  expect(began == beganBefore, "palm test: no new stroke while palm still down");

  pad.slotUp(1);
  pad.emitEvent(EV_KEY, BTN_TOUCH, 0);
  pad.emitEvent(EV_KEY, BTN_TOOL_FINGER, 0);
  pad.syn();
  app.processEvents();
  QThread::msleep(20);

  pad.fingerDown(0, 30, 150, 150);
  expect(pumpUntil(app, [&] { return began > beganBefore; }),
         "palm test: new stroke after full lift");
  pad.fingerUp(0);
  expect(pumpUntil(app, [&] { return ended >= endedBefore + 2; }), "palm test: second stroke ends");

  // --- SYN_DROPPED resync ---------------------------------------------------
  began = moved = ended = 0;
  beganAt.clear();
  pad.fingerDown(0, 40, 500, 500);
  expect(pumpUntil(app, [&] { return began >= 1; }), "drop test: stroke begins");
  // Inject a drop; following ABS until SYN_REPORT is discarded, then resync.
  pad.synDropped();
  pad.emitEvent(EV_ABS, ABS_MT_POSITION_X, 999);  // dropped packet noise
  pad.syn();                                      // end-of-drop → NeedsResync
  pad.fingerMove(0, 600, 500);
  expect(pumpUntil(app, [&] { return moved >= 1 || began >= 2; }, 1500),
         "drop test: contact stream recovers after SYN_DROPPED");
  pad.fingerUp(0);
  expect(pumpUntil(app, [&] { return ended >= 1; }), "drop test: stroke ends after recovery");

  // --- Device unplug + re-probe ---------------------------------------------
  began = moved = ended = problems = 0;
  pad.fingerDown(0, 50, 100, 100);
  expect(pumpUntil(app, [&] { return began >= 1; }), "unplug test: stroke begins");
  pad.destroy();  // removes the event node → read errors → deviceLost
  expect(pumpUntil(app, [&] { return problems >= 1 || ended >= 1; }, 2000),
         "unplug test: deviceLost reports a problem or ends the stroke");

  // Recreate and activate again (GUI would endHold/beginHold around this).
  src.setActive(false);
  const QString err2 = pad.create();
  expect(err2.isEmpty(), "unplug test: recreate uinput fixture");
  if (!err2.isEmpty()) {
    QTextStream(stdout) << "  recreate failed: " << err2 << "\n";
  } else {
    qputenv("HANDWRITING_EVDEV_DEVICE", pad.eventPath().toLocal8Bit());
    began = moved = ended = 0;
    src.setActive(true);
    expect(src.isActive(), "unplug test: backend reactivates on new fixture");
    pad.fingerDown(0, 60, 200, 200);
    expect(pumpUntil(app, [&] { return began >= 1; }), "unplug test: stroke works after re-probe");
    pad.fingerUp(0);
    expect(pumpUntil(app, [&] { return ended >= 1; }), "unplug test: stroke ends after re-probe");
  }

  src.setActive(false);
  pad.destroy();

  QTextStream(stdout) << (failures == 0 ? "All evdev_uinput tests passed.\n"
                                        : "Some evdev_uinput tests failed.\n");
  return failures == 0 ? 0 : 1;
}
