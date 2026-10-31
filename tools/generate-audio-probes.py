#!/usr/bin/env python3
"""Generate original PCM16 audio probes; optionally encode audio-track SMAF."""
# SPDX-License-Identifier: Apache-2.0
import argparse
from array import array
import binascii
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import wave


def envelope(t, duration):
    return max(0.0, min(1.0, t / 0.02, (duration - t) / 0.02))


def signal(kind, t, duration):
    if kind == "silence":
        return 0.0
    if kind == "quiet":
        local = t - 0.5
        return (0.003 * math.sin(2 * math.pi * 440 * local)
                * envelope(local, duration - 1.0)) if 0 <= local < duration - 1.0 else 0.0
    if kind == "steps":
        return 0.2 if 0.5 <= t < 0.65 else -0.2 if 1.5 <= t < 1.65 else 0.0
    if kind == "sweep":
        start, end = 80.0, 9000.0
        k = math.log(end / start) / duration
        phase = start * math.expm1(k * t) / k
        return 0.25 * math.sin(2 * math.pi * phase) * envelope(t, duration)
    if kind == "high_band":
        return 0.12 * (math.sin(2 * math.pi * 440 * t)
                       + math.sin(2 * math.pi * 17000 * t)) * envelope(t, duration)
    raise ValueError("unknown probe")


CASES = (
    ("silence", 8000, 2.0, "All-zero PCM input; codec reconstruction may contain quantization noise."),
    ("quiet", 8000, 3.0, "Quiet 440 Hz tone with leading/trailing silence; no gain normalization."),
    ("steps", 8000, 3.0, "Positive/negative steps for codec state and transient comparison."),
    ("sweep", 22050, 3.0, "80 Hz to 9 kHz logarithmic sweep with 20 ms edge fades."),
    ("high_band", 44100, 3.0, "440 Hz plus 17 kHz; observes high-band reconstruction."),
)


def quantize(value):
    value = max(-1.0, min(1.0, value))
    return int(math.copysign(math.floor(abs(value) * 32767 + 0.5), value))


def native_pcm16_to_le(data):
    """Convert wave.readframes' native-order PCM16 to FFmpeg's s16le order."""
    pcm = array('h')
    pcm.frombytes(data)
    if sys.byteorder == 'big':
        pcm.byteswap()
    return pcm.tobytes()


def bounded_chunks(data, allow_crc=False):
    """Return intact chunks and a possible trailer at a proven chunk boundary."""
    chunks = []
    offset = 0
    while offset < len(data):
        remaining = len(data) - offset
        if allow_crc and remaining == 2:
            return chunks, data[offset:]
        if remaining < 8:
            raise ValueError("truncated SMAF chunk header")
        size = struct.unpack_from('>I', data, offset + 4)[0]
        end = offset + 8 + size
        if end > len(data):
            raise ValueError("SMAF chunk exceeds its parent length")
        chunks.append(data[offset:end])
        offset = end
    return chunks, b''


def normalize_smaf(data):
    """Normalize only the known FFmpeg mono audio-track container shape."""
    if len(data) < 8 or data[:4] != b'MMMD':
        raise ValueError("expected MMMD container")
    if struct.unpack_from('>I', data, 4)[0] != len(data) - 8:
        raise ValueError("MMMD length does not match the complete file")
    chunks, trailer = bounded_chunks(data[8:], allow_crc=True)
    if trailer and int.from_bytes(trailer, 'big') != (binascii.crc_hqx(data[:-2], 65535) ^ 65535):
        raise ValueError("invalid existing SMAF CRC")
    tags = [part[:4] for part in chunks]
    if tags not in ([b'CNTI', b'ATR\0'], [b'CNTI', b'OPDA', b'ATR\0']):
        raise ValueError("unsupported FFmpeg audio container shape")
    if len(chunks[0]) != 13:
        raise ValueError("unsupported FFmpeg CNTI size")
    removed = len(chunks) == 3
    if removed and not (chunks[1][8:].startswith(b'VN:') and chunks[1].endswith(b',')):
        raise ValueError("unsupported FFmpeg OPDA metadata")
    atr = chunks[-1]
    if len(atr) < 14 or atr[8:10] != b'\0\0':
        raise ValueError("unsupported FFmpeg ATR header")
    # Six-byte HPS audio header, then Atsq and raw Awa1 ADPCM data. Restrict the
    # codec/rate headers to the mono Yamaha ADPCM rates used by these probes.
    if atr[10:14] not in (bytes((0x11, 0, 2, 2)), bytes((0x13, 0, 2, 2)),
                         bytes((0x14, 0, 2, 2))):
        raise ValueError("unsupported FFmpeg ATR codec/rate")
    nested, rest = bounded_chunks(atr[14:])
    if rest or [part[:4] for part in nested] != [b'Atsq', b'Awa\x01']:
        raise ValueError("unsupported FFmpeg ATR subchunks")
    if any(len(part) == 8 for part in nested):
        raise ValueError("empty FFmpeg sequence or audio payload")
    payload = chunks[0] + atr
    normalized = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
    normalized += struct.pack('>H', binascii.crc_hqx(normalized, 65535) ^ 65535)
    record = {
        'operation': 'remove optional raw FFmpeg OPDA; preserve CNTI and ATR; write outer CRC',
        'encoded_bytes': len(data), 'encoded_sha256': hashlib.sha256(data).hexdigest(),
        'encoded_crc_present': bool(trailer), 'removed_opda': removed,
        'normalized_bytes': len(normalized), 'normalized_crc': normalized[-2:].hex(),
        'preserved_cnti_sha256': hashlib.sha256(chunks[0]).hexdigest(),
        'preserved_atr_sha256': hashlib.sha256(atr).hexdigest(),
    }
    return normalized, record


def generate(directory, smaf=False, ffmpeg=None):
    if smaf:
        ffmpeg = ffmpeg or shutil.which("ffmpeg")
        if not ffmpeg:
            raise ValueError("--smaf requires FFmpeg with the mmf muxer and adpcm_yamaha encoder")
    # Refuse existing directories, including empty ones: never overwrite captures.
    directory.mkdir(parents=True, exist_ok=False)
    manifest = {"purpose": "original synthetic audio-track probes, not FM/ROM calibration",
                "smaf_validation": "normalized chunk lengths/CRC; unchanged FFmpeg PCM; handset/external-player acceptance unverified",
                "cases": []}
    if smaf:
        version = subprocess.run([ffmpeg, "-version"], check=True, capture_output=True, text=True)
        manifest["ffmpeg"] = version.stdout.splitlines()[0]
    for kind, rate, duration, description in CASES:
        name = f"{kind}-{rate}"
        source = directory / (name + ".wav")
        pcm = array("h", (quantize(signal(kind, n / rate, duration))
                          for n in range(round(rate * duration))))
        # wave.writeframes expects native-order PCM and handles big-endian hosts.
        with wave.open(str(source), "wb") as out:
            out.setparams((1, 2, rate, 0, "NONE", "not compressed"))
            out.writeframes(pcm.tobytes())
        files = [source]
        if smaf:
            mmf = directory / (name + ".mmf")
            reference = directory / (name + ".decoded.wav")
            subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-n",
                            "-i", str(source), "-map_metadata", "-1", "-flags", "+bitexact",
                            "-c:a", "adpcm_yamaha", "-f", "mmf", str(mmf)], check=True)
            normalized, normalization = normalize_smaf(mmf.read_bytes())
            original_decode = subprocess.run(
                [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-i", str(mmf),
                 "-map_metadata", "-1", "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"],
                check=True, capture_output=True).stdout
            mmf.write_bytes(normalized)
            subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-n",
                            "-i", str(mmf), "-map_metadata", "-1", "-c:a", "pcm_s16le",
                            str(reference)], check=True)
            files.extend((mmf, reference))
        record = {"id": name, "description": description,
                  "source_rate": rate, "source_frames": len(pcm), "source_seconds": duration,
                  "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
        if smaf:
            with wave.open(str(reference), "rb") as decoded:
                record["decoded_rate"] = decoded.getframerate()
                record["decoded_frames"] = decoded.getnframes()
                record["decoded_channels"] = decoded.getnchannels()
                decoded_pcm = native_pcm16_to_le(decoded.readframes(decoded.getnframes()))
            if decoded_pcm != original_decode:
                raise ValueError("normalization changed FFmpeg-decoded PCM")
            normalization['decoded_pcm_unchanged'] = True
            normalization['decoded_pcm_sha256'] = hashlib.sha256(decoded_pcm).hexdigest()
            record['normalization'] = normalization
            record['padding_frames'] = record['decoded_frames'] - record['source_frames']
        manifest["cases"].append(record)
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (directory / "README.txt").write_text(
        "MMF Pocket original synthetic audio probes\n\n"
        "These are test signals, not songs or Yamaha ROM samples.\n"
        "*.wav: original PCM16 input. *.mmf: optional normalized FFmpeg audio-track-only SMAF.\n"
        "*.decoded.wav: independent FFmpeg decode of that MMF, not the original PCM.\n"
        "ADPCM is lossy: input and decoded WAV should not be expected to match.\n"
        "The encoder may pad the tail; source/decoded frame counts are in manifest.json.\n"
        "Use the SAME MMF in each player; record settings and the recording chain.\n"
        "These audio tracks do not test FM parameters, MIDI controllers or ROM timbres.\n"
        "MMF lengths/CRC are normalized; CNTI/ATR payloads and decoded PCM are preserved.\n"
        "The manifest records original hashes, normalization and decoded padding.\n"
        "Handset and external-player acceptance remain unverified.\n"
        "Do not treat raw WAV differences as a perceptual quality score.\n"
        "See docs/reference-capture.md and docs/audio-comparison.md in the repository.\n",
        encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--smaf", action="store_true", help="also encode .mmf and decode reference WAV with FFmpeg")
    args = parser.parse_args()
    try:
        manifest = generate(args.directory, args.smaf)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"error: {error}\n")
    print(f"Generated {len(manifest['cases'])} probes in {args.directory}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
