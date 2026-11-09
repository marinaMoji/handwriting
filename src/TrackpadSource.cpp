#include "TrackpadSource.h"

#include <QtMath>

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
    hasLastNormalized_ = false;
    active_ = true;
    return;
  }

  stopBackend();
  const bool wasTouching = touching_;
  touching_ = false;
  awaitingLift_ = false;
  activeFingerId_ = -1;
  receivedContact_ = false;
  hasLastNormalized_ = false;
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
    hasLastNormalized_ = false;
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
      hasLastNormalized_ = false;
      awaitingLift_ = true;
      emit strokeEnded();
      return;
    }
    chosen = &contacts.first();
  }

  if (!touching_) {
    touching_ = true;
    activeFingerId_ = chosen->identifier;
    lastNormalized_ = chosen->normalized;
    hasLastNormalized_ = true;
    emit strokeBegan(chosen->normalized);
    return;
  }

  // Same contact id after a missed lift: the finger teleports. Split the stroke
  // so lift→reposition→draw does not connect with a long line.
  if (hasLastNormalized_) {
    const qreal dx = chosen->normalized.x() - lastNormalized_.x();
    const qreal dy = chosen->normalized.y() - lastNormalized_.y();
    if (dx * dx + dy * dy >= kLiftJumpNormalized * kLiftJumpNormalized) {
      emit strokeEnded();
      activeFingerId_ = chosen->identifier;
      lastNormalized_ = chosen->normalized;
      hasLastNormalized_ = true;
      touching_ = true;
      emit strokeBegan(chosen->normalized);
      return;
    }
  }

  lastNormalized_ = chosen->normalized;
  hasLastNormalized_ = true;
  emit strokeMoved(chosen->normalized);
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
