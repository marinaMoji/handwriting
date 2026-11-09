#pragma once

#include "MtSlotTracker.h"
#include "TrackpadSource.h"

#include <QVector>

class QSocketNotifier;

// Linux backend: absolute multitouch contacts via evdev (ABS_MT_* type B).
// Needs read access to the touchpad node (typically membership in the `input` group).
class EvdevTrackpadSource : public TrackpadSource {
  Q_OBJECT

 public:
  explicit EvdevTrackpadSource(QObject* parent = nullptr);
  ~EvdevTrackpadSource() override;

  QString name() const override { return QStringLiteral("evdev"); }
  bool isAvailable() const override;

 protected:
  bool startBackend() override;
  void stopBackend() override;

 private slots:
  void onReadable();

 private:
  struct DeviceInfo {
    QString path;
    QString name;
    int absXMin = 0;
    int absXMax = 1;
    int absYMin = 0;
    int absYMax = 1;
    int resX = 0;  // units per mm, 0 if unknown
    int resY = 0;
    int slotCount = 1;
    bool hasToolType = false;
    bool hasPressure = false;
    int pressureMin = 0;
    int pressureMax = 1;
  };

  static QVector<DeviceInfo> discoverDevices();
  bool openDevice(const DeviceInfo& info);
  void loadInitialSlots();
  void resyncAfterDrop();
  void deviceLost();

  DeviceInfo chosen_;
  int fd_ = -1;
  QSocketNotifier* notifier_ = nullptr;
  MtSlotTracker tracker_;
  bool probed_ = false;
  bool probeOk_ = false;
};
