// STDplus — TCI dialect detection
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

// Known TCI server behaviours that differ from one another.
//
// Generic   — multi-client; AUDIO_START per socket is enough; no START needed.
//             Lyra-SDR-cpp, BoltSDR, AnanCore/Zeus-derived servers.
// DeskHpsdr — single-client; expects START before AUDIO_START; ExpertSDR3
//             semantics as implemented by dl1bz/deskHPSDR and Thetis.
// ExpertSdr — original ExpertSDR3 server; same START requirement as DeskHpsdr
//             but may differ on sensor/SNR commands.
//
// Auto      — detect from the device: handshake (default).
enum class TciDialect {
  Auto,
  Generic,
  DeskHpsdr,
  ExpertSdr,
};

// Parse the value of a "device:<name>" TCI message and return the dialect.
// Returns Generic for unknown device strings.
// AnanCore/BoltSDR sends device:Zeus — that is Generic, not ExpertSDR3,
// even though its protocol: field also says "ExpertSDR3".
TciDialect dialectFromDeviceString(const QString& device);

// Refine an already-detected dialect using the "protocol:<name>,<ver>" value.
// Call this after dialectFromDeviceString: device: wins over protocol:.
TciDialect refineWithProtocolString(TciDialect current, const QString& protocol);

// Human-readable label, for the connect dialog status line.
QString dialectLabel(TciDialect d);

// True when this dialect requires a global START command before AUDIO_START.
bool dialectRequiresStart(TciDialect d);

// True when this dialect is single-client (yields on "already connected").
bool dialectIsSingleClient(TciDialect d);
