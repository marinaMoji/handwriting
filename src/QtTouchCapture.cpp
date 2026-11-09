#include "QtTouchCapture.h"

#include <QInputDevice>
#include <QTouchEvent>

QtTouchCapture::QtTouchCapture(QObject* parent) : TrackpadSource(parent) {
  available_ = isAvailable();
}

bool QtTouchCapture::isAvailable() const {
  // On Linux/Wayland, Qt almost never delivers raw TouchPad contacts to the
  // window (the compositor owns the pad). Pretending we are available makes the
  // app activate a no-op backend and fall through to broken cursor mapping.
  // Opt back in with HANDWRITING_FORCE_QT_TOUCH=1 for rare setups that do work.
#if defined(Q_OS_LINUX)
  return qEnvironmentVariableIsSet("HANDWRITING_FORCE_QT_TOUCH");
#else
  return true;
#endif
}

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
