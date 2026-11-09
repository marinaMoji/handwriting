#include "TrackpadSource.h"

TrackpadSource::TrackpadSource(QObject* parent) : QObject(parent) {
  qRegisterMetaType<QVector<TrackpadContact>>("QVector<TrackpadContact>");
}

void TrackpadSource::setActive(bool active) {
  if (active == active_) {
    return;
  }
  if (active) {
    // A failed start is transient (permissions, device busy); keep probing available_
    // so the UI can retry when the user holds the key again.
    problemSinceStart_ = false;
    if (!available_ || !startBackend()) {
      if (!problemSinceStart_) {
        emit problem(tr("Trackpad input (%1) is not available; using cursor fallback.").arg(name()));
      }
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

void TrackpadSource::reportPadAspect(qreal aspect) {
  if (!(aspect > 0.2 && aspect < 5.0) || qAbs(aspect - reportedAspect_) < 0.005) {
    return;
  }
  reportedAspect_ = aspect;
  emit padAspectChanged(aspect);
}

void TrackpadSource::reportProblem(const QString& message) {
  problemSinceStart_ = true;
  emit problem(message);
}
