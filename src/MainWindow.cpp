#include "MainWindow.h"

#include "CandidatePanel.h"
#include "DrawingCanvas.h"
#include "HoldKey.h"
#include "Recognizer.h"
#include "SampleIO.h"
#include "StrokePreprocess.h"
#include "StrokeMapping.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QClipboard>
#include <QDateTime>
#include <QComboBox>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QtConcurrent>
#include <QVBoxLayout>
#include <QWidget>

#ifndef HANDWRITING_PRACTICE_DOCS_DIR
#define HANDWRITING_PRACTICE_DOCS_DIR "docs"
#endif
#ifndef HANDWRITING_DEFAULT_SAMPLES_DIR
#define HANDWRITING_DEFAULT_SAMPLES_DIR "samples"
#endif

MainWindow::MainWindow(ZinniaBackend* backend, const ink::InkNet* net, const QString& neuralNote,
                       QWidget* parent)
    : QMainWindow(parent), backend_(backend), net_(net), neuralNote_(neuralNote) {
  setWindowTitle(tr("marinaMoji handwriting prototype"));
  resize(820, 620);

  auto* central = new QWidget(this);
  setCentralWidget(central);
  auto* root = new QHBoxLayout(central);

  auto* leftColumn = new QWidget(central);
  auto* left = new QVBoxLayout(leftColumn);
  left->setContentsMargins(0, 0, 0, 0);
  left->setSpacing(8);

  QSettings settings;
  profile_.writer = settings.value(QStringLiteral("writer/name")).toString();
  profile_.handedness = settings.value(QStringLiteral("writer/handedness")).toString();
  profile_.device = settings.value(QStringLiteral("writer/device"), QStringLiteral("trackpad")).toString();
  sessionId_ = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));

  // Practice bar: which list, the character to write, progress.
  practiceBar_ = new QWidget(leftColumn);
  auto* practiceLayout = new QVBoxLayout(practiceBar_);
  practiceLayout->setContentsMargins(0, 0, 0, 0);
  auto* practiceTop = new QHBoxLayout();
  practiceSetCombo_ = new QComboBox(practiceBar_);
  practiceProgress_ = new QLabel(practiceBar_);
  practiceShowRecognition_ = new QCheckBox(tr("Show recognition"), practiceBar_);
  practiceShowRecognition_->setToolTip(
      tr("Off by default: seeing the result can change how you write. Samples are saved either way."));
  practiceTop->addWidget(new QLabel(tr("List:"), practiceBar_));
  practiceTop->addWidget(practiceSetCombo_, 1);
  practiceTop->addWidget(practiceProgress_);
  practiceTop->addWidget(practiceShowRecognition_);
  practiceLayout->addLayout(practiceTop);
  practiceTarget_ = new QLabel(practiceBar_);
  practiceTarget_->setAlignment(Qt::AlignCenter);
  QFont targetFont = practiceTarget_->font();
  targetFont.setPointSize(64);
  practiceTarget_->setFont(targetFont);
  practiceTarget_->setMinimumHeight(110);
  practiceLayout->addWidget(practiceTarget_);
  auto* practiceHint = new QLabel(
      tr("Return: save and next · Backspace: redo this one · →: skip · ←: delete the last saved"),
      practiceBar_);
  practiceHint->setAlignment(Qt::AlignCenter);
  practiceLayout->addWidget(practiceHint);
  practiceBar_->setVisible(false);
  left->addWidget(practiceBar_, /*stretch*/ 0);

  canvas_ = new DrawingCanvas(leftColumn);
  left->addWidget(canvas_, /*stretch*/ 1);

  auto* controlBar = new QWidget(leftColumn);
  controlBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  auto* controls = new QHBoxLayout(controlBar);
  controls->setContentsMargins(0, 0, 0, 0);

  modeCombo_ = new QComboBox(controlBar);
  modeCombo_->addItem(tr("Japanese"), static_cast<int>(RecognitionMode::Japanese));
  modeCombo_->addItem(tr("Traditional Chinese"),
                      static_cast<int>(RecognitionMode::TraditionalChinese));
  modeCombo_->addItem(tr("Both"), static_cast<int>(RecognitionMode::Both));
  modeCombo_->setCurrentIndex(2);

  expectedEdit_ = new QLineEdit(controlBar);
  expectedEdit_->setPlaceholderText(tr("Intended character"));
  expectedEdit_->setMaxLength(2);  // one character, possibly a surrogate pair
  expectedEdit_->setFixedWidth(130);
  saveSampleButton_ = new QPushButton(tr("Save sample"), controlBar);
  saveSampleButton_->setToolTip(tr("Save the raw strokes with the intended character for offline evaluation"));
  saveSampleButton_->setEnabled(false);

  auto* clearBtn = new QPushButton(tr("Clear"), controlBar);
  auto* undoBtn = new QPushButton(tr("Undo"), controlBar);
  copyButton_ = new QPushButton(tr("Copy"), controlBar);
  copyButton_->setEnabled(false);

  controls->addWidget(new QLabel(tr("Mode:"), controlBar));
  controls->addWidget(modeCombo_, 1);
  if (backend_->hasAlternativeJapanese()) {
    altJaCheck_ = new QCheckBox(tr("Alt. JA model"), controlBar);
    altJaCheck_->setToolTip(tr("Use %1 instead of %2 for Japanese")
                                .arg(QFileInfo(backend_->alternativeJapanesePath()).fileName(),
                                     QFileInfo(backend_->defaultJapaneseModelPath()).fileName()));
    controls->addWidget(altJaCheck_);
  }
  controls->addWidget(undoBtn);
  controls->addWidget(clearBtn);
  controls->addWidget(copyButton_);
  controls->addWidget(expectedEdit_);
  controls->addWidget(saveSampleButton_);
  practiceButton_ = new QPushButton(tr("Practice"), controlBar);
  practiceButton_->setCheckable(true);
  practiceButton_->setToolTip(tr("Prompted sample collection: write the character shown, Return saves"));
  auto* writerButton = new QPushButton(tr("Writer…"), controlBar);
  writerButton->setToolTip(tr("Who is writing: saved with every sample"));
  controls->addWidget(practiceButton_);
  controls->addWidget(writerButton);
  left->addWidget(controlBar, /*stretch*/ 0);

  diagnostics_ = new QLabel(leftColumn);
  diagnostics_->setWordWrap(true);
  diagnostics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  diagnostics_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  left->addWidget(diagnostics_, /*stretch*/ 0);

  candidates_ = new CandidatePanel(central);

  root->addWidget(leftColumn, 3);
  root->addWidget(candidates_, 2);

  connect(canvas_, &DrawingCanvas::strokesChanged, this, &MainWindow::onStrokesChanged);
  connect(canvas_, &DrawingCanvas::holdToWriteChanged, this, &MainWindow::onHoldToWriteChanged);
  connect(canvas_, &DrawingCanvas::trackpadProblem, this,
          [this](const QString& message) { statusBar()->showMessage(message, 10000); });
  connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::onStrokesChanged);
  if (altJaCheck_) {
    connect(altJaCheck_, &QCheckBox::toggled, this, [this](bool on) {
      backend_->useAlternativeJapanese(on);
      statusBar()->showMessage(tr("Japanese model: %1")
                                   .arg(QFileInfo(backend_->japaneseModelPath()).fileName()));
      onStrokesChanged();
    });
  }
  connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearAll);
  connect(saveSampleButton_, &QPushButton::clicked, this, &MainWindow::saveSample);
  connect(expectedEdit_, &QLineEdit::returnPressed, this, &MainWindow::saveSample);
  connect(expectedEdit_, &QLineEdit::textChanged, this, &MainWindow::updateSaveEnabled);
  connect(undoBtn, &QPushButton::clicked, this, &MainWindow::undoStroke);
  connect(writerButton, &QPushButton::clicked, this, &MainWindow::editWriter);
  connect(practiceButton_, &QPushButton::toggled, this, &MainWindow::togglePractice);
  connect(practiceSetCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
    startPractice();
    canvas_->setFocus(Qt::OtherFocusReason);
  });
  connect(practiceShowRecognition_, &QCheckBox::toggled, this, [this](bool) {
    canvas_->setFocus(Qt::OtherFocusReason);
    onStrokesChanged();
  });
  // Keys for the writing hand's partner: Return saves, Backspace redoes, arrows skip / go back.
  auto addShortcut = [this](const QKeySequence& seq, void (MainWindow::*slot)()) {
    auto* sc = new QShortcut(seq, this);
    sc->setContext(Qt::WindowShortcut);
    sc->setEnabled(false);
    connect(sc, &QShortcut::activated, this, slot);
    practiceShortcuts_.push_back(sc);
  };
  addShortcut(Qt::Key_Return, &MainWindow::practiceSaveNext);
  addShortcut(Qt::Key_Enter, &MainWindow::practiceSaveNext);
  addShortcut(Qt::Key_Backspace, &MainWindow::practiceRedo);
  addShortcut(Qt::Key_Right, &MainWindow::practiceSkip);
  addShortcut(Qt::Key_Left, &MainWindow::practiceUndoLast);
  connect(copyButton_, &QPushButton::clicked, this, &MainWindow::copySelected);
  connect(candidates_, &CandidatePanel::selectionChanged, this, [this](const QString& ch) {
    copyButton_->setEnabled(!ch.isEmpty());
  });
  connect(qApp, &QGuiApplication::applicationStateChanged, this,
          &MainWindow::onApplicationStateChanged);

  // App-wide filter for the hold key (see HoldKey.h); ignored while typing in text widgets.
  qApp->installEventFilter(this);
  canvas_->setFocus(Qt::OtherFocusReason);

  updateDiagnostics({});
  QString engine = net_ ? tr("hybrid (Zinnia + InkNet in Both mode)") : neuralNote_;
  if (net_ && !neuralNote_.isEmpty()) engine += QStringLiteral(" · ") + neuralNote_;
  if (backend_->hasAlternativeJapanese()) engine += tr(" · alt. JA model available");
  statusBar()->showMessage(
      tr("Click-drag or hold ` to write · JA: %1 · TW: %2 · %3")
          .arg(QFileInfo(backend_->japaneseModelPath()).fileName())
          .arg(QFileInfo(backend_->traditionalModelPath()).fileName())
          .arg(engine));
  if (!canvas_->backendNote().isEmpty()) {
    statusBar()->showMessage(canvas_->backendNote(), 15000);
  }
}

namespace {
// Default measured with handwriting_eval on synthetic jittery ink (docs/findings.md).
// Override with HANDWRITING_PREPROCESS (e.g. "none", "smooth:2", "resample:4+smooth:3").
const Recognizer::Options& recognizerOptions() {
  static const Recognizer::Options opts = []() {
    QString spec = qEnvironmentVariable("HANDWRITING_PREPROCESS",
                                        QString::fromLatin1(Recognizer::kDefaultPreprocessSpec));
    Recognizer::Options o;
    QString error;
    if (!Recognizer::optionsFromSpec(spec, &o, &error)) {
      qWarning("HANDWRITING_PREPROCESS: %s; using the default", qPrintable(error));
      o = Recognizer::defaultOptions();
      spec = QString::fromLatin1(Recognizer::kDefaultPreprocessSpec);
    }
    qInfo("stroke preprocessing: %s", qPrintable(spec));
    return o;
  }();
  return opts;
}
}  // namespace

RecognitionMode MainWindow::currentMode() const {
  return static_cast<RecognitionMode>(modeCombo_->currentData().toInt());
}

bool MainWindow::shouldHandleHoldKeyFromFocus() const {
  // Always handle the key while already holding, so release is not lost.
  if (canvas_->holdToWriteActive()) {
    return true;
  }
  QWidget* focus = QApplication::focusWidget();
  if (focus == nullptr) {
    return true;
  }
  // Only text-entry widgets keep the key (it types ` / ² there).
  return !(qobject_cast<QLineEdit*>(focus) || qobject_cast<QTextEdit*>(focus) ||
           qobject_cast<QAbstractSpinBox*>(focus) ||
           (qobject_cast<QComboBox*>(focus) &&
            qobject_cast<QComboBox*>(focus)->isEditable()));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  // While hold-to-write is active, pad presses/taps must not click buttons.
  // A click on the canvas itself means the user wants click-drag: end hold and
  // let the press through (also recovers if a hold-key release was lost).
  if (canvas_->holdToWriteActive()) {
    switch (event->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonDblClick: {
        for (QObject* o = watched; o != nullptr; o = o->parent()) {
          if (o == canvas_) {
            canvas_->endHoldToWrite();
            return false;
          }
        }
        return true;
      }
      case QEvent::MouseButtonRelease:
      case QEvent::ContextMenu:
        return true;
      default:
        break;
    }
  }
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    static const bool debugKeys = qEnvironmentVariableIsSet("HANDWRITING_DEBUG_KEYS");
    if (debugKeys) {
      qInfo("key %s qtKey=0x%x nativeVK=0x%x scan=0x%x text='%s' hold=%d",
            event->type() == QEvent::KeyPress ? "press" : "release", keyEvent->key(),
            keyEvent->nativeVirtualKey(), keyEvent->nativeScanCode(),
            qPrintable(keyEvent->text()), hold_key::isHoldToWriteKey(keyEvent));
    }
    if (hold_key::isHoldToWriteKey(keyEvent) && !keyEvent->isAutoRepeat() &&
        shouldHandleHoldKeyFromFocus()) {
      if (event->type() == QEvent::KeyPress) {
        canvas_->beginHoldToWrite();
      } else {
        canvas_->endHoldToWrite();
      }
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::WindowDeactivate) {
    canvas_->endHoldToWrite();
  }
  QMainWindow::changeEvent(event);
}

void MainWindow::onApplicationStateChanged(Qt::ApplicationState state) {
  if (state != Qt::ApplicationActive) {
    canvas_->endHoldToWrite();
  }
}

void MainWindow::onHoldToWriteChanged(bool active) {
  if (active) {
    statusBar()->showMessage(
        tr("Hold-to-write active — write on the trackpad; release ` when done"));
  } else {
    statusBar()->showMessage(tr("Click-drag or hold ` to write"));
  }
  updateDiagnostics(lastResult_);
}

void MainWindow::clearAll() {
  canvas_->clearStrokes();
  candidates_->clear();
  lastResult_ = {};
  updateDiagnostics(lastResult_);
}

void MainWindow::updateSaveEnabled() {
  saveSampleButton_->setEnabled(!canvas_->strokes().isEmpty() &&
                                !expectedEdit_->text().trimmed().isEmpty());
}

void MainWindow::saveSample() {
  const QString expected = expectedEdit_->text().trimmed();
  if (expected.isEmpty() || canvas_->strokes().isEmpty()) {
    statusBar()->showMessage(tr("Type the intended character and draw something first."), 4000);
    return;
  }
  Sample sample;
  sample.character = expected;
  sample.strokes = canvas_->strokes();
  sample.surfaceWidth = canvas_->surfaceWidth();
  sample.surfaceHeight = canvas_->surfaceHeight();
  sample.source = canvas_->inputSourceLabel();
  fillMetadata(&sample);
  sample.set = QStringLiteral("dev");
  sample.prompted = false;

  QString dir = qEnvironmentVariable("HANDWRITING_SAMPLES_DIR");
  if (dir.isEmpty()) {
    dir = QString::fromUtf8(HANDWRITING_DEFAULT_SAMPLES_DIR);
  }
  QString path;
  QString error;
  if (!sample_io::saveSample(dir, sample, &path, &error)) {
    statusBar()->showMessage(tr("Could not save sample: %1").arg(error), 8000);
    return;
  }
  statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
  clearAll();
  expectedEdit_->selectAll();
  canvas_->setFocus(Qt::OtherFocusReason);
}

void MainWindow::undoStroke() {
  canvas_->undoLastStroke();
}

void MainWindow::copySelected() {
  const QString ch = candidates_->selectedCharacter();
  if (!ch.isEmpty()) {
    QApplication::clipboard()->setText(ch);
    statusBar()->showMessage(tr("Copied %1").arg(ch), 2000);
  }
}

void MainWindow::updateDiagnostics(const RecognitionResult& result) {
  lastResult_ = result;
  const QString modeName = modeCombo_->currentText();
  QString writeState = canvas_->holdToWriteActive() ? tr("hold-to-write") : tr("click-drag");
  if (canvas_->holdToWriteActive()) {
    if (canvas_->absoluteFingerDown()) {
      writeState += tr(" · absolute finger");
    } else if (canvas_->absoluteCaptureActive()) {
      writeState += tr(" · absolute listening");
    } else {
      writeState += tr(" · cursor fallback");
    }
  }
  diagnostics_->setText(
      tr("Input: %1 · Backend: %2 · Mode: %3%8 · Strokes: %4 · Latency: %5 ms · Pad aspect: %6%7")
          .arg(writeState)
          .arg(canvas_->captureBackendName())
          .arg(modeName)
          .arg(result.strokeCount)
          .arg(result.latencyMs)
          .arg(canvas_->padAspect(), 0, 'f', 2)
          .arg(result.error.isEmpty()
                   ? QString()
                   : QStringLiteral(" · Error: %1").arg(result.error))
          .arg(lastEngine_.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(lastEngine_)));
}

void MainWindow::onStrokesChanged() {
  updateSaveEnabled();
  runRecognition();
}

void MainWindow::runRecognition() {
  if (practice_ && !practiceShowRecognition_->isChecked()) {
    ++recognitionGeneration_;  // drop any answer still in flight
    candidates_->clear();
    return;
  }
  const QVector<Stroke> rawStrokes = canvas_->strokes();
  if (rawStrokes.isEmpty()) {
    candidates_->clear();
    lastResult_ = {};
    updateDiagnostics(lastResult_);
    return;
  }

  const int generation = ++recognitionGeneration_;
  const RecognitionMode mode = currentMode();
  const int w = canvas_->logicalWidth();
  const int h = canvas_->logicalHeight();
  // Crop/fit/preprocess happen inside the shared Recognizer pipeline.
  // Hybrid (Zinnia + InkNet) only exists for Both mode; other modes use Zinnia alone.
  const bool hybrid = net_ != nullptr && mode == RecognitionMode::Both;
  const Recognizer recognizer(backend_, net_);
  Recognizer::Options options = recognizerOptions();
  if (hybrid) options.neural = Recognizer::Options::Neural::Hybrid;
  lastEngine_ = hybrid ? tr("hybrid") : tr("Zinnia");
  if (backend_->usingAlternativeJapanese()) {
    lastEngine_ += tr(", alt. JA model");
  }

  auto* watcher = new QFutureWatcher<RecognitionResult>(this);
  connect(watcher, &QFutureWatcher<RecognitionResult>::finished, this, [this, watcher, generation]() {
    const RecognitionResult result = watcher->result();
    watcher->deleteLater();
    if (generation != recognitionGeneration_) {
      return;
    }
    candidates_->setResult(result);
    updateDiagnostics(result);
    copyButton_->setEnabled(!candidates_->selectedCharacter().isEmpty());
  });

  QFuture<RecognitionResult> future = QtConcurrent::run([recognizer, rawStrokes, mode, w, h, options]() {
    return recognizer.recognize(rawStrokes, w, h, mode, options);
  });
  watcher->setFuture(future);
}

// --- Writer profile and Practice mode ------------------------------------------------------------

void MainWindow::fillMetadata(Sample* sample) const {
  sample->writer = profile_.writer;
  sample->handedness = profile_.handedness;
  sample->device = profile_.device;
  sample->session = sessionId_;
}

bool MainWindow::askWriter() {
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Who is writing?"));
  auto* form = new QFormLayout(&dlg);
  auto* name = new QLineEdit(profile_.writer, &dlg);
  name->setMaxLength(24);
  name->setPlaceholderText(tr("a pseudonym, e.g. w1 (not your name)"));
  auto* hand = new QComboBox(&dlg);
  hand->addItem(tr("Right-handed"), QStringLiteral("right"));
  hand->addItem(tr("Left-handed"), QStringLiteral("left"));
  hand->addItem(tr("Not stated"), QString());
  hand->setCurrentIndex(qMax(0, hand->findData(profile_.handedness)));
  auto* device = new QComboBox(&dlg);
  device->addItem(tr("Trackpad"), QStringLiteral("trackpad"));
  device->addItem(tr("Mouse"), QStringLiteral("mouse"));
  device->addItem(tr("Stylus / tablet"), QStringLiteral("stylus"));
  device->setCurrentIndex(qMax(0, device->findData(profile_.device)));
  form->addRow(tr("Writer id:"), name);
  form->addRow(tr("Writing hand:"), hand);
  form->addRow(tr("Device:"), device);
  form->addRow(new QLabel(tr("Stored in every sample you save. Samples stay on this computer."), &dlg));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
    // Letters, digits, - and _ only: easy to type again and safe in a file name.
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,24}$")).match(name->text().trimmed()).hasMatch()) {
      name->setStyleSheet(QStringLiteral("border: 1px solid red"));
      return;
    }
    dlg.accept();
  });
  if (dlg.exec() != QDialog::Accepted) return false;
  profile_.writer = name->text().trimmed();
  profile_.handedness = hand->currentData().toString();
  profile_.device = device->currentData().toString();
  QSettings settings;
  settings.setValue(QStringLiteral("writer/name"), profile_.writer);
  settings.setValue(QStringLiteral("writer/handedness"), profile_.handedness);
  settings.setValue(QStringLiteral("writer/device"), profile_.device);
  return true;
}

void MainWindow::editWriter() {
  if (askWriter() && practice_) startPractice();  // another writer: their own progress
}

void MainWindow::togglePractice(bool on) {
  if (!on) {
    practice_.reset();
    practiceBar_->setVisible(false);
    candidates_->setVisible(true);
    expectedEdit_->setVisible(true);
    saveSampleButton_->setVisible(true);
    copyButton_->setVisible(true);
    for (QShortcut* sc : practiceShortcuts_) sc->setEnabled(false);
    statusBar()->showMessage(tr("Practice off"), 3000);
    clearAll();
    return;
  }
  if (profile_.writer.isEmpty() && !askWriter()) {
    QSignalBlocker block(practiceButton_);
    practiceButton_->setChecked(false);
    return;
  }
  if (practiceSets_.isEmpty()) {
    QString err;
    const QString docs = qEnvironmentVariable("HANDWRITING_PRACTICE_DOCS", QString::fromUtf8(HANDWRITING_PRACTICE_DOCS_DIR));
    practiceSets_ = practice::loadSets(docs, &err);
    if (practiceSets_.isEmpty()) {
      statusBar()->showMessage(err, 8000);
      QSignalBlocker block(practiceButton_);
      practiceButton_->setChecked(false);
      return;
    }
    for (const PracticeSet& set : practiceSets_) {
      practiceSetCombo_->addItem(tr("%1 (%2)").arg(set.label, set.tag), set.id);
    }
  }
  practiceBar_->setVisible(true);
  candidates_->setVisible(false);
  expectedEdit_->setVisible(false);
  saveSampleButton_->setVisible(false);
  copyButton_->setVisible(false);
  for (QShortcut* sc : practiceShortcuts_) sc->setEnabled(true);
  canvas_->clearStrokes();
  startPractice();
  canvas_->setFocus(Qt::OtherFocusReason);
}

void MainWindow::startPractice() {
  const QString id = practiceSetCombo_->currentData().toString();
  for (const PracticeSet& set : practiceSets_) {
    if (set.id != id) continue;
    // $HANDWRITING_SAMPLES_DIR, if set, wins inside sampleDirForTag.
    practiceDir_ = practice::sampleDirForTag(set.tag, QString::fromUtf8(HANDWRITING_DEFAULT_SAMPLES_DIR));
    practice_ = std::make_unique<practice::PracticeSession>(
        set, profile_.writer, practice::charsWrittenBy(practiceDir_, profile_.writer));
    canvas_->clearStrokes();
    refreshPractice();
    return;
  }
}

void MainWindow::refreshPractice() {
  if (!practice_) return;
  practiceTarget_->setText(practice_->finished() ? tr("Done") : practice_->current());
  practiceProgress_->setText(tr("%1 / %2 · %3 left").arg(practice_->written()).arg(practice_->total()).arg(practice_->remaining()));
  statusBar()->showMessage(tr("Practice · writer %1 · %2 · saving to %3")
                               .arg(profile_.writer, profile_.handedness.isEmpty() ? tr("hand not stated") : tr("%1-handed").arg(profile_.handedness), practiceDir_));
}

void MainWindow::practiceSaveNext() {
  if (!practice_ || practice_->finished()) return;
  if (canvas_->strokes().isEmpty()) {
    statusBar()->showMessage(tr("Write the character first (→ skips it)."), 3000);
    return;
  }
  Sample sample;
  sample.character = practice_->current();
  sample.strokes = canvas_->strokes();
  sample.surfaceWidth = canvas_->surfaceWidth();
  sample.surfaceHeight = canvas_->surfaceHeight();
  sample.source = canvas_->inputSourceLabel();
  fillMetadata(&sample);
  sample.set = practice_->set().tag;
  sample.prompted = true;
  QString path, error;
  if (!sample_io::saveSample(practiceDir_, sample, &path, &error)) {
    statusBar()->showMessage(tr("Could not save sample: %1").arg(error), 8000);
    return;
  }
  practice_->saved(path);
  canvas_->clearStrokes();
  refreshPractice();
  if (practice_->finished()) statusBar()->showMessage(tr("This list is finished. Thank you!"), 8000);
}

void MainWindow::practiceRedo() {
  if (practice_) canvas_->clearStrokes();
}

void MainWindow::practiceSkip() {
  if (!practice_) return;
  practice_->skip();
  canvas_->clearStrokes();
  refreshPractice();
}

void MainWindow::practiceUndoLast() {
  if (!practice_) return;
  QString removed;
  if (practice_->undoLastSaved(&removed)) {
    canvas_->clearStrokes();
    refreshPractice();
    statusBar()->showMessage(tr("Deleted %1").arg(removed), 4000);
  } else {
    statusBar()->showMessage(tr("Nothing saved in this session to delete."), 3000);
  }
}
