// STDplus — Voice Keyer (DVK)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plays WAV files over TCI TX audio.  F1-F9 slots, contest-style.
//
// Usage:
//   VoiceKeyer vk;
//   vk.setClient(tciClient);          // must be set before play()
//   vk.loadSlot(0, "/path/to/f1.wav");
//   vk.play(0);                        // keys TX and starts playback
//   vk.stop();                         // abort early
//
// The keyer sets transmit true before pushing audio and clears it
// afterwards (or on stop/error).  No external PTT management needed.
//
// Resampling: input WAV is resampled to 48 kHz float32 mono using
// a simple linear interpolation.  For broadcast-quality resampling,
// substitute SRC (libsamplerate) or Qt Multimedia's pipeline.

#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>
#include <array>

class TciClient;

class VoiceKeyer : public QObject {
  Q_OBJECT

public:
  static constexpr int kSlots = 9;   // F1 … F9
  static constexpr int kRate  = 48000;
  // Samples per push cycle (~20 ms).  Must match TCI server expectation.
  static constexpr int kChunk = kRate / 50;   // 960 samples

  explicit VoiceKeyer(QObject* parent = nullptr);
  ~VoiceKeyer() override;

  // Attach the TCI client that will receive the audio.
  void setClient(TciClient* client);

  // Load (and resample) a WAV file into the given slot (0 = F1 … 8 = F9).
  // Returns true on success.  Previous slot data is replaced.
  bool loadSlot(int slot, const QString& path);

  // Clear one slot (unloads the WAV data).
  void clearSlot(int slot);

  // Start playback of slot.  Keys TX.  No-op if already playing.
  // Returns false when the slot is empty or no client is set.
  bool play(int slot);

  // Stop playback immediately and drop TX.
  void stop();

  bool isPlaying() const { return playing_; }
  int  currentSlot() const { return slot_; }

  // File path stored per slot (empty = unloaded).
  QString slotPath(int slot) const;

signals:
  // Emitted when playback of a slot starts/ends.
  void playbackStarted(int slot);
  void playbackFinished(int slot);
  // Emitted when a WAV file could not be loaded.
  void loadError(int slot, const QString& reason);

private slots:
  void onTimer();

private:
  // Raw float32 mono 48 kHz PCM per slot.
  struct Slot {
    QVector<float> pcm;
    QString        path;
  };

  // Read a .wav file, mono-mix channels, resample to 48 kHz.
  // Returns true and fills `out` on success.
  static bool loadWav(const QString& path, QVector<float>& out, QString& err);

  // Simple linear interpolation resample from `srcRate` to `dstRate`.
  static QVector<float> resample(const QVector<float>& in,
                                  int srcRate, int dstRate);

  std::array<Slot, kSlots> slots_{};
  TciClient* client_   = nullptr;
  QTimer*    timer_    = nullptr;
  bool       playing_  = false;
  int        slot_     = -1;
  int        pos_      = 0;   // next sample to push
};
