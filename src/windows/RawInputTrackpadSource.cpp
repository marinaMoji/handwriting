#include "RawInputTrackpadSource.h"

#ifdef Q_OS_WIN

#include <QGuiApplication>
#include <QHash>
#include <QWidget>
#include <QWindow>

#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <hidusage.h>
#include <hidpi.h>
#include <hidsdi.h>

namespace {

constexpr USAGE kPageDigitizer = 0x0D;
constexpr USAGE kPageGenericDesktop = 0x01;
constexpr USAGE kUsageTouchPad = 0x05;
constexpr USAGE kUsageContactId = 0x51;
constexpr USAGE kUsageTipSwitch = 0x42;
constexpr USAGE kUsageContactCount = 0x54;
constexpr USAGE kUsageX = 0x30;
constexpr USAGE kUsageY = 0x31;

}  // namespace

RawInputTrackpadSource::RawInputTrackpadSource(QWidget* host, QObject* parent)
    : TrackpadSource(parent), host_(host) {
  available_ = isAvailable();
}

RawInputTrackpadSource::~RawInputTrackpadSource() {
  setActive(false);
}

bool RawInputTrackpadSource::isAvailable() const {
  if (probed_) {
    return probeOk_;
  }
  auto* self = const_cast<RawInputTrackpadSource*>(this);
  self->probed_ = true;
  UINT num = 0;
  GetRawInputDeviceList(nullptr, &num, sizeof(RAWINPUTDEVICELIST));
  if (num == 0) {
    self->probeOk_ = false;
    return false;
  }
  QVector<RAWINPUTDEVICELIST> list(static_cast<int>(num));
  if (GetRawInputDeviceList(list.data(), &num, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1)) {
    self->probeOk_ = false;
    return false;
  }
  bool found = false;
  for (UINT i = 0; i < num; ++i) {
    if (list[i].dwType != RIM_TYPEHID) {
      continue;
    }
    RID_DEVICE_INFO info = {};
    info.cbSize = sizeof(info);
    UINT size = sizeof(info);
    if (GetRawInputDeviceInfo(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) <= 0) {
      continue;
    }
    // Touchpad only — touchscreens already work via Qt/mouse.
    if (info.hid.usUsagePage == kPageDigitizer && info.hid.usUsage == kUsageTouchPad) {
      found = true;
      break;
    }
  }
  self->probeOk_ = found;
  self->available_ = found;
  return probeOk_;
}

bool RawInputTrackpadSource::registerDevices() {
  if (registered_) {
    return true;
  }
  RAWINPUTDEVICE rid = {};
  rid.usUsagePage = kPageDigitizer;
  rid.usUsage = kUsageTouchPad;
  rid.dwFlags = RIDEV_INPUTSINK;

  HWND hwnd = nullptr;
  if (host_ != nullptr) {
    host_->winId();
    if (host_->window() != nullptr && host_->window()->windowHandle() != nullptr) {
      hwnd = reinterpret_cast<HWND>(host_->window()->windowHandle()->winId());
    }
  }
  rid.hwndTarget = hwnd;

  if (!RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE))) {
    qWarning("rawinput: RegisterRawInputDevices failed (%lu)", GetLastError());
    return false;
  }
  qApp->installNativeEventFilter(this);
  registered_ = true;
  return true;
}

void RawInputTrackpadSource::unregisterDevices() {
  if (!registered_) {
    return;
  }
  qApp->removeNativeEventFilter(this);
  RAWINPUTDEVICE rid = {};
  rid.usUsagePage = kPageDigitizer;
  rid.usUsage = kUsageTouchPad;
  rid.dwFlags = RIDEV_REMOVE;
  RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE));
  registered_ = false;
  deviceCache_.clear();
  assembler_.reset();
}

bool RawInputTrackpadSource::startBackend() {
  if (!isAvailable()) {
    return false;
  }
  return registerDevices();
}

void RawInputTrackpadSource::stopBackend() {
  unregisterDevices();
}

bool RawInputTrackpadSource::nativeEventFilter(const QByteArray& eventType, void* message,
                                              qintptr* result) {
  Q_UNUSED(result);
  // Only one Qt message path — avoid processing each WM_INPUT twice.
  if (eventType != "windows_generic_MSG") {
    return false;
  }
  auto* msg = static_cast<MSG*>(message);
  if (msg->message != WM_INPUT) {
    return false;
  }
  handleRawInput(reinterpret_cast<void*>(msg->lParam));
  return false;
}

RawInputTrackpadSource::DeviceCache* RawInputTrackpadSource::cacheForDevice(void* deviceHandle) {
  const quintptr key = reinterpret_cast<quintptr>(deviceHandle);
  auto it = deviceCache_.find(key);
  if (it != deviceCache_.end()) {
    return &it.value();
  }

  UINT prepSize = 0;
  GetRawInputDeviceInfo(deviceHandle, RIDI_PREPARSEDDATA, nullptr, &prepSize);
  if (prepSize == 0) {
    return nullptr;
  }
  DeviceCache cache;
  cache.preparsed.resize(static_cast<int>(prepSize));
  if (GetRawInputDeviceInfo(deviceHandle, RIDI_PREPARSEDDATA, cache.preparsed.data(), &prepSize) <=
      0) {
    return nullptr;
  }

  PHIDP_PREPARSED_DATA ppd = reinterpret_cast<PHIDP_PREPARSED_DATA>(cache.preparsed.data());
  HIDP_CAPS caps = {};
  if (HidP_GetCaps(ppd, &caps) != HIDP_STATUS_SUCCESS) {
    return nullptr;
  }

  USHORT valueCapLen = caps.NumberInputValueCaps;
  QVector<HIDP_VALUE_CAPS> valueCaps(valueCapLen);
  QHash<USHORT, ContactFields> byLink;
  if (valueCapLen > 0 &&
      HidP_GetValueCaps(HidP_Input, valueCaps.data(), &valueCapLen, ppd) == HIDP_STATUS_SUCCESS) {
    for (USHORT i = 0; i < valueCapLen; ++i) {
      const HIDP_VALUE_CAPS& vc = valueCaps[i];
      ContactFields& f = byLink[vc.LinkCollection];
      f.link = vc.LinkCollection;
      const USAGE usage = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
      if (vc.UsagePage == kPageDigitizer && usage == kUsageContactId) {
        f.hasId = true;
      }
      if ((vc.UsagePage == kPageGenericDesktop || vc.UsagePage == kPageDigitizer) &&
          usage == kUsageX) {
        f.hasX = true;
        f.xPage = vc.UsagePage;
        cache.logicalMaxX = vc.LogicalMax;
        if (vc.PhysicalMax > 0) {
          cache.physicalMaxX = vc.PhysicalMax;
          cache.hasPhysical = true;
        }
      }
      if ((vc.UsagePage == kPageGenericDesktop || vc.UsagePage == kPageDigitizer) &&
          usage == kUsageY) {
        f.hasY = true;
        f.yPage = vc.UsagePage;
        cache.logicalMaxY = vc.LogicalMax;
        if (vc.PhysicalMax > 0) {
          cache.physicalMaxY = vc.PhysicalMax;
          cache.hasPhysical = true;
        }
      }
    }
  }

  USHORT buttonCapLen = caps.NumberInputButtonCaps;
  QVector<HIDP_BUTTON_CAPS> buttonCaps(buttonCapLen);
  if (buttonCapLen > 0 &&
      HidP_GetButtonCaps(HidP_Input, buttonCaps.data(), &buttonCapLen, ppd) == HIDP_STATUS_SUCCESS) {
    for (USHORT i = 0; i < buttonCapLen; ++i) {
      const HIDP_BUTTON_CAPS& bc = buttonCaps[i];
      if (bc.UsagePage != kPageDigitizer) {
        continue;
      }
      ContactFields& f = byLink[bc.LinkCollection];
      f.link = bc.LinkCollection;
      const USAGE usage = bc.IsRange ? bc.Range.UsageMin : bc.NotRange.Usage;
      if (usage == kUsageTipSwitch ||
          (bc.IsRange && kUsageTipSwitch >= bc.Range.UsageMin &&
           kUsageTipSwitch <= bc.Range.UsageMax)) {
        f.hasTip = true;
      }
    }
  }

  QList<USHORT> links = byLink.keys();
  std::sort(links.begin(), links.end());
  for (USHORT link : links) {
    const ContactFields& f = byLink[link];
    if (f.hasX && f.hasY) {
      cache.collections.push_back(f);
    }
  }

  if (cache.hasPhysical && cache.physicalMaxY > 0) {
    reportPadAspect(static_cast<qreal>(cache.physicalMaxX) /
                    static_cast<qreal>(cache.physicalMaxY));
  } else if (cache.logicalMaxX > 0 && cache.logicalMaxY > 0) {
    // Last resort; logical units may not be isotropic.
    reportPadAspect(static_cast<qreal>(cache.logicalMaxX) / static_cast<qreal>(cache.logicalMaxY));
  }

  it = deviceCache_.insert(key, cache);
  return &it.value();
}

void RawInputTrackpadSource::handleRawInput(void* lParam) {
  if (!isActive()) {
    return;
  }
  UINT size = 0;
  GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size,
                  sizeof(RAWINPUTHEADER));
  if (size == 0) {
    return;
  }
  QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
  if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size,
                      sizeof(RAWINPUTHEADER)) != size) {
    return;
  }
  auto* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
  if (raw->header.dwType != RIM_TYPEHID) {
    return;
  }

  DeviceCache* cache = cacheForDevice(raw->header.hDevice);
  if (cache == nullptr || cache->collections.isEmpty()) {
    return;
  }
  PHIDP_PREPARSED_DATA ppd = reinterpret_cast<PHIDP_PREPARSED_DATA>(cache->preparsed.data());

  // dwSizeHid = size of each report; dwCount = number of reports in this packet.
  const ULONG reportSize = raw->data.hid.dwSizeHid;
  const ULONG reportCount = qMax(1UL, raw->data.hid.dwCount);
  const BYTE* reportBase = raw->data.hid.bRawData;

  for (ULONG r = 0; r < reportCount; ++r) {
    const BYTE* one = reportBase + r * reportSize;
    PCHAR reportPtr = reinterpret_cast<PCHAR>(const_cast<BYTE*>(one));

    PtpReport report;
    ULONG contactCount = 0;
    report.haveCount =
        HidP_GetUsageValue(HidP_Input, kPageDigitizer, 0, kUsageContactCount, &contactCount, ppd,
                           reportPtr, reportSize) == HIDP_STATUS_SUCCESS;
    report.contactCount = static_cast<int>(contactCount);

    const qreal maxX = qMax(1, cache->logicalMaxX);
    const qreal maxY = qMax(1, cache->logicalMaxY);
    bool compatible = true;  // false for other report IDs (buttons, feature echoes, ...)

    for (int ci = 0; ci < cache->collections.size(); ++ci) {
      const ContactFields& f = cache->collections[ci];

      ULONG x = 0;
      ULONG y = 0;
      const NTSTATUS sx =
          HidP_GetUsageValue(HidP_Input, f.xPage, f.link, kUsageX, &x, ppd, reportPtr, reportSize);
      const NTSTATUS sy =
          HidP_GetUsageValue(HidP_Input, f.yPage, f.link, kUsageY, &y, ppd, reportPtr, reportSize);
      if (sx != HIDP_STATUS_SUCCESS || sy != HIDP_STATUS_SUCCESS) {
        if (ci == 0) {
          compatible = false;  // not a touch report; ignore the whole report
          break;
        }
        continue;
      }

      // Tip Switch is a button usage (HidP_GetButtonCaps), not a value.
      bool tipDown = !f.hasTip;
      if (f.hasTip) {
        tipDown = false;
        USAGE usages[16] = {};
        ULONG usageLen = 16;
        if (HidP_GetUsages(HidP_Input, kPageDigitizer, f.link, usages, &usageLen, ppd, reportPtr,
                           reportSize) == HIDP_STATUS_SUCCESS) {
          for (ULONG u = 0; u < usageLen; ++u) {
            if (usages[u] == kUsageTipSwitch) {
              tipDown = true;
              break;
            }
          }
        }
      }

      ULONG contactId = static_cast<ULONG>(f.link);
      if (f.hasId) {
        HidP_GetUsageValue(HidP_Input, kPageDigitizer, f.link, kUsageContactId, &contactId, ppd,
                           reportPtr, reportSize);
      }

      PtpSlot slot;
      slot.tipDown = tipDown;
      slot.id = static_cast<int>(contactId);
      slot.x = qBound<qreal>(0.0, x / maxX, 1.0);
      slot.y = qBound<qreal>(0.0, y / maxY, 1.0);
      report.fingers.push_back(slot);
    }
    if (!compatible) {
      continue;
    }

    QVector<TrackpadContact> frame;
    if (assembler_.feed(report, &frame)) {
      processContacts(frame);
    }
  }
}

#else  // !Q_OS_WIN

RawInputTrackpadSource::RawInputTrackpadSource(QWidget* host, QObject* parent)
    : TrackpadSource(parent), host_(host) {
  available_ = false;
}

RawInputTrackpadSource::~RawInputTrackpadSource() = default;

bool RawInputTrackpadSource::isAvailable() const {
  return false;
}

bool RawInputTrackpadSource::startBackend() {
  return false;
}

void RawInputTrackpadSource::stopBackend() {}

bool RawInputTrackpadSource::nativeEventFilter(const QByteArray&, void*, qintptr*) {
  return false;
}

RawInputTrackpadSource::DeviceCache* RawInputTrackpadSource::cacheForDevice(void*) {
  return nullptr;
}

void RawInputTrackpadSource::handleRawInput(void*) {}

#endif
