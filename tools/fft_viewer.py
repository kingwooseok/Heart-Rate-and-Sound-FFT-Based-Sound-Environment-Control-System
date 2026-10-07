#!/usr/bin/env python3
"""Plot integer FFT bins emitted by the project's UART firmware."""

import argparse
import math
import sys
import time


def parse_fft_frame(text, fft_size=128):
    """Return bins, None for other log lines, or raise ValueError for bad FFT."""
    _, marker, payload = text.partition("FFT:")
    if not marker:
        return None
    fields = payload.strip().split(",")
    if fields and not fields[-1].strip():  # Firmware may emit a trailing comma.
        fields.pop()
    if len(fields) != fft_size // 2 or any(not field.strip() for field in fields):
        raise ValueError(f"expected {fft_size // 2} integer bins")
    return [int(field) for field in fields]


def read_complete_line(source, buffer, max_bytes=65536):
    """Preserve partial UART lines across read timeouts with bounded storage."""
    buffer.extend(source.readline(max_bytes - len(buffer) + 1))
    if len(buffer) > max_bytes:
        buffer.clear()
        raise ValueError("UART line exceeds buffer limit")
    if not buffer.endswith(b"\n"):
        return None
    line = bytes(buffer)
    buffer.clear()
    return line


def positive_float(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return number


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    source.add_argument("--input-file", help="replay a UTF-8 UART text capture")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--fs", type=positive_float, default=8000.0,
                        help="firmware sample rate in Hz (default: 8000)")
    parser.add_argument("--fft-size", type=int, default=128)
    parser.add_argument("--update-interval", type=positive_float, default=0.1,
                        help="plot/replay interval in seconds (default: 0.1)")
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.baud <= 0:
        parser.error("--baud must be greater than zero")
    if args.fft_size < 2 or args.fft_size % 2:
        parser.error("--fft-size must be an even integer of at least 2")
    try:
        import matplotlib.pyplot as plt
        if args.port:
            import serial
    except ImportError as error:
        parser.exit(1, f"Missing dependency: {error}. Run pip install -r requirements.txt\n")

    source = None
    fig = None
    malformed = 0
    try:
        source = (serial.Serial(args.port, args.baud, timeout=0.1) if args.port
                  else open(args.input_file, encoding="utf-8", errors="replace"))
        plt.ion()
        fig, ax = plt.subplots()
        frequencies = [i * args.fs / args.fft_size for i in range(args.fft_size // 2)]
        line, = ax.plot(frequencies, [0] * len(frequencies))
        ax.set(xlim=(0, args.fs / 2), ylim=(0, 3000),
               xlabel="Frequency (Hz)", ylabel="FFT magnitude (raw output)",
               title="UART FFT viewer")
        ax.grid(True)
        last_update = -math.inf
        receive_buffer = bytearray()
        while plt.fignum_exists(fig.number):
            try:
                raw = (read_complete_line(source, receive_buffer) if args.port
                       else source.readline())
            except ValueError:
                malformed += 1
                raw = None
            if raw is None:
                plt.pause(0.001)
                continue
            if not args.port and raw == "":
                plt.ioff()
                plt.show(block=True)
                break
            text = raw.decode("ascii", errors="replace") if isinstance(raw, bytes) else raw
            try:
                bins = parse_fft_frame(text, args.fft_size)
            except ValueError:
                malformed += 1
                bins = None
            now = time.monotonic()
            if bins is not None and (args.input_file or now - last_update >= args.update_interval):
                line.set_ydata(bins)
                ax.set_ylim(min(0, min(bins) * 1.1), max(3000, max(bins) * 1.1))
                fig.canvas.draw_idle()
                last_update = now
                if args.input_file:
                    plt.pause(args.update_interval)
            plt.pause(0.001)  # Process window events even without valid UART data.
    except KeyboardInterrupt:
        pass
    except OSError as error:
        print(f"Input error: {error}", file=sys.stderr)
        return 1
    finally:
        if source is not None:
            source.close()
        if fig is not None:
            plt.close(fig)
        if malformed:
            print(f"Skipped {malformed} malformed FFT frame(s).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
