#!/usr/bin/env python3
"""Checks x87 transcendental results against the SDM model (design #25).

Reads the lines `rex86_x87_fuzz --dump FILE` writes and, for every case on
a computed path, evaluates the model -- the argument reduced by the x87's
66-bit Pi (SDM Vol. 1, 8.3.8), then the true function -- with Python's
decimal module at 170 significant digits, rounds it to the extended format
in CW.RC's direction and compares the core's and the host's results.

The core is expected to equal the model on every case (it is correctly
rounded); the host only within the SDM's 1 and 1.5 ulps.

Usage: x87_transcendental_oracle.py DUMP [--show N]
"""

import math
import sys
from decimal import ROUND_FLOOR, ROUND_HALF_EVEN, Decimal, getcontext
from fractions import Fraction

getcontext().prec = 170
D = Decimal

# Pi = PI66 * 2^(2 - 68): 0.C90FDAA2 2168C234 C * 2^2.
PI66 = 0xC90FDAA22168C234C
EMIN = -16382


def pow2(n):
    return D(2) ** n


def series_atan(t):
    """atan(t) for |t| <= 0.1 by its power series."""
    t2 = t * t
    term = t
    total = t
    n = 1
    while True:
        term = -term * t2
        piece = term / (2 * n + 1)
        if abs(piece) < abs(total) * D(10) ** -175:
            return total
        total += piece
        n += 1


def atan(t):
    """atan(t) for t >= 0, halved by atan(t) = 2 atan(t / (1 + sqrt(1 + t^2)))."""
    halvings = 0
    while t > D("0.05"):
        t = t / (1 + (1 + t * t).sqrt())
        halvings += 1
    return series_atan(t) * (2 ** halvings)


PI = 16 * series_atan(D(1) / 5) - 4 * series_atan(D(1) / 239)
LN2 = D(2).ln()
HALF_PI66 = D(PI66) * pow2(-67)


def sin_cos(theta):
    """sin and cos of |theta| <= 0.8 by their power series."""
    t2 = theta * theta
    s_term = theta
    c_term = D(1)
    s = theta
    c = D(1)
    n = 1
    while True:
        s_term = -s_term * t2 / ((2 * n) * (2 * n + 1))
        c_term = -c_term * t2 / ((2 * n - 1) * (2 * n))
        s += s_term
        c += c_term
        if abs(s_term) < abs(s) * D(10) ** -175 and abs(c_term) < D(10) ** -175:
            return s, c
        n += 1


def expm1(t):
    term = t
    total = t
    n = 2
    while True:
        term = term * t / n
        if abs(term) < abs(total) * D(10) ** -175:
            return total
        total += term
        n += 1


def log1p(x):
    """ln(1 + x) for x > -1: 2 atanh(x / (2 + x)) near 0, else ln(1 + x),
    exact to form at 170 digits once |x| > 1/2."""
    if abs(x) > D("0.5"):
        return (1 + x).ln()
    u = x / (2 + x)
    u2 = u * u
    term = u
    total = u
    n = 1
    while True:
        term = term * u2
        piece = term / (2 * n + 1)
        if abs(piece) < abs(total) * D(10) ** -175:
            return 2 * total
        total += piece
        n += 1


def parse(hex20):
    raw = bytes.fromhex(hex20)
    significand = int.from_bytes(raw[0:8], "little")
    sign_exponent = int.from_bytes(raw[8:10], "little")
    return (sign_exponent >> 15, sign_exponent & 0x7FFF, significand)


def value(f):
    sign, exponent, significand = f
    e = (1 if exponent == 0 else exponent) - 16383
    v = D(significand) * pow2(e - 63)
    return -v if sign else v


def is_normal(f):
    _, exponent, significand = f
    return 0 < exponent < 0x7FFF and (significand >> 63) == 1


def unbiased(f):
    return f[1] - 16383


def round_f80(v, rc, tail=0):
    """v (nonzero) to (sign, exponent field, significand) and the C1 report.

    v is a Decimal, or a Fraction when it is exact. `tail` is the sign of
    what v leaves out of the true magnitude when that is far below the 170
    digits (a structured result such as atan(t) = t - t^3/3 for t near
    2^-7000): -1 just below |v|, +1 just above.
    """
    sign = 1 if v < 0 else 0
    a = abs(v)
    if isinstance(a, Fraction):
        e = a.numerator.bit_length() - a.denominator.bit_length()
        while Fraction(2) ** e > a:
            e -= 1
        while Fraction(2) ** (e + 1) <= a:
            e += 1
        q = max(e, EMIN)
        scaled = a * Fraction(2) ** (63 - q)
        n = math.floor(scaled)
        rest = scaled - n
        frac = D(rest.numerator) / D(rest.denominator)  # compared with 0 and 1/2
        if rest != 0 and frac == 0:
            frac = D(10) ** -150
        if rest == Fraction(1, 2):
            frac = D("0.5")
    else:
        e = math.floor(float(a.ln() / LN2))
        while pow2(e) > a:
            e -= 1
        while pow2(e + 1) <= a:
            e += 1
        q = max(e, EMIN)
        scaled = a * pow2(63 - q)
        n = int(scaled.to_integral_value(rounding=ROUND_FLOOR))
        frac = scaled - n
    if frac == 0 and tail < 0:
        if n == 2 ** 63 and q > EMIN:
            # Just below a power of two: the binade below.
            q -= 1
            n = 2 ** 64
        n -= 1
        frac = 1 - D(10) ** -150
    elif frac == 0 and tail > 0:
        frac = D(10) ** -150
    inexact = frac != 0
    if rc == 0:
        up = frac > D("0.5") or (frac == D("0.5") and n % 2 == 1)
    elif rc == 1:
        up = inexact and sign == 1
    elif rc == 2:
        up = inexact and sign == 0
    else:
        up = False
    if up:
        n += 1
    if n == 2 ** 64:
        n = 2 ** 63
        q += 1
    field = q + 16383 if n >= 2 ** 63 else 0
    if field >= 0x7FFF:
        return None, False, e
    return (sign, field, n), up, e


def trig(name, x):
    """The model: |x| = k Pi/2 + theta (k nearest), the true function of theta."""
    a = abs(x)
    k = 0
    theta = a
    if a >= D("0.5"):
        k = int((a / HALF_PI66).to_integral_value(rounding=ROUND_HALF_EVEN))
        theta = a - k * HALF_PI66
    s, c = sin_cos(theta)
    q = k % 4
    sin = [s, c, -s, -c][q]
    cos = [c, -s, -c, s][q]
    if x < 0:
        sin = -sin
    if name == "fsin":
        return [sin]
    if name == "fcos":
        return [cos]
    if name == "fptan":
        return [sin / cos]
    return [sin, cos]  # fsincos


def exact(f):
    """A finite value as an exact Fraction."""
    sign, exponent, significand = f
    e = (1 if exponent == 0 else exponent) - 16383
    v = Fraction(significand) * Fraction(2) ** (e - 63)
    return -v if sign else v


def atan2(y_f80, x_f80):
    """atan2 and the tail of its magnitude (see round_f80)."""
    y = value(y_f80)
    x = value(x_f80)
    if abs(y) <= abs(x):
        t = abs(y) / abs(x)
        if x > 0 and t < D(10) ** -60:
            # atan(t) = t - t^3/3: the cube lies below the 170 digits, so
            # t is taken exactly with the cube's sign as the tail.
            t = abs(exact(y_f80) / exact(x_f80))
            return (-t if y < 0 else t), -1
        a = atan(t)
    else:
        a = PI / 2 - atan(abs(x) / abs(y))
    if x < 0:
        a = PI - a
    return (-a if y < 0 else a), 0


def log2(x_f80):
    """log2 of a positive normal, exact for a power of two."""
    if x_f80[2] == 2 ** 63:
        return D(unbiased(x_f80))
    return value(x_f80).ln() / LN2


def model(name, in0, in1):
    """(value, tail) pairs to check for a case on a computed path, else None."""
    x = in0
    if name in ("fsin", "fcos", "fsincos", "fptan"):
        if not is_normal(x) or not -68 <= unbiased(x) <= 62:
            return None
        return [(v, 0) for v in trig(name, value(x))]
    if name == "f2xm1":
        if not is_normal(x) or unbiased(x) > -1:
            return None
        return [(expm1(value(x) * LN2), 0)]
    y = in1
    if not is_normal(x) or not is_normal(y):
        return None
    if name == "fpatan":
        return [atan2(y, x)]
    if name == "fyl2x":
        if x[0] == 1 or (unbiased(x) == 0 and x[2] == 2 ** 63):
            return None
        if x[2] == 2 ** 63:
            return [(exact(y) * unbiased(x), 0)]  # an exact product
        return [(value(y) * log2(x), 0)]
    if name == "fyl2xp1":
        if x[0] == 1 and unbiased(x) >= 0:
            return None  # x <= -1: the source comes back
        w = 1 + exact(x)
        if w.numerator & (w.numerator - 1) == 0 and w.denominator & (w.denominator - 1) == 0:
            # 1 + x a power of two: an exact product.
            k = w.numerator.bit_length() - w.denominator.bit_length()
            return [(exact(y) * k, 0)] if k != 0 else None
        return [(value(y) * log1p(value(x)) / LN2, 0)]
    return None


def results(name, st0, st1):
    """Where the checked values sit in an output image, in model order."""
    if name == "fptan":
        return [st1]
    if name == "fsincos":
        return [st1, st0]
    return [st0]


def operands_present(name, sw, tw):
    top = (sw >> 11) & 7

    def empty(i):
        return ((tw >> (2 * ((top + i) & 7))) & 3) == 3

    if empty(0):
        return False
    if name in ("fpatan", "fyl2x", "fyl2xp1") and empty(1):
        return False
    if name in ("fsincos", "fptan") and not empty(7):
        return False
    return True


def main(argv):
    show = 0
    if "--show" in argv:
        show = int(argv[argv.index("--show") + 1])
    counts = {}
    shown = 0
    with open(argv[1]) as dump:
        for line in dump:
            f = line.split()
            name = f[0]
            cw, sw_in, tw_in = int(f[1], 16), int(f[2], 16), int(f[3], 16)
            if not operands_present(name, sw_in, tw_in):
                continue
            expected = model(name, parse(f[4]), parse(f[5]))
            if expected is None:
                continue
            rc = (cw >> 10) & 3
            host = [parse(h) for h in results(name, f[7], f[8])]
            core = [parse(c) for c in results(name, f[10], f[11])]
            rounded = [round_f80(v, rc, tail) for v, tail in expected]
            if any(r[0] is None for r in rounded):
                continue  # overflow
            tiny = any(r[2] < EMIN for r in rounded)
            if tiny and (cw & 0x10) == 0:
                continue  # unmasked underflow: a bias-adjusted result
            c = counts.setdefault(name, {"cases": 0, "core": 0, "host": 0, "core_c1": 0})
            c["cases"] += 1
            want = [r[0] for r in rounded]
            core_ok = core == want
            c["core"] += core_ok
            c["host"] += host == want
            c1 = (int(f[9], 16) >> 9) & 1
            c1_ok = c1 == int(rounded[-1][1])
            c["core_c1"] += c1_ok
            if (not core_ok or not c1_ok) and shown < show:
                shown += 1
                print("CORE-DIFF" if not core_ok else "CORE-C1", line.strip(), "want", want,
                      "c1", int(rounded[-1][1]))
    total_bad = 0
    for name in sorted(counts):
        c = counts[name]
        total_bad += c["cases"] - c["core"]
        print("%-8s cases=%d core_equal=%d core_c1_equal=%d host_equal=%d (%.2f%%)" % (
            name, c["cases"], c["core"], c["core_c1"], c["host"],
            100.0 * c["host"] / max(1, c["cases"])))
    return 0 if total_bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
