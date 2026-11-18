"""Analysis and encoding helpers for tools/samples/build.py. numpy and scipy only."""
import subprocess
import numpy as np
from scipy import signal

SR = 44100          # everything is analysed at the engine's rate
ZSR = 22050         # what is stored


def load(path, sr=SR):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(sr), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def envelope(x, ms=5.0, sr=SR):
    n = max(1, int(ms * 0.001 * sr))
    return np.sqrt(signal.lfilter(np.ones(n) / n, [1.0], x * x) + 1e-20)


def onset(x, frac=0.05, back_ms=1.5, sr=SR):
    a = np.abs(x)
    i = int(np.argmax(a > frac * a.max()))
    return max(0, i - int(back_ms * 0.001 * sr))


def f0_at(x, guess, sr=SR):
    """Fundamental of a steady stretch, near `guess` Hz (within a fourth), by autocorrelation."""
    seg = x - x.mean()
    n = 1 << int(np.ceil(np.log2(len(seg) * 2)))
    X = np.fft.rfft(seg * np.hanning(len(seg)), n)
    ac = np.fft.irfft(np.abs(X) ** 2)
    lo, hi = int(sr / (guess * 1.3)), int(sr / (guess / 1.3)) + 2
    l = lo + int(np.argmax(ac[lo:hi]))
    y0, y1, y2 = ac[l - 1], ac[l], ac[l + 1]
    return sr / (l + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2 + 1e-20))


def f0_any(x, sr=SR, lo=30.0, hi=2000.0):
    seg = x - x.mean()
    n = 1 << int(np.ceil(np.log2(len(seg) * 2)))
    ac = np.fft.irfft(np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)) ** 2)
    ac = ac / ac[0]
    lmin, lmax = int(sr / hi), int(sr / lo)
    c = ac[lmin:lmax]
    thr = 0.85 * c.max()
    l = lmin + int(np.argmax(c))
    for i in range(1, len(c) - 1):
        if c[i] > thr and c[i] >= c[i - 1] and c[i] >= c[i + 1]:
            l = i + lmin
            break
    return f0_at(x, sr / l, sr)


def midi(f):
    return 69.0 + 12.0 * np.log2(f / 440.0)


def hz(m):
    return 440.0 * 2.0 ** ((m - 69.0) / 12.0)


# ---- the string loop, exactly as core/str.h runs it ----------------------------------------------
DCB = 0.3           # how much of the loop's slow average comes off, and
DCK = 0.025         # its corner against the note (STR_DCB, STR_DCK)


def lp_delay(f0, g, sr=SR):
    w = 2 * np.pi * f0 / sr
    a = 1.0 - g
    return np.arctan2(a * np.sin(w), 1.0 - a * np.cos(w)) / w


def loop_delay(f0, g, sr=SR):
    """The delay line's length for a string at f0: a period, less what the lowpass adds, plus what taking
    the slow average off takes away."""
    w = 2 * np.pi * f0 / sr
    k = w * DCK
    return sr / f0 - lp_delay(f0, g, sr) + DCB * k / (w * w + k * k)


def lagrange(D):
    N = int(np.floor(D)) - 1
    d = D - N
    h = np.array([-(d - 1) * (d - 2) * (d - 3) / 6.0, d * (d - 2) * (d - 3) / 2.0,
                  -d * (d - 1) * (d - 3) / 2.0, d * (d - 1) * (d - 2) / 6.0])
    return N, h


def _loop_polys(L, g, rho, f0):
    """The loop as two polynomials in 1/z: out = e * den / (den - num)."""
    N, h = lagrange(L)
    k = 2 * np.pi * f0 / SR * DCK
    den = np.convolve([1.0, -(1.0 - k)], [1.0, -(1.0 - g)])
    num = np.zeros(N + 6)
    num[N:N + 5] = rho * g * np.convolve(h, [1.0 - DCB * k, -(1.0 - k)])
    full = -num
    full[:3] += den
    return den, full


def string_inverse(y, L, g, rho, f0):
    """What has to be fed into the loop for it to give y back."""
    den, full = _loop_polys(L, g, rho, f0)
    return signal.lfilter(full, den, y)


def string_run(e, nout, L, g, rho, f0):
    """The loop fed with e."""
    den, full = _loop_polys(L, g, rho, f0)
    ex = np.zeros(nout)
    ex[:min(len(e), nout)] = e[:nout]
    return signal.lfilter(den, full, ex)


def rho_of(t60, f0):
    return 10.0 ** (-3.0 / (t60 * f0))


# ---- storage --------------------------------------------------------------------------------------
MU = 255.0


def mulaw_enc(x):
    x = np.clip(x, -1.0, 1.0)
    y = np.sign(x) * np.log1p(MU * np.abs(x)) / np.log1p(MU)
    return np.clip(np.round(y * 127.0) + 128, 1, 255).astype(np.uint8)


def mulaw_dec(b):
    y = (b.astype(np.float64) - 128.0) / 127.0
    return np.sign(y) * (np.exp(np.abs(y) * np.log1p(MU)) - 1.0) / MU


def down2(x):
    """44.1 kHz to 22.05 kHz."""
    return signal.resample_poly(x, 1, 2, window=("kaiser", 9.0))


def up2(e22):
    """22.05 kHz back to 44.1 kHz as smp_exc does it: every other sample from its four neighbours."""
    p = np.concatenate([[0.0], e22, [0.0, 0.0]])
    out = np.zeros(len(e22) * 2)
    out[0::2] = e22
    out[1::2] = (9.0 * (p[1:-2] + p[2:-1]) - p[0:-3] - p[3:]) / 16.0
    return out
