#pragma once

#include "TrackpadSource.h"

// macOS absolute trackpad capture via MultitouchSupport (private framework).
// Kept as an optional backend (HANDWRITING_CAPTURE=mt); the portable
// QtTouchCapture is preferred where it works. Devices are started once (lazy)
// and gated with setActive(); stopped only on destruction.
class TrackpadStrokeCapture : public TrackpadSource {
  Q_OBJECT

 public:
  explicit TrackpadStrokeCapture(QObject* parent = nullptr);
  ~TrackpadStrokeCapture() override;

  QString name() const override { return QStringLiteral("multitouch-support"); }

 public slots:
  // Called from the Multitouch callback thread via QueuedConnection.
  void onContacts(quint64 generation, const QVector<TrackpadContact>& contacts);

 protected:
  bool startBackend() override;
  void stopBackend() override;

 private:
  void ensureDevicesStarted();
  void stopDevices();

  void* deviceList_ = nullptr;  // CFArrayRef of MTDeviceRef (owns create ref)
  QVector<void*> devices_;
  bool devicesStarted_ = false;
  quint64 generation_ = 0;
};
