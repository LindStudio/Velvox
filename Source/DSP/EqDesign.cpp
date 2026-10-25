#include "EqDesign.h"

#include <cmath>

namespace velvox::dsp::eqdesign
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        // Below this normalised frequency (about 27 Hz at 48 kHz) the bilinear transform is already
        // exact for every practical purpose, and the matched formulas would lose precision
        constexpr double matchedFrom = 0.0035;
        // Highest match point: the formulas use the slope there, which diverges at Nyquist
        constexpr double maxMatchPoint = 0.9 * pi;

        // Analog prototype, s normalised to the band frequency: (n0 + n1 s + n2 s^2) / (d0 + d1 s + d2 s^2)
        struct Proto
        {
            double n0, n1, n2, d0, d1, d2;
        };

        double protoMagnitudeSquared (const Proto& p, double W) noexcept
        {
            const double u = W * W;
            const double nr = p.n0 - p.n2 * u, dr = p.d0 - p.d2 * u;
            return (nr * nr + p.n1 * p.n1 * u) / (dr * dr + p.d1 * p.d1 * u);
        }

        struct Poles
        {
            double a1, a2;
        };

        // Impulse-invariant poles of s^2 + s w/q + w^2 (w in rad/sample)
        Poles matchedPoles (double w, double q) noexcept
        {
            const double zeta = 0.5 / q;
            const double e = std::exp (-zeta * w);
            const double a1 = zeta <= 1.0 ? -2.0 * e * std::cos (std::sqrt (1.0 - zeta * zeta) * w)
                                          : -2.0 * e * std::cosh (std::sqrt (zeta * zeta - 1.0) * w);
            return { a1, e * e };
        }

        // |N(e^jw)|^2 = B0 phi0 + B1 phi1 + B2 phi2 -> minimum-phase b0, b1, b2 (Vicanek)
        BiquadCoeffs factor (double B0, double B1, double B2, const Poles& p) noexcept
        {
            const double W0 = std::sqrt (std::max (B0, 0.0)), W1 = std::sqrt (std::max (B1, 0.0));
            const double b1 = 0.5 * (W0 - W1);
            const double S = 0.5 * (W0 + W1);
            const double disc = S * S + B2;
            double b0, b2;
            if (disc > 0.0)
            {
                b0 = 0.5 * (S + std::sqrt (disc));
                b2 = -B2 / (4.0 * b0);
            }
            else
            {
                b0 = 0.5 * S;
                b2 = 0.5 * S;
            }
            // Keep the zeros strictly inside the unit circle: cuts are the inverse of a boost, so these
            // zeros become poles (only reached by resonant shelves right at Nyquist)
            constexpr double maxZeroRadiusSquared = 0.9999;
            if (std::abs (b2) > maxZeroRadiusSquared * b0)
                b2 = std::copysign (maxZeroRadiusSquared * b0, b2);
            return { b0, b1, b2, p.a1, p.a2 };
        }

        // Numerator matching |H|^2 of `proto` at DC, at `wm` and its slope (d/d phi1) at `wm`.
        // `w0` is the frequency the prototype is normalised to (rad/sample).
        BiquadCoeffs matchNumerator (const Proto& proto, double w0, const Poles& p, double wm) noexcept
        {
            const double A0 = (1.0 + p.a1 + p.a2) * (1.0 + p.a1 + p.a2);
            const double A1 = (1.0 - p.a1 + p.a2) * (1.0 - p.a1 + p.a2);
            const double A2 = -4.0 * p.a2;

            const double s = std::sin (0.5 * wm);
            const double phi1 = s * s, phi0 = 1.0 - phi1;

            const double Dm = A0 * phi0 + A1 * phi1 + A2 * 4.0 * phi0 * phi1;
            const double dDm = -A0 + A1 + 4.0 * A2 * (1.0 - 2.0 * phi1);

            const double W = wm / w0, u = W * W;
            const double nr = proto.n0 - proto.n2 * u, dr = proto.d0 - proto.d2 * u;
            const double Hn = nr * nr + proto.n1 * proto.n1 * u, Hd = dr * dr + proto.d1 * proto.d1 * u;
            const double dHn = -2.0 * proto.n2 * nr + proto.n1 * proto.n1;
            const double dHd = -2.0 * proto.d2 * dr + proto.d1 * proto.d1;
            const double H2 = Hn / Hd;
            const double dH2du = (dHn * Hd - Hn * dHd) / (Hd * Hd);
            const double dudphi = 2.0 * W / w0 / std::sqrt (phi1 * phi0);

            const double R1 = H2 * Dm;
            const double R2 = dH2du * dudphi * Dm + H2 * dDm;
            const double dc = proto.n0 / proto.d0;
            const double B0 = dc * dc * A0;
            double B2 = (R1 - R2 * phi1 - B0) / (4.0 * phi1 * phi1);
            double B1 = R2 + B0 + 4.0 * (phi1 - phi0) * B2;

            // Resonant prototypes right at Nyquist: the slope condition can ask for |N(-1)|^2 < 0.
            // Then match the analog value at Nyquist instead of the slope.
            const double S = 0.5 * (std::sqrt (std::max (B0, 0.0)) + std::sqrt (std::max (B1, 0.0)));
            if (B1 <= 1.0e-12 * B0 || S * S + B2 < 0.0)
            {
                B1 = protoMagnitudeSquared (proto, pi / w0) * A1;
                B2 = (R1 - B0 * phi0 - B1 * phi1) / (4.0 * phi0 * phi1);
            }
            return factor (B0, B1, B2, p);
        }

        BiquadCoeffs invert (const BiquadCoeffs& c) noexcept
        {
            const double g = 1.0 / c.b0;
            return { g, c.a1 * g, c.a2 * g, c.b1 * g, c.b2 * g };
        }

        bool finite (const BiquadCoeffs& c) noexcept
        {
            return std::isfinite (c.b0) && std::isfinite (c.b1) && std::isfinite (c.b2) && std::isfinite (c.a1) && std::isfinite (c.a2)
                && c.b0 != 0.0;
        }

        // ---- matched second-order sections -------------------------------------------------

        BiquadCoeffs matchedBell (double w0, double q, double gainDb) noexcept
        {
            const double A = std::pow (10.0, std::abs (gainDb) / 40.0);
            const Proto boost { 1.0, A / q, 1.0, 1.0, 1.0 / (A * q), 1.0 };
            const auto c = matchNumerator (boost, w0, matchedPoles (w0, A * q), std::min (w0, maxMatchPoint));
            return gainDb < 0.0 ? invert (c) : c;
        }

        // Low shelf boost and high shelf cut have their poles at the lower corner f0 / sqrt(A); the
        // other two are their exact inverses. Matched at the zero frequency f0 * sqrt(A).
        BiquadCoeffs matchedShelf (bool low, double w0, double q, double gainDb) noexcept
        {
            const double A = std::pow (10.0, std::abs (gainDb) / 40.0), rA = std::sqrt (A);
            const double wm = std::min (w0 * rA, maxMatchPoint);
            const auto poles = matchedPoles (w0 / rA, q);

            if (low)
            {
                const Proto boost { A * A, A * rA / q, A, 1.0, rA / q, A };
                const auto c = matchNumerator (boost, w0, poles, wm);
                return gainDb < 0.0 ? invert (c) : c;
            }

            const double Ac = 1.0 / A, rAc = 1.0 / rA;
            const Proto cut { Ac, Ac * rAc / q, Ac * Ac, Ac, rAc / q, 1.0 };
            const auto c = matchNumerator (cut, w0, poles, wm);
            return gainDb > 0.0 ? invert (c) : c;
        }

        // Vicanek's closed form (double zero at DC, matched at f0 with |H(f0)| = q)
        BiquadCoeffs matchedHighPass (double w0, double q) noexcept
        {
            const auto p = matchedPoles (w0, q);
            const double A0 = (1.0 + p.a1 + p.a2) * (1.0 + p.a1 + p.a2), A1 = (1.0 - p.a1 + p.a2) * (1.0 - p.a1 + p.a2), A2 = -4.0 * p.a2;
            const double s = std::sin (0.5 * w0), phi1 = s * s, phi0 = 1.0 - phi1;
            const double b0 = std::sqrt (std::max (0.0, A0 * phi0 + A1 * phi1 + A2 * 4.0 * phi0 * phi1)) * q / (4.0 * phi1);
            return { b0, -2.0 * b0, b0, p.a1, p.a2 };
        }

        // Low pass: the generic match (DC, f0 and the slope at f0) follows the analog Butterworth sections
        // closer near Nyquist than Vicanek's two-point form with b2 = 0
        BiquadCoeffs matchedLowPass (double w0, double q) noexcept
        {
            return matchNumerator ({ 1.0, 0.0, 0.0, 1.0, 1.0 / q, 1.0 }, w0, matchedPoles (w0, q), std::min (w0, maxMatchPoint));
        }

        BiquadCoeffs matchedBandPass (double w0, double q) noexcept
        {
            const auto p = matchedPoles (w0, q);
            const double A0 = (1.0 + p.a1 + p.a2) * (1.0 + p.a1 + p.a2), A1 = (1.0 - p.a1 + p.a2) * (1.0 - p.a1 + p.a2), A2 = -4.0 * p.a2;
            const double s = std::sin (0.5 * w0), phi1 = s * s, phi0 = 1.0 - phi1;
            const double R1 = A0 * phi0 + A1 * phi1 + A2 * 4.0 * phi0 * phi1;
            const double R2 = -A0 + A1 + 4.0 * (phi0 - phi1) * A2;
            const double B2 = (R1 - R2 * phi1) / (4.0 * phi1 * phi1);
            const double B1 = R2 + 4.0 * (phi1 - phi0) * B2;
            const double b1 = -0.5 * std::sqrt (std::max (0.0, B1));
            const double b0 = 0.5 * (std::sqrt (std::max (0.0, B2 + b1 * b1)) - b1);
            return { b0, b1, -b0 - b1, p.a1, p.a2 };
        }

        // Zeros exactly on the unit circle at w0, unity gain at DC
        BiquadCoeffs matchedNotch (double w0, double q) noexcept
        {
            const auto p = matchedPoles (w0, q);
            const double c = std::cos (w0);
            const double g = (1.0 + p.a1 + p.a2) / (2.0 - 2.0 * c);
            return { g, -2.0 * c * g, g, p.a1, p.a2 };
        }

        // First order (n0 + n1 s) / (d0 + d1 s), s normalised to w0: impulse-invariant pole, numerator
        // matched at DC and at Nyquist
        BiquadCoeffs matchedFirstOrder (double n0, double n1, double d0, double d1, double w0) noexcept
        {
            const double pole = std::exp (-w0 * d0 / d1);
            const double W = pi / w0;
            const double hNyq = std::sqrt ((n0 * n0 + n1 * n1 * W * W) / (d0 * d0 + d1 * d1 * W * W));
            const double sum = (n0 / d0) * (1.0 - pole), diff = hNyq * (1.0 + pole);
            return { 0.5 * (sum + diff), 0.5 * (sum - diff), 0.0, -pole, 0.0 };
        }

        // Tilt: H(s) = (sqrt(G) s + 1) / (s + sqrt(G)), lows -gain/2, highs +gain/2 (G = 10^(gain/20))
        BiquadCoeffs matchedTilt (double w0, double gainDb) noexcept
        {
            const double r = std::sqrt (std::pow (10.0, -std::abs (gainDb) / 20.0));   // design the falling tilt
            const auto c = matchedFirstOrder (1.0, r, r, 1.0, w0);
            return gainDb > 0.0 ? invert (c) : c;
        }

        // ---- bilinear (0.5 / RBJ) sections -------------------------------------------------

        double clampFreq (double f, double fs) noexcept { return std::clamp (f, 5.0, 0.49 * fs); }

        BiquadCoeffs bilinearFirstOrder (double n0, double n1, double d0, double d1, double w0) noexcept
        {
            const double K = std::tan (0.5 * w0);
            const double a0 = d0 * K + d1;
            return { (n0 * K + n1) / a0, (n0 * K - n1) / a0, 0.0, (d0 * K - d1) / a0, 0.0 };
        }

        BiquadCoeffs rbjNotch (double fs, double f, double q) noexcept
        {
            const double w0 = 2.0 * pi * clampFreq (f, fs) / fs, alpha = std::sin (w0) / (2.0 * std::max (q, 0.05)), c = std::cos (w0);
            const double inv = 1.0 / (1.0 + alpha);
            return { inv, -2.0 * c * inv, inv, -2.0 * c * inv, (1.0 - alpha) * inv };
        }

        BiquadCoeffs rbjBandPass (double fs, double f, double q) noexcept
        {
            const double w0 = 2.0 * pi * clampFreq (f, fs) / fs, alpha = std::sin (w0) / (2.0 * std::max (q, 0.05)), c = std::cos (w0);
            const double inv = 1.0 / (1.0 + alpha);
            return { alpha * inv, 0.0, -alpha * inv, -2.0 * c * inv, (1.0 - alpha) * inv };
        }

        // ---- Butterworth cascades ------------------------------------------------------------

        // Section Q values; the 2nd/4th/8th order rows are the constants of 0.5 (bit-exact legacy)
        constexpr double bw2[1] { 0.70710678 };
        constexpr double bw4[2] { 0.54119610, 1.30656296 };
        constexpr double bw8[4] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };

        int butterworthOrder (int slope) noexcept { return eqSlopeDbPerOct (slope) / 6; }

        double butterworthQ (int order, int k) noexcept
        {
            if (order == 2) return bw2[k];
            if (order == 4) return bw4[k];
            if (order == 8) return bw8[k];
            // odd orders: k = 1..(order-1)/2 -> 1 / (2 cos (k pi / N)); even: 1 / (2 cos ((2k - 1) pi / 2N))
            const double angle = (order % 2 == 1) ? (k + 1) * pi / order : (2 * k + 1) * pi / (2.0 * order);
            return 0.5 / std::cos (angle);
        }

        EqBandDesign cutDesign (bool lowCut, double f, int slope, bool bilinear, double fs) noexcept
        {
            EqBandDesign d;
            const double fc = clampFreq (f, fs);
            const double w0 = 2.0 * pi * fc / fs;
            const int order = butterworthOrder (slope);

            if (order % 2 == 1)
                d.sections[(size_t) d.numSections++] = bilinear ? (lowCut ? bilinearFirstOrder (0.0, 1.0, 1.0, 1.0, w0) : bilinearFirstOrder (1.0, 0.0, 1.0, 1.0, w0))
                                                                : (lowCut ? matchedFirstOrder (0.0, 1.0, 1.0, 1.0, w0) : matchedFirstOrder (1.0, 0.0, 1.0, 1.0, w0));
            for (int k = 0; k < order / 2; ++k)
            {
                const double q = butterworthQ (order, k);
                d.sections[(size_t) d.numSections++] = bilinear ? (lowCut ? design::highPass (fs, f, q) : design::lowPass (fs, f, q))
                                                                : (lowCut ? matchedHighPass (w0, q) : matchedLowPass (w0, q));
            }
            return d;
        }

        EqBandDesign single (const BiquadCoeffs& c) noexcept
        {
            EqBandDesign d;
            d.sections[0] = c;
            d.numSections = 1;
            return d;
        }

        EqBandDesign bilinearDesign (const EqBand& b, double fs) noexcept
        {
            // Exactly the 0.5 calls: float band values handed to the double designs
            switch (b.bandType())
            {
                case EqBandType::bell:      return single (design::peak (fs, b.freq, b.q, b.gain));
                case EqBandType::lowShelf:  return single (design::lowShelf (fs, b.freq, b.q, b.gain));
                case EqBandType::highShelf: return single (design::highShelf (fs, b.freq, b.q, b.gain));
                case EqBandType::lowCut:    return cutDesign (true, b.freq, b.slope, true, fs);
                case EqBandType::highCut:   return cutDesign (false, b.freq, b.slope, true, fs);
                case EqBandType::notch:     return single (rbjNotch (fs, b.freq, b.q));
                case EqBandType::bandPass:  return single (rbjBandPass (fs, b.freq, b.q));
                case EqBandType::tilt:
                {
                    const double r = std::sqrt (std::pow (10.0, (double) b.gain / 20.0));
                    return single (bilinearFirstOrder (1.0, r, r, 1.0, 2.0 * pi * clampFreq (b.freq, fs) / fs));
                }
            }
            return single ({});
        }
    }

    EqBandDesign design (const EqBand& band, bool legacy, double fs) noexcept
    {
        if (legacy)
            return bilinearDesign (band, fs);

        EqBand b = band;
        b.freq = std::clamp (b.freq, minFreq, maxFreq);
        b.q = std::clamp (b.q, minQ, maxQ);
        b.gain = std::clamp (b.gain, -maxGainDb, maxGainDb);

        const double f = clampFreq (b.freq, fs);
        const double w0 = 2.0 * pi * f / fs;
        if (w0 < matchedFrom)
            return bilinearDesign (b, fs);

        EqBandDesign d;
        switch (b.bandType())
        {
            case EqBandType::bell:      d = single (matchedBell (w0, b.q, b.gain)); break;
            case EqBandType::lowShelf:  d = single (matchedShelf (true, w0, b.q, b.gain)); break;
            case EqBandType::highShelf: d = single (matchedShelf (false, w0, b.q, b.gain)); break;
            case EqBandType::lowCut:    d = cutDesign (true, f, b.slope, false, fs); break;
            case EqBandType::highCut:   d = cutDesign (false, f, b.slope, false, fs); break;
            case EqBandType::notch:     d = single (matchedNotch (w0, b.q)); break;
            case EqBandType::bandPass:  d = single (matchedBandPass (w0, b.q)); break;
            case EqBandType::tilt:      d = single (matchedTilt (w0, b.gain)); break;
        }

        for (int i = 0; i < d.numSections; ++i)
            if (! finite (d.sections[(size_t) i]))
                return bilinearDesign (b, fs);   // never reached for legal values; a safe fallback
        return d;
    }

    double magnitude (const EqBandDesign& d, double freq, double fs) noexcept
    {
        double m = 1.0;
        for (int i = 0; i < d.numSections; ++i)
            m *= design::magnitude (d.sections[(size_t) i], freq, fs);
        return m;
    }

    double analogMagnitude (const EqBand& band, double freq) noexcept
    {
        const double f0 = std::clamp (band.freq, minFreq, maxFreq);
        const double q = std::clamp (band.q, minQ, maxQ);
        const double g = std::clamp (band.gain, -maxGainDb, maxGainDb);
        const double W = freq / f0;
        const double A = std::pow (10.0, g / 40.0), rA = std::sqrt (A);
        const int order = butterworthOrder (band.slope);

        switch (band.bandType())
        {
            case EqBandType::bell:      return std::sqrt (protoMagnitudeSquared ({ 1.0, A / q, 1.0, 1.0, 1.0 / (A * q), 1.0 }, W));
            case EqBandType::lowShelf:  return std::sqrt (protoMagnitudeSquared ({ A * A, A * rA / q, A, 1.0, rA / q, A }, W));
            case EqBandType::highShelf: return std::sqrt (protoMagnitudeSquared ({ A, A * rA / q, A * A, A, rA / q, 1.0 }, W));
            case EqBandType::lowCut:    return std::pow (W, order) / std::sqrt (1.0 + std::pow (W, 2 * order));
            case EqBandType::highCut:   return 1.0 / std::sqrt (1.0 + std::pow (W, 2 * order));
            case EqBandType::notch:     return std::sqrt (protoMagnitudeSquared ({ 1.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0 }, W));
            case EqBandType::bandPass:  return std::sqrt (protoMagnitudeSquared ({ 0.0, 1.0 / q, 0.0, 1.0, 1.0 / q, 1.0 }, W));
            case EqBandType::tilt:
            {
                const double G = std::pow (10.0, g / 20.0);
                return std::sqrt ((G * W * W + 1.0) / (W * W + G));
            }
        }
        return 1.0;
    }
}
