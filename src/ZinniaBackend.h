#pragma once

#include <QString>
#include <QVector>
#include <memory>
#include <mutex>

struct StrokePoint {
  int x = 0;
  int y = 0;
};

using Stroke = QVector<StrokePoint>;

enum class RecognitionMode {
  Japanese,
  TraditionalChinese,
  Both,
};

struct Candidate {
  QString character;
  float score = 0.0f;
  QString modelSource;  // "JA" or "TW"
  int modelRank = 0;    // 1-based rank within that model
};

struct RecognitionResult {
  QVector<Candidate> candidates;
  qint64 latencyMs = 0;
  QString error;
  int strokeCount = 0;
};

class ZinniaBackend {
 public:
  ZinniaBackend();
  ~ZinniaBackend();

  ZinniaBackend(const ZinniaBackend&) = delete;
  ZinniaBackend& operator=(const ZinniaBackend&) = delete;

  // Load models from a directory containing handwriting-ja.model and
  // handwriting-zh_TW.model. Safe to call once at startup.
  bool loadModels(const QString& modelDir, QString* errorOut = nullptr);

  bool japaneseReady() const { return japaneseReady_; }
  bool traditionalReady() const { return traditionalReady_; }
  // The Japanese model recognition currently uses (the alternative one when switched on).
  QString japaneseModelPath() const { return useAlternative_ ? alternativePath_ : japaneseModelPath_; }
  QString defaultJapaneseModelPath() const { return japaneseModelPath_; }

  // An optional second Japanese model that can be switched in at run time (compare models
  // without restarting). Loaded after loadModels(); the default model stays the default.
  bool loadAlternativeJapanese(const QString& path, QString* errorOut = nullptr);
  bool hasAlternativeJapanese() const { return !alternativePath_.isEmpty(); }
  QString alternativeJapanesePath() const { return alternativePath_; }
  void useAlternativeJapanese(bool on);
  bool usingAlternativeJapanese() const { return useAlternative_; }
  QString traditionalModelPath() const { return traditionalModelPath_; }

  RecognitionResult recognize(const QVector<Stroke>& strokes,
                              RecognitionMode mode,
                              int canvasWidth = 300,
                              int canvasHeight = 300,
                              int topN = 10) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  mutable std::mutex mutex_;
  bool japaneseReady_ = false;
  bool traditionalReady_ = false;
  QString japaneseModelPath_;
  QString traditionalModelPath_;
  QString alternativePath_;
  bool useAlternative_ = false;
};
