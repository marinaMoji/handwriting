#pragma once

#include "ZinniaBackend.h"

#include <QWidget>

class QListWidget;
class QLabel;

class CandidatePanel : public QWidget {
  Q_OBJECT

 public:
  explicit CandidatePanel(QWidget* parent = nullptr);

  void setResult(const RecognitionResult& result);
  void clear();
  QString selectedCharacter() const;

 signals:
  void selectionChanged(const QString& character);

 private:
  QListWidget* list_ = nullptr;
  QLabel* emptyLabel_ = nullptr;
};
