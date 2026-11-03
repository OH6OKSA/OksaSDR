.pragma library

// Scale marks shared by the analog and the bar meter face, each
// [value, label, red?]. S meter: S1…S9 then +20/+40/+60 in red (S1 =
// -121 dBm, 6 dB per S unit; S9 = -73; +60 = -13, the scale's end, so the
// labels reach both ends); other meters: five labels from min to max, red
// from the alarm level.
function marks(kind, min, max, red) {
    if (kind === "S") {
        return [[-121, "1", false], [-109, "3", false], [-97, "5", false], [-85, "7", false],
                [-73, "9", false], [-53, "+20", true], [-33, "+40", true], [-13, "+60", true]]
    }
    const out = []
    for (let i = 0; i <= 4; ++i) {
        const v = min + (max - min) * i / 4
        const label = Math.abs(max - min) <= 10 ? v.toFixed(1) : Math.round(v).toString()
        out.push([v, label, v >= red])
    }
    return out
}

// Where the scale turns red: S9 on the S meter, else the alarm level.
function redFrom(kind, red) {
    return kind === "S" ? -73 : red
}

// The name a face shows for a meter: "PA Temp", the amplifier's readings
// as "Amp …" (narrow faces: "PA", "Amp", the value's unit and the scale
// telling which reading it is).
function meterName(kind, narrow) {
    if (kind === "PA temp") return narrow ? "PA" : "PA Temp"
    if (kind.indexOf("Amp ") === 0) return narrow ? "Amp" : "Amp " + ampLabel(kind)
    return kind
}

// An amplifier reading as the meter chooser names it under "Amplifier".
function ampLabel(kind) {
    const names = { "Amp power": "Power", "Amp refl": "Refl", "Amp SWR": "SWR",
                    "Amp temp": "Temp", "Amp volts": "Volts", "Amp current": "Current" }
    return names[kind] || kind
}
