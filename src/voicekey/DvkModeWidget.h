// STDplus — DVK Mode Widget
// Central screen shown when DVK is the active mode.
// Provides F1-F9 slot management, waveform preview, non-destructive
// DSP (gain, HPF, 3-band EQ, compressor) and microphone recording.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <array>

class VoiceKeyer;
class QLabel;
class QSlider;
class QPushButton;
class QCheckBox;
class QProgressBar;
class QDoubleSpinBox;
class QAudioSource;
class QIODevice;

// ---------------------------------------------------------------------------
// DvkSlotWidget — one row: filename, load, record, play/stop, waveform, DSP
// ---------------------------------------------------------------------------
class DvkSlotWidget : public QWidget {
  Q_OBJECT
public:
  explicit DvkSlotWidget(int slot, VoiceKeyer* vk, QWidget* parent = nullptr);
  ~DvkSlotWidget() override;

  void setPlaying(bool on);

  QString path() const { return path_; }
  void    setPath(const QString& p);

signals:
  void playRequested(int slot);
  void stopRequested();
  void saveProcessed(int slot,
                     float gainDb,
                     bool  hpfOn,   float hpfHz,
                     float lowDb,   float midDb,  float highDb,
                     float compThreshDb, float compRatio);

private slots:
  void onLoad();
  void onRecord();
  void onPlayStop();
  void onSave();

private:
  void buildWaveform(const QString& path);
  void startRecording();
  void stopRecording();
  QString recordPath() const;

  int         slot_;
  VoiceKeyer* vk_;
  QString     path_;
  bool        playing_   = false;
  bool        recording_ = false;

  // Recording state
  QAudioSource* audioSource_  = nullptr;
  QIODevice*    audioDevice_  = nullptr;  // buffer device from QAudioSource
  QVector<float> recBuf_;                 // accumulated float32 mono samples
  int            recSampleRate_ = 48000;

  QLabel*         nameLabel_  = nullptr;
  QPushButton*    loadBtn_    = nullptr;
  QPushButton*    recBtn_     = nullptr;
  QPushButton*    playBtn_    = nullptr;
  QPushButton*    saveBtn_    = nullptr;
  QWidget*        waveWidget_ = nullptr;

  QDoubleSpinBox* gainSpin_   = nullptr;
  QCheckBox*      hpfCheck_   = nullptr;
  QDoubleSpinBox* hpfSpin_    = nullptr;
  QDoubleSpinBox* lowSpin_    = nullptr;
  QDoubleSpinBox* midSpin_    = nullptr;
  QDoubleSpinBox* highSpin_   = nullptr;
  QDoubleSpinBox* thrSpin_    = nullptr;
  QDoubleSpinBox* ratioSpin_  = nullptr;
};

// ---------------------------------------------------------------------------
// DvkModeWidget — full DVK mode screen
// ---------------------------------------------------------------------------
class DvkModeWidget : public QWidget {
  Q_OBJECT
public:
  explicit DvkModeWidget(VoiceKeyer* vk, QWidget* parent = nullptr);

  void triggerSlot(int slot);

  void saveSettings(const QString& prefix) const;
  void loadSettings(const QString& prefix);

public slots:
  void onPlaybackStarted(int slot);
  void onPlaybackFinished(int slot);

private slots:
  void onSaveProcessed(int slot,
                       float gainDb,
                       bool  hpfOn,   float hpfHz,
                       float lowDb,   float midDb,  float highDb,
                       float compThreshDb, float compRatio);

private:
  VoiceKeyer* vk_;
  std::array<DvkSlotWidget*, 9> rows_{};
};
