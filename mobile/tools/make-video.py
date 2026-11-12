#!/usr/bin/env python3
"""Make the demo video from ShotPlan record runs of the dev build.

    QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1.40625 build-mobile/mobile/aether-mobile \
        --size 1366x1024 --connect RADIO_IP --controls visible --font-px 17 \
        --shot-plan mobile/tools/video/ipad.plan --shot-dir RUN_DIR > RUN.log 2>&1
    (the phone part: --size 440x956 and mobile/tools/video/phone.plan)

    make-video.py segment RUN_DIR RUN.log OUT.mp4 [--scale 1.40625] [--footer TEXT] [--loudness LUFS]
    make-video.py still OUT.mp4 SECONDS IMAGE        (mobile/tools/video/brand.png)
    make-video.py card OUT.mp4 SECONDS "Title" "line" ... [--icon mobile/tools/video/icon.png]
    make-video.py join OUT.mp4 SEG.mp4 ...

RUN_DIR/frames/MS.jpg come from `record`; the log has "shot-plan: at MS
LINE" for every plan line after it. Captions in the plan:
  caption # Title            a chapter heading (until the next one)
  caption [box x,y,w,h] Text a caption, with a highlight box in frame pixels
  caption !mark x,y          a tap mark at frame pixels (for opens with no tap)
  caption !speed N           play N times faster from here (1: normal)
  caption !cut / !uncut      leave the frames between out
  caption -                  clear the caption
Taps logged by tapname / taptext / tapmhz get a mark by themselves.
Receive only: the plans key nothing; they change the radio's slice and put
it back (14.074 DIGU), so a run is for a time the radio is free.
"""
import os
import re
import subprocess
import sys
from array import array

W, H = 1920, 1080
APP_W = 1440               # the app's area at the left; the caption panel right of it
PANEL_X = APP_W + 30
BG = "0x0b0f13"
FPS = 30
FONT = "Noto Sans"
AUDIO_RATE = 24000          # record writes 24 kHz stereo float32
BAND_LOUDNESS = -24         # LUFS: the band behind the tour (easy to turn down)


def ass_time(t):
    t = max(0.0, t)
    h = int(t // 3600)
    m = int(t % 3600 // 60)
    s = t % 60
    return f"{h}:{m:02d}:{s:05.2f}"


def ass_header():
    return f"""[Script Info]
ScriptType: v4.00+
PlayResX: {W}
PlayResY: {H}
WrapStyle: 0
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Chapter,{FONT},64,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,1,0,0,0,100,100,0,0,1,0,0,7,{PANEL_X},30,70,1
Style: Caption,{FONT},48,&H00F3EDE6,&H00F3EDE6,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,{PANEL_X},28,260,1
Style: Footer,{FONT},28,&H00A5988B,&H00A5988B,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,1,{PANEL_X},30,40,1
Style: Shape,{FONT},20,&H00FFFFFF,&H00FFFFFF,&H00FFB63F,&H00000000,0,0,0,0,100,100,0,0,1,4,0,7,0,0,0,1
Style: CardTitle,{FONT},110,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,1,0,0,0,100,100,0,0,1,0,0,8,120,120,330,1
Style: CardLine,{FONT},56,&H00F3EDE6,&H00F3EDE6,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,8,160,160,480,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""


def esc(text):
    return text.replace("\\", "\\\\").replace("{", "(").replace("}", ")")


def circle(r):
    k = r * 0.5523
    return (f"m 0 {-r} b {k} {-r} {r} {-k} {r} 0 b {r} {k} {k} {r} 0 {r} "
            f"b {-k} {r} {-r} {k} {-r} 0 b {-r} {-k} {-k} {-r} 0 {-r}")


def parse_log(log, scale):
    """[(ms, kind, value)] from the run's log, in order."""
    events = []
    last_ms = None
    for raw in open(log, encoding="utf-8", errors="replace"):
        line = raw.rstrip("\n")
        m = re.search(r"shot-plan: at (\d+) (.*)$", line)
        if m:
            last_ms = int(m.group(1))
            cmd = m.group(2)
            if cmd.startswith("caption "):
                events.append((last_ms, "caption", cmd[len("caption "):].strip()))
            elif cmd.startswith("tap ") and last_ms is not None:
                fx, fy = (float(v) for v in cmd[4:].split(","))
                events.append((last_ms, "tapfrac", (fx, fy)))
            continue
        m = re.search(r"shot-plan: (?:tap|hold) \S+ at (-?\d+),(-?\d+)", line)
        if m and last_ms is not None:
            events.append((last_ms, "tap", (int(m.group(1)) * scale, int(m.group(2)) * scale)))
            continue
        m = re.search(r"shot-plan: (?:tap|hold) at [\d.]+ MHz \(x (\d+), y (\d+)\)", line)
        if m and last_ms is not None:
            events.append((last_ms, "tap", (int(m.group(1)) * scale, int(m.group(2)) * scale)))
    return events


def band_audio(run_dir, times, out_t, durations, speeds, path):
    """The record's RX audio laid along the video: at normal speed as it was,
    through a sped-up wait the band plays on at its own pace (it sounds like
    the band, not a chipmunk), cut frames leave it out. False if none."""
    src_path = os.path.join(run_dir, "audio.f32")
    if not os.path.exists(src_path) or os.path.getsize(src_path) == 0:
        return False
    src = array("f")
    with open(src_path, "rb") as f:
        src.frombytes(f.read())
    # RADE's decoded speech, as the speaker mixes it in.
    speech_path = os.path.join(run_dir, "speech.f32")
    if os.path.exists(speech_path) and os.path.getsize(speech_path) > 0:
        speech = array("f")
        with open(speech_path, "rb") as f:
            speech.frombytes(f.read())
        if len(speech) > len(src):
            src.extend(array("f", [0.0]) * (len(speech) - len(src)))
        for i, v in enumerate(speech):
            if v:
                src[i] += v
    frames = len(src) // 2
    out = array("f")
    cursor = 0.0                      # source position, ms
    for i, ms in enumerate(times):
        if out_t[i] is None or durations[i] <= 0:
            continue
        if speeds[i] == 1.0:
            cursor = float(ms)
        want = round((out_t[i] + durations[i]) * AUDIO_RATE) - len(out) // 2
        if want <= 0:
            continue
        start = int(cursor * AUDIO_RATE / 1000)
        end = min(start + want, frames)
        if start < end:
            out.extend(src[start * 2:end * 2])
        missing = want - max(0, end - start)
        if missing > 0:
            out.extend(array("f", [0.0]) * (missing * 2))
        cursor += want * 1000 / AUDIO_RATE
    with open(path, "wb") as f:
        out.tofile(f)
    return True


def segment(run_dir, log, out, scale=1.40625, footer="OksaSDR  ·  receive only", loudness=BAND_LOUDNESS):
    frames_dir = os.path.join(run_dir, "frames")
    names = sorted(f for f in os.listdir(frames_dir) if f.endswith(".jpg"))
    times = [int(n[:-4]) for n in names]
    events = parse_log(log, scale)
    # The frame size and where it goes in the output.
    fw, fh = map(int, subprocess.check_output(
        ["magick", "identify", "-format", "%w %h", os.path.join(frames_dir, names[0])]).split())
    s = min(APP_W / fw, H / fh)
    ox = (APP_W - fw * s) / 2
    oy = (H - fh * s) / 2

    def xy(x, y):
        return ox + x * s, oy + y * s

    # Speed and cuts: source ms -> output seconds.
    controls = sorted((ms, v) for ms, k, v in events if k == "caption" and v.startswith("!") and v.split()[0] in ("!speed", "!cut", "!uncut"))

    def state_at(ms):
        speed, cut = 1.0, False
        for t, v in controls:
            if t > ms:
                break
            word = v.split()
            if word[0] == "!speed":
                speed = float(word[1])
            elif word[0] == "!cut":
                cut = True
            elif word[0] == "!uncut":
                cut = False
        return speed, cut

    out_t = []          # output start time of each frame (None when cut)
    durations = []
    speeds = []
    t_out = 0.0
    for i, ms in enumerate(times):
        nxt = times[i + 1] if i + 1 < len(times) else ms + 66
        speed, cut = state_at(ms)
        speeds.append(speed)
        if cut:
            out_t.append(None)
            durations.append(0)
            continue
        d = (nxt - ms) / 1000.0 / speed
        out_t.append(t_out)
        durations.append(d)
        t_out += d
    total = t_out

    def to_out(ms):
        """Output time of a source moment (the next kept frame's)."""
        for i, t in enumerate(times):
            if t >= ms and out_t[i] is not None:
                return out_t[i]
        return total

    # Captions, chapters, boxes and marks.
    lines = [ass_header()]
    caps = [(ms, v) for ms, k, v in events if k == "caption"]
    chapter = None
    chapter_start = 0.0
    cur = None          # (start, text, box)

    def flush_caption(end):
        if cur and cur[1]:
            lines.append(f"Dialogue: 1,{ass_time(cur[0])},{ass_time(end)},Caption,,0,0,0,,{esc(cur[1])}")
        if cur and cur[2]:
            x, y, w, h = cur[2]
            bx, by = xy(x, y)
            bw, bh = w * s, h * s
            lines.append(f"Dialogue: 2,{ass_time(cur[0])},{ass_time(end)},Shape,,0,0,0,,"
                         f"{{\\an7\\pos({bx:.0f},{by:.0f})\\1a&HFF&\\bord4\\3c&HFFB63F&\\fad(200,200)\\p1}}"
                         f"m 0 0 l {bw:.0f} 0 l {bw:.0f} {bh:.0f} l 0 {bh:.0f}")

    for ms, v in caps:
        t = to_out(ms)
        if v.startswith("#"):
            if chapter:
                lines.append(f"Dialogue: 1,{ass_time(chapter_start)},{ass_time(t)},Chapter,,0,0,0,,{esc(chapter)}")
            chapter, chapter_start = v[1:].strip(), t
            continue
        if v.startswith("!mark"):
            x, y = (float(p) for p in v.split()[1].split(","))
            events.append((ms, "tap", (x, y)))
            continue
        if v.startswith("!"):
            continue
        flush_caption(t)
        if v == "-":
            cur = None
            continue
        box = None
        m = re.match(r"\[box (\d+),(\d+),(\d+),(\d+)\]\s*(.*)$", v)
        if m:
            box = tuple(int(m.group(i)) for i in range(1, 5))
            v = m.group(5)
        cur = (t, v, box)
    flush_caption(total)
    if chapter:
        lines.append(f"Dialogue: 1,{ass_time(chapter_start)},{ass_time(total)},Chapter,,0,0,0,,{esc(chapter)}")
    lines.append(f"Dialogue: 0,{ass_time(0)},{ass_time(total)},Footer,,0,0,0,,{esc(footer)}")

    # Tap marks: a white ring that fades; a tap in a cut part is left out
    # (it would ring where the cut ends).
    for ms, k, v in events:
        if k in ("tap", "tapfrac") and state_at(ms)[1]:
            continue
        if k == "tap":
            x, y = xy(*v)
        elif k == "tapfrac":
            x, y = xy(v[0] * fw, v[1] * fh)
        else:
            continue
        t = to_out(ms)
        lines.append(f"Dialogue: 3,{ass_time(t)},{ass_time(t + 0.8)},Shape,,0,0,0,,"
                     f"{{\\an5\\pos({x:.0f},{y:.0f})\\1c&HFFFFFF&\\1a&H90&\\3c&HFFFFFF&\\bord3\\fad(0,450)\\p1}}{circle(30)}")

    ass = out[:-4] + ".ass"
    open(ass, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    concat = out[:-4] + ".txt"
    with open(concat, "w") as f:
        last = None
        for name, t0, d in zip(names, out_t, durations):
            if t0 is None or d <= 0:
                continue
            f.write(f"file '{os.path.join(frames_dir, name)}'\nduration {d:.4f}\n")
            last = name
        f.write(f"file '{os.path.join(frames_dir, last)}'\n")
    # Each frame fitted to the app's area (a phone turning changes the size).
    vf = (f"scale=w={APP_W}:h={H}:force_original_aspect_ratio=decrease:flags=lanczos,"
          f"pad={W}:{H}:x=({APP_W}-iw)/2:y=({H}-ih)/2:color={BG},"
          f"drawbox=x={APP_W}:y=0:w=2:h={H}:color=0x243040:t=fill,"
          f"fps={FPS},format=yuv420p,ass={ass}")
    audio = out[:-4] + ".f32"
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", concat]
    if band_audio(run_dir, times, out_t, durations, speeds, audio):
        cmd += ["-f", "f32le", "-ar", str(AUDIO_RATE), "-ac", "2", "-i", audio]
        af = f"loudnorm=I={loudness}:TP=-6:LRA=11,aresample=48000,afade=t=in:d=1,afade=t=out:st={max(0, total - 1.5):.2f}:d=1.5"
    else:
        cmd += ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo"]
        af = "aresample=48000"
    cmd += ["-vf", vf, "-af", af, "-map", "0:v", "-map", "1:a", "-c:v", "libx264", "-preset", "medium",
            "-crf", "20", "-r", str(FPS), "-c:a", "aac", "-b:a", "128k", "-t", f"{total:.3f}",
            "-movflags", "+faststart", out]
    subprocess.check_call(cmd)
    print(f"{out}: {total:.1f} s from {len(names)} frames")


def card(out, seconds, title, *rows, icon=None):
    """A dark card: the app icon over the title when given, the lines under it."""
    lines = [ass_header()]
    top = 440 if icon else 300
    lines.append(f"Dialogue: 0,{ass_time(0)},{ass_time(seconds)},CardTitle,,0,0,{top},,{{\\fad(400,400)}}{esc(title)}")
    body = "\\N".join(esc(r) for r in rows)
    lines.append(f"Dialogue: 0,{ass_time(0.3)},{ass_time(seconds)},CardLine,,0,0,{top + 190},,{{\\fad(400,400)}}{body}")
    ass = out[:-4] + ".ass"
    open(ass, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    # Inputs: the background, a silent track, and the icon when given.
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
           "-f", "lavfi", "-i", f"color=c={BG}:s={W}x{H}:r={FPS}:d={seconds}",
           "-f", "lavfi", "-t", str(seconds), "-i", "anullsrc=r=48000:cl=stereo"]
    if icon:
        cmd += ["-loop", "1", "-t", str(seconds), "-i", icon]
        graph = (f"[2:v]format=rgba,fade=in:st=0:d=0.4:alpha=1,fade=out:st={seconds - 0.4}:d=0.4:alpha=1[ic];"
                 f"[0:v][ic]overlay=x=(W-w)/2:y=120,format=yuv420p,ass={ass}[v]")
    else:
        graph = f"[0:v]format=yuv420p,ass={ass}[v]"
    cmd += ["-filter_complex", graph, "-map", "[v]", "-map", "1:a", "-c:v", "libx264", "-preset", "medium",
            "-crf", "20", "-r", str(FPS), "-c:a", "aac", "-b:a", "128k", "-shortest", out]
    subprocess.check_call(cmd)
    print(f"{out}: card {seconds} s")


def still(out, seconds, image):
    """A full-frame picture (the brand logo) fading in and out."""
    subprocess.check_call(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-loop", "1", "-t", str(seconds),
                           "-i", image, "-f", "lavfi", "-t", str(seconds), "-i", "anullsrc=r=48000:cl=stereo", "-vf",
                           f"scale={W}:{H},fps={FPS},fade=in:st=0:d=0.6,fade=out:st={seconds - 0.6}:d=0.6,format=yuv420p",
                           "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-r", str(FPS),
                           "-c:a", "aac", "-b:a", "128k", "-shortest", out])
    print(f"{out}: still {seconds} s")


def join(out, *segs):
    lst = out[:-4] + ".join.txt"
    with open(lst, "w") as f:
        for sgm in segs:
            f.write(f"file '{os.path.abspath(sgm)}'\n")
    subprocess.check_call(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0",
                           "-i", lst, "-c", "copy", "-movflags", "+faststart", out])
    print(f"{out}: joined {len(segs)}")


if __name__ == "__main__":
    what = sys.argv[1]
    if what == "segment":
        scale = float(sys.argv[sys.argv.index("--scale") + 1]) if "--scale" in sys.argv else 1.40625
        footer = sys.argv[sys.argv.index("--footer") + 1] if "--footer" in sys.argv else "OksaSDR  ·  receive only"
        loud = float(sys.argv[sys.argv.index("--loudness") + 1]) if "--loudness" in sys.argv else BAND_LOUDNESS
        segment(sys.argv[2], sys.argv[3], sys.argv[4], scale, footer, loud)
    elif what == "card":
        rest = sys.argv[4:]
        icon = None
        if "--icon" in rest:
            i = rest.index("--icon")
            icon = rest[i + 1]
            rest = rest[:i] + rest[i + 2:]
        card(sys.argv[2], float(sys.argv[3]), *rest, icon=icon)
    elif what == "still":
        still(sys.argv[2], float(sys.argv[3]), sys.argv[4])
    elif what == "join":
        join(sys.argv[2], *sys.argv[3:])
    else:
        sys.exit(__doc__)
