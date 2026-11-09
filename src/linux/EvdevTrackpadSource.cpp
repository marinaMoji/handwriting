#include "EvdevTrackpadSource.h"

#include <QByteArray>
#include <QDir>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

bool testBit(int bit, const unsigned long* array) {
  return (array[bit / (8 * sizeof(unsigned long))] >> (bit % (8 * sizeof(unsigned long)))) & 1UL;
}

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
  const QDir dir(QStringLiteral("/dev/input"));
  const QStringList nodes = dir.entryList(QStringList{QStringLiteral("event*")}, QDir::System);
  for (const QString& node : nodes) {
    const QString path = dir.filePath(node);
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
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
    if (!hasMt || !hasSlot || !looksLikePad || isDirect) {
      ::close(fd);
      continue;
    }

    DeviceInfo info;
    info.path = path;
    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
      info.name = QString::fromLocal8Bit(name);
    } else {
      info.name = node;
    }

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
    if (testBit(ABS_MT_PRESSURE, absBits)) {
      input_absinfo absP = {};
      if (ioctl(fd, EVIOCGABS(ABS_MT_PRESSURE), &absP) >= 0) {
        info.hasPressure = true;
        info.pressureMin = absP.minimum;
        info.pressureMax = absP.maximum;
      }
    }

    ::close(fd);
    out.push_back(info);
  }
  return out;
}

bool EvdevTrackpadSource::openDevice(const DeviceInfo& info) {
  fd_ = ::open(info.path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
  if (fd_ < 0) {
    qWarning("evdev: cannot open %s (add user to `input` group?)", qPrintable(info.path));
    return false;
  }
  if (ioctl(fd_, EVIOCGRAB, 1) < 0) {
    qWarning("evdev: EVIOCGRAB failed on %s (continuing without grab)", qPrintable(info.path));
    reportProblem(tr("Could not take exclusive control of the touchpad (%1); the pointer will "
                     "move while you write.").arg(info.name));
  }
  slots_.fill(SlotState{}, info.slotCount);
  currentSlot_ = 0;
  hasObserved_ = false;
  obsXMin_ = obsXMax_ = obsYMin_ = obsYMax_ = 0;

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

  qInfo("evdev: using %s (%s) absX=[%d,%d] absY=[%d,%d] slots=%d", qPrintable(info.path),
        qPrintable(info.name), info.absXMin, info.absXMax, info.absYMin, info.absYMax,
        info.slotCount);
  return true;
}

void EvdevTrackpadSource::loadInitialSlots() {
  if (fd_ < 0 || slots_.isEmpty()) {
    return;
  }
  // EVIOCGMTSLOTS: __u32 code followed by one __s32 value per slot.
  const int n = slots_.size();
  QByteArray buf(static_cast<int>(sizeof(__u32) + static_cast<size_t>(n) * sizeof(__s32)),
                 Qt::Uninitialized);
  auto* req = reinterpret_cast<__u32*>(buf.data());

  auto readCode = [&](int code, auto apply) {
    *req = static_cast<__u32>(code);
    if (ioctl(fd_, EVIOCGMTSLOTS(buf.size()), buf.data()) < 0) {
      return;
    }
    const __s32* values = reinterpret_cast<__s32*>(buf.data() + sizeof(__u32));
    for (int i = 0; i < n; ++i) {
      apply(slots_[i], values[i]);
    }
  };

  readCode(ABS_MT_TRACKING_ID, [](SlotState& s, __s32 v) {
    s.trackingId = static_cast<int>(v);
    s.seen = true;
  });
  readCode(ABS_MT_POSITION_X, [](SlotState& s, __s32 v) { s.x = static_cast<int>(v); });
  readCode(ABS_MT_POSITION_Y, [](SlotState& s, __s32 v) { s.y = static_cast<int>(v); });
  if (chosen_.hasToolType) {
    readCode(ABS_MT_TOOL_TYPE, [](SlotState& s, __s32 v) { s.toolType = static_cast<int>(v); });
  }
  if (chosen_.hasPressure) {
    readCode(ABS_MT_PRESSURE, [](SlotState& s, __s32 v) { s.pressure = static_cast<int>(v); });
  }
}

void EvdevTrackpadSource::resyncAfterDrop() {
  qWarning("evdev: SYN_DROPPED — resyncing slots");
  loadInitialSlots();
  handleSynReport();
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
  slots_.clear();
  currentSlot_ = 0;
  dropping_ = false;
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
  slots_.clear();
  dropping_ = false;
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

    // Kernel rule: after SYN_DROPPED discard everything up to the next SYN_REPORT, then
    // re-read the full device state.
    if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
      dropping_ = true;
      continue;
    }
    if (dropping_) {
      if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
        dropping_ = false;
        resyncAfterDrop();
      }
      continue;
    }

    if (ev.type == EV_ABS) {
      if (ev.code == ABS_MT_SLOT) {
        currentSlot_ = qBound(0, static_cast<int>(ev.value), slots_.size() - 1);
        continue;
      }
      if (slots_.isEmpty()) {
        continue;
      }
      SlotState& slot = slots_[currentSlot_];
      switch (ev.code) {
        case ABS_MT_TRACKING_ID:
          slot.trackingId = static_cast<int>(ev.value);
          slot.seen = true;
          break;
        case ABS_MT_POSITION_X:
          slot.x = static_cast<int>(ev.value);
          slot.seen = true;
          break;
        case ABS_MT_POSITION_Y:
          slot.y = static_cast<int>(ev.value);
          slot.seen = true;
          break;
        case ABS_MT_TOOL_TYPE:
          slot.toolType = static_cast<int>(ev.value);
          break;
        case ABS_MT_PRESSURE:
          slot.pressure = static_cast<int>(ev.value);
          break;
        default:
          break;
      }
    } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
      handleSynReport();
    }
  }
}

void EvdevTrackpadSource::noteObservedSample(int x, int y) {
  if (!hasObserved_) {
    obsXMin_ = obsXMax_ = x;
    obsYMin_ = obsYMax_ = y;
    hasObserved_ = true;
    return;
  }
  obsXMin_ = qMin(obsXMin_, x);
  obsXMax_ = qMax(obsXMax_, x);
  obsYMin_ = qMin(obsYMin_, y);
  obsYMax_ = qMax(obsYMax_, y);
}

void EvdevTrackpadSource::effectiveAbsRange(int* xMin, int* xMax, int* yMin, int* yMax) const {
  *xMin = chosen_.absXMin;
  *xMax = chosen_.absXMax;
  *yMin = chosen_.absYMin;
  *yMax = chosen_.absYMax;
  if (!hasObserved_) {
    return;
  }
  const int absSpanX = qMax(1, chosen_.absXMax - chosen_.absXMin);
  const int absSpanY = qMax(1, chosen_.absYMax - chosen_.absYMin);
  const int obsSpanX = obsXMax_ - obsXMin_;
  const int obsSpanY = obsYMax_ - obsYMin_;
  // Wait until the finger has covered a meaningful fraction of the advertised
  // range so a single corner tap does not shrink the map to a point.
  if (obsSpanX < absSpanX / 5 || obsSpanY < absSpanY / 5) {
    return;
  }
  // Small padding so the finger can still reach the last millimetre of glass.
  const int padX = qMax(1, obsSpanX / 50);
  const int padY = qMax(1, obsSpanY / 50);
  *xMin = qMax(chosen_.absXMin, obsXMin_ - padX);
  *xMax = qMin(chosen_.absXMax, obsXMax_ + padX);
  *yMin = qMax(chosen_.absYMin, obsYMin_ - padY);
  *yMax = qMin(chosen_.absYMax, obsYMax_ + padY);
  if (*xMax <= *xMin || *yMax <= *yMin) {
    *xMin = chosen_.absXMin;
    *xMax = chosen_.absXMax;
    *yMin = chosen_.absYMin;
    *yMax = chosen_.absYMax;
  }
}

void EvdevTrackpadSource::handleSynReport() {
  QVector<TrackpadContact> contacts;
  for (const SlotState& slot : slots_) {
    if (slot.trackingId < 0) {
      continue;
    }
#ifdef MT_TOOL_PALM
    if (chosen_.hasToolType && slot.toolType == MT_TOOL_PALM) {
      continue;
    }
#endif
    // Skip near-zero pressure contacts when the device reports pressure.
    if (chosen_.hasPressure && slot.pressure >= 0) {
      const int span = qMax(1, chosen_.pressureMax - chosen_.pressureMin);
      const qreal norm = static_cast<qreal>(slot.pressure - chosen_.pressureMin) / span;
      if (norm < 0.05) {
        continue;
      }
    }
    noteObservedSample(slot.x, slot.y);
    TrackpadContact c;
    c.identifier = slot.trackingId;
    int xMin = 0, xMax = 1, yMin = 0, yMax = 1;
    effectiveAbsRange(&xMin, &xMax, &yMin, &yMax);
    const qreal xSpan = qMax(1, xMax - xMin);
    const qreal ySpan = qMax(1, yMax - yMin);
    const qreal nx = (slot.x - xMin) / xSpan;
    const qreal ny = (slot.y - yMin) / ySpan;
    c.normalized = QPointF(qBound<qreal>(0.0, nx, 1.0), qBound<qreal>(0.0, ny, 1.0));
    contacts.push_back(c);
  }
  processContacts(contacts);
}
