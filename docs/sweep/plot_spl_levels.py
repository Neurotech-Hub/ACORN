#!/usr/bin/env python3
"""Clustered bar chart of ACORN maximum SPL versus volume.

The levels are manual sound-level-meter readings of REW signals played through
ACORN: a 1 kHz sine and pink noise, each at five volume settings. Writes PNG,
SVG, and PDF next to the frequency-response figure.

    python3 docs/sweep/plot_spl_levels.py
    python3 docs/sweep/plot_spl_levels.py --out-dir docs/assets --stem acorn-spl-levels
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import MultipleLocator

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / "docs" / "assets"

# Volume setting, then maximum SPL (dB) for the 1 kHz tone and for pink noise.
VOLUME = np.array([10, 25, 50, 75, 100], dtype=float)
TONE_1KHZ_DB = np.array([69.2, 82.4, 94.3, 100.9, 104.8])
PINK_DB = np.array([68.7, 82.6, 94.2, 100.7, 104.9])

SERIES = (
    ("1 kHz", TONE_1KHZ_DB, "#1a1a1a"),
    ("Pink noise", PINK_DB, "#b7b1a6"),
)


def style_axes(ax: plt.Axes) -> None:
    ax.set_xlabel("Volume")
    ax.set_ylabel("Maximum SPL (dB)")
    ax.set_title("ACORN | Sound Pressure Level", pad=12, loc="left", fontweight="medium")
    ax.set_xticks(np.arange(VOLUME.size))
    ax.set_xticklabels([str(int(v)) for v in VOLUME])
    ax.set_ylim(0, 120)
    ax.yaxis.set_major_locator(MultipleLocator(20))
    ax.set_axisbelow(True)
    ax.grid(True, which="major", axis="y", color="#d5d5d0", linewidth=0.6)
    ax.tick_params(which="major", length=4, width=0.7, color="#2a2a2a", labelsize=10)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    for spine in ax.spines.values():
        spine.set_color("#2a2a2a")
        spine.set_linewidth(0.7)
    ax.legend(frameon=False, loc="upper left", fontsize=10)


def annotate(ax: plt.Axes, bars) -> None:
    for bar in bars:
        height = bar.get_height()
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            height + 1.4,
            f"{height:.1f}",
            ha="center",
            va="bottom",
            fontsize=8.5,
            color="#1a1a1a",
        )


def plot(out_dir: Path, stem: str) -> list[Path]:
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
    fig, ax = plt.subplots(figsize=(7.6, 3.0), layout="constrained")
    x = np.arange(VOLUME.size)
    width = 0.36
    offsets = (-width / 2, width / 2)
    for (label, levels, color), offset in zip(SERIES, offsets):
        bars = ax.bar(x + offset, levels, width, label=label, color=color, zorder=3)
        annotate(ax, bars)
    style_axes(ax)

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
    parser = argparse.ArgumentParser(description="Plot maximum SPL for the 1 kHz tone and pink noise.")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT, help="Directory for PNG, SVG, and PDF")
    parser.add_argument("--stem", default="acorn-spl-levels", help="Output filename without an extension")
    args = parser.parse_args()

    paths = plot(args.out_dir, args.stem)
    for label, levels, _color in SERIES:
        print(f"{label}: " + ", ".join(f"{int(v)} → {db:.1f} dB" for v, db in zip(VOLUME, levels)))
    for path in paths:
        print(f"Wrote {path}")


if __name__ == "__main__":
    main()
