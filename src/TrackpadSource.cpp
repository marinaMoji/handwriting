#include "TrackpadSource.h"

TrackpadSource::TrackpadSource(QObject* parent) : QObject(parent) {
  qRegisterMetaType<QVector<TrackpadContact>>("QVector<TrackpadContact>");
}

void TrackpadSource::setActive(bool active) {
  if (active == active_) {
    return;
  }
  if (active) {
    if (!available_ || !startBackend()) {
      available_ = false;
      return;
    }
    receivedContact_ = false;
    touching_ = false;
    awaitingLift_ = false;
    activeFingerId_ = -1;
    active_ = true;
    return;
  }

  stopBackend();
  const bool wasTouching = touching_;
  touching_ = false;
  awaitingLift_ = false;
  activeFingerId_ = -1;
  receivedContact_ = false;
  active_ = false;
  if (wasTouching) {
    emit strokeEnded();
  }
}

void TrackpadSource::processContacts(const QVector<TrackpadContact>& contacts) {
  if (!active_) {
    return;
  }

  if (contacts.isEmpty()) {
    awaitingLift_ = false;
    if (touching_) {
      touching_ = false;
      activeFingerId_ = -1;
      emit strokeEnded();
    }
    return;
  }

  if (!receivedContact_) {
    receivedContact_ = true;
    emit firstContact();
  }
  if (awaitingLift_) {
    return;
  }

  const TrackpadContact* chosen = nullptr;
  if (touching_ && activeFingerId_ >= 0) {
    for (const TrackpadContact& c : contacts) {
      if (c.identifier == activeFingerId_) {
        chosen = &c;
        break;
      }
    }
  }
  if (chosen == nullptr) {
    if (touching_) {
      // Tracked finger lifted while another contact (likely a palm) remains:
      // end the stroke and ignore everything until the pad is fully clear.
      touching_ = false;
      activeFingerId_ = -1;
      awaitingLift_ = true;
      emit strokeEnded();
      return;
    }
    chosen = &contacts.first();
  }

  if (!touching_) {
    touching_ = true;
    activeFingerId_ = chosen->identifier;
    emit strokeBegan(chosen->normalized);
  } else {
    emit strokeMoved(chosen->normalized);
  }
}
