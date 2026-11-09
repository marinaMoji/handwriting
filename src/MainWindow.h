#pragma once

#include "ZinniaBackend.h"

#include <QMainWindow>

class DrawingCanvas;
class CandidatePanel;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(ZinniaBackend* backend, QWidget* parent = nullptr);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;

 private slots:
  void onStrokesChanged();
  void copySelected();
  void clearAll();
  void undoStroke();
  void saveSample();
  void updateSaveEnabled();
  void onHoldToWriteChanged(bool active);
  void onApplicationStateChanged(Qt::ApplicationState state);

 private:
  RecognitionMode currentMode() const;
  void updateDiagnostics(const RecognitionResult& result);
  void runRecognition();
  bool shouldHandleHoldKeyFromFocus() const;

  ZinniaBackend* backend_ = nullptr;
  DrawingCanvas* canvas_ = nullptr;
  CandidatePanel* candidates_ = nullptr;
  QComboBox* modeCombo_ = nullptr;
  QLabel* diagnostics_ = nullptr;
  QPushButton* copyButton_ = nullptr;
  QLineEdit* expectedEdit_ = nullptr;
  QPushButton* saveSampleButton_ = nullptr;
  int recognitionGeneration_ = 0;
  RecognitionResult lastResult_{};
};
