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
    // A KPA1500's fan speed: its six steps, 0 (off) to 5.
    if (kind === "Amp fan") {
        return [[0, "0", false], [1, "1", false], [2, "2", false], [3, "3", false], [4, "4", false], [5, "5", false]]
    }
    // The radio's main fan: round numbers of RPM up to the scale's top (to
    // 2000: 0, 500 ... 2000; to the radio's own 8192: 0, 2000 ... 8000), at
    // most five of them.
    if (kind === "Fan") {
        let step = 10000
        for (const s of [250, 500, 1000, 2000, 2500, 5000]) {
            if (Math.floor(max / s) <= 4) { step = s; break }
        }
        const fan = []
        for (let v = 0; v <= max; v += step) fan.push([v, v.toString(), false])
        return fan
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
    // A fan speed has no unit to tell it from the other "Amp" readings.
    if (kind === "Amp fan") return narrow ? "Fan" : "Amp Fan"
    if (kind.indexOf("Amp ") === 0) return narrow ? "Amp" : "Amp " + ampLabel(kind)
    if (kind === "Tuner power") return narrow ? "Tuner" : "Tuner Power"
    if (kind === "Tuner SWR") return narrow ? "Tuner" : "Tuner SWR"
    return kind
}

// An amplifier reading as the meter chooser names it under "Amplifier".
function ampLabel(kind) {
    const names = { "Amp power": "Power", "Amp refl": "Refl", "Amp SWR": "SWR",
                    "Amp temp": "Temp", "Amp volts": "Volts", "Amp current": "Current", "Amp fan": "Fan" }
    return names[kind] || kind
}
