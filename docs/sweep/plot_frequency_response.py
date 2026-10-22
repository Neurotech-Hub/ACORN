#!/usr/bin/env python3
"""Publication frequency-response plot from a Room EQ Wizard export.

Reads the frequency and SPL columns of an REW measurement text file (phase is
ignored), applies 1/6-octave smoothing by averaging acoustic power on a
logarithmic frequency axis, and normalizes so the mean level from 500 Hz to
2 kHz is 0 dB. Writes PNG, SVG, and PDF.

Reuse with another export:

    python3 docs/sweep/plot_frequency_response.py path/to/export.txt
    python3 docs/sweep/plot_frequency_response.py export.txt --out-dir docs/assets --stem my-response
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import LogLocator, MultipleLocator

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXPORT = Path(__file__).resolve().parent / "ACORN_sweep_export_REW.txt"
DEFAULT_OUT = ROOT / "docs" / "assets"

F_MIN_HZ = 100.0
F_MAX_HZ = 20_000.0
REF_MIN_HZ = 500.0
REF_MAX_HZ = 2_000.0
OCTAVE_FRACTION = 6.0
POINTS_PER_OCTAVE = 64

X_TICKS = [100, 200, 500, 1_000, 2_000, 5_000, 10_000, 20_000]
X_LABELS = ["100", "200", "500", "1k", "2k", "5k", "10k", "20k"]


def load_rew(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Return frequency (Hz) and SPL (dB), dropping the phase column and comments."""
    rows = []
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("*") or line.lower().startswith("freq"):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                freq = float(parts[0])
                spl = float(parts[1])
            except ValueError:
                continue
            if np.isfinite(freq) and np.isfinite(spl) and freq > 0:
                rows.append((freq, spl))
    if len(rows) < 2:
        raise ValueError(f"No measurement rows found in {path}")
    data = np.asarray(rows, dtype=np.float64)
    order = np.argsort(data[:, 0])
    return data[order, 0], data[order, 1]


def smooth_power_octave(freq_hz: np.ndarray, spl_db: np.ndarray, fraction: float) -> np.ndarray:
    """Average acoustic power over a 1/fraction-octave window centered on each bin.

    The window is rectangular in log2(frequency), half a fraction-octave wide
    on either side of the bin. Power is 10^(SPL/10); the result is converted
    back to dB. Averaging dB directly would under-weight louder bins.
    """
    log_f = np.log2(freq_hz)
    power = np.power(10.0, spl_db / 10.0)
    half = 0.5 / fraction
    left = np.searchsorted(log_f, log_f - half, side="left")
    right = np.searchsorted(log_f, log_f + half, side="right")
    prefix = np.empty(power.size + 1, dtype=np.float64)
    prefix[0] = 0.0
    np.cumsum(power, out=prefix[1:])
    counts = np.maximum(right - left, 1).astype(np.float64)
    return 10.0 * np.log10((prefix[right] - prefix[left]) / counts)


def log_frequency_mean(freq_hz: np.ndarray, spl_db: np.ndarray, f_lo: float, f_hi: float) -> float:
    """Mean level across a band, weighted uniformly per octave rather than per Hz."""
    mask = (freq_hz >= f_lo) & (freq_hz <= f_hi)
    if np.count_nonzero(mask) < 2:
        raise ValueError(f"Not enough points between {f_lo:g} and {f_hi:g} Hz to normalize")
    log_f = np.log(freq_hz[mask])
    integrate = getattr(np, "trapezoid", np.trapz)
    return float(integrate(spl_db[mask], log_f) / (log_f[-1] - log_f[0]))


def resample_log(freq_hz: np.ndarray, spl_db: np.ndarray, f_lo: float, f_hi: float, points_per_octave: int):
    """Sample a slowly varying curve on a dense log grid for a compact vector path."""
    n = int(np.ceil(np.log2(f_hi / f_lo) * points_per_octave)) + 1
    grid = np.geomspace(f_lo, f_hi, n)
    sampled = np.interp(np.log(grid), np.log(freq_hz), spl_db)
    return grid, sampled


def process(freq_hz: np.ndarray, spl_db: np.ndarray) -> tuple[np.ndarray, np.ndarray, float]:
    """Smooth, crop to the analysis band, and normalize. Returns Hz, relative dB, and the removed offset."""
    # Keep a margin outside the band so the windows at 100 Hz and 20 kHz stay symmetric.
    half = 0.5 / OCTAVE_FRACTION
    margin = (freq_hz >= F_MIN_HZ / 2**half) & (freq_hz <= F_MAX_HZ * 2**half)
    smoothed = smooth_power_octave(freq_hz[margin], spl_db[margin], OCTAVE_FRACTION)
    freq_m = freq_hz[margin]
    band = (freq_m >= F_MIN_HZ) & (freq_m <= F_MAX_HZ)
    freq_b = freq_m[band]
    spl_b = smoothed[band]
    offset = log_frequency_mean(freq_b, spl_b, REF_MIN_HZ, REF_MAX_HZ)
    grid, sampled = resample_log(freq_b, spl_b - offset, F_MIN_HZ, F_MAX_HZ, POINTS_PER_OCTAVE)
    return grid, sampled, offset


def style_axes(ax: plt.Axes, spl_db: np.ndarray) -> None:
    ax.set_xscale("log")
    ax.set_xlim(F_MIN_HZ, F_MAX_HZ)
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Relative Sound Level (dB)")
    ax.set_title("ACORN | Acoustic Frequency Response", pad=12, loc="left", fontweight="medium")

    ax.set_xticks(X_TICKS)
    ax.set_xticklabels(X_LABELS)
    ax.xaxis.set_minor_locator(LogLocator(base=10.0, subs=np.arange(2, 10) * 0.1))

    span = float(spl_db.max() - spl_db.min())
    step = 5.0 if span <= 40 else 10.0
    # Snap outward to a gridline, keeping a small margin so the curve does not sit on the frame.
    y_lo = np.floor((spl_db.min() - 1.0) / step) * step
    y_hi = np.ceil((spl_db.max() + 1.0) / step) * step
    if y_hi <= y_lo:
        y_hi = y_lo + step
    ax.set_ylim(y_lo, y_hi)
    ax.yaxis.set_major_locator(MultipleLocator(step))

    ax.set_axisbelow(True)
    ax.grid(True, which="major", axis="both", color="#d5d5d0", linewidth=0.6)
    ax.grid(True, which="minor", axis="x", color="#ecece8", linewidth=0.45)
    ax.tick_params(which="major", length=4, width=0.7, color="#2a2a2a", labelsize=10)
    ax.tick_params(which="minor", length=2.5, width=0.5, color="#6a6a66")
    for spine in ax.spines.values():
        spine.set_color("#2a2a2a")
        spine.set_linewidth(0.7)


def plot(freq_hz: np.ndarray, spl_db: np.ndarray, out_dir: Path, stem: str) -> list[Path]:
    plt.rcParams.update(
        {
            "font.family": "sans-serif",
            "font.sans-serif": ["Helvetica Neue", "Helvetica", "Arial", "DejaVu Sans"],
            "font.size": 11,
            "axes.labelsize": 12,
            "axes.titlesize": 14,
            "axes.linewidth": 0.7,
            "figure.facecolor": "white",
            "axes.facecolor": "white",
            "savefig.facecolor": "white",
            "axes.unicode_minus": False,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
            "svg.fonttype": "none",
        }
    )
    fig, ax = plt.subplots(figsize=(7.6, 4.5), layout="constrained")
    ax.plot(freq_hz, spl_db, color="#1a1a1a", linewidth=1.6, solid_capstyle="round", solid_joinstyle="round")
    style_axes(ax, spl_db)

    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for suffix, kwargs in (
        ("png", {"dpi": 600}),
        ("svg", {}),
        ("pdf", {}),
    ):
        path = out_dir / f"{stem}.{suffix}"
        fig.savefig(path, **kwargs)
        paths.append(path)
    plt.close(fig)
    return paths


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot a normalized frequency response from an REW export.")
    parser.add_argument("export", nargs="?", type=Path, default=DEFAULT_EXPORT, help="REW measurement text export")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT, help="Directory for PNG, SVG, and PDF")
    parser.add_argument("--stem", default="acorn-frequency-response", help="Output filename without an extension")
    args = parser.parse_args()

    freq, spl = load_rew(args.export)
    grid, relative, offset = process(freq, spl)
    paths = plot(grid, relative, args.out_dir, args.stem)

    print(f"Source: {args.export}")
    print(f"Points parsed: {freq.size}")
    print(f"Reference offset removed (500 Hz–2 kHz mean): {offset:.2f} dB")
    print(f"Relative range: {relative.min():.1f} to {relative.max():.1f} dB")
    for path in paths:
        print(f"Wrote {path}")


if __name__ == "__main__":
    main()
