#include "ZinniaBackend.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>

#include "zinnia.h"

namespace {

QVector<Candidate> classifyOne(zinnia::Recognizer* recognizer,
                               const zinnia::Character& character,
                               const QString& source,
                               int topN) {
  QVector<Candidate> out;
  if (!recognizer) {
    return out;
  }
  zinnia::Result* result = recognizer->classify(character, static_cast<size_t>(topN));
  if (!result) {
    return out;
  }
  for (size_t i = 0; i < result->size(); ++i) {
    Candidate c;
    c.character = QString::fromUtf8(result->value(i));
    c.score = result->score(i);
    c.modelSource = source;
    c.modelRank = static_cast<int>(i) + 1;
    out.push_back(c);
  }
  delete result;
  return out;
}

void fillCharacter(zinnia::Character* character,
                   const QVector<Stroke>& strokes,
                   int canvasWidth,
                   int canvasHeight) {
  character->clear();
  character->set_width(static_cast<size_t>(canvasWidth));
  character->set_height(static_cast<size_t>(canvasHeight));
  for (int s = 0; s < strokes.size(); ++s) {
    const Stroke& stroke = strokes[s];
    for (const StrokePoint& p : stroke) {
      character->add(static_cast<size_t>(s), p.x, p.y);
    }
  }
}

QVector<Candidate> mergeBoth(const QVector<Candidate>& a,
                             const QVector<Candidate>& b,
                             int topN) {
  QVector<Candidate> merged;
  QSet<QString> seen;
  // Interleave by original model rank so neither model dominates by score scale.
  const int maxRank = qMax(a.size(), b.size());
  for (int rank = 1; rank <= maxRank && merged.size() < topN; ++rank) {
    for (const Candidate& c : a) {
      if (c.modelRank == rank && !seen.contains(c.character)) {
        seen.insert(c.character);
        merged.push_back(c);
        break;
      }
    }
    if (merged.size() >= topN) {
      break;
    }
    for (const Candidate& c : b) {
      if (c.modelRank == rank && !seen.contains(c.character)) {
        seen.insert(c.character);
        merged.push_back(c);
        break;
      }
    }
  }
  return merged;
}

}  // namespace

struct ZinniaBackend::Impl {
  zinnia::Recognizer* japanese = nullptr;
  zinnia::Recognizer* traditional = nullptr;
  zinnia::Recognizer* japaneseAlt = nullptr;

  ~Impl() {
    delete japaneseAlt;
    delete japanese;
    delete traditional;
  }
};

ZinniaBackend::ZinniaBackend() : impl_(std::make_unique<Impl>()) {}

ZinniaBackend::~ZinniaBackend() = default;

bool ZinniaBackend::loadModels(const QString& modelDir, QString* errorOut) {
  std::lock_guard<std::mutex> lock(mutex_);

  const QString jaPath = QDir(modelDir).filePath(QStringLiteral("handwriting-ja.model"));
  const QString twPath = QDir(modelDir).filePath(QStringLiteral("handwriting-zh_TW.model"));

  if (!QFileInfo::exists(jaPath)) {
    if (errorOut) {
      *errorOut = QStringLiteral("Missing Japanese model: %1").arg(jaPath);
    }
    return false;
  }
  if (!QFileInfo::exists(twPath)) {
    if (errorOut) {
      *errorOut = QStringLiteral("Missing Traditional Chinese model: %1").arg(twPath);
    }
    return false;
  }

  delete impl_->japanese;
  delete impl_->traditional;
  impl_->japanese = zinnia::Recognizer::create();
  impl_->traditional = zinnia::Recognizer::create();

  if (!impl_->japanese->open(jaPath.toUtf8().constData())) {
    if (errorOut) {
      *errorOut = QString::fromUtf8(impl_->japanese->what());
    }
    japaneseReady_ = false;
    traditionalReady_ = false;
    return false;
  }
  if (!impl_->traditional->open(twPath.toUtf8().constData())) {
    if (errorOut) {
      *errorOut = QString::fromUtf8(impl_->traditional->what());
    }
    japaneseReady_ = false;
    traditionalReady_ = false;
    return false;
  }

  japaneseModelPath_ = jaPath;
  traditionalModelPath_ = twPath;
  japaneseReady_ = true;
  traditionalReady_ = true;
  return true;
}

bool ZinniaBackend::loadAlternativeJapanese(const QString& path, QString* errorOut) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!QFileInfo::exists(path)) {
    if (errorOut) *errorOut = QStringLiteral("Missing alternative Japanese model: %1").arg(path);
    return false;
  }
  zinnia::Recognizer* r = zinnia::Recognizer::create();
  if (!r->open(path.toUtf8().constData())) {
    if (errorOut) *errorOut = QString::fromUtf8(r->what());
    delete r;
    return false;
  }
  delete impl_->japaneseAlt;
  impl_->japaneseAlt = r;
  alternativePath_ = path;
  return true;
}

void ZinniaBackend::useAlternativeJapanese(bool on) {
  std::lock_guard<std::mutex> lock(mutex_);
  useAlternative_ = on && impl_->japaneseAlt != nullptr;
}

RecognitionResult ZinniaBackend::recognize(const QVector<Stroke>& strokes,
                                           RecognitionMode mode,
                                           int canvasWidth,
                                           int canvasHeight,
                                           int topN) const {
  RecognitionResult out;
  out.strokeCount = strokes.size();

  if (strokes.isEmpty()) {
    out.error = QStringLiteral("No strokes");
    return out;
  }
  if (!japaneseReady_ || !traditionalReady_) {
    out.error = QStringLiteral("Models not loaded");
    return out;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  zinnia::Character* character = zinnia::Character::create();
  fillCharacter(character, strokes, canvasWidth, canvasHeight);

  QElapsedTimer timer;
  timer.start();

  QVector<Candidate> ja;
  QVector<Candidate> tw;
  if (mode == RecognitionMode::Japanese || mode == RecognitionMode::Both) {
    ja = classifyOne(useAlternative_ && impl_->japaneseAlt ? impl_->japaneseAlt : impl_->japanese,
                     *character, QStringLiteral("JA"), topN);
  }
  if (mode == RecognitionMode::TraditionalChinese || mode == RecognitionMode::Both) {
    tw = classifyOne(impl_->traditional, *character, QStringLiteral("TW"), topN);
  }

  out.latencyMs = timer.elapsed();
  delete character;

  if (mode == RecognitionMode::Japanese) {
    out.candidates = ja;
  } else if (mode == RecognitionMode::TraditionalChinese) {
    out.candidates = tw;
  } else {
    out.candidates = mergeBoth(ja, tw, topN);
  }

  if (out.candidates.isEmpty()) {
    out.error = QStringLiteral("No candidates returned");
  }
  return out;
}
