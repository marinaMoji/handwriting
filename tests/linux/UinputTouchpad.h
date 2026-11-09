#pragma once

// Virtual type-B multitouch touchpad for Linux integration tests (issue #4).
// Creates a device via /dev/uinput, finds its /dev/input/event* node, and
// injects ABS_MT_* frames. Header-only so the fixture stays next to the test.

#include <QDir>
#include <QFile>
#include <QString>
#include <QThread>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/stat.h>
#include <unistd.h>

class UinputTouchpad {
 public:
  static constexpr const char* kName = "handwriting-uinput-fixture Touchpad";
  static constexpr int kAbsMaxX = 1000;
  static constexpr int kAbsMaxY = 1000;
  static constexpr int kSlots = 5;
  static constexpr int kPressureMax = 100;

  // Returns an empty error string on success.
  QString create() {
    destroy();
    fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd_ < 0) {
      return QStringLiteral("cannot open /dev/uinput (%1) — need write access "
                            "(root, or a udev rule).")
          .arg(QString::fromLocal8Bit(std::strerror(errno)));
    }

    auto enable = [this](unsigned long req, int code) {
      return ioctl(fd_, req, code) == 0;
    };
    if (!enable(UI_SET_EVBIT, EV_KEY) || !enable(UI_SET_EVBIT, EV_ABS) ||
        !enable(UI_SET_EVBIT, EV_SYN)) {
      return failClose("UI_SET_EVBIT");
    }
    // BTN_LEFT makes this look like a normal pointer device to some stacks.
    if (!enable(UI_SET_KEYBIT, BTN_TOUCH) || !enable(UI_SET_KEYBIT, BTN_TOOL_FINGER) ||
        !enable(UI_SET_KEYBIT, BTN_LEFT)) {
      return failClose("UI_SET_KEYBIT");
    }
    if (!enable(UI_SET_PROPBIT, INPUT_PROP_POINTER)) {
      return failClose("UI_SET_PROPBIT");
    }

    const int absCodes[] = {ABS_X,
                            ABS_Y,
                            ABS_MT_SLOT,
                            ABS_MT_TRACKING_ID,
                            ABS_MT_POSITION_X,
                            ABS_MT_POSITION_Y,
                            ABS_MT_TOOL_TYPE,
                            ABS_MT_PRESSURE};
    for (int code : absCodes) {
      if (!enable(UI_SET_ABSBIT, code)) {
        return failClose("UI_SET_ABSBIT");
      }
    }

    uinput_abs_setup abs = {};
    auto setAbs = [&](int code, int minV, int maxV) -> bool {
      std::memset(&abs, 0, sizeof(abs));
      abs.code = static_cast<__u16>(code);
      abs.absinfo.minimum = minV;
      abs.absinfo.maximum = maxV;
      abs.absinfo.resolution = 10;  // units/mm — gives a 1.0 aspect
      return ioctl(fd_, UI_ABS_SETUP, &abs) == 0;
    };
    // Tracking id -1 means "finger up" in type B; the abs range must allow it.
    if (!setAbs(ABS_X, 0, kAbsMaxX) || !setAbs(ABS_Y, 0, kAbsMaxY) ||
        !setAbs(ABS_MT_SLOT, 0, kSlots - 1) || !setAbs(ABS_MT_TRACKING_ID, -1, 65535) ||
        !setAbs(ABS_MT_POSITION_X, 0, kAbsMaxX) || !setAbs(ABS_MT_POSITION_Y, 0, kAbsMaxY) ||
        !setAbs(ABS_MT_TOOL_TYPE, 0, 2 /* MT_TOOL_PALM */) ||
        !setAbs(ABS_MT_PRESSURE, 0, kPressureMax)) {
      return failClose("UI_ABS_SETUP");
    }

    uinput_setup setup = {};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    setup.id.version = 1;
    std::snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", kName);
    if (ioctl(fd_, UI_DEV_SETUP, &setup) < 0) {
      return failClose("UI_DEV_SETUP");
    }
    if (ioctl(fd_, UI_DEV_CREATE) < 0) {
      return failClose("UI_DEV_CREATE");
    }
    created_ = true;

    // Kernel publishes the event node shortly after create; udev then applies ACLs.
    for (int i = 0; i < 50; ++i) {
      eventPath_ = findEventNode();
      if (!eventPath_.isEmpty()) {
        break;
      }
      QThread::msleep(20);
    }
    if (eventPath_.isEmpty()) {
      destroy();
      return QStringLiteral("uinput device created but no /dev/input/event* node appeared");
    }

    // Best-effort: make the node readable if we own it / have CAP_FOWNER.
    ::chmod(eventPath_.toLocal8Bit().constData(), 0666);
    // Wait for us to be able to open it (seat ACL / input group via udev).
    bool readable = false;
    for (int i = 0; i < 100; ++i) {
      const int rfd = ::open(eventPath_.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
      if (rfd >= 0) {
        ::close(rfd);
        readable = true;
        break;
      }
      if (i == 5) {
        // Give udev a chance to tag the new node for the active seat.
        std::system("udevadm settle -t 2 >/dev/null 2>&1");
      }
      QThread::msleep(20);
    }
    if (!readable) {
      const QString path = eventPath_;
      destroy();
      return QStringLiteral(
                 "created %1 but cannot open it for reading (%2). "
                 "Join the `input` group or relax udev rules for virtual devices.")
          .arg(path, QString::fromLocal8Bit(std::strerror(errno)));
    }
    return {};
  }

  void destroy() {
    if (fd_ >= 0) {
      if (created_) {
        ioctl(fd_, UI_DEV_DESTROY);
        created_ = false;
      }
      ::close(fd_);
      fd_ = -1;
    }
    eventPath_.clear();
  }

  ~UinputTouchpad() { destroy(); }

  QString eventPath() const { return eventPath_; }
  bool isOpen() const { return fd_ >= 0 && created_; }

  void emitEvent(quint16 type, quint16 code, qint32 value) {
    if (fd_ < 0) {
      return;
    }
    input_event ev = {};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    (void)::write(fd_, &ev, sizeof(ev));
  }

  void syn() { emitEvent(EV_SYN, SYN_REPORT, 0); }
  void synDropped() { emitEvent(EV_SYN, SYN_DROPPED, 0); }

  void setSlot(int slot) { emitEvent(EV_ABS, ABS_MT_SLOT, slot); }

  void fingerDown(int slot, int trackingId, int x, int y, int toolType = MT_TOOL_FINGER,
                  int pressure = 50) {
    setSlot(slot);
    emitEvent(EV_ABS, ABS_MT_TRACKING_ID, trackingId);
    emitEvent(EV_ABS, ABS_MT_TOOL_TYPE, toolType);
    emitEvent(EV_ABS, ABS_MT_POSITION_X, x);
    emitEvent(EV_ABS, ABS_MT_POSITION_Y, y);
    emitEvent(EV_ABS, ABS_X, x);
    emitEvent(EV_ABS, ABS_Y, y);
    emitEvent(EV_ABS, ABS_MT_PRESSURE, pressure);
    emitEvent(EV_KEY, BTN_TOOL_FINGER, 1);
    emitEvent(EV_KEY, BTN_TOUCH, 1);
    syn();
  }

  void fingerMove(int slot, int x, int y, int pressure = 50) {
    setSlot(slot);
    emitEvent(EV_ABS, ABS_MT_POSITION_X, x);
    emitEvent(EV_ABS, ABS_MT_POSITION_Y, y);
    emitEvent(EV_ABS, ABS_X, x);
    emitEvent(EV_ABS, ABS_Y, y);
    emitEvent(EV_ABS, ABS_MT_PRESSURE, pressure);
    syn();
  }

  void fingerUp(int slot) {
    setSlot(slot);
    emitEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
    emitEvent(EV_KEY, BTN_TOUCH, 0);
    emitEvent(EV_KEY, BTN_TOOL_FINGER, 0);
    syn();
  }

  // Lift one slot while leaving others (and BTN state) alone.
  void slotUp(int slot) {
    setSlot(slot);
    emitEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
    syn();
  }

 private:
  QString failClose(const char* what) {
    const QString err = QStringLiteral("%1 failed: %2")
                            .arg(QLatin1String(what), QString::fromLocal8Bit(std::strerror(errno)));
    destroy();
    return err;
  }

  QString findEventNode() const {
#ifdef UI_GET_SYSNAME
    char sysname[64] = {};
    if (ioctl(fd_, UI_GET_SYSNAME(sizeof(sysname)), sysname) >= 0) {
      const QString dirPath =
          QStringLiteral("/sys/devices/virtual/input/%1").arg(QString::fromLocal8Bit(sysname));
      const QDir dir(dirPath);
      const QStringList events = dir.entryList(QStringList{QStringLiteral("event*")},
                                               QDir::Dirs | QDir::Files | QDir::System |
                                                   QDir::NoDotAndDotDot);
      if (!events.isEmpty()) {
        return QStringLiteral("/dev/input/") + events.first();
      }
    }
#endif
    // Fallback: scan /sys/class/input for our device name.
    const QDir classDir(QStringLiteral("/sys/class/input"));
    const QStringList entries = classDir.entryList(QStringList{QStringLiteral("event*")},
                                                   QDir::Dirs | QDir::Files | QDir::System |
                                                       QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
      QFile nameFile(classDir.filePath(entry + QStringLiteral("/device/name")));
      if (!nameFile.open(QIODevice::ReadOnly)) {
        nameFile.setFileName(classDir.filePath(entry + QStringLiteral("/name")));
        if (!nameFile.open(QIODevice::ReadOnly)) {
          continue;
        }
      }
      const QByteArray name = nameFile.readAll().trimmed();
      if (name == QByteArray(kName)) {
        return QStringLiteral("/dev/input/") + entry;
      }
    }
    return {};
  }

  int fd_ = -1;
  bool created_ = false;
  QString eventPath_;
};
