#!/usr/bin/env python3
"""Compare aligned PCM16 WAV renders without changing gain or timing."""
# SPDX-License-Identifier: Apache-2.0
import argparse
from array import array
import json
import math
from pathlib import Path
import sys
import wave


class Signal:
    def __init__(self):
        self.samples = self.total = self.squares = self.peak = self.full_scale = 0

    def add(self, value):
        self.samples += 1
        self.total += value
        self.squares += value * value
        self.peak = max(self.peak, abs(value))
        self.full_scale += value in (-32768, 32767)

    def report(self):
        rms = math.sqrt(self.squares / self.samples) if self.samples else 0.0
        return {"samples": self.samples, "peak_pcm": self.peak,
                "rms_pcm": rms, "rms_dbfs": 20 * math.log10(rms / 32768) if rms else None,
                "dc_pcm": self.total / self.samples if self.samples else 0.0,
                "full_scale_samples": self.full_scale}


def samples(reader, frames, channels):
    data = reader.readframes(frames)
    if len(data) % (2 * channels):
        raise ValueError("WAV contains an incomplete PCM16 frame")
    result = array("h")
    # wave.readframes() already returns samples in native byte order.
    result.frombytes(data)
    return result


def compare(reference, candidate, *, start_frame=0, frames=None):
    if start_frame < 0 or (frames is not None and frames <= 0):
        raise ValueError("start frame must be nonnegative and frame count must be positive")
    with wave.open(str(reference), "rb") as old, wave.open(str(candidate), "rb") as new:
        for reader in (old, new):
            if reader.getsampwidth() != 2 or reader.getcomptype() != "NONE":
                raise ValueError("only uncompressed PCM16 WAV is supported")
        rate, channels = old.getframerate(), old.getnchannels()
        if rate <= 0 or new.getframerate() <= 0:
            raise ValueError("sample rate must be positive")
        if (rate, channels) != (new.getframerate(), new.getnchannels()):
            raise ValueError("sample rate and channel count must match; no automatic resampling")
        source_old, source_new = old.getnframes(), new.getnframes()
        for reader in (old, new):
            available = reader.getnframes() - start_frame
            if available < 0 or (frames is not None and frames > available):
                raise ValueError("requested comparison interval must fit in both WAV files")
            reader.setpos(start_frame)
        expected_old = source_old - start_frame if frames is None else frames
        expected_new = source_new - start_frame if frames is None else frames
        before, after = [Signal() for _ in range(channels)], [Signal() for _ in range(channels)]
        errors = [Signal() for _ in range(channels)]
        changed = [0] * channels
        read_old = read_new = frame = 0
        first_difference = last_difference = None
        missing_changed = 0
        while True:
            count = 4096 if frames is None else min(4096, frames - frame)
            if count == 0:
                break
            a, b = samples(old, count, channels), samples(new, count, channels)
            read_old += len(a) // channels
            read_new += len(b) // channels
            if not a and not b:
                break
            for index in range(max(len(a), len(b))):
                channel = index % channels
                has_a, has_b = index < len(a), index < len(b)
                x, y = a[index] if has_a else 0, b[index] if has_b else 0
                if has_a:
                    before[channel].add(x)
                if has_b:
                    after[channel].add(y)
                # Error energy uses the union of the timelines. Missing tails
                # are explicitly reported, rather than silently truncated.
                errors[channel].add(y - x)
                if x != y:
                    changed[channel] += 1
                    at = start_frame + frame + index // channels
                    if first_difference is None:
                        first_difference = at
                    last_difference = at
                    missing_changed += has_a != has_b
            frame += max(len(a), len(b)) // channels
        if read_old != expected_old or read_new != expected_new:
            raise ValueError("WAV data is truncated relative to its declared frame count")
        results = []
        for channel in range(channels):
            results.append({"channel": channel, "reference": before[channel].report(),
                            "candidate": after[channel].report(), "difference": errors[channel].report(),
                            "changed_samples": changed[channel]})
        return {"sample_rate": rate, "channels": channels,
                "comparison_start_frame": start_frame, "comparison_frame_limit": frames,
                "reference_source_frames": source_old, "candidate_source_frames": source_new,
                "reference_frames": read_old, "candidate_frames": read_new,
                "reference_seconds": read_old / rate, "candidate_seconds": read_new / rate,
                "identical_pcm": read_old == read_new and not any(changed),
                "first_different_frame": first_difference, "last_different_frame": last_difference,
                "reference_only_frames": max(0, read_old - read_new),
                "candidate_only_frames": max(0, read_new - read_old),
                "nonzero_samples_in_unmatched_tail": missing_changed,
                "difference_timeline": "union; missing tail samples are zero for difference metrics",
                "channel_metrics": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--start-frame", type=int, default=0,
                        help="start at this frame in both inputs (default: 0)")
    parser.add_argument("--frames", type=int,
                        help="compare exactly this many frames; interval must fit in both inputs")
    parser.add_argument("--require-identical", action="store_true",
                        help="exit 1 if PCM data or frame counts differ")
    args = parser.parse_args()
    try:
        result = compare(args.reference, args.candidate,
                         start_frame=args.start_frame, frames=args.frames)
    except (OSError, ValueError, wave.Error, EOFError) as error:
        parser.exit(2, f"error: {error}\n")
    print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))
    return 1 if args.require_identical and not result["identical_pcm"] else 0


if __name__ == "__main__":
    sys.exit(main())
