#include "QtTouchCapture.h"

#include <QInputDevice>
#include <QTouchEvent>

bool QtTouchCapture::handleTouchEvent(QTouchEvent* event) {
  if (!isActive() || event->device() == nullptr ||
      event->device()->type() != QInputDevice::DeviceType::TouchPad) {
    return false;
  }

  static const bool debug = qEnvironmentVariableIsSet("HANDWRITING_DEBUG_TOUCH");

  QVector<TrackpadContact> contacts;
  for (const QEventPoint& p : event->points()) {
    if (debug) {
      qInfo("touch id=%d state=%d norm=(%.3f, %.3f) pos=(%.1f, %.1f) global=(%.1f, %.1f) "
            "ellipse=(%.1f, %.1f) dev=%s",
            p.id(), static_cast<int>(p.state()), p.normalizedPosition().x(),
            p.normalizedPosition().y(), p.position().x(), p.position().y(),
            p.globalPosition().x(), p.globalPosition().y(), p.ellipseDiameters().width(),
            p.ellipseDiameters().height(), qPrintable(event->device()->name()));
    }
    if (p.state() == QEventPoint::State::Released) {
      continue;
    }
    TrackpadContact c;
    c.identifier = p.id();
    c.normalized = p.normalizedPosition();
    contacts.push_back(c);
  }
  processContacts(contacts);
  return true;
}
