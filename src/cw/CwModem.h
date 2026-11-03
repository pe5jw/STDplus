// STDplus — CW modem: fldigi-derived decoder + sinus TX encoder
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Algorithm ported from fldigi (W1HKJ et al., GPL-3+).
// No fldigi headers or libraries required at compile time.
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class TciClient;

// CwModem wraps a self-contained CW decoder/encoder for use with the TCI
// audio stream.
//
// RX path:  TciClient → audioReceived(samples, 48kHz)
//           → processAudio() → downsample 6× to 8kHz
//           → fldigi-derived Schmitt-trigger + dot/dash tracker
//           → textDecoded(QString)
//
// TX path:  sendText(QString)
//           → Morse encoder → sinus PCM at cwToneHz_ / 48kHz
//           → TciClient::sendAudio()   [hooked up when API is available]

class CwModem : public QObject {
  Q_OBJECT
public:
  explicit CwModem(QObject* parent = nullptr);
  ~CwModem() override;

  void setClient(TciClient* client);

  // RX: feed 48 kHz float32 mono samples from TCI.
  void processAudio(const QVector<float>& samples);

  // TX: encode text as Morse and send via TCI audio.
  void sendText(const QString& text);
  void stopTx();

  // Settings
  void setCwToneHz(double hz);   // AF tone, default 700 Hz
  void setWpm(int wpm);          // TX/RX speed, default 20 WPM
  double cwToneHz() const { return cwToneHz_; }
  int    wpm()      const { return wpm_; }

signals:
  void textDecoded(const QString& text);   // from fldigi-derived RX decoder
  void txFinished();

private:
  void initFldigi();

  TciClient* client_   = nullptr;
  double     cwToneHz_ = 700.0;
  int        wpm_      = 20;
  bool       fldigiOk_ = false;

  // Opaque pointer to decoder state (keeps implementation details private)
  struct FldigiCw;
  FldigiCw* fldigi_ = nullptr;
};
