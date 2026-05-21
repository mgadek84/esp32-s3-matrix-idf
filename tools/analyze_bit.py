#!/usr/bin/env python3
"""Analyze MP3 and generate beat-sync LED timeline for ESP32-S3 Matrix."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import librosa
import numpy as np

SR = 22050
HOP = 512
REPO_ROOT = Path(__file__).resolve().parent.parent
OUT_H = REPO_ROOT / "main" / "bit_timeline.h"
OUT_JSON = REPO_ROOT / "tools" / "bit_analysis.json"


def detect_sections(y, sr):
    """Rough intro / drop / break / outro from energy curve."""
    rms = librosa.feature.rms(y=y, hop_length=HOP)[0]
    times = librosa.frames_to_time(np.arange(len(rms)), sr=sr, hop_length=HOP)
    smooth = np.convolve(rms, np.ones(31) / 31, mode="same")
    thresh = np.percentile(smooth, 55)
    loud = smooth > thresh

    sections = []
    in_loud = loud[0]
    for i in range(1, len(loud)):
        if loud[i] != in_loud:
            sections.append({"t": float(times[i]), "loud": in_loud})
            in_loud = loud[i]
    sections.append({"t": float(times[-1]), "loud": in_loud})

    labels = []
    loud_blocks = [s for s in sections if s["loud"]]
    if sections and not sections[0]["loud"]:
        labels.append({"start": 0.0, "end": sections[0]["t"], "kind": "intro"})
    for i, blk in enumerate(loud_blocks):
        idx = next(j for j, s in enumerate(sections) if s is blk)
        end = sections[idx + 1]["t"] if idx + 1 < len(sections) else float(times[-1])
        kind = "drop" if i == 0 else "verse" if i % 2 else "chorus"
        labels.append({"start": blk["t"], "end": end, "kind": kind})
    return labels


def analyze(mp3: Path, dump_json: bool) -> int:
    if not mp3.exists():
        print(f"Missing: {mp3}", file=sys.stderr)
        return 1

    print(f"Loading {mp3.name} ...")
    y, sr = librosa.load(mp3, sr=SR, mono=True)
    duration = float(librosa.get_duration(y=y, sr=sr))
    print(f"Duration: {duration:.2f}s")

    tempo, beat_frames = librosa.beat.beat_track(y=y, sr=sr, hop_length=HOP)
    beat_times = librosa.frames_to_time(beat_frames, sr=sr, hop_length=HOP)
    if hasattr(tempo, "__len__"):
        tempo = float(tempo[0]) if len(tempo) else 140.0
    else:
        tempo = float(tempo)
    print(f"Tempo: {tempo:.1f} BPM, beats: {len(beat_times)}")

    onset_env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=HOP)
    onset_times = librosa.frames_to_time(
        librosa.onset.onset_detect(onset_envelope=onset_env, sr=sr, hop_length=HOP, backtrack=True),
        sr=sr,
        hop_length=HOP,
    )

    S = np.abs(librosa.stft(y, n_fft=2048, hop_length=HOP))
    freqs = librosa.fft_frequencies(sr=sr, n_fft=2048)
    bass_mask = freqs < 150
    bass = S[bass_mask].mean(axis=0)
    bass_times = librosa.frames_to_time(np.arange(len(bass)), sr=sr, hop_length=HOP)
    bass_norm = bass / (bass.max() + 1e-9)

    hat_mask = (freqs >= 2000) & (freqs <= 8000)
    hat = S[hat_mask].mean(axis=0)
    hat_norm = hat / (hat.max() + 1e-9)

    sections = detect_sections(y, sr)

    events = []

    def add(ms: int, kind: int, r: int, g: int, b: int, param: int = 0):
        events.append({"ms": ms, "kind": kind, "r": r, "g": g, "b": b, "param": param})

    KICK = 0
    SNARE = 1
    BASS = 2
    HAT = 3
    DROP = 4
    BREAK = 5
    BUILD = 6
    FLASH = 7

    for sec in sections:
        ms = int(sec["start"] * 1000)
        if sec["kind"] == "intro":
            add(ms, BUILD, 8, 0, 24, 0)
        elif sec["kind"] == "drop":
            add(ms, DROP, 32, 0, 8, 1)
        elif sec["kind"] == "chorus":
            add(ms, FLASH, 32, 8, 0, 2)
        elif sec["kind"] == "verse":
            add(ms, BREAK, 0, 16, 32, 0)

    for i, t in enumerate(beat_times):
        ms = int(t * 1000)
        beat_in_bar = i % 4
        fi = np.argmin(np.abs(bass_times - t))
        bass_lvl = float(bass_norm[fi])
        hat_lvl = float(hat_norm[min(fi, len(hat_norm) - 1)])

        if beat_in_bar == 0:
            br = int(28 + 12 * bass_lvl)
            add(ms, KICK, br, 0, 4, int(bass_lvl * 255))
        elif beat_in_bar == 2:
            add(ms, SNARE, 32, 12, 0, 0)
        else:
            add(ms, BASS, 0, int(8 + 20 * bass_lvl), int(20 + 12 * bass_lvl), 0)

        if hat_lvl > 0.55 and beat_in_bar in (1, 3):
            add(ms + 30, HAT, 0, int(20 * hat_lvl), int(28 * hat_lvl), int(hat_lvl * 200))

    for t in onset_times:
        ms = int(t * 1000)
        if any(abs(ms - int(bt * 1000)) < 80 for bt in beat_times):
            continue
        fi = np.argmin(np.abs(bass_times - t))
        if bass_norm[fi] > 0.7:
            add(ms, BASS, 24, 0, 32, 1)
        elif hat_norm[fi] > 0.65:
            add(ms, HAT, 0, 24, 32, 0)

    events.sort(key=lambda e: e["ms"])
    seen = set()
    deduped = []
    for e in events:
        key = (e["ms"], e["kind"])
        if key in seen:
            continue
        seen.add(key)
        deduped.append(e)
    events = deduped

    duration_ms = int(duration * 1000)

    if dump_json:
        analysis = {
            "duration_ms": duration_ms,
            "tempo_bpm": round(tempo, 1),
            "beat_count": len(beat_times),
            "event_count": len(events),
            "sections": sections,
        }
        OUT_JSON.write_text(
            json.dumps({"analysis": analysis, "events": events}, indent=2),
            encoding="utf-8",
        )
        print(f"Wrote {OUT_JSON}")

    lines = [
        "/* Auto-generated by tools/analyze_bit.py — do not edit by hand */",
        "#pragma once",
        "",
        f"#define BIT_DURATION_MS {duration_ms}",
        f"#define BIT_TEMPO_BPM {int(round(tempo))}",
        "",
        "typedef enum {",
        "    EVT_KICK = 0,",
        "    EVT_SNARE,",
        "    EVT_BASS,",
        "    EVT_HAT,",
        "    EVT_DROP,",
        "    EVT_BREAK,",
        "    EVT_BUILD,",
        "    EVT_FLASH,",
        "} bit_event_kind_t;",
        "",
        "typedef struct {",
        "    uint32_t ms;",
        "    uint8_t kind;",
        "    uint8_t r, g, b;",
        "    uint8_t param;",
        "} bit_event_t;",
        "",
        f"#define BIT_EVENT_COUNT {len(events)}",
        "",
        "static const bit_event_t BIT_TIMELINE[] = {",
    ]
    for e in events:
        lines.append(
            f"    {{ {e['ms']:5d}, {e['kind']}, {e['r']:3d}, {e['g']:3d}, {e['b']:3d}, {e['param']:3d} }},"
        )
    lines.append("};")
    lines.append("")

    OUT_H.write_text("\n".join(lines), encoding="utf-8")
    print(f"Events: {len(events)}, wrote {OUT_H}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate bit_timeline.h from an MP3")
    parser.add_argument("mp3", type=Path, nargs="?", help="Path to MP3 (required)")
    parser.add_argument(
        "--json",
        action="store_true",
        help="Also write tools/bit_analysis.json (debug, gitignored)",
    )
    args = parser.parse_args()
    if args.mp3 is None:
        parser.error("mp3 path is required")
    return analyze(args.mp3.resolve(), args.json)


if __name__ == "__main__":
    raise SystemExit(main())
