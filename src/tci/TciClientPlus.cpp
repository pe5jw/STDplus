// STDplus — TciClient with dialect detection
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TciClientPlus sits in front of TciClient and intercepts the WebSocket
// text stream to catch the "device:" message.  When the dialect requires
// START it sends it *before* TciClient's beginSession() sends AUDIO_START.
//
// Implementation note: TciClient does not expose a virtual beginSession(),
// so we cannot override it directly.  Instead we:
//   1. Connect to sessionUp to know when the socket is open.
//   2. Parse the raw text stream via a QWebSocket event filter or a
//      separate signal path.
//
// For now the dialect is settable manually or from the connect dialog.
// Full auto-detection requires hooking the raw WebSocket text; that is
// done in the TODO below once the basic structure builds.

#include "TciClientPlus.h"
#include <QDebug>

TciClientPlus::TciClientPlus(QObject* parent) : TciClient(parent) {
  connect(this, &TciClient::sessionUp, this, &TciClientPlus::onSessionUp);
}

void TciClientPlus::setDialectOverride(TciDialect d) {
  override_ = d;
  effective_ = (d == TciDialect::Auto) ? TciDialect::Generic : d;
}

void TciClientPlus::onSessionUp(const QString& /*host*/, quint16 /*port*/) {
  startSent_ = false;
  // When the dialect is known at connect time (manual override), send START
  // now.  Auto-detection from the device: message is handled in onTextMessage
  // once that hook is wired.
  if (override_ != TciDialect::Auto) {
    effective_ = override_;
  }
  if (dialectRequiresStart(effective_) && !startSent_) {
    // Send the global START that deskHPSDR/ExpertSDR3 require.
    // TciClient::sendCommand is private; expose via a thin wrapper or
    // TODO: subclass sendCommand when TciClient grows a protected overload.
    // For now, use the public QMetaObject path as a stub.
    // TODO: wire sendCommand("START") here once access is available.
    qDebug() << "[STDplus] dialect" << dialectLabel(effective_)
             << "requires START — not yet sent (needs TciClient hook)";
    startSent_ = true;
  }
  emit dialectDetected(effective_, dialectLabel(effective_));
}

// TODO: intercept QWebSocket::textMessageReceived to parse device: lines
// and call dialectFromDeviceString().  Requires either:
//   a) TciClient exposing onRawText as a virtual/signal, or
//   b) STDplus owning the QWebSocket and passing it to TciClient.
// Option (b) is the cleanest long-term approach.
void TciClientPlus::onTextMessage(const QString& msg) {
  QString name;
  const int colon = msg.indexOf(':');
  if (colon > 0) {
    name = msg.left(colon).trimmed().toLower();
    if (name == QLatin1String("device") && override_ == TciDialect::Auto) {
      const QString value = msg.mid(colon + 1).trimmed().remove(';');
      effective_ = dialectFromDeviceString(value);
      qDebug() << "[STDplus] device:" << value << "→" << dialectLabel(effective_);
      emit dialectDetected(effective_, dialectLabel(effective_));
    }
  }
}
