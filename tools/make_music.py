#!/usr/bin/env python3
"""Turn a recording of the shop music into /ds-shop/music.bin for the DS.

The DS sound hardware plays IMA-ADPCM and loops it by itself, so the shop
just loads this file and starts one channel: no CPU time, no streaming.

    tools/make_music.py "19. Nintendo DSi Shop.mp3" sdcard/ds-shop/music.bin

The defaults fit the DSi Shop theme: the song repeats every 60.8 s from its
first note (152 beats at 150 BPM), except that the first ~6 s play a little
differently the first time through. So the file keeps an 8 s lead-in plus one
full loop, and the DS jumps back to the end of the lead-in. The jump lands in
the middle of the music, where the notes still ringing match the ones from
the end of the loop, so there's no click. For other music, give --loop-start
and --loop-length in seconds (--find-loop measures the length for you).

File layout (little-endian):
    0   'DSMU'
    4   u32 sample rate (Hz)
    8   u32 loop start, in 32-bit words from the start of the ADPCM data
    12  u32 ADPCM data size in bytes (a multiple of 4)
    16  ADPCM data: the DS's 4-byte header (s16 first sample, u8 step index,
        u8 0), then 4-bit samples, low nibble first

Needs ffmpeg and numpy.
"""
import argparse
import struct
import subprocess
import sys

import numpy as np

STEPS = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
]
INDEX_ADJ = [-1, -1, -1, -1, 2, 4, 6, 8]


def decode_audio(path, rate):
    pcm = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-vn", "-ac", "1", "-ar", str(rate),
         "-af", "aresample=resampler=soxr", "-f", "s16le", "-"],
        check=True, stdout=subprocess.PIPE).stdout
    return np.frombuffer(pcm, dtype=np.int16).astype(np.int32)


def find_loop(a, rate, guess, start=5.0, window=20.0):
    """The loop length (in samples) near `guess` seconds, by correlating a
    window of the song with the same window one loop later."""
    x = a.astype(np.float64)
    s, w = int(start * rate), int(window * rate)
    base = x[s:s + w]

    def corr(n):
        b = x[s + n:s + n + w]
        return float(np.dot(base, b) / (np.linalg.norm(base) * np.linalg.norm(b)))

    lo, hi = int((guess - 0.5) * rate), int((guess + 0.5) * rate)
    best = max(range(lo, hi, 4), key=corr)
    best = max(range(best - 4, best + 5), key=corr)
    return best, corr(best)


def encode_adpcm(samples):
    """IMA-ADPCM as the DS hardware decodes it (see GBATEK "DS Sound")."""
    pred, index = int(samples[0]), 0
    out = bytearray(struct.pack("<hBB", pred, index, 0))
    nibbles = []
    for s in samples:
        step = STEPS[index]
        diff = int(s) - pred
        code = 0
        if diff < 0:
            code, diff = 8, -diff
        # pick the 3 magnitude bits the same way the decoder adds them up
        delta = step >> 3
        if diff >= step:
            code |= 4; diff -= step; delta += step
        if diff >= step >> 1:
            code |= 2; diff -= step >> 1; delta += step >> 1
        if diff >= step >> 2:
            code |= 1; delta += step >> 2
        pred = max(pred - delta, -0x7FFF) if code & 8 else min(pred + delta, 0x7FFF)
        index = min(max(index + INDEX_ADJ[code & 7], 0), 88)
        nibbles.append(code)
    for i in range(0, len(nibbles), 2):
        out.append(nibbles[i] | (nibbles[i + 1] << 4))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--rate", type=int, default=16000, help="sample rate (default 16000)")
    ap.add_argument("--loop-start", type=float, default=8.0,
                    help="seconds of lead-in before the loop point (default 8.0)")
    ap.add_argument("--loop-length", type=float, default=60.8,
                    help="seconds per loop (default 60.8, the DSi Shop theme)")
    ap.add_argument("--find-loop", action="store_true",
                    help="measure the loop length near --loop-length instead of trusting it")
    ap.add_argument("--gain", type=float, default=1.0, help="volume multiplier")
    args = ap.parse_args()

    a = decode_audio(args.input, args.rate)
    loop_len = round(args.loop_length * args.rate)
    if args.find_loop:
        loop_len, c = find_loop(a, args.rate, args.loop_length)
        print(f"loop length: {loop_len / args.rate:.4f} s (correlation {c:.4f})")

    # 8 samples per 32-bit word: the loop point and the end must land on words
    start = round(args.loop_start * args.rate) // 8 * 8
    loop_len = loop_len // 8 * 8
    end = start + loop_len
    if end > len(a):
        sys.exit(f"the recording is too short for a {args.loop_start}+{args.loop_length} s loop")

    pcm = np.clip(a[:end] * args.gain, -32767, 32767).astype(np.int32)
    data = encode_adpcm(pcm)
    # the header word comes first, so sample n is in word 1 + n // 8
    loop_word = 1 + start // 8
    with open(args.output, "wb") as f:
        f.write(b"DSMU" + struct.pack("<III", args.rate, loop_word, len(data)))
        f.write(data)
    print(f"{args.output}: {end / args.rate:.1f} s at {args.rate} Hz, "
          f"loops back to {start / args.rate:.2f} s, {16 + len(data)} bytes")


if __name__ == "__main__":
    main()
