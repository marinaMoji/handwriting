#import "TrackpadStrokeCapture.h"

#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import <dlfcn.h>

#include <QMetaObject>
#include <QMetaType>
#include <atomic>

namespace {

struct MTPoint {
  float x;
  float y;
};

// Community-documented MultitouchSupport contact layout (~96 bytes with padding).
// `normalized` is position + velocity (4 floats), not position alone.
struct MTTouch {
  int frame;
  double timestamp;
  int identifier;
  int state;
  int finger;
  int hand;
  MTPoint normalized;          // position in [0, 1], origin bottom-left
  MTPoint normalizedVelocity;
  float size;
  int unknown0;
  float angle;
  float majorAxis;
  float minorAxis;
  MTPoint mmPosition;  // physical position in mm
  MTPoint mmVelocity;
  int unknown2[2];
  float density;
};

static_assert(sizeof(MTTouch) == 96, "MTTouch must be 96 bytes to match MultitouchSupport");

enum {
  MTTouchStateNotTracking = 0,
  MTTouchStateStartInRange = 1,
  MTTouchStateHoverInRange = 2,
  MTTouchStateMakeTouch = 3,
  MTTouchStateTouching = 4,
  MTTouchStateBreakTouch = 5,
  MTTouchStateLingerInRange = 6,
  MTTouchStateOutOfRange = 7
};

using MTDeviceRef = void*;
using MTContactCallback =
    int (*)(int device, MTTouch* touches, int numTouches, double timestamp, int frame);

using FnMTDeviceCreateDefault = MTDeviceRef (*)();
using FnMTDeviceCreateList = CFMutableArrayRef (*)();
using FnMTRegisterContactFrameCallback = void (*)(MTDeviceRef, MTContactCallback);
using FnMTUnregisterContactFrameCallback = void (*)(MTDeviceRef, MTContactCallback);
using FnMTDeviceStart = void (*)(MTDeviceRef, int);
using FnMTDeviceStop = void (*)(MTDeviceRef);

FnMTDeviceCreateDefault gCreateDefault = nullptr;
FnMTDeviceCreateList gCreateList = nullptr;
FnMTRegisterContactFrameCallback gRegister = nullptr;
FnMTUnregisterContactFrameCallback gUnregister = nullptr;
FnMTDeviceStart gStart = nullptr;
FnMTDeviceStop gStop = nullptr;

std::atomic<TrackpadStrokeCapture*> gActiveCapture{nullptr};
std::atomic<quint64> gActiveGeneration{0};

bool loadMultitouchSymbols() {
  static bool attempted = false;
  static bool ok = false;
  if (attempted) {
    return ok;
  }
  attempted = true;

  NSBundle* bundle = [NSBundle
      bundleWithPath:@"/System/Library/PrivateFrameworks/MultitouchSupport.framework"];
  if (bundle == nil || ![bundle load]) {
    dlopen("/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport",
           RTLD_LAZY);
  }

  gCreateDefault = (FnMTDeviceCreateDefault)dlsym(RTLD_DEFAULT, "MTDeviceCreateDefault");
  gCreateList = (FnMTDeviceCreateList)dlsym(RTLD_DEFAULT, "MTDeviceCreateList");
  gRegister =
      (FnMTRegisterContactFrameCallback)dlsym(RTLD_DEFAULT, "MTRegisterContactFrameCallback");
  gUnregister = (FnMTUnregisterContactFrameCallback)dlsym(RTLD_DEFAULT,
                                                          "MTUnregisterContactFrameCallback");
  gStart = (FnMTDeviceStart)dlsym(RTLD_DEFAULT, "MTDeviceStart");
  gStop = (FnMTDeviceStop)dlsym(RTLD_DEFAULT, "MTDeviceStop");

  ok = gRegister && gStart && (gCreateDefault || gCreateList);
  return ok;
}

int contactCallback(int device, MTTouch* touches, int numTouches, double timestamp, int frame) {
  (void)device;
  (void)timestamp;
  (void)frame;

  TrackpadStrokeCapture* capture = gActiveCapture.load(std::memory_order_acquire);
  if (capture == nullptr) {
    return 0;
  }
  const quint64 generation = gActiveGeneration.load(std::memory_order_acquire);

  QVector<TrackpadContact> contacts;
  if (touches != nullptr) {
    contacts.reserve(numTouches);
    for (int i = 0; i < numTouches; ++i) {
      const MTTouch& t = touches[i];
      if (t.state != MTTouchStateTouching && t.state != MTTouchStateMakeTouch) {
        continue;
      }
      TrackpadContact c;
      c.identifier = t.identifier;
      // normalized position: bottom-left → Qt top-left.
      c.normalized = QPointF(t.normalized.x, 1.0 - t.normalized.y);
      contacts.push_back(c);
    }
  }

  // Queued to the capture's thread; generation discards events after deactivate/destroy.
  QMetaObject::invokeMethod(capture, "onContacts", Qt::QueuedConnection,
                            Q_ARG(quint64, generation),
                            Q_ARG(QVector<TrackpadContact>, contacts));
  return 0;
}

}  // namespace

TrackpadStrokeCapture::TrackpadStrokeCapture(QObject* parent) : TrackpadSource(parent) {
  available_ = loadMultitouchSymbols();
}

TrackpadStrokeCapture::~TrackpadStrokeCapture() {
  setActive(false);
  stopDevices();
}

void TrackpadStrokeCapture::ensureDevicesStarted() {
  if (devicesStarted_ || !available_) {
    return;
  }

  if (gCreateList) {
    CFMutableArrayRef list = gCreateList();  // returns +1 create ref
    if (list != nullptr) {
      deviceList_ = list;  // take ownership of the create ref (no extra CFRetain)
      const CFIndex count = CFArrayGetCount(list);
      for (CFIndex i = 0; i < count; ++i) {
        MTDeviceRef device = (MTDeviceRef)CFArrayGetValueAtIndex(list, i);
        if (device == nullptr) {
          continue;
        }
        gRegister(device, contactCallback);
        gStart(device, 0);
        devices_.push_back(device);
      }
    }
  }

  if (devices_.isEmpty() && gCreateDefault) {
    MTDeviceRef device = gCreateDefault();
    if (device != nullptr) {
      gRegister(device, contactCallback);
      gStart(device, 0);
      devices_.push_back(device);
    }
  }

  devicesStarted_ = !devices_.isEmpty();
  if (!devicesStarted_) {
    available_ = false;
  }
}

void TrackpadStrokeCapture::stopDevices() {
  for (void* device : devices_) {
    if (gUnregister) {
      gUnregister((MTDeviceRef)device, contactCallback);
    }
    if (gStop) {
      gStop((MTDeviceRef)device);
    }
  }
  devices_.clear();
  devicesStarted_ = false;
  if (deviceList_ != nullptr) {
    CFRelease((CFArrayRef)deviceList_);
    deviceList_ = nullptr;
  }
}

bool TrackpadStrokeCapture::startBackend() {
  ensureDevicesStarted();
  if (!available_) {
    return false;
  }
  // New generation invalidates any in-flight queued callbacks.
  generation_ += 1;
  gActiveGeneration.store(generation_, std::memory_order_release);
  gActiveCapture.store(this, std::memory_order_release);
  return true;
}

void TrackpadStrokeCapture::stopBackend() {
  // Clear the global pointer first so the MT thread stops targeting us, then bump the
  // generation so already-queued invokes are ignored.
  if (gActiveCapture.load(std::memory_order_relaxed) == this) {
    gActiveCapture.store(nullptr, std::memory_order_release);
  }
  ++generation_;
  gActiveGeneration.store(generation_, std::memory_order_release);
}

void TrackpadStrokeCapture::onContacts(quint64 generation,
                                       const QVector<TrackpadContact>& contacts) {
  if (generation != generation_) {
    return;
  }
  processContacts(contacts);
}
