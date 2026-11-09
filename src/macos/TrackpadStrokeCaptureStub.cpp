#include "TrackpadStrokeCapture.h"

#if !defined(Q_OS_MACOS)

TrackpadStrokeCapture::TrackpadStrokeCapture(QObject* parent) : QObject(parent) {
  available_ = false;
}

TrackpadStrokeCapture::~TrackpadStrokeCapture() = default;

void TrackpadStrokeCapture::setActive(bool) {}

void TrackpadStrokeCapture::onContacts(quint64, const QVector<TrackpadContact>&) {}

void TrackpadStrokeCapture::setCursorFrozen(bool) {}

void TrackpadStrokeCapture::ensureDevicesStarted() {}

void TrackpadStrokeCapture::stopDevices() {}

#endif
