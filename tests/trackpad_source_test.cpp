#include "TrackpadSource.h"

#include <QCoreApplication>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream out(stdout);
  out << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

class FakeSource : public TrackpadSource {
 public:
  QString name() const override { return QStringLiteral("fake"); }
  void feed(const QVector<TrackpadContact>& c) { processContacts(c); }
  void aspect(qreal a) { reportPadAspect(a); }
};

static TrackpadContact C(int id, double x, double y) { return {id, QPointF(x, y)}; }

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  FakeSource s;
  int began = 0, moved = 0, ended = 0, first = 0;
  QObject::connect(&s, &TrackpadSource::strokeBegan, [&](QPointF) { ++began; });
  QObject::connect(&s, &TrackpadSource::strokeMoved, [&](QPointF) { ++moved; });
  QObject::connect(&s, &TrackpadSource::strokeEnded, [&] { ++ended; });
  QObject::connect(&s, &TrackpadSource::firstContact, [&] { ++first; });

  int aspects = 0;
  qreal lastAspect = 0;
  QObject::connect(&s, &TrackpadSource::padAspectChanged, [&](qreal a) { ++aspects; lastAspect = a; });
  s.aspect(1.6);
  s.aspect(1.6);
  s.aspect(0.0);
  s.aspect(100.0);
  expect(aspects == 1 && qAbs(lastAspect - 1.6) < 1e-9, "pad aspect reported once; invalid/duplicate ignored");
  s.aspect(1.4);
  expect(aspects == 2 && qAbs(lastAspect - 1.4) < 1e-9, "changed pad aspect is reported");

  s.feed({C(1, 0.5, 0.5)});
  expect(began == 0, "inactive source ignores contacts");

  s.setActive(true);
  s.feed({C(1, 0.1, 0.1)});
  s.feed({C(1, 0.2, 0.2)});
  expect(began == 1 && moved == 1 && first == 1, "begin, then move; firstContact once");
  expect(s.hasReceivedContact(), "contact flag set");
  s.feed({});
  expect(ended == 1, "lift ends stroke");

  // Second finger appears: tracked finger is kept.
  s.feed({C(1, 0.1, 0.1)});
  s.feed({C(1, 0.15, 0.1), C(2, 0.9, 0.9)});
  expect(began == 2 && moved == 2 && ended == 1, "extra finger does not hijack stroke");

  // Tracked finger lifts, palm remains: stroke ends, palm ignored until all lift.
  s.feed({C(2, 0.9, 0.9)});
  expect(ended == 2, "tracked finger lift ends stroke");
  s.feed({C(2, 0.9, 0.8)});
  s.feed({C(3, 0.4, 0.4), C(2, 0.9, 0.8)});
  expect(began == 2, "no new stroke while palm still down");
  s.feed({});
  s.feed({C(4, 0.3, 0.3)});
  expect(began == 3, "new stroke after full lift");

  // Missed empty frame: same id teleports — treat as end + new stroke.
  s.feed({C(4, 0.35, 0.3)});
  expect(moved >= 1, "small move stays on same stroke");
  const int beganBeforeJump = began;
  const int endedBeforeJump = ended;
  s.feed({C(4, 0.9, 0.9)});
  expect(began == beganBeforeJump + 1 && ended == endedBeforeJump + 1,
         "large jump with same id starts a new stroke");

  // Deactivating mid-stroke ends it.
  s.setActive(false);
  expect(ended == endedBeforeJump + 2, "deactivate ends in-progress stroke");
  expect(!s.hasReceivedContact(), "contact flag reset on deactivate");

  QTextStream(stdout) << (failures == 0 ? "All trackpad_source tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
