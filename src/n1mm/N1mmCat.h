// STDplus — N1MM CAT TCP server + UDP broadcast
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;

// N1mmCat listens on a TCP port and speaks a subset of the N1MM / Ham Radio
// Deluxe CAT dialect, so N1MM macro commands land in STDplus just as if it
// were talking to a real transceiver.
//
// Supported incoming commands (from N1MM macros):
//   FA<freq>;        — set VFO A frequency (Hz, 11 digits)
//   FA;              — query VFO A frequency → FA<freq>;
//   TX;              — key (PTT on)
//   RX;              — unkey (PTT off)
//   MD<mode>;        — set mode (1=LSB 2=USB 3=CW 4=FM …)
//   MD;              — query mode
//   PS;              — power on/off query → PS1; (always on)
//   ID;              — rig ID query → ID019; (IC-7300 compat.)
//
// UDP broadcast (N1MM Logger+ ContactInfo, port 12060):
//   sendCallToN1mm() sends a ContactInfo XML datagram so N1MM fills its
//   callsign field automatically whenever the CW/digi decoder finds a call.
//   Active only while the CAT server is listening.

class N1mmCat : public QObject {
  Q_OBJECT
public:
  static constexpr quint16 kDefaultPort    = 4532;
  static constexpr quint16 kUdpBroadcastPort = 12060;  // N1MM ContactInfo

  explicit N1mmCat(QObject* parent = nullptr);
  ~N1mmCat() override;

  bool listen(quint16 port = kDefaultPort);
  void close();
  bool isListening() const;
  quint16 port() const;

  // Push state from the TCI radio into the CAT server so query replies
  // reflect the current radio state.
  void setFreqHz(qint64 hz);
  void setMode(const QString& tciMode);  // "digu", "digl", "cw", etc.
  void setPtt(bool on);

  // Broadcast a decoded callsign to N1MM Logger+ via UDP (port 12060).
  // The CW decoder / digi decoder calls this when it spots a callsign.
  // No-op when the CAT server is not listening.
  void sendCallToN1mm(const QString& call);

signals:
  void freqChangeRequested(qint64 hz);
  void pttChangeRequested(bool on);
  void modeChangeRequested(const QString& tciMode);
  void clientConnected(const QString& address);
  void clientDisconnected();
  void error(const QString& msg);

private:
  void onNewConnection();
  void onReadyRead();
  void handleCommand(const QByteArray& cmd, QTcpSocket* socket);
  QByteArray freqReply() const;
  QByteArray modeReply() const;

  QTcpServer* server_ = nullptr;
  QTcpSocket* client_ = nullptr;  // one client at a time (N1MM model)
  QUdpSocket* udp_    = nullptr;  // N1MM UDP ContactInfo broadcast
  QByteArray  rxBuf_;
  qint64  freqHz_  = 14074000;
  QString tciMode_ = QStringLiteral("digu");
  bool    ptt_     = false;
};
