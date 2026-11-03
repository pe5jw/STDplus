// STDplus — TciClient with dialect detection
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TciClient.h"   // upstream STD
#include "TciDialect.h"

// TciClientPlus extends TciClient with:
//  - dialect detection from the "device:" handshake
//  - sending START before AUDIO_START when the dialect requires it
//  - exposing the detected dialect for the connect dialog
//
// Drop-in replacement: use TciClientPlus wherever TciClient is used.
class TciClientPlus : public TciClient {
  Q_OBJECT
public:
  explicit TciClientPlus(QObject* parent = nullptr);

  // Override the detected dialect (Auto = let the device: string decide).
  void setDialectOverride(TciDialect d);
  TciDialect effectiveDialect() const { return effective_; }

signals:
  void dialectDetected(TciDialect d, const QString& label);

private:
  // Intercept beginSession to inject START when needed.
  // We hook the sessionUp signal from the base class.
  void onSessionUp(const QString& host, quint16 port);
  // Intercept incoming text to catch "device:" before passing to base.
  void onTextMessage(const QString& msg);

  TciDialect override_ = TciDialect::Auto;
  TciDialect effective_ = TciDialect::Generic;
  bool startSent_ = false;
};
