#pragma once

#include <QMetaType>
#include <QObject>
#include <QPointF>
#include <QVector>

struct TrackpadContact {
  int identifier = -1;
  QPointF normalized;  // 0–1, Qt top-left origin
};

Q_DECLARE_METATYPE(TrackpadContact)
Q_DECLARE_METATYPE(QVector<TrackpadContact>)

// macOS absolute trackpad capture via MultitouchSupport (private framework).
// Finger position is normalized 0–1 over the trackpad surface (Qt top-left origin).
// Independent of mouse-cursor location.
//
// Devices are started once (lazy) and gated with setActive(); they are stopped
// only on destruction — not on every hold-key press/release.
class TrackpadStrokeCapture : public QObject {
  Q_OBJECT

 public:
  explicit TrackpadStrokeCapture(QObject* parent = nullptr);
  ~TrackpadStrokeCapture() override;

  void setActive(bool active);
  bool isActive() const { return active_; }
  bool isAvailable() const { return available_; }
  // True after at least one absolute contact while active (disable mouse fallback).
  bool hasReceivedContact() const { return receivedContact_; }

 signals:
  void strokeBegan(QPointF normalized);
  void strokeMoved(QPointF normalized);
  void strokeEnded();

 public slots:
  // Called from Multitouch callback thread via QueuedConnection.
  void onContacts(quint64 generation, const QVector<TrackpadContact>& contacts);

 private:
  // While writing with absolute touches, detach the pointer from the pad so
  // finger movement cannot drag the cursor onto other windows and click there.
  void setCursorFrozen(bool frozen);
  void ensureDevicesStarted();
  void stopDevices();

  void* deviceList_ = nullptr;  // CFArrayRef of MTDeviceRef (owns create ref)
  QVector<void*> devices_;
  bool devicesStarted_ = false;
  bool active_ = false;
  bool available_ = false;
  bool receivedContact_ = false;
  bool touching_ = false;
  bool cursorFrozen_ = false;
  bool awaitingLift_ = false;  // ignore contacts until all fingers lift
  int activeFingerId_ = -1;
  quint64 generation_ = 0;
};
