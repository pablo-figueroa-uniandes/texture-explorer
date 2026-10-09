#!/usr/bin/env python3
"""Runs the two probe programs and compares what they print, quantity by quantity.

    python3 compare.py <probe_windows> <probe_mac> <repository root> [tolerance]

probe_windows is probe.cpp built from the Windows sources (src/), probe_mac the same file
built from the macOS port (mac/src/). Counts must be equal; real numbers may differ by
rounding only (the two math libraries order some float operations differently), so they
are compared with an absolute tolerance, 1e-5 by default.
"""
import subprocess
import sys

# The columns of each kind of line, after its key, grouped into named quantities.
LAYOUT = {
    "vertex": [("position", 3), ("normal", 3), ("tangent", 4), ("uv", 2)],
    "probe": [("color", 3), ("bump, displacement, roughness", 3), ("slope", 2), ("n_ts", 3), ("on surface", 1),
              ("world position", 3), ("world N", 3), ("world N'", 3), ("world T", 3), ("world B", 3)],
}
KEY_LENGTH = {"mesh": 3, "vertex": 3, "probe": 5}


def run(program, root):
    out = subprocess.run([program, root], check=True, capture_output=True, text=True).stdout
    lines = out.splitlines()
    print(f"  {program}: {lines[0][5:]}, {len(lines) - 1} lines")
    table = {}
    for line in lines[1:]:
        words = line.split()
        n = KEY_LENGTH[words[0]]
        table[tuple(words[:n])] = words[n:]
    return table


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    tolerance = float(sys.argv[4]) if len(sys.argv) > 4 else 1e-5
    win = run(sys.argv[1], sys.argv[3])
    mac = run(sys.argv[2], sys.argv[3])
    if win.keys() != mac.keys():
        sys.exit("FAIL: the programs print different lines")

    worst = {}       # (kind, quantity) -> (difference, key)
    exact_errors = []
    for key, a in win.items():
        b = mac[key]
        kind = key[0]
        if kind == "mesh" or len(a) != len(b):
            if a != b:
                exact_errors.append(f"{' '.join(key)}: {a} != {b}")
            continue
        col = 0
        for name, width in LAYOUT[kind]:
            if col >= len(a):
                break
            d = max(abs(float(x) - float(y)) for x, y in zip(a[col:col + width], b[col:col + width]))
            if d > worst.get((kind, name), (-1,))[0]:
                worst[(kind, name)] = (d, key)
            col += width

    print(f"\n  {'quantity':34} {'max |windows - mac|':>20}")
    failed = bool(exact_errors)
    for (kind, name), (d, key) in sorted(worst.items()):
        flag = "" if d <= tolerance else f"   > {tolerance}  at {' '.join(key[1:])}"
        failed |= d > tolerance
        print(f"  {kind + ': ' + name:34} {d:20.3g}{flag}")
    for e in exact_errors:
        print("  differs:", e)
    print(f"\n{'FAIL' if failed else 'PASS'}: {len(win)} lines compared, tolerance {tolerance}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
