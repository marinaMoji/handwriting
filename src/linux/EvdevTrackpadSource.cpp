#include "EvdevTrackpadSource.h"

#include <QByteArray>
#include <QSocketNotifier>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

bool testBit(int bit, const unsigned long* array) {
  return (array[bit / (8 * sizeof(unsigned long))] >> (bit % (8 * sizeof(unsigned long)))) & 1UL;
}

// The portable tracker mirrors these constants; make sure they cannot drift.
static_assert(MtSlotTracker::kEvSyn == EV_SYN && MtSlotTracker::kEvAbs == EV_ABS, "EV_* mismatch");
static_assert(MtSlotTracker::kSynReport == SYN_REPORT && MtSlotTracker::kSynDropped == SYN_DROPPED,
              "SYN_* mismatch");
static_assert(MtSlotTracker::kAbsMtSlot == ABS_MT_SLOT &&
                  MtSlotTracker::kAbsMtPositionX == ABS_MT_POSITION_X &&
                  MtSlotTracker::kAbsMtPositionY == ABS_MT_POSITION_Y &&
                  MtSlotTracker::kAbsMtToolType == ABS_MT_TOOL_TYPE &&
                  MtSlotTracker::kAbsMtTrackingId == ABS_MT_TRACKING_ID &&
                  MtSlotTracker::kAbsMtPressure == ABS_MT_PRESSURE,
              "ABS_MT_* mismatch");
#ifdef MT_TOOL_PALM
static_assert(MtSlotTracker::kToolPalm == MT_TOOL_PALM, "MT_TOOL_PALM mismatch");
#endif

}  // namespace

EvdevTrackpadSource::EvdevTrackpadSource(QObject* parent) : TrackpadSource(parent) {
  available_ = isAvailable();
}

EvdevTrackpadSource::~EvdevTrackpadSource() {
  setActive(false);
}

bool EvdevTrackpadSource::isAvailable() const {
  if (probed_) {
    return probeOk_;
  }
  auto* self = const_cast<EvdevTrackpadSource*>(this);
  self->probed_ = true;
  const QVector<DeviceInfo> devices = discoverDevices();
  self->probeOk_ = !devices.isEmpty();
  if (self->probeOk_) {
    self->chosen_ = devices.first();
    for (const DeviceInfo& d : devices) {
      const QString n = d.name.toLower();
      if (n.contains(QStringLiteral("touchpad")) || n.contains(QStringLiteral("trackpad"))) {
        self->chosen_ = d;
        break;
      }
    }
  }
  self->available_ = self->probeOk_;
  return self->probeOk_;
}

QVector<EvdevTrackpadSource::DeviceInfo> EvdevTrackpadSource::discoverDevices() {
  QVector<DeviceInfo> out;
  // Prefer POSIX readdir over QDir::System: device nodes in /dev/input have been
  // easy to miss with Qt filters depending on version/platform.
  DIR* dir = ::opendir("/dev/input");
  if (dir == nullptr) {
    qWarning("evdev: cannot open /dev/input (%s)", std::strerror(errno));
    return out;
  }
  static const bool debug = qEnvironmentVariableIsSet("HANDWRITING_DEBUG_TOUCH");
  int denied = 0;
  while (true) {
    errno = 0;
    dirent* ent = ::readdir(dir);
    if (ent == nullptr) {
      break;
    }
    const QString node = QString::fromLocal8Bit(ent->d_name);
    if (!node.startsWith(QStringLiteral("event"))) {
      continue;
    }
    const QString path = QStringLiteral("/dev/input/") + node;
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
      if (errno == EACCES || errno == EPERM) {
        ++denied;
      }
      if (debug) {
        qInfo("evdev: skip %s (%s)", qPrintable(path), std::strerror(errno));
      }
      continue;
    }

    unsigned long absBits[(ABS_CNT + 63) / 64] = {};
    unsigned long propBits[(INPUT_PROP_CNT + 63) / 64] = {};
    unsigned long keyBits[(KEY_CNT + 63) / 64] = {};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) {
      ::close(fd);
      continue;
    }
    ioctl(fd, EVIOCGPROP(sizeof(propBits)), propBits);
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits);

    const bool hasMt =
        testBit(ABS_MT_POSITION_X, absBits) && testBit(ABS_MT_POSITION_Y, absBits);
    // Type B only: without ABS_MT_SLOT, slot protocol assumptions break.
    const bool hasSlot = testBit(ABS_MT_SLOT, absBits);
    const bool looksLikePad =
        testBit(INPUT_PROP_POINTER, propBits) || testBit(BTN_TOOL_FINGER, keyBits) ||
        testBit(BTN_TOUCH, keyBits);
    const bool isDirect = testBit(INPUT_PROP_DIRECT, propBits);
    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
      std::snprintf(name, sizeof(name), "%s", qPrintable(node));
    }
    if (!hasMt || !hasSlot || !looksLikePad || isDirect) {
      if (debug) {
        qInfo("evdev: reject %s (%s) mt=%d slot=%d pad=%d direct=%d", qPrintable(path), name,
              int(hasMt), int(hasSlot), int(looksLikePad), int(isDirect));
      }
      ::close(fd);
      continue;
    }

    DeviceInfo info;
    info.path = path;
    info.name = QString::fromLocal8Bit(name);

    input_absinfo absX = {};
    input_absinfo absY = {};
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absX) < 0 ||
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY) < 0 || absX.maximum <= absX.minimum ||
        absY.maximum <= absY.minimum) {
      ::close(fd);
      continue;
    }
    info.absXMin = absX.minimum;
    info.absXMax = absX.maximum;
    info.absYMin = absY.minimum;
    info.absYMax = absY.maximum;
    info.resX = absX.resolution;
    info.resY = absY.resolution;

    input_absinfo absSlot = {};
    if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &absSlot) < 0) {
      ::close(fd);
      continue;
    }
    info.slotCount = qMax(1, absSlot.maximum + 1);

    info.hasToolType = testBit(ABS_MT_TOOL_TYPE, absBits);
    info.hasBtnTouch = testBit(BTN_TOUCH, keyBits);
    info.hasBtnToolFinger = testBit(BTN_TOOL_FINGER, keyBits);
    if (testBit(ABS_MT_PRESSURE, absBits)) {
      input_absinfo absP = {};
      if (ioctl(fd, EVIOCGABS(ABS_MT_PRESSURE), &absP) >= 0) {
        info.hasPressure = true;
        info.pressureMin = absP.minimum;
        info.pressureMax = absP.maximum;
      }
    }

    ::close(fd);
    if (debug) {
      qInfo("evdev: candidate %s (%s) absX=[%d,%d] absY=[%d,%d]", qPrintable(info.path),
            qPrintable(info.name), info.absXMin, info.absXMax, info.absYMin, info.absYMax);
    }
    out.push_back(info);
  }
  ::closedir(dir);
  if (out.isEmpty() && denied > 0) {
    qWarning("evdev: %d /dev/input/event* node(s) not readable — add user to `input` group "
             "and re-login (or run scripts/evdev_probe.py)",
             denied);
  }
  return out;
}

bool EvdevTrackpadSource::openDevice(const DeviceInfo& info) {
  fd_ = ::open(info.path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
  if (fd_ < 0) {
    qWarning("evdev: cannot open %s (add user to `input` group?)", qPrintable(info.path));
    return false;
  }
  // Exclusive grab freezes the compositor's pointer. That is nice once absolute
  // contacts are flowing, but if anything goes wrong the pad feels completely dead
  // (no cursor fallback either). Default to sharing the event stream; opt in with
  // HANDWRITING_EVDEV_GRAB=1 when you want the pointer locked.
  const bool wantGrab = qEnvironmentVariableIntValue("HANDWRITING_EVDEV_GRAB") > 0;
  if (wantGrab) {
    if (ioctl(fd_, EVIOCGRAB, 1) < 0) {
      qWarning("evdev: EVIOCGRAB failed on %s (continuing without grab)", qPrintable(info.path));
      reportProblem(tr("Could not take exclusive control of the touchpad (%1); the pointer will "
                       "move while you write.")
                        .arg(info.name));
    } else {
      qInfo("evdev: grabbed %s (HANDWRITING_EVDEV_GRAB=1)", qPrintable(info.path));
    }
  } else {
    qInfo("evdev: reading %s without grab (set HANDWRITING_EVDEV_GRAB=1 to lock the pointer)",
          qPrintable(info.path));
  }
  MtSlotTracker::Config cfg;
  cfg.absXMin = info.absXMin;
  cfg.absXMax = info.absXMax;
  cfg.absYMin = info.absYMin;
  cfg.absYMax = info.absYMax;
  cfg.slotCount = info.slotCount;
  cfg.hasToolType = info.hasToolType;
  cfg.hasPressure = info.hasPressure;
  cfg.pressureMin = info.pressureMin;
  cfg.pressureMax = info.pressureMax;
  // Full-pad mapping: use the device abs range with a small edge inset. Opt in to
  // post-lift range learning only if the pad's abs window is wildly oversized.
  bool edgeOk = false;
  const qreal edge = qEnvironmentVariable("HANDWRITING_PAD_EDGE").toDouble(&edgeOk);
  cfg.edgeInset = edgeOk ? qBound<qreal>(0.0, edge, 0.2) : 0.04;
  cfg.learnRange = qEnvironmentVariableIntValue("HANDWRITING_LEARN_RANGE") > 0;
  tracker_.configure(cfg);
  hasBtnTouch_ = info.hasBtnTouch;
  hasBtnToolFinger_ = info.hasBtnToolFinger;
  btnTouchDown_ = false;
  btnToolFingerDown_ = false;
  sawBtnTouchEvent_ = false;
  sawBtnToolFingerEvent_ = false;

  // Prefer physical aspect (device units / resolution) when both axes report it.
  qreal aspect = 0.0;
  if (info.resX > 0 && info.resY > 0) {
    const qreal wMm = static_cast<qreal>(info.absXMax - info.absXMin) / info.resX;
    const qreal hMm = static_cast<qreal>(info.absYMax - info.absYMin) / info.resY;
    if (hMm > 0.0) {
      aspect = wMm / hMm;
    }
  }
  if (aspect <= 0.0) {
    const qreal w = static_cast<qreal>(info.absXMax - info.absXMin);
    const qreal h = static_cast<qreal>(info.absYMax - info.absYMin);
    if (h > 0.0) {
      aspect = w / h;
    }
  }
  if (aspect > 0.0) {
    reportPadAspect(aspect);
  }

  loadInitialSlots();

  qInfo("evdev: using %s (%s) absX=[%d,%d] absY=[%d,%d] res=%d/%d aspect=%.3f "
        "edgeInset=%.3f learnRange=%d slots=%d",
        qPrintable(info.path), qPrintable(info.name), info.absXMin, info.absXMax, info.absYMin,
        info.absYMax, info.resX, info.resY, aspect, cfg.edgeInset, int(cfg.learnRange),
        info.slotCount);
  return true;
}

void EvdevTrackpadSource::loadInitialSlots() {
  const int n = tracker_.slotCount();
  if (fd_ < 0 || n <= 0) {
    return;
  }
  // EVIOCGMTSLOTS: __u32 code followed by one __s32 value per slot.
  QByteArray buf(static_cast<int>(sizeof(__u32) + static_cast<size_t>(n) * sizeof(__s32)),
                 Qt::Uninitialized);
  auto* req = reinterpret_cast<__u32*>(buf.data());

  auto readCode = [&](quint16 code) {
    *req = static_cast<__u32>(code);
    if (ioctl(fd_, EVIOCGMTSLOTS(buf.size()), buf.data()) < 0) {
      return;
    }
    const __s32* values = reinterpret_cast<__s32*>(buf.data() + sizeof(__u32));
    for (int i = 0; i < n; ++i) {
      tracker_.setSlotValue(i, code, values[i]);
    }
  };

  readCode(MtSlotTracker::kAbsMtTrackingId);
  readCode(MtSlotTracker::kAbsMtPositionX);
  readCode(MtSlotTracker::kAbsMtPositionY);
  if (chosen_.hasToolType) {
    readCode(MtSlotTracker::kAbsMtToolType);
  }
  if (chosen_.hasPressure) {
    readCode(MtSlotTracker::kAbsMtPressure);
  }
}

void EvdevTrackpadSource::resyncAfterDrop() {
  qWarning("evdev: SYN_DROPPED — resyncing slots");
  loadInitialSlots();
  processContacts(tracker_.contacts());
}

bool EvdevTrackpadSource::startBackend() {
  if (!isAvailable()) {
    return false;
  }
  if (fd_ >= 0) {
    return true;
  }
  if (!openDevice(chosen_)) {
    // Leave probeOk_/available_ alone so the next hold can retry (e.g. after
    // joining the `input` group or unplugging another grabber).
    reportProblem(tr("Cannot open the touchpad %1 (is your user in the `input` group?); "
                     "using cursor fallback.").arg(chosen_.path));
    return false;
  }
  notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
  connect(notifier_, &QSocketNotifier::activated, this, [this](QSocketDescriptor) { onReadable(); });
  return true;
}

void EvdevTrackpadSource::stopBackend() {
  if (notifier_ != nullptr) {
    delete notifier_;
    notifier_ = nullptr;
  }
  if (fd_ >= 0) {
    ioctl(fd_, EVIOCGRAB, 0);
    ::close(fd_);
    fd_ = -1;
  }
  tracker_.reset();
}

void EvdevTrackpadSource::deviceLost() {
  qWarning("evdev: device lost");
  processContacts({});  // end any stroke in progress
  if (notifier_ != nullptr) {
    notifier_->setEnabled(false);
    notifier_->deleteLater();
    notifier_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  tracker_.reset();
  // Force a re-probe the next time the key is held (the pad may be back, or another one).
  probed_ = false;
  probeOk_ = false;
  reportProblem(tr("The touchpad disappeared; using cursor fallback."));
}

void EvdevTrackpadSource::onReadable() {
  if (fd_ < 0) {
    return;
  }
  input_event ev = {};
  while (true) {
    const ssize_t n = ::read(fd_, &ev, sizeof(ev));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      qWarning("evdev: read error %d", errno);
      deviceLost();  // ENODEV etc.: otherwise the notifier would fire forever
      return;
    }
    if (n != static_cast<ssize_t>(sizeof(ev))) {
      break;
    }

    if (ev.type == EV_KEY) {
      if (ev.code == BTN_TOUCH) {
        sawBtnTouchEvent_ = true;
        btnTouchDown_ = ev.value != 0;
      } else if (ev.code == BTN_TOOL_FINGER) {
        sawBtnToolFingerEvent_ = true;
        btnToolFingerDown_ = ev.value != 0;
      }
    }

    // SYN_DROPPED handling and slot bookkeeping live in the portable tracker.
    QVector<TrackpadContact> frame;
    switch (tracker_.feed(ev.type, ev.code, ev.value, &frame)) {
      case MtSlotTracker::Result::Frame: {
        // Prefer explicit finger-up keys when the pad keeps a stale tracking id.
        // Only after we have seen the key at least once (initial state is unknown).
        const bool keySaysUp =
            (hasBtnTouch_ && sawBtnTouchEvent_ && !btnTouchDown_) ||
            (hasBtnToolFinger_ && sawBtnToolFingerEvent_ && !btnToolFingerDown_ &&
             !btnTouchDown_);
        if (keySaysUp) {
          frame.clear();
        }
        if (frame.isEmpty()) {
          // Freeze the coordinate system used during the next stroke.
          tracker_.commitLearnedRange();
        }
        static const bool debug = qEnvironmentVariableIsSet("HANDWRITING_DEBUG_TOUCH");
        if (debug) {
          qInfo("evdev frame contacts=%d btnTouch=%d toolFinger=%d", frame.size(),
                int(btnTouchDown_), int(btnToolFingerDown_));
          for (const TrackpadContact& c : frame) {
            qInfo("  id=%d norm=(%.3f, %.3f)", c.identifier, c.normalized.x(),
                  c.normalized.y());
          }
        }
        processContacts(frame);
        break;
      }
      case MtSlotTracker::Result::NeedsResync:
        resyncAfterDrop();
        break;
      case MtSlotTracker::Result::None:
        break;
    }
  }
}
