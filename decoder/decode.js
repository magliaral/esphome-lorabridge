// ============================================================
// Victron MPPT / Battery / Orion Decoder  -  TTN v3 Format
// With Home Assistant _sensor_attr Extension
//
// Unified decoder: auto-detects payload layout
//   "boat-v4": boat-v3 + GPS lat/lon (2x 4B, /1e7) + charger states as
//              bits (12 binaries, 2 bitmask bytes) + only 3 texts
//   "boat-v3": 14x2B + 1x1B sensors + 4 binaries + 5 texts (SY Alema, + Feuchte)
//   "boat-v2": 14 sensors + 4 binaries + 5 texts  (SY Alema, + BTHome Temps)
//   "boat":    11 sensors + 4 binaries + 5 texts  (SY Alema, legacy)
//   "home":    10 sensors + 3 binaries + 3 texts  (esphome-lorabridge-home)
// Detection: a parse is only valid if the length-prefixed text fields
// consume the payload EXACTLY to its last byte.
//
// Changes v4 (2026-07-12):
//   - Invalid-value sentinel: the firmware (esphome-lorabridge >= 0.3.0)
//     encodes sensors without a valid state (NaN) as the bit pattern
//     0x80 00..00 (MSB set, rest 0). Such fields are OMITTED from the
//     decoded payload so Home Assistant keeps the last known state.
//   - boat-v4 layout with GPS: latitude/longitude as 4-byte signed
//     ints, divisor 1e7 (~1 cm). Consumed by the HA integration's
//     device_tracker (map), not emitted as regular sensors.
//   - boat-v4: mpptState/orionState travel as 4 bits each ("@"-entries
//     in the bitmask, decoded back into the SAME text fields) instead of
//     length-prefixed texts. This frees the payload budget the GPS bytes
//     consume: with "Absorption" as text, GPS would have exceeded the
//     51-byte DR0 limit and the whole uplink would have been dropped.
//     The bitmask grows to ceil(n/8) bytes (firmware packs LSB-first).
//   - readInt() rewritten without bit-shifts: JS shifts are 32-bit
//     signed, which corrupted 4-byte values.
//   - batteryTimeRemaining 0xFFFF ("infinite") now also omits the field
//     instead of emitting null.
//
// Changes v3.1 (2026-07-11):
//   - boat-v3: batteryPower divisor 100 -> 1 (int16 overflow at >327.67 W
//     showed e.g. +337 W as -318 W), batteryCurrent 1000 -> 10 (overflow
//     at >32.767 A, e.g. alternator charging). Wire format unchanged;
//     MUST be deployed together with firmware multiplier change (1 / 10).
//     Legacy layouts (boat-v2/boat/home) intentionally keep old divisors.
//
// Changes v2 (2026-07):
//   - boat-v2 layout: fridge/salon/engine temperature (Shelly BLU H&T
//     via bthome_receiver, 0.1 degC resolution -> divisor 10, signed)
//   - legacy "boat" layout kept as fallback; remove once all devices
//     run the new firmware
//
// Fixes vs. previous version:
//   - bounds-checked text reader (no more  garbage on mismatch)
//   - _sensor_attr only contains fields present in the detected layout
//
// Adapted for ttn_client / Home Assistant _sensor_attr by magliaral, 2025
// ============================================================

var HA_ATTR = {
  mpptPower:             { unit: "W",   device_class: "power",       state_class: "measurement",      friendly_name: "MPPT Leistung" },
  mpptCurrent:           { unit: "A",   device_class: "current",     state_class: "measurement",      friendly_name: "MPPT Strom" },
  mpptYieldToday:        { unit: "kWh", device_class: "energy",      state_class: "total_increasing", friendly_name: "MPPT Tagesertrag" },
  mpptFault:             { friendly_name: "MPPT Fehler",       entity_category: "diagnostic" },
  mpptError:             { friendly_name: "MPPT Fehlermeldung", entity_category: "diagnostic" },
  mpptState:             { friendly_name: "MPPT Status" },
  mpptErrorReason:       { friendly_name: "MPPT Fehlergrund",  entity_category: "diagnostic" },
  batteryVoltage:        { unit: "V",   device_class: "voltage",     state_class: "measurement", suggested_display_precision: "1", friendly_name: "Batteriespannung" },
  batteryPower:          { unit: "W",   device_class: "power",       state_class: "measurement", suggested_display_precision: "0", friendly_name: "Batterieleistung" },
  batteryCurrent:        { unit: "A",   device_class: "current",     state_class: "measurement", suggested_display_precision: "1", friendly_name: "Batteriestrom" },
  batteryConsumedAh:     { unit: "Ah",  state_class: "measurement",  friendly_name: "Verbrauchte Ah" },
  batteryStateOfCharge:  { unit: "%",   device_class: "battery",     state_class: "measurement", friendly_name: "Ladezustand" },
  batteryTimeRemaining:  { unit: "min", device_class: "duration",    state_class: "measurement", friendly_name: "Restlaufzeit" },
  batteryTemperature:    { unit: "°C",  device_class: "temperature", state_class: "measurement", friendly_name: "Batterietemperatur" },
  batteryStarterVoltage: { unit: "V",   device_class: "voltage",     state_class: "measurement", suggested_display_precision: "1", friendly_name: "Starterbatterie Spannung" },
  batteryAlarm:          { friendly_name: "Batterie Alarm",     entity_category: "diagnostic" },
  batteryAlarmReason:    { friendly_name: "Batterie Alarmgrund", entity_category: "diagnostic" },
  orionError:            { friendly_name: "Orion Fehler",      entity_category: "diagnostic" },
  orionState:            { friendly_name: "Orion Status" },
  orionErrorReason:      { friendly_name: "Orion Fehlergrund", entity_category: "diagnostic" },
  fridgeTemperature:     { unit: "°C",  device_class: "temperature", state_class: "measurement", suggested_display_precision: "1", friendly_name: "Kühlschrank Temperatur" },
  salonTemperature:      { unit: "°C",  device_class: "temperature", state_class: "measurement", suggested_display_precision: "1", friendly_name: "Salon Temperatur" },
  engineTemperature:     { unit: "°C",  device_class: "temperature", state_class: "measurement", suggested_display_precision: "1", friendly_name: "Motorraum Temperatur" },
  salonHumidity:         { unit: "%",   device_class: "humidity",    state_class: "measurement", suggested_display_precision: "0", friendly_name: "Salon Luftfeuchte" }
  // latitude/longitude deliberately have no HA_ATTR entry: they are
  // consumed by the HA integration's device_tracker, not as sensors.
};

// ── Layout Definitions ───────────────────────────────────────────────────────
// sensors: [name, divisor, signed, bytes?] - big-endian, in payload order
//          bytes is optional and defaults to 2; 1-byte fields are unsigned-capable too
// binaries: bit order in the bitmask (ceil(n/8) bytes, LSB-first per byte).
//          Plain entries decode to "on"/"off" fields. "@field:Label" entries
//          are state bits: a set bit assigns Label to data[field]; if no bit
//          of a field is set, the field is omitted (unknown -> keep last).
// texts: length-prefixed strings, in payload order
// NOTE: layouts are tried in order; keep the longest/most specific first.

var LAYOUTS = [
  {
    name: "boat-v4",
    sensors: [
      ["mpptPower", 1, true],
      ["mpptCurrent", 10, true],
      ["mpptYieldToday", 100, true],
      ["batteryVoltage", 100, true],
      ["batteryPower", 1, true],
      ["batteryCurrent", 10, true],
      ["batteryConsumedAh", 10, true],
      ["batteryStateOfCharge", 10, false],
      ["batteryTimeRemaining", 1, false],
      ["batteryTemperature", 100, true],
      ["batteryStarterVoltage", 100, true],
      ["fridgeTemperature", 10, true],
      ["salonTemperature", 10, true],
      ["engineTemperature", 10, true],
      ["salonHumidity", 1, false, 1],
      ["latitude", 10000000, true, 4],
      ["longitude", 10000000, true, 4]
    ],
    binaries: [
      "batteryAlarm", "mpptFault", "mpptError", "orionError",
      "@mpptState:off", "@mpptState:Bulk", "@mpptState:Absorption", "@mpptState:Float",
      "@orionState:off", "@orionState:Bulk", "@orionState:Absorption", "@orionState:Float"
    ],
    texts: ["batteryAlarmReason", "mpptErrorReason", "orionErrorReason"]
  },
  {
    name: "boat-v3",
    sensors: [
      ["mpptPower", 1, true],
      ["mpptCurrent", 10, true],
      ["mpptYieldToday", 100, true],
      ["batteryVoltage", 100, true],
      ["batteryPower", 1, true],     // v3.1: 1 W resolution, +/-32767 W (was /100 -> overflow at 327 W)
      ["batteryCurrent", 10, true],  // v3.1: 0.1 A resolution, +/-3276 A (was /1000 -> overflow at 32.7 A)
      ["batteryConsumedAh", 10, true],
      ["batteryStateOfCharge", 10, false],
      ["batteryTimeRemaining", 1, false],
      ["batteryTemperature", 100, true],
      ["batteryStarterVoltage", 100, true],
      ["fridgeTemperature", 10, true],
      ["salonTemperature", 10, true],
      ["engineTemperature", 10, true],
      ["salonHumidity", 1, false, 1]
    ],
    binaries: ["batteryAlarm", "mpptFault", "mpptError", "orionError"],
    texts: ["batteryAlarmReason", "mpptState", "mpptErrorReason", "orionState", "orionErrorReason"]
  },
  {
    name: "boat-v2",
    sensors: [
      ["mpptPower", 1, true],
      ["mpptCurrent", 10, true],
      ["mpptYieldToday", 100, true],
      ["batteryVoltage", 100, true],
      ["batteryPower", 100, true],
      ["batteryCurrent", 1000, true],
      ["batteryConsumedAh", 10, true],
      ["batteryStateOfCharge", 10, false],
      ["batteryTimeRemaining", 1, false],
      ["batteryTemperature", 100, true],
      ["batteryStarterVoltage", 100, true],
      ["fridgeTemperature", 10, true],
      ["salonTemperature", 10, true],
      ["engineTemperature", 10, true]
    ],
    binaries: ["batteryAlarm", "mpptFault", "mpptError", "orionError"],
    texts: ["batteryAlarmReason", "mpptState", "mpptErrorReason", "orionState", "orionErrorReason"]
  },
  {
    name: "boat",   // legacy without BTHome temps; remove after full rollout
    sensors: [
      ["mpptPower", 1, true],
      ["mpptCurrent", 10, true],
      ["mpptYieldToday", 100, true],
      ["batteryVoltage", 100, true],
      ["batteryPower", 100, true],
      ["batteryCurrent", 1000, true],
      ["batteryConsumedAh", 10, true],
      ["batteryStateOfCharge", 10, false],
      ["batteryTimeRemaining", 1, false],
      ["batteryTemperature", 100, true],
      ["batteryStarterVoltage", 100, true]
    ],
    binaries: ["batteryAlarm", "mpptFault", "mpptError", "orionError"],
    texts: ["batteryAlarmReason", "mpptState", "mpptErrorReason", "orionState", "orionErrorReason"]
  },
  {
    name: "home",
    sensors: [
      ["mpptPower", 1, true],
      ["mpptCurrent", 10, true],
      ["mpptYieldToday", 100, true],
      ["batteryVoltage", 100, true],
      ["batteryPower", 100, true],
      ["batteryCurrent", 1000, true],
      ["batteryConsumedAh", 10, true],
      ["batteryStateOfCharge", 10, false],
      ["batteryTimeRemaining", 1, false],
      ["batteryTemperature", 100, true]
    ],
    binaries: ["batteryAlarm", "mpptFault", "mpptError"],
    texts: ["batteryAlarmReason", "mpptState", "mpptErrorReason"]
  }
];

// ── Helper Functions ─────────────────────────────────────────────────────────

// Big-endian integer reader. Uses arithmetic instead of bit-shifts:
// JS shifts operate on 32-bit signed ints and corrupt 4-byte values.
function readInt(bytes, i, signed, width) {
  var raw = 0;
  for (var b = 0; b < width; b++) raw = raw * 256 + bytes[i + b];
  if (signed) {
    var signBit = Math.pow(2, width * 8 - 1);
    if (raw >= signBit) raw -= signBit * 2;
  }
  return raw;
}

// Firmware sentinel for "no valid value": bit pattern 0x80 00..00
// (MSB set, rest 0). Reads as -2^(8n-1) signed or +2^(8n-1) unsigned.
function isInvalidSentinel(raw, signed, width) {
  var signBit = Math.pow(2, width * 8 - 1);
  return signed ? raw === -signBit : raw === signBit;
}

// Bounds-checked text reader. Returns null if the declared length
// would run past the end of the payload (=> layout mismatch).
function readText(bytes, i) {
  if (i >= bytes.length) return null;
  var len = bytes[i];
  if (i + 1 + len > bytes.length) return null;
  var text = "";
  for (var j = 0; j < len; j++) {
    text += String.fromCharCode(bytes[i + 1 + j]);
  }
  if (text === "Off") text = "off";
  return { text: text, next: i + 1 + len };
}

// Attempts to parse the payload against one layout.
// Valid only if the text fields consume the payload exactly.
function tryParse(bytes, layout) {
  var numBinBytes = Math.ceil(layout.binaries.length / 8);
  var fixedLen = numBinBytes;
  for (var f = 0; f < layout.sensors.length; f++) {
    fixedLen += layout.sensors[f].length > 3 ? layout.sensors[f][3] : 2;
  }
  if (bytes.length < fixedLen + layout.texts.length) return null;

  var data = {};
  var i = 0;

  for (var s = 0; s < layout.sensors.length; s++) {
    var def = layout.sensors[s];  // [name, divisor, signed, bytes?]
    var width = def.length > 3 ? def[3] : 2;
    var raw = readInt(bytes, i, def[2], width);
    if (isInvalidSentinel(raw, def[2], width)) {
      // Field omitted entirely: ttn_client/HA never sees an update,
      // so the entity keeps its last known state.
    } else if (def[0] === "batteryTimeRemaining" && raw === 0xFFFF) {
      // Shunt reports "infinite" while charging -> omit as well.
    } else {
      data[def[0]] = raw / def[1];
    }
    i += width;
  }

  for (var b = 0; b < layout.binaries.length; b++) {
    var bit = (bytes[i + Math.floor(b / 8)] >> (b % 8)) & 1;
    var name = layout.binaries[b];
    if (name.charAt(0) === "@") {
      // State bit "@field:Label": a set bit selects the label; if no bit
      // of the field is set, the field stays omitted (state unknown).
      if (bit === 1) {
        var sep = name.indexOf(":");
        var field = name.substring(1, sep);
        if (data[field] === undefined) data[field] = name.substring(sep + 1);
      }
    } else {
      data[name] = bit === 1 ? "on" : "off";
    }
  }
  i += numBinBytes;

  for (var t = 0; t < layout.texts.length; t++) {
    var r = readText(bytes, i);
    if (r === null) return null;       // length byte inconsistent -> wrong layout
    data[layout.texts[t]] = r.text;
    i = r.next;
  }

  if (i !== bytes.length) return null;  // leftover bytes -> wrong layout

  return data;
}

// ── Main Decoder ─────────────────────────────────────────────────────────────

function decodeUplink(input) {
  var bytes = input.bytes;

  if (!bytes || bytes.length < 24) {
    return {
      errors: ["Payload too short: " + (bytes ? bytes.length : 0) + " bytes."]
    };
  }

  var data = null;
  var matched = null;
  for (var l = 0; l < LAYOUTS.length; l++) {
    data = tryParse(bytes, LAYOUTS[l]);
    if (data !== null) {
      matched = LAYOUTS[l];
      break;
    }
  }

  if (data === null) {
    return {
      errors: ["Payload (" + bytes.length + " bytes) matches no known layout (boat-v4/boat-v3/boat-v2/boat/home)."]
    };
  }

  // GPS plausibility: drop an all-zero fix (no GPS lock yet) so the
  // tracker is never sent to 0/0 ("Null Island").
  if (data.latitude === 0 && data.longitude === 0) {
    delete data.latitude;
    delete data.longitude;
  }

  // _sensor_attr: only the fields this layout actually delivers
  var attr = {};
  for (var key in data) {
    if (HA_ATTR[key]) attr[key] = HA_ATTR[key];
  }
  data._sensor_attr = attr;
  data._layout = matched.name;

  return { data: data };
}
