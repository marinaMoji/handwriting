#pragma once

#include "PtpFrameAssembler.h"
#include "TrackpadSource.h"

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QHash>
#include <QVector>

class QWidget;

// Windows backend: absolute contacts from HID precision touchpad via Raw Input.
class RawInputTrackpadSource : public TrackpadSource, public QAbstractNativeEventFilter {
  Q_OBJECT

 public:
  explicit RawInputTrackpadSource(QWidget* host, QObject* parent = nullptr);
  ~RawInputTrackpadSource() override;

  QString name() const override { return QStringLiteral("rawinput"); }
  bool isAvailable() const override;

 protected:
  bool startBackend() override;
  void stopBackend() override;
  bool nativeEventFilter(const QByteArray& eventType, void* message,
                         qintptr* result) override;

 private:
  struct ContactFields {
    quint16 link = 0;
    bool hasId = false;
    bool hasTip = false;
    bool hasX = false;
    bool hasY = false;
    quint16 xPage = 0x01;  // Generic Desktop
    quint16 yPage = 0x01;
  };

  struct DeviceCache {
    QByteArray preparsed;
    QVector<ContactFields> collections;  // ordered by link collection
    int logicalMaxX = 0;
    int logicalMaxY = 0;
    int physicalMaxX = 0;
    int physicalMaxY = 0;
    bool hasPhysical = false;
  };

  bool registerDevices();
  void unregisterDevices();
  void handleRawInput(void* lParam);
  DeviceCache* cacheForDevice(void* deviceHandle);  // HANDLE

  QWidget* host_ = nullptr;
  bool registered_ = false;
  bool probed_ = false;
  bool probeOk_ = false;
  QHash<quintptr, DeviceCache> deviceCache_;  // HANDLE as integer
  PtpFrameAssembler assembler_;
};
