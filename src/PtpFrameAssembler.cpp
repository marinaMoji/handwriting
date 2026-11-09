#include "PtpFrameAssembler.h"

void PtpFrameAssembler::reset() {
  open_ = false;
  expected_ = 0;
  reported_ = 0;
  contacts_.clear();
}

bool PtpFrameAssembler::feed(const PtpReport& report, QVector<TrackpadContact>* frame) {
  frame->clear();

  if (!report.haveCount) {
    // No Contact Count: every report is a complete frame of the slots that are down.
    reset();
    for (const PtpSlot& s : report.fingers) {
      if (s.tipDown) {
        frame->push_back(TrackpadContact{s.id, QPointF(s.x, s.y)});
      }
    }
    return true;
  }

  if (report.contactCount > 0) {
    // First (or only) report of a frame; drops any incomplete earlier frame.
    reset();
    open_ = true;
    expected_ = report.contactCount;
  } else if (!open_) {
    // Empty report: nothing touching.
    return true;
  }
  // else: continuation report of an open hybrid frame.

  const int take = qMin(report.fingers.size(), expected_ - reported_);
  for (int i = 0; i < take; ++i) {
    const PtpSlot& s = report.fingers[i];
    ++reported_;
    if (s.tipDown) {
      contacts_.push_back(TrackpadContact{s.id, QPointF(s.x, s.y)});
    }
  }

  if (reported_ >= expected_) {
    *frame = contacts_;
    reset();
    return true;
  }
  return false;
}
