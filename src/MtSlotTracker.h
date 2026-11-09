#pragma once

#include "TrackpadSource.h"

#include <QVector>

// Pure state machine for the Linux multitouch type B protocol (ABS_MT_SLOT /
// ABS_MT_TRACKING_ID ...). It consumes raw kernel input events and produces
// contact frames. It has no Linux dependencies so it can be unit-tested anywhere;
// the evdev backend static_asserts that the mirrored constants match the system
// headers.
//
// Rules:
//  * ABS_MT_SLOT selects the slot that following ABS_MT_* events apply to.
//  * A slot is a contact while its tracking id >= 0 (-1 = finger lifted).
//  * Palm tool type and (when the device reports pressure) near-zero pressure
//    contacts are excluded from frames.
//  * SYN_DROPPED: discard everything up to the next SYN_REPORT, then the owner must
//    re-read the full device state (setSlotValue) and call contacts().

class MtSlotTracker {
 public:
  // Mirrors of <linux/input-event-codes.h>.
  static constexpr quint16 kEvSyn = 0x00;
  static constexpr quint16 kEvAbs = 0x03;
  static constexpr quint16 kSynReport = 0;
  static constexpr quint16 kSynDropped = 3;
  static constexpr quint16 kAbsMtSlot = 0x2f;
  static constexpr quint16 kAbsMtPositionX = 0x35;
  static constexpr quint16 kAbsMtPositionY = 0x36;
  static constexpr quint16 kAbsMtToolType = 0x37;
  static constexpr quint16 kAbsMtTrackingId = 0x39;
  static constexpr quint16 kAbsMtPressure = 0x3a;
  static constexpr int kToolPalm = 2;  // MT_TOOL_PALM

  struct Config {
    int absXMin = 0, absXMax = 1;
    int absYMin = 0, absYMax = 1;
    int slotCount = 1;
    bool hasToolType = false;
    bool hasPressure = false;
    int pressureMin = 0, pressureMax = 1;
    qreal minPressureFraction = 0.05;  // contacts below this are ignored (if hasPressure)
    // Many pads advertise ranges larger than the usable glass. Inset each axis by this
    // fraction of the span so the physical edges of the glass map near the canvas edges
    // (0 = use the raw abs min/max). Override with HANDWRITING_PAD_EDGE.
    qreal edgeInset = 0.04;
    // Optional: after a lift, shrink the mapping to the finger span actually seen.
    // Off by default — committing after a small character makes the rest of the pad
    // clamp to the canvas border ("not capturing the full trackpad").
    bool learnRange = false;
  };

  enum class Result {
    None,         // event consumed, nothing to report yet
    Frame,        // SYN_REPORT: *frame holds the current contacts (possibly empty)
    NeedsResync,  // end of a dropped packet: reload state, then call contacts()
  };

  void configure(const Config& config);
  void reset();

  Result feed(quint16 type, quint16 code, qint32 value, QVector<TrackpadContact>* frame);

  // Initial state or resync: set one ABS_MT_* value (tracking id, position, tool type,
  // pressure) for a slot directly.
  void setSlotValue(int slot, quint16 code, qint32 value);

  QVector<TrackpadContact> contacts();
  int slotCount() const { return slots_.size(); }

  // Promote the observed finger span into the range used for normalization.
  // Call on finger-up (empty frame). Applying a new learned range mid-stroke
  // remaps coordinates and looks like a teleport, which splits the stroke.
  void commitLearnedRange();

 private:
  struct Slot {
    int trackingId = -1;
    int x = 0;
    int y = 0;
    int toolType = 0;
    int pressure = -1;
  };
  void apply(Slot& slot, quint16 code, qint32 value);
  void noteObserved(int x, int y);
  void effectiveRange(int* xMin, int* xMax, int* yMin, int* yMax) const;

  Config cfg_;
  QVector<Slot> slots_;
  int current_ = 0;
  bool dropping_ = false;
  bool hasObserved_ = false;
  int obsXMin_ = 0, obsXMax_ = 0, obsYMin_ = 0, obsYMax_ = 0;
  bool hasCommitted_ = false;
  int comXMin_ = 0, comXMax_ = 0, comYMin_ = 0, comYMax_ = 0;
};
