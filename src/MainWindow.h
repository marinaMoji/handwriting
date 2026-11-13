#pragma once

#include "InkNet.h"
#include "PracticeSession.h"
#include "ZinniaBackend.h"

#include <QMainWindow>
#include <QVector>

#include <memory>

struct Sample;
class DrawingCanvas;
class CandidatePanel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QShortcut;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  // `net` (optional, must outlive the window) enables hybrid Zinnia + InkNet recognition in
  // Both mode; `neuralNote` explains to the user why it is unavailable, if it is.
  explicit MainWindow(ZinniaBackend* backend, const ink::InkNet* net = nullptr,
                      const QString& neuralNote = QString(), QWidget* parent = nullptr);

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
  // Practice mode (see docs/sample-protocol.md): the app prompts, the writer writes, Return saves.
  void togglePractice(bool on);
  void startPractice();
  void practiceSaveNext();
  void practiceRedo();
  void practiceSkip();
  void practiceUndoLast();
  void editWriter();

 private:
  RecognitionMode currentMode() const;
  void updateDiagnostics(const RecognitionResult& result);
  void runRecognition();
  bool shouldHandleHoldKeyFromFocus() const;
  bool askWriter();  // dialog for writer, handedness, device; false if cancelled
  void refreshPractice();
  void fillMetadata(Sample* sample) const;

  ZinniaBackend* backend_ = nullptr;
  const ink::InkNet* net_ = nullptr;
  QString neuralNote_;
  QString lastEngine_;
  DrawingCanvas* canvas_ = nullptr;
  CandidatePanel* candidates_ = nullptr;
  QComboBox* modeCombo_ = nullptr;
  QCheckBox* altJaCheck_ = nullptr;
  QLabel* diagnostics_ = nullptr;
  QPushButton* copyButton_ = nullptr;
  QLineEdit* expectedEdit_ = nullptr;
  QPushButton* saveSampleButton_ = nullptr;
  // Who is writing (saved in the samples). A pseudonym, never a name.
  struct WriterProfile {
    QString writer;
    QString handedness;  // "left", "right" or ""
    QString device;      // "trackpad", "mouse", "stylus"
  };
  WriterProfile profile_;
  QString sessionId_;
  QVector<PracticeSet> practiceSets_;
  std::unique_ptr<practice::PracticeSession> practice_;
  QString practiceDir_;
  QWidget* practiceBar_ = nullptr;
  QComboBox* practiceSetCombo_ = nullptr;
  QLabel* practiceTarget_ = nullptr;
  QLabel* practiceProgress_ = nullptr;
  QCheckBox* practiceShowRecognition_ = nullptr;
  QPushButton* practiceButton_ = nullptr;
  QVector<QShortcut*> practiceShortcuts_;
  int recognitionGeneration_ = 0;
  RecognitionResult lastResult_{};
};
