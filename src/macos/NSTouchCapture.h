#pragma once

#include "TrackpadSource.h"

#include <QHash>

class QWidget;

// macOS backend using public AppKit API: indirect NSTouch contacts delivered as
// gesture events to the host window, read with a local NSEvent monitor.
// normalizedPosition is the absolute finger position on the pad (0-1).
// No private frameworks. Also reports the pad's real aspect (NSTouch.deviceSize).
class NSTouchCapture : public TrackpadSource {
  Q_OBJECT

 public:
  explicit NSTouchCapture(QWidget* host, QObject* parent = nullptr);
  ~NSTouchCapture() override;

  QString name() const override { return QStringLiteral("nstouch"); }

 protected:
  bool startBackend() override;
  void stopBackend() override;

 private:
  void handleGestureEvent(void* nsEvent);  // NSEvent*, kept opaque for C++ callers

  QWidget* host_ = nullptr;
  void* monitor_ = nullptr;  // retained id from addLocalMonitor
  quint64 savedAllowedTypes_ = 0;
  bool savedWantsResting_ = false;
  bool savedValid_ = false;
  QHash<quint64, int> ids_;  // NSTouch identity hash -> small int
  int nextId_ = 1;
};
