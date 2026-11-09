#include "MtSlotTracker.h"

void MtSlotTracker::configure(const Config& config) {
  cfg_ = config;
  slots_.fill(Slot{}, qMax(1, config.slotCount));
  current_ = 0;
  dropping_ = false;
  hasObserved_ = false;
}

void MtSlotTracker::reset() {
  slots_.clear();
  current_ = 0;
  dropping_ = false;
}

void MtSlotTracker::apply(Slot& slot, quint16 code, qint32 value) {
  switch (code) {
    case kAbsMtTrackingId: slot.trackingId = value; break;
    case kAbsMtPositionX: slot.x = value; break;
    case kAbsMtPositionY: slot.y = value; break;
    case kAbsMtToolType: slot.toolType = value; break;
    case kAbsMtPressure: slot.pressure = value; break;
    default: break;
  }
}

void MtSlotTracker::setSlotValue(int slot, quint16 code, qint32 value) {
  if (slot >= 0 && slot < slots_.size()) {
    apply(slots_[slot], code, value);
  }
}

MtSlotTracker::Result MtSlotTracker::feed(quint16 type, quint16 code, qint32 value,
                                          QVector<TrackpadContact>* frame) {
  if (type == kEvSyn && code == kSynDropped) {
    dropping_ = true;
    return Result::None;
  }
  if (dropping_) {
    if (type == kEvSyn && code == kSynReport) {
      dropping_ = false;
      return Result::NeedsResync;
    }
    return Result::None;
  }
  if (slots_.isEmpty()) {
    return Result::None;
  }

  if (type == kEvAbs) {
    if (code == kAbsMtSlot) {
      current_ = qBound(0, static_cast<int>(value), static_cast<int>(slots_.size()) - 1);
    } else {
      apply(slots_[current_], code, value);
    }
    return Result::None;
  }
  if (type == kEvSyn && code == kSynReport) {
    *frame = contacts();
    return Result::Frame;
  }
  return Result::None;
}

void MtSlotTracker::noteObserved(int x, int y) {
  if (!hasObserved_) {
    obsXMin_ = obsXMax_ = x;
    obsYMin_ = obsYMax_ = y;
    hasObserved_ = true;
    return;
  }
  obsXMin_ = qMin(obsXMin_, x);
  obsXMax_ = qMax(obsXMax_, x);
  obsYMin_ = qMin(obsYMin_, y);
  obsYMax_ = qMax(obsYMax_, y);
}

void MtSlotTracker::effectiveRange(int* xMin, int* xMax, int* yMin, int* yMax) const {
  *xMin = cfg_.absXMin;
  *xMax = cfg_.absXMax;
  *yMin = cfg_.absYMin;
  *yMax = cfg_.absYMax;
  if (!cfg_.learnRange || !hasObserved_) {
    return;
  }
  const int absSpanX = qMax(1, cfg_.absXMax - cfg_.absXMin);
  const int absSpanY = qMax(1, cfg_.absYMax - cfg_.absYMin);
  const int obsSpanX = obsXMax_ - obsXMin_;
  const int obsSpanY = obsYMax_ - obsYMin_;
  // Wait until the finger has covered a meaningful fraction of the advertised range so a
  // single corner tap does not shrink the map to a point.
  if (obsSpanX < absSpanX / 5 || obsSpanY < absSpanY / 5) {
    return;
  }
  const int padX = qMax(1, obsSpanX / 50);
  const int padY = qMax(1, obsSpanY / 50);
  const int x0 = qMax(cfg_.absXMin, obsXMin_ - padX);
  const int x1 = qMin(cfg_.absXMax, obsXMax_ + padX);
  const int y0 = qMax(cfg_.absYMin, obsYMin_ - padY);
  const int y1 = qMin(cfg_.absYMax, obsYMax_ + padY);
  if (x1 <= x0 || y1 <= y0) {
    return;
  }
  *xMin = x0; *xMax = x1; *yMin = y0; *yMax = y1;
}

QVector<TrackpadContact> MtSlotTracker::contacts() {
  QVector<TrackpadContact> out;
  for (const Slot& slot : slots_) {
    if (slot.trackingId < 0) {
      continue;
    }
    if (cfg_.hasToolType && slot.toolType == kToolPalm) {
      continue;
    }
    if (cfg_.hasPressure && slot.pressure >= 0) {
      const qreal span = qMax(1, cfg_.pressureMax - cfg_.pressureMin);
      if ((slot.pressure - cfg_.pressureMin) / span < cfg_.minPressureFraction) {
        continue;
      }
    }
    noteObserved(slot.x, slot.y);
    int xMin = 0, xMax = 1, yMin = 0, yMax = 1;
    effectiveRange(&xMin, &xMax, &yMin, &yMax);
    const qreal xSpan = qMax(1, xMax - xMin);
    const qreal ySpan = qMax(1, yMax - yMin);
    TrackpadContact c;
    c.identifier = slot.trackingId;
    // evdev touchpads have their origin at the top-left, matching our contract.
    c.normalized = QPointF(qBound<qreal>(0.0, (slot.x - xMin) / xSpan, 1.0),
                           qBound<qreal>(0.0, (slot.y - yMin) / ySpan, 1.0));
    out.push_back(c);
  }
  return out;
}
