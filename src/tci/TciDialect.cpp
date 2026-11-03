// STDplus — TCI dialect detection
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TciDialect.h"

TciDialect dialectFromDeviceString(const QString& device) {
  const QString d = device.trimmed().toLower();

  // Lyra — multi-client, AUDIO_START per socket, no START needed.
  if (d.contains(QLatin1String("lyra")))
    return TciDialect::Generic;

  // BoltSDR and AnanCore-derived servers send device:Zeus (TciProtocol.cs:
  // DeviceName = "Zeus").  Multi-client, AUDIO_START per socket, no START.
  // Match "zeus" before the ExpertSDR3 check below so the protocol: name
  // "ExpertSDR3" in the same handshake does not override it.
  if (d.contains(QLatin1String("zeus")) || d.contains(QLatin1String("bolt")) ||
      d.contains(QLatin1String("anancore")))
    return TciDialect::Generic;

  // deskHPSDR — single-client, needs START before AUDIO_START.
  if (d.contains(QLatin1String("deskhpsdr")) || d.contains(QLatin1String("desk hpsdr")) ||
      d.contains(QLatin1String("thetis")))
    return TciDialect::DeskHpsdr;

  // ExpertSDR3 — same START semantics as deskHPSDR.
  if (d.contains(QLatin1String("expert")) || d.contains(QLatin1String("eesdr")))
    return TciDialect::ExpertSdr;

  // SDROxide, NereusSDR, Kainos — no IQ streaming, but AUDIO_START works.
  // Treat as Generic for now; extend when differences surface.
  return TciDialect::Generic;
}

TciDialect refineWithProtocolString(TciDialect current, const QString& protocol) {
  // device: has already been parsed and wins.  Only apply the protocol: string
  // when device: gave no verdict (Generic as fallback, not from a known name).
  // AnanCore sends protocol:ExpertSDR3 — do NOT upgrade that to ExpertSdr
  // because its device:Zeus already identified it as Generic multi-client.
  if (current != TciDialect::Generic)
    return current;
  const QString p = protocol.trimmed().toLower();
  // Real ExpertSDR3 server: protocol:ExpertSDR3 without a device: override.
  if (p.startsWith(QLatin1String("expertsdr3")) && !p.contains(QLatin1String("zeus")))
    return TciDialect::ExpertSdr;
  return current;
}

QString dialectLabel(TciDialect d) {
  switch (d) {
    case TciDialect::Auto:      return QStringLiteral("Auto");
    case TciDialect::Generic:   return QStringLiteral("Generic (Lyra / BoltSDR)");
    case TciDialect::DeskHpsdr: return QStringLiteral("deskHPSDR / Thetis");
    case TciDialect::ExpertSdr: return QStringLiteral("ExpertSDR3");
  }
  return QStringLiteral("Unknown");
}

bool dialectRequiresStart(TciDialect d) {
  return d == TciDialect::DeskHpsdr || d == TciDialect::ExpertSdr;
}

bool dialectIsSingleClient(TciDialect d) {
  return d == TciDialect::DeskHpsdr || d == TciDialect::ExpertSdr;
}
