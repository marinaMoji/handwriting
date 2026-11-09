#pragma once

#include <QMetaType>
#include <QObject>
#include <QPointF>
#include <QVector>

struct TrackpadContact {
  int identifier = -1;
  QPointF normalized;  // 0-1 over the pad surface, top-left origin
};

Q_DECLARE_METATYPE(TrackpadContact)
Q_DECLARE_METATYPE(QVector<TrackpadContact>)

// Platform-independent source of absolute trackpad finger positions.
//
// Backends (macOS NSTouch, generic Qt touch events, later Linux evdev and
// Windows Raw Input) only translate their native data into contact lists and
// call processContacts(). Stroke begin/move/end semantics, finger tracking by
// identifier, and palm handling live here so every platform behaves the same.
class TrackpadSource : public QObject {
  Q_OBJECT

 public:
  explicit TrackpadSource(QObject* parent = nullptr);
  ~TrackpadSource() override = default;

  virtual QString name() const = 0;
  void setActive(bool active);
  bool isActive() const { return active_; }
  virtual bool isAvailable() const { return available_; }
  // True after at least one absolute contact while active (disables cursor fallback).
  bool hasReceivedContact() const { return receivedContact_; }

 signals:
  void strokeBegan(QPointF normalized);
  void strokeMoved(QPointF normalized);
  void strokeEnded();
  void firstContact();  // first contact since activation
  // Physical pad width/height, when the backend can tell. Emitted before the contact
  // it was learned from is processed.
  void padAspectChanged(qreal aspect);

 protected:
  // Backend hooks. startBackend() returns false if the device cannot be used.
  virtual bool startBackend() { return true; }
  virtual void stopBackend() {}

  // Feed the currently touching contacts (empty = all fingers lifted).
  void processContacts(const QVector<TrackpadContact>& contacts);

  void reportPadAspect(qreal aspect);  // call with width/height; ignores invalid values

  bool available_ = true;

 private:
  qreal reportedAspect_ = 0.0;
  bool active_ = false;
  bool receivedContact_ = false;
  bool touching_ = false;
  bool awaitingLift_ = false;  // ignore contacts until all fingers lift
  int activeFingerId_ = -1;
};
