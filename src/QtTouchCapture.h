#pragma once

#include "TrackpadSource.h"

class QTouchEvent;

// Portable backend: absolute finger positions from Qt's TouchPad touch events
// (QEventPoint::normalizedPosition). Uses only public Qt API. Works wherever the
// Qt platform plugin reports touchpad contacts (macOS); on platforms where it
// does not, no events arrive and the cursor fallback is used.
class QtTouchCapture : public TrackpadSource {
  Q_OBJECT

 public:
  explicit QtTouchCapture(QObject* parent = nullptr) : TrackpadSource(parent) {}
  QString name() const override { return QStringLiteral("qt-touch"); }

  // Returns true if the event was a touchpad event consumed by this source.
  bool handleTouchEvent(QTouchEvent* event);
};
