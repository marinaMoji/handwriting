#include "PtpFrameAssembler.h"

#include <QCoreApplication>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

static PtpSlot S(bool down, int id, double x = 0.5, double y = 0.5) {
  PtpSlot s;
  s.tipDown = down;
  s.id = id;
  s.x = x;
  s.y = y;
  return s;
}

static PtpReport R(int count, QVector<PtpSlot> fingers) {
  PtpReport r;
  r.haveCount = true;
  r.contactCount = count;
  r.fingers = std::move(fingers);
  return r;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  PtpFrameAssembler a;
  QVector<TrackpadContact> f;

  // Touch down.
  expect(a.feed(R(1, {S(true, 3), S(false, 0), S(false, 0), S(false, 0), S(false, 0)}), &f) &&
             f.size() == 1 && f[0].identifier == 3,
         "single finger down -> one contact");

  // THE lift case: count still 1, tip switch 0 -> frame is ready and empty.
  expect(a.feed(R(1, {S(false, 3), S(false, 0), S(false, 0), S(false, 0), S(false, 0)}), &f) &&
             f.isEmpty(),
         "lift report (count 1, tip up) flushes an empty frame");

  // Two fingers, then one lifts: the lifted one is reported with tip up, the other stays.
  expect(a.feed(R(2, {S(true, 1), S(true, 2), S(false, 0), S(false, 0), S(false, 0)}), &f) &&
             f.size() == 2,
         "two fingers -> two contacts");
  expect(a.feed(R(2, {S(false, 1), S(true, 2), S(false, 0), S(false, 0), S(false, 0)}), &f) &&
             f.size() == 1 && f[0].identifier == 2,
         "one lifts, other stays");

  // Stale padding slots beyond the count are ignored even if they look 'down'.
  expect(a.feed(R(1, {S(true, 7), S(true, 99), S(true, 98), S(false, 0), S(false, 0)}), &f) &&
             f.size() == 1 && f[0].identifier == 7,
         "slots beyond Contact Count are ignored");

  // Hybrid: 7 contacts over two reports (5 slots each).
  expect(!a.feed(R(7, {S(true, 1), S(true, 2), S(true, 3), S(true, 4), S(true, 5)}), &f),
         "hybrid first report is not yet a frame");
  expect(a.feed(R(0, {S(true, 6), S(true, 7), S(false, 0), S(false, 0), S(false, 0)}), &f) &&
             f.size() == 7,
         "hybrid continuation (count 0) completes the frame");

  // Count 0 with no frame open: nothing touching.
  expect(a.feed(R(0, {S(false, 0), S(false, 0), S(false, 0)}), &f) && f.isEmpty(),
         "empty report with no open frame -> empty frame");

  // A new first report discards an incomplete hybrid frame (lost continuation).
  a.feed(R(7, {S(true, 1), S(true, 2), S(true, 3), S(true, 4), S(true, 5)}), &f);
  expect(a.feed(R(1, {S(true, 9), S(false, 0), S(false, 0)}), &f) && f.size() == 1 &&
             f[0].identifier == 9,
         "new frame start drops incomplete hybrid frame");

  // Device without Contact Count: each report is a frame.
  PtpReport nc;
  nc.haveCount = false;
  nc.fingers = {S(true, 1), S(false, 0)};
  expect(a.feed(nc, &f) && f.size() == 1, "no-count device: report is its own frame");
  nc.fingers = {S(false, 1), S(false, 0)};
  expect(a.feed(nc, &f) && f.isEmpty(), "no-count device: all tips up -> empty frame");

  // Coordinates are passed through.
  a.feed(R(1, {S(true, 4, 0.25, 0.75)}), &f);
  expect(qAbs(f[0].normalized.x() - 0.25) < 1e-9 && qAbs(f[0].normalized.y() - 0.75) < 1e-9,
         "coordinates preserved");

  QTextStream(stdout) << (failures == 0 ? "All ptp_frame tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
