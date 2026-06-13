from __future__ import annotations

import argparse
from pathlib import Path

from png_pixel_tools import flip_horizontal, paste, read_png, write_png


ACTIONS = (
    ("chop", ("action_frame_00.png", "action_frame_01.png")),
    ("mine", ("action_frame_02.png", "action_frame_03.png")),
    ("dig", ("action_frame_04.png", "action_frame_05.png")),
)


def build_strips(frame_directory: Path, output_directory: Path) -> None:
    frame_directory = frame_directory.resolve()
    output_directory = output_directory.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)

    print(f"[assets] Building player action strips from '{frame_directory}'.")

    written = 0
    for action_name, frame_names in ACTIONS:
        frames = []
        frame_width = frame_height = None
        for frame_name in frame_names:
            width, height, pixels = read_png(frame_directory / frame_name)
            if frame_width is None:
                frame_width = width
                frame_height = height
            elif width != frame_width or height != frame_height:
                raise ValueError(f"Frame '{frame_name}' does not match the first frame size.")
            frames.append(pixels)

        assert frame_width is not None and frame_height is not None
        strip_width = frame_width * len(frames)
        strip_height = frame_height
        strip = [(0, 0, 0, 0) for _ in range(strip_width * strip_height)]

        for index, pixels in enumerate(frames):
            paste(
                strip,
                strip_width,
                strip_height,
                pixels,
                frame_width,
                frame_height,
                index * frame_width,
                0,
            )

        left_path = output_directory / f"player_{action_name}_left_32x48_strip.png"
        write_png(left_path, strip_width, strip_height, strip)
        written += 1

        mirrored = flip_horizontal(strip, strip_width, strip_height)
        right_path = output_directory / f"player_{action_name}_right_32x48_strip.png"
        write_png(right_path, strip_width, strip_height, mirrored)
        written += 1

    print(f"[assets] Wrote {written} player action strips to '{output_directory}'.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build two-frame player action strips.")
    parser.add_argument("frame_directory", type=Path)
    parser.add_argument("output_directory", type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    build_strips(args.frame_directory, args.output_directory)


if __name__ == "__main__":
    main()
