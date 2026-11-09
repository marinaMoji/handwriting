#pragma once

#include "TrackpadSource.h"

#include <QVector>

// Pure frame assembly for HID Precision Touchpad reports (used by the Windows
// backend, kept portable so it can be unit-tested with fixture reports).
//
// PTP semantics:
//  * A report holds a fixed number of finger collections (slots). Only the first
//    `Contact Count` of them are valid; the rest are stale padding.
//  * A lifting finger is reported once with Tip Switch = 0 and still counts toward
//    Contact Count. So "no contact down" arrives as count > 0 with all tips up.
//  * In hybrid mode, more contacts than slots span several reports: the first carries
//    the total Contact Count, continuations carry Contact Count = 0.
//  * Count = 0 with no frame in progress means an empty report (nothing touching).

struct PtpSlot {
  bool tipDown = false;
  int id = 0;
  qreal x = 0.0;  // normalized 0-1, top-left origin
  qreal y = 0.0;
};

struct PtpReport {
  bool haveCount = false;  // false if the device exposes no Contact Count
  int contactCount = 0;
  QVector<PtpSlot> fingers;  // all finger collections, in collection order
};

class PtpFrameAssembler {
 public:
  // Returns true when a complete frame is ready in *frame (possibly empty = all lifted).
  bool feed(const PtpReport& report, QVector<TrackpadContact>* frame);
  void reset();

 private:
  bool open_ = false;
  int expected_ = 0;
  int reported_ = 0;
  QVector<TrackpadContact> contacts_;
};
