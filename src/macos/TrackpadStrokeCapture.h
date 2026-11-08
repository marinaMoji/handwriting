#pragma once

#include <QObject>
#include <QPointF>

class QWidget;

// macOS absolute trackpad capture via MultitouchSupport (private framework).
// Finger position is normalized 0–1 over the trackpad surface (Qt top-left origin).
// Independent of mouse-cursor location.
class TrackpadStrokeCapture : public QObject {
  Q_OBJECT

 public:
  explicit TrackpadStrokeCapture(QWidget* host, QObject* parent = nullptr);
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
  void onContacts(const QVector<QPointF>& touchingPoints);

 private:
  void startDevice();
  void stopDevice();

  QWidget* host_ = nullptr;
  void* deviceList_ = nullptr;   // CFArrayRef of MTDeviceRef
  QVector<void*> devices_;
  bool active_ = false;
  bool available_ = false;
  bool receivedContact_ = false;
  bool touching_ = false;
  int activeFingerId_ = -1;
};
