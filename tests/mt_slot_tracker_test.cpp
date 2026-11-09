#include "MtSlotTracker.h"

#include <QCoreApplication>
#include <QTextStream>

static int failures = 0;
static void expect(bool cond, const char* msg) {
  QTextStream(stdout) << (cond ? "OK  " : "FAIL ") << msg << "\n";
  if (!cond) ++failures;
}

using T = MtSlotTracker;

// Event helpers.
static T::Result abs(T& t, quint16 code, int v, QVector<TrackpadContact>* f) {
  return t.feed(T::kEvAbs, code, v, f);
}
static T::Result syn(T& t, QVector<TrackpadContact>* f, quint16 code = T::kSynReport) {
  return t.feed(T::kEvSyn, code, 0, f);
}

static T::Config cfg() {
  T::Config c;
  c.absXMin = 0; c.absXMax = 1000;
  c.absYMin = 0; c.absYMax = 500;
  c.slotCount = 5;
  return c;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QVector<TrackpadContact> f;

  // Single finger down, move, lift.
  {
    T t; t.configure(cfg());
    abs(t, T::kAbsMtSlot, 0, &f);
    abs(t, T::kAbsMtTrackingId, 10, &f);
    abs(t, T::kAbsMtPositionX, 500, &f);
    abs(t, T::kAbsMtPositionY, 250, &f);
    expect(syn(t, &f) == T::Result::Frame && f.size() == 1 && f[0].identifier == 10, "finger down -> one contact");
    expect(qAbs(f[0].normalized.x() - 0.5) < 1e-9 && qAbs(f[0].normalized.y() - 0.5) < 1e-9,
           "position normalized to 0-1");
    abs(t, T::kAbsMtPositionX, 1000, &f);
    syn(t, &f);
    expect(f.size() == 1 && qAbs(f[0].normalized.x() - 1.0) < 1e-9, "move updates the contact");
    abs(t, T::kAbsMtTrackingId, -1, &f);
    expect(syn(t, &f) == T::Result::Frame && f.isEmpty(), "tracking id -1 lifts the finger (empty frame)");
  }

  // Two fingers in different slots; slot switching.
  {
    T t; t.configure(cfg());
    abs(t, T::kAbsMtSlot, 0, &f); abs(t, T::kAbsMtTrackingId, 1, &f);
    abs(t, T::kAbsMtPositionX, 100, &f); abs(t, T::kAbsMtPositionY, 100, &f);
    abs(t, T::kAbsMtSlot, 1, &f); abs(t, T::kAbsMtTrackingId, 2, &f);
    abs(t, T::kAbsMtPositionX, 900, &f); abs(t, T::kAbsMtPositionY, 400, &f);
    syn(t, &f);
    expect(f.size() == 2 && f[0].identifier == 1 && f[1].identifier == 2, "two slots -> two contacts");
    // Move only slot 0 (slot select must persist for following events).
    abs(t, T::kAbsMtSlot, 0, &f); abs(t, T::kAbsMtPositionX, 200, &f);
    syn(t, &f);
    expect(f.size() == 2 && qAbs(f[0].normalized.x() - 0.2) < 1e-9 && qAbs(f[1].normalized.x() - 0.9) < 1e-9,
           "events apply only to the selected slot");
    // Lift slot 0 only.
    abs(t, T::kAbsMtTrackingId, -1, &f);
    syn(t, &f);
    expect(f.size() == 1 && f[0].identifier == 2, "lifting one slot keeps the other");
  }

  // Out-of-range slot is clamped, never crashes or writes out of bounds.
  {
    T t; t.configure(cfg());
    abs(t, T::kAbsMtSlot, 99, &f); abs(t, T::kAbsMtTrackingId, 5, &f);
    abs(t, T::kAbsMtSlot, -3, &f); abs(t, T::kAbsMtTrackingId, 6, &f);
    syn(t, &f);
    expect(f.size() == 2, "out-of-range slots clamp to valid slots");
  }

  // Palm tool type is excluded.
  {
    T::Config c = cfg(); c.hasToolType = true;
    T t; t.configure(c);
    abs(t, T::kAbsMtSlot, 0, &f); abs(t, T::kAbsMtTrackingId, 1, &f); abs(t, T::kAbsMtToolType, 0, &f);
    abs(t, T::kAbsMtSlot, 1, &f); abs(t, T::kAbsMtTrackingId, 2, &f); abs(t, T::kAbsMtToolType, T::kToolPalm, &f);
    syn(t, &f);
    expect(f.size() == 1 && f[0].identifier == 1, "palm contacts are excluded");
    // A finger reclassified as palm mid-stroke drops out.
    abs(t, T::kAbsMtSlot, 0, &f); abs(t, T::kAbsMtToolType, T::kToolPalm, &f);
    syn(t, &f);
    expect(f.isEmpty(), "contact reclassified as palm drops out");
  }
  // Without tool type support, tool type is ignored.
  {
    T t; t.configure(cfg());
    abs(t, T::kAbsMtTrackingId, 1, &f); abs(t, T::kAbsMtToolType, T::kToolPalm, &f);
    syn(t, &f);
    expect(f.size() == 1, "tool type ignored when the device does not report it");
  }

  // Pressure threshold.
  {
    T::Config c = cfg(); c.hasPressure = true; c.pressureMin = 0; c.pressureMax = 100;
    T t; t.configure(c);
    abs(t, T::kAbsMtSlot, 0, &f); abs(t, T::kAbsMtTrackingId, 1, &f); abs(t, T::kAbsMtPressure, 2, &f);
    abs(t, T::kAbsMtSlot, 1, &f); abs(t, T::kAbsMtTrackingId, 2, &f); abs(t, T::kAbsMtPressure, 40, &f);
    syn(t, &f);
    expect(f.size() == 1 && f[0].identifier == 2, "near-zero pressure contact ignored");
    // Pressure not yet reported (-1) is not filtered.
    abs(t, T::kAbsMtSlot, 2, &f); abs(t, T::kAbsMtTrackingId, 3, &f);
    syn(t, &f);
    expect(f.size() == 2, "unknown pressure is not filtered");
  }

  // Positions clamp to 0-1 even if the device overshoots its range.
  {
    T t; t.configure(cfg());
    abs(t, T::kAbsMtTrackingId, 1, &f);
    abs(t, T::kAbsMtPositionX, -50, &f); abs(t, T::kAbsMtPositionY, 9999, &f);
    syn(t, &f);
    expect(f[0].normalized.x() == 0.0 && f[0].normalized.y() == 1.0, "out-of-range position clamps");
  }

  // SYN_DROPPED: ignore until SYN_REPORT, then ask for resync; state is not corrupted.
  {
    T t; t.configure(cfg());
    abs(t, T::kAbsMtTrackingId, 1, &f); abs(t, T::kAbsMtPositionX, 500, &f); abs(t, T::kAbsMtPositionY, 250, &f);
    syn(t, &f);
    expect(syn(t, &f, T::kSynDropped) == T::Result::None, "SYN_DROPPED itself reports nothing");
    abs(t, T::kAbsMtPositionX, 999, &f);  // part of the dropped packet: must be ignored
    abs(t, T::kAbsMtTrackingId, -1, &f);
    expect(syn(t, &f) == T::Result::NeedsResync, "next SYN_REPORT after a drop requests resync");
    expect(t.contacts().size() == 1 && qAbs(t.contacts()[0].normalized.x() - 0.5) < 1e-9,
           "events from the dropped packet were discarded");
    // Owner re-reads state, then normal operation resumes.
    t.setSlotValue(0, T::kAbsMtTrackingId, 7);
    t.setSlotValue(0, T::kAbsMtPositionX, 100);
    expect(t.contacts().size() == 1 && t.contacts()[0].identifier == 7, "resync via setSlotValue");
    abs(t, T::kAbsMtPositionX, 200, &f);
    expect(syn(t, &f) == T::Result::Frame && qAbs(f[0].normalized.x() - 0.2) < 1e-9, "normal after resync");
  }

  // Range learning: pad advertises 0-1000 but fingers only reach 200-800.
  {
    T::Config c = cfg(); c.absYMax = 1000; c.learnRange = true;
    T t; t.configure(c);
    abs(t, T::kAbsMtTrackingId, 1, &f);
    abs(t, T::kAbsMtPositionX, 500, &f); abs(t, T::kAbsMtPositionY, 500, &f);
    syn(t, &f);
    expect(qAbs(f[0].normalized.x() - 0.5) < 1e-9, "no learning before enough range is covered");
    abs(t, T::kAbsMtPositionX, 200, &f); abs(t, T::kAbsMtPositionY, 200, &f); syn(t, &f);
    abs(t, T::kAbsMtPositionX, 800, &f); abs(t, T::kAbsMtPositionY, 800, &f); syn(t, &f);
    expect(f[0].normalized.x() > 0.97 && f[0].normalized.x() <= 1.0, "learned range stretches the reachable span");
    abs(t, T::kAbsMtPositionX, 200, &f); syn(t, &f);
    expect(f[0].normalized.x() < 0.03, "learned lower edge maps near 0");
  }

  // Unconfigured / reset tracker is inert.
  {
    T t;
    expect(abs(t, T::kAbsMtTrackingId, 1, &f) == T::Result::None && syn(t, &f) == T::Result::None,
           "unconfigured tracker ignores events");
    t.configure(cfg()); abs(t, T::kAbsMtTrackingId, 1, &f); t.reset();
    expect(syn(t, &f) == T::Result::None, "reset tracker ignores events");
    t.setSlotValue(3, T::kAbsMtTrackingId, 1);  // must not crash
  }

  QTextStream(stdout) << (failures == 0 ? "All mt_slot_tracker tests passed.\n" : "Some tests failed.\n");
  return failures == 0 ? 0 : 1;
}
