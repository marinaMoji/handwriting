#include "TrackpadStrokeCapture.h"

#include <QWidget>

#if !defined(Q_OS_MACOS)

TrackpadStrokeCapture::TrackpadStrokeCapture(QWidget* host, QObject* parent)
    : QObject(parent), host_(host) {
  available_ = false;
}

TrackpadStrokeCapture::~TrackpadStrokeCapture() = default;

void TrackpadStrokeCapture::setActive(bool) {}

void TrackpadStrokeCapture::onContacts(const QVector<QPointF>&) {}

void TrackpadStrokeCapture::startDevice() {}

void TrackpadStrokeCapture::stopDevice() {}

#endif
