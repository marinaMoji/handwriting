#pragma once

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

  struct SlotState {
    int trackingId = -1;
    int x = 0;
    int y = 0;
    int toolType = 0;  // MT_TOOL_FINGER by default
    int pressure = -1;
    bool seen = false;
  };

  static QVector<DeviceInfo> discoverDevices();
  bool openDevice(const DeviceInfo& info);
  void loadInitialSlots();
  void resyncAfterDrop();
  void deviceLost();
  void handleSynReport();
  void noteObservedSample(int x, int y);
  void effectiveAbsRange(int* xMin, int* xMax, int* yMin, int* yMax) const;

  DeviceInfo chosen_;
  int fd_ = -1;
  QSocketNotifier* notifier_ = nullptr;
  QVector<SlotState> slots_;
  int currentSlot_ = 0;
  bool dropping_ = false;  // discarding events until SYN_REPORT after SYN_DROPPED
  bool probed_ = false;
  bool probeOk_ = false;
  // Many pads advertise ABS ranges larger than the glass. Learning the span
  // that fingers actually use removes dead "out of bounds" margins.
  int obsXMin_ = 0;
  int obsXMax_ = 0;
  int obsYMin_ = 0;
  int obsYMax_ = 0;
  bool hasObserved_ = false;
};
