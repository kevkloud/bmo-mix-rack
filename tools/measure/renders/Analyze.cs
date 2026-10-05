// BMO render analysis harness.
//
//   Analyze env  <dry.wav> <proc.wav> <label> [<proc.wav> <label> ...]
//   Analyze band <ref.wav> <label>    <file.wav> <label> [...]
//
// env  -- recovers each processed file's time-varying gain relative to the dry
//         file, finds the phrase gaps in the dry, and reports how much
//         reduction is still standing when the next phrase arrives. This is
//         the measurement that turns "it falls slower" into a number.
//
// band -- RMS-normalises every file to the reference and prints a band energy
//         table, with rows above 9 kHz, which every previous table stopped
//         short of.
//
// Local-only: there is no C++ toolchain on this machine, and CI cannot see
// these WAVs. If this gets run per version it belongs in tools/measure/.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;

static class Program
{
    //== WAV ===================================================================

    /// Returns interleaved-to-mono samples. Walks the chunk list properly:
    /// Ableton writes a JUNK chunk ahead of fmt, so seeking to a fixed offset
    /// reads the wrong bytes.
    static float[] ReadMono(string path, out int rate)
    {
        using (var fs = File.OpenRead(path))
        using (var br = new BinaryReader(fs))
        {
            if (new string(br.ReadChars(4)) != "RIFF") throw new Exception("not RIFF: " + path);
            br.ReadUInt32();
            if (new string(br.ReadChars(4)) != "WAVE") throw new Exception("not WAVE: " + path);

            int channels = 0, bits = 0, format = 0;
            rate = 0;
            byte[] data = null;

            while (fs.Position + 8 <= fs.Length)
            {
                var id = new string(br.ReadChars(4));
                var size = br.ReadUInt32();
                var next = fs.Position + size + (size % 2);   // chunks are word-aligned

                if (id == "fmt ")
                {
                    format = br.ReadUInt16();
                    channels = br.ReadUInt16();
                    rate = (int)br.ReadUInt32();
                    br.ReadUInt32();                          // byte rate
                    br.ReadUInt16();                          // block align
                    bits = br.ReadUInt16();
                }
                else if (id == "data")
                {
                    data = br.ReadBytes((int)size);
                }

                fs.Position = next;
                if (data != null && rate != 0) break;
            }

            if (data == null) throw new Exception("no data chunk: " + path);

            var bytesPerSample = bits / 8;
            var frames = data.Length / (bytesPerSample * channels);
            var mono = new float[frames];

            for (int f = 0; f < frames; f++)
            {
                double sum = 0;
                for (int c = 0; c < channels; c++)
                {
                    var off = (f * channels + c) * bytesPerSample;
                    double v;
                    if (format == 3 && bits == 32)      v = BitConverter.ToSingle(data, off);
                    else if (bits == 16)                v = BitConverter.ToInt16(data, off) / 32768.0;
                    else if (bits == 24)                v = ((data[off] | (data[off + 1] << 8) | ((sbyte)data[off + 2] << 16))) / 8388608.0;
                    else if (bits == 32)                v = BitConverter.ToInt32(data, off) / 2147483648.0;
                    else throw new Exception("unhandled format " + format + "/" + bits);
                    sum += v;
                }
                mono[f] = (float)(sum / channels);
            }

            return mono;
        }
    }

    static double Rms(float[] x, int start, int n)
    {
        double s = 0;
        var end = Math.Min(start + n, x.Length);
        for (int i = start; i < end; i++) s += (double)x[i] * x[i];
        var count = end - start;
        return count > 0 ? Math.Sqrt(s / count) : 0.0;
    }

    static double Db(double lin) { return 20.0 * Math.Log10(Math.Max(lin, 1e-12)); }

    /// Short-time RMS in dB. 10 ms frames, 5 ms hop.
    static double[] Envelope(float[] x, int rate, out int hop)
    {
        var frame = rate / 100;
        hop = frame / 2;
        var n = Math.Max(0, (x.Length - frame) / hop);
        var env = new double[n];
        for (int i = 0; i < n; i++) env[i] = Db(Rms(x, i * hop, frame));
        return env;
    }

    /// Integer sample lag that best aligns b to a, searched coarsely over
    /// +/- 4800 samples. Ableton's PDC should make this zero; if it is not,
    /// every number after it is suspect, so it gets printed.
    static int BestLag(float[] a, float[] b, int rate)
    {
        var limit = rate / 10;
        var step = 8;
        var window = Math.Min(rate * 4, Math.Min(a.Length, b.Length) - limit - 1);
        int bestLag = 0; double best = double.NegativeInfinity;

        for (int lag = -limit; lag <= limit; lag += step)
        {
            double dot = 0;
            for (int i = limit; i < window; i += 4)
            {
                var j = i + lag;
                if (j < 0 || j >= b.Length) continue;
                dot += (double)a[i] * b[j];
            }
            if (dot > best) { best = dot; bestLag = lag; }
        }
        return bestLag;
    }

    //== env ==================================================================

    static void Env(string[] args)
    {
        int rate;
        var dry = ReadMono(args[1], out rate);
        int hop;
        var dryEnv = Envelope(dry, rate, out hop);
        var framesPerSec = (double)rate / hop;

        // A gap is where the dry sits far enough under its own loud level for
        // long enough to count as between phrases.
        var loud = Percentile(dryEnv.Where(v => v > -90).ToArray(), 0.90);
        var gapFloor = loud - 24.0;
        var minGapFrames = (int)(0.25 * framesPerSec);

        Console.WriteLine("dry: {0} frames at {1:F1}/s, loud level {2:F1} dB, gap floor {3:F1} dB",
                          dryEnv.Length, framesPerSec, loud, gapFloor);

        var gaps = new List<int[]>();
        int run = -1;
        for (int i = 0; i < dryEnv.Length; i++)
        {
            var below = dryEnv[i] < gapFloor;
            if (below && run < 0) run = i;
            if (!below && run >= 0)
            {
                if (i - run >= minGapFrames) gaps.Add(new[] { run, i });
                run = -1;
            }
        }
        Console.WriteLine("found {0} phrase gaps of 250 ms or more\n", gaps.Count);

        for (int a = 2; a + 1 < args.Length; a += 2)
        {
            var path = args[a];
            var label = args[a + 1];
            int r2;
            var proc = ReadMono(path, out r2);
            var lag = BestLag(dry, proc, rate);

            int h2;
            var procEnv = Envelope(proc, rate, out h2);

            // Relative gain, dry-referenced. The renders are gain-matched, so
            // the unreduced gain is whatever the curve sits at when nothing is
            // being asked of it -- the top of the distribution.
            var n = Math.Min(dryEnv.Length, procEnv.Length);
            var gain = new double[n];
            for (int i = 0; i < n; i++)
            {
                var j = i + lag / hop;
                gain[i] = (j >= 0 && j < procEnv.Length && dryEnv[i] > gapFloor)
                          ? procEnv[j] - dryEnv[i]
                          : double.NaN;
            }

            var valid = gain.Where(v => !double.IsNaN(v)).ToArray();
            if (valid.Length == 0) { Console.WriteLine("{0}: no usable frames", label); continue; }

            var unity = Percentile(valid, 0.95);
            Console.WriteLine("=== {0} ===", label);
            Console.WriteLine("  lag {0} samples, unity gain {1:F2} dB", lag, unity);

            var peak = valid.Min();
            Console.WriteLine("  peak reduction {0:F2} dB", unity - peak);

            Console.WriteLine("  {0,-8} {1,-10} {2,-12} {3,-12} {4}",
                              "gap", "length", "GR entering", "GR leaving", "recovered");
            var entering = new List<double>();
            var leaving = new List<double>();

            for (int gi = 0; gi < gaps.Count; gi++)
            {
                var g0 = gaps[gi][0];
                var g1 = gaps[gi][1];

                // Reduction standing as the phrase ends, and as the next begins.
                var pre = Window(gain, g0 - (int)(0.15 * framesPerSec), g0);
                var post = Window(gain, g1, g1 + (int)(0.15 * framesPerSec));
                if (pre.Length == 0 || post.Length == 0) continue;

                var grIn = unity - pre.Min();
                var grOut = unity - post.Max();
                entering.Add(grIn);
                leaving.Add(grOut);

                Console.WriteLine("  {0,-8} {1,-10} {2,-12} {3,-12} {4}",
                                  gi + 1,
                                  ((g1 - g0) / framesPerSec).ToString("F2", CultureInfo.InvariantCulture) + " s",
                                  grIn.ToString("F2", CultureInfo.InvariantCulture) + " dB",
                                  grOut.ToString("F2", CultureInfo.InvariantCulture) + " dB",
                                  (grIn > 0.05 ? (100.0 * (grIn - grOut) / grIn).ToString("F0") + "%" : "-"));
            }

            if (entering.Count > 0)
                Console.WriteLine("  MEAN     {0,-10} {1,-12} {2,-12} {3}",
                                  "",
                                  entering.Average().ToString("F2", CultureInfo.InvariantCulture) + " dB",
                                  leaving.Average().ToString("F2", CultureInfo.InvariantCulture) + " dB",
                                  (100.0 * (entering.Average() - leaving.Average()) / entering.Average()).ToString("F0") + "%");
            Console.WriteLine();
        }
    }

    static double[] Window(double[] x, int from, int to)
    {
        var list = new List<double>();
        for (int i = Math.Max(0, from); i < Math.Min(x.Length, to); i++)
            if (!double.IsNaN(x[i])) list.Add(x[i]);
        return list.ToArray();
    }

    static double Percentile(double[] x, double p)
    {
        if (x.Length == 0) return 0;
        var s = (double[])x.Clone();
        Array.Sort(s);
        return s[Math.Min(s.Length - 1, Math.Max(0, (int)(p * (s.Length - 1))))];
    }

    //== band =================================================================

    static void Fft(double[] re, double[] im)
    {
        var n = re.Length;
        for (int i = 1, j = 0; i < n; i++)
        {
            var bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { var t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            var ang = -2.0 * Math.PI / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < len / 2; k++)
                {
                    var wr = Math.Cos(ang * k); var wi = Math.Sin(ang * k);
                    var ur = re[i + k]; var ui = im[i + k];
                    var vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                    var vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                    re[i + k] = ur + vr; im[i + k] = ui + vi;
                    re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                }
        }
    }

    /// Welch power spectrum, Hann window with power correction.
    static double[] Spectrum(float[] x, int size)
    {
        var acc = new double[size / 2 + 1];
        var win = new double[size];
        double winPow = 0;
        for (int i = 0; i < size; i++)
        {
            win[i] = 0.5 - 0.5 * Math.Cos(2.0 * Math.PI * i / size);
            winPow += win[i] * win[i];
        }
        winPow /= size;

        var hop = size / 2;
        int blocks = 0;
        for (int start = 0; start + size <= x.Length; start += hop)
        {
            var re = new double[size];
            var im = new double[size];
            for (int i = 0; i < size; i++) re[i] = x[start + i] * win[i];
            Fft(re, im);
            for (int k = 0; k < acc.Length; k++)
                acc[k] += (re[k] * re[k] + im[k] * im[k]) / (size * size * winPow);
            blocks++;
        }
        if (blocks > 0) for (int k = 0; k < acc.Length; k++) acc[k] /= blocks;
        return acc;
    }

    static void Band(string[] args)
    {
        var edges = new[] { 20.0, 100, 250, 500, 1000, 2000, 3000, 5000, 7000, 9000, 12000, 16000, 20000 };
        if (Environment.GetEnvironmentVariable("BMO_FINE") == "1")
            edges = new[] { 1200.0, 1500, 1800, 2000, 2200, 2400, 2600, 2900, 3200, 3600, 4000, 4500, 5000 };
        const int size = 8192;

        int rate;
        var refSig = ReadMono(args[1], out rate);
        var refLabel = args[2];
        var refRms = Rms(refSig, 0, refSig.Length);

        var names = new List<string>();
        var tables = new List<double[]>();

        // Each file maps its own bins. The reference vocals are 44.1k PCM and
        // the Ableton bounces are 48k float, so binning everything at the
        // reference's rate stretched every bounce's frequency axis by 8% --
        // which read as a phantom -8 dB shelf at the top of the band table.
        Action<float[], string, double, int> add = (sig, label, rms, sigRate) =>
        {
            // Normalise to the reference so the table shows spectral balance,
            // not level. With AUTO off the absolute level moves, and without
            // this the overall-hotness figure changes meaning.
            var scale = refRms / Math.Max(rms, 1e-12);
            var scaled = new float[sig.Length];
            for (int i = 0; i < sig.Length; i++) scaled[i] = (float)(sig[i] * scale);

            var spec = Spectrum(scaled, size);
            var binHz = (double)sigRate / size;
            var row = new double[edges.Length - 1];
            for (int b = 0; b < row.Length; b++)
            {
                double sum = 0;
                for (int k = 0; k < spec.Length; k++)
                {
                    var f = k * binHz;
                    if (f >= edges[b] && f < edges[b + 1]) sum += spec[k];
                }
                row[b] = Db(Math.Sqrt(sum));
            }
            names.Add(label);
            tables.Add(row);
        };

        add(refSig, refLabel, refRms, rate);
        for (int a = 3; a + 1 < args.Length; a += 2)
        {
            int r2;
            var sig = ReadMono(args[a], out r2);
            add(sig, args[a + 1], Rms(sig, 0, sig.Length), r2);
        }

        Console.Write("{0,-14}", "band");
        foreach (var n in names) Console.Write("{0,14}", n);
        for (int i = 1; i < names.Count; i++) Console.Write("{0,14}", "d:" + names[i]);
        Console.WriteLine();

        for (int b = 0; b < edges.Length - 1; b++)
        {
            var name = (edges[b] >= 1000 ? (edges[b] / 1000).ToString("0.#") + "k" : edges[b].ToString("0"))
                     + "-" + (edges[b + 1] >= 1000 ? (edges[b + 1] / 1000).ToString("0.#") + "k" : edges[b + 1].ToString("0"));
            Console.Write("{0,-14}", name);
            foreach (var t in tables) Console.Write("{0,14:F2}", t[b]);
            for (int i = 1; i < tables.Count; i++) Console.Write("{0,14:F2}", tables[i][b] - tables[0][b]);
            Console.WriteLine();
        }
    }

    //== sim ==================================================================
    //
    // A port of modules/opto/dsp/Detector.h, so the release constants can be
    // fitted against the measured target instead of guessed at. There is no
    // C++ toolchain on this machine; this is the only way to try a constant
    // without spending a CI cycle. Kept deliberately literal -- if it drifts
    // from Detector.h it is worthless.

    class Curve { public float T, Slope, Knee; }

    static float Knee(float levelDb, Curve c)
    {
        var diff = levelDb - c.T;
        var half = c.Knee * 0.5f;
        if (diff <= -half) return 0.0f;
        if (diff < half) { var t = diff + half; return c.Slope * (t * t) / (2.0f * c.Knee); }
        return c.Slope * diff;
    }

    static float Coeff(float tau, double rate)
    {
        return 1.0f - (float)Math.Exp(-1.0 / (Math.Max(rate, 1.0) * tau));
    }

    class Cell
    {
        public bool Feedback;                       // Tele is feedback, Stressed feedforward
        public double Rate = 48000.0;
        public float FastTau = 0.06f;
        public float SlowMin = 1.0f, SlowMax = 15.0f;   // Tele: dosage slides between these
        public float SlowFixed = 20.0f;                 // Stressed: one ceiling
        public float ChargeAttack = 0.3f;
        public float ChargeForget = 1.0f;               // Tele used SlowMin; Stressed used 4.0
        public float DosageEngage = 1.0f, DosageGrowth = 3.0f, DosageForget = 4.0f;
        public float DepthOnsetDb = 0.0f, DepthFullDb = 20.0f;

        float env, gain = 1.0f, red, charge, dosage;

        public float Reduction { get { return red; } }

        public float Step(float x, Curve c)
        {
            var y = Feedback ? x * gain : x;
            var lin = Math.Abs(y);
            var rising = lin > env;

            var dosageAmount = 1.0f;
            var slow = SlowFixed;
            if (Feedback)
            {
                var dosageT = Math.Min(dosage / DosageGrowth, 4.0f);
                dosageAmount = 1.0f - (float)Math.Exp(-dosageT);
                slow = SlowMin + (SlowMax - SlowMin) * dosageAmount;
            }

            var span = Math.Max(DepthFullDb - DepthOnsetDb, 0.001f);
            var depth = Math.Min(Math.Max((charge - DepthOnsetDb) / span, 0.0f), 1.0f);
            var relTau = FastTau + (slow - FastTau) * depth;

            env += (rising ? Coeff(FastTau == 0 ? 0.01f : 0.010f, Rate) : Coeff(relTau, Rate)) * (lin - env);

            var envDb = 20.0f * (float)Math.Log10(Math.Max(env, 1e-6f));
            red = Math.Min(Math.Max(Knee(envDb, c), 0.0f), 40.0f);
            gain = (float)Math.Pow(10.0, -red / 20.0);

            var chargeCoeff = red > charge ? Coeff(ChargeAttack, Rate) : Coeff(ChargeForget, Rate);
            charge += chargeCoeff * (red - charge);

            if (Feedback)
            {
                var dt = (float)(1.0 / Rate);
                dosage += red > DosageEngage ? dt : -dt * (DosageGrowth / DosageForget);
                dosage = Math.Min(Math.Max(dosage, 0.0f), DosageGrowth * 4.0f);
            }

            return Feedback ? y : x * gain;
        }
    }

    static void Sim(string[] args)
    {
        // sim <dry.wav> <tele|eld> <crush%> [key=value ...]
        int rate;
        var dry = ReadMono(args[1], out rate);
        var mode = args[2].ToLowerInvariant();
        var crush = float.Parse(args[3], CultureInfo.InvariantCulture);

        var cell = new Cell { Rate = rate, Feedback = (mode == "tele") };
        if (mode != "tele") { cell.ChargeForget = 4.0f; cell.SlowFixed = 20.0f; }

        for (int a = 4; a < args.Length; a++)
        {
            var kv = args[a].Split('=');
            if (kv.Length != 2) continue;
            var v = float.Parse(kv[1], CultureInfo.InvariantCulture);
            switch (kv[0])
            {
                case "chargeForget": cell.ChargeForget = v; break;
                case "dosageForget": cell.DosageForget = v; break;
                case "depthOnset":   cell.DepthOnsetDb = v; break;
                case "depthFull":    cell.DepthFullDb = v; break;
                case "slowMax":      cell.SlowMax = v; break;
                case "slowFixed":    cell.SlowFixed = v; break;
                default: Console.Error.WriteLine("unknown key " + kv[0]); break;
            }
        }

        var c = mode == "tele"
              ? new Curve { T = -8.0f + (crush / 100.0f) * -30.0f, Slope = 3.0f - 1.0f, Knee = 16.0f }
              : new Curve { T = -8.0f + (crush / 100.0f) * -30.0f, Slope = 1.0f - 1.0f / 10.0f, Knee = 6.0f };

        // Reduction straight from the cell, frame-maxed onto the same grid the
        // render comparison uses.
        var frame = rate / 100;
        var hop = frame / 2;
        var frames = Math.Max(0, (dry.Length - frame) / hop);
        var red = new double[frames];
        var idx = 0; var acc = 0.0f; var n = 0;
        double eIn = 0, eOut = 0;

        for (int i = 0; i < dry.Length; i++)
        {
            var yOut = cell.Step(dry[i], c);
            eIn += (double)dry[i] * dry[i]; eOut += (double)yOut * yOut;
            acc = Math.Max(acc, cell.Reduction);
            if (++n == hop) { if (idx < frames) red[idx++] = acc; acc = 0; n = 0; }
        }

        var makeupDb = 10.0 * Math.Log10(Math.Max(eIn, 1e-20) / Math.Max(eOut, 1e-20));

        int h2;
        var dryEnv = Envelope(dry, rate, out h2);
        var framesPerSec = (double)rate / hop;
        var loud = Percentile(dryEnv.Where(v => v > -90).ToArray(), 0.90);
        var gapFloor = loud - 24.0;
        var minGapFrames = (int)(0.25 * framesPerSec);

        var gaps = new List<int[]>();
        int run = -1;
        for (int i = 0; i < dryEnv.Length; i++)
        {
            var below = dryEnv[i] < gapFloor;
            if (below && run < 0) run = i;
            if (!below && run >= 0) { if (i - run >= minGapFrames) gaps.Add(new[] { run, i }); run = -1; }
        }

        var entering = new List<double>();
        var leaving = new List<double>();
        Console.WriteLine("  {0,-6} {1,-9} {2,-13} {3,-13} {4}", "gap", "length", "GR entering", "GR leaving", "recovered");

        foreach (var g in gaps)
        {
            var pre = Window(red, g[0] - (int)(0.15 * framesPerSec), g[0]);
            var post = Window(red, g[1], g[1] + (int)(0.15 * framesPerSec));
            if (pre.Length == 0 || post.Length == 0) continue;
            var grIn = pre.Max();
            var grOut = post.Min();
            entering.Add(grIn); leaving.Add(grOut);
            Console.WriteLine("  {0,-6} {1,-9} {2,-13} {3,-13} {4}",
                              gaps.IndexOf(g) + 1,
                              ((g[1] - g[0]) / framesPerSec).ToString("F2", CultureInfo.InvariantCulture) + " s",
                              grIn.ToString("F2", CultureInfo.InvariantCulture) + " dB",
                              grOut.ToString("F2", CultureInfo.InvariantCulture) + " dB",
                              grIn > 0.05 ? (100.0 * (grIn - grOut) / grIn).ToString("F0") + "%" : "-");
        }

        Console.WriteLine("  peak reduction {0:F2} dB   makeup needed {1:F2} dB", red.Max(), makeupDb);
        // (was:         Console.WriteLine("  peak reduction {0:F2} dB", red.Max());)
        if (entering.Count > 0)
            Console.WriteLine("  MEAN   {0,-9} {1,-13} {2,-13} {3}", "",
                              entering.Average().ToString("F2", CultureInfo.InvariantCulture) + " dB",
                              leaving.Average().ToString("F2", CultureInfo.InvariantCulture) + " dB",
                              (100.0 * (entering.Average() - leaving.Average()) / entering.Average()).ToString("F0") + "%");
    }

    /// Mirrors tests/dsp/OptoDspTests.cpp's reductionAtEndAndAfter: a 200 Hz
    /// sine at 0.9 for `loud` seconds, then silence, reporting the reduction
    /// when the hit ends and again at the end of the silence. Lets the CI
    /// test's outcome be predicted without a C++ toolchain.
    static void Hold(string[] args)
    {
        var mode = args[1].ToLowerInvariant();
        var loud = double.Parse(args[2], CultureInfo.InvariantCulture);
        var silence = double.Parse(args[3], CultureInfo.InvariantCulture);
        var crush = float.Parse(args[4], CultureInfo.InvariantCulture);
        var amp = 0.9;
        double rate = 44100.0;

        var cell = new Cell { Rate = rate, Feedback = (mode == "tele") };
        if (mode != "tele") { cell.ChargeForget = 4.0f; cell.SlowFixed = 20.0f; }
        for (int a = 5; a < args.Length; a++)
        {
            var kv = args[a].Split('=');
            if (kv.Length != 2) continue;
            var v = float.Parse(kv[1], CultureInfo.InvariantCulture);
            switch (kv[0])
            {
                case "chargeForget": cell.ChargeForget = v; break;
                case "dosageForget": cell.DosageForget = v; break;
                case "slowMax":      cell.SlowMax = v; break;
                case "amp": amp = v; break;
                case "slowFixed":    cell.SlowFixed = v; break;
            }
        }

        var c = mode == "tele"
              ? new Curve { T = -8.0f + (crush / 100.0f) * -30.0f, Slope = 2.0f, Knee = 16.0f }
              : new Curve { T = -8.0f + (crush / 100.0f) * -30.0f, Slope = 1.0f - 1.0f / 10.0f, Knee = 6.0f };

        var loudSamples = (int)(loud * rate);
        var total = loudSamples + (int)(silence * rate);
        float atEnd = 0, atCheckpoint = 0;

        for (int i = 0; i < total; i++)
        {
            var x = i < loudSamples ? (float)(amp * Math.Sin(2.0 * Math.PI * 200.0 * i / rate)) : 0.0f;
            cell.Step(x, c);
            if (i == loudSamples - 1) atEnd = cell.Reduction;
            atCheckpoint = cell.Reduction;
        }

        Console.WriteLine("{0,-8} end {1,7:F3} dB   after {2,7:F4} dB   fraction {3:F4}",
                          mode, atEnd, atCheckpoint, atEnd > 0 ? atCheckpoint / atEnd : 0.0);
    }

    /// A literal port of tests/plugin/TestUtil.h's voice() -- the generator the
    /// preset level-matching test runs on, normalised to -18 dBFS RMS.
    static float[] Voice(int samples)
    {
        var outv = new float[samples];
        double sumSquares = 0.0;
        var twoPi = 2.0 * Math.PI;

        for (int i = 0; i < samples; i++)
        {
            var t = i / 48000.0;
            var beat = t % 0.55;
            var envelope = ((t % 3.0) < 1.6 ? 1.0 : 0.0)
                         * (beat < 0.01 ? beat / 0.01 : Math.Exp(-(beat - 0.01) * 7.0));

            double sum = 0.0;
            for (int h = 1; h <= 120; h++)
                sum += Math.Pow(h, -1.4) * Math.Sin(2.0 * twoPi * 75.0 * h * t);

            outv[i] = (float)(envelope * sum);
            sumSquares += (double)outv[i] * outv[i];
        }

        var rms = Math.Sqrt(sumSquares / samples);
        var gain = rms > 0.0 ? Math.Pow(10.0, -18.0 / 20.0) / rms : 1.0;
        for (int i = 0; i < samples; i++) outv[i] = (float)(outv[i] * gain);
        return outv;
    }

    /// preset <tele|eld> <crush> [key=value ...]
    /// Reports the makeup LEVEL a preset needs to come back level-matched on
    /// voice(), so the figure can be solved here instead of read off a CI run.
    /// Detector only -- DspCore's drive and Color stages are not modelled, so
    /// treat it as accurate to about a decibel, inside a +/-3 dB tolerance.
    static void Preset(string[] args)
    {
        var mode = args[1].ToLowerInvariant();
        var crush = float.Parse(args[2], CultureInfo.InvariantCulture);
        double rate = 48000.0;

        var cell = new Cell { Rate = rate, Feedback = (mode == "tele") };
        if (mode != "tele") { cell.ChargeForget = 4.0f; cell.SlowFixed = 20.0f; }
        for (int a = 3; a < args.Length; a++)
        {
            var kv = args[a].Split('=');
            if (kv.Length != 2) continue;
            var v = float.Parse(kv[1], CultureInfo.InvariantCulture);
            switch (kv[0])
            {
                case "chargeForget": cell.ChargeForget = v; break;
                case "slowMax":      cell.SlowMax = v; break;
                case "slowFixed":    cell.SlowFixed = v; break;
            }
        }

        var c = mode == "tele"
              ? new Curve { T = -8.0f + (crush / 100.0f) * -30.0f, Slope = 2.0f, Knee = 16.0f }
              : new Curve { T = -8.0f + (crush / 100.0f) * -30.0f, Slope = 1.0f - 1.0f / 10.0f, Knee = 6.0f };

        var src = Voice(512 * 300);
        double eIn = 0, eOut = 0, peak = 0;
        // The test skips the first 20 blocks; match that so the settling
        // transient does not drag the figure.
        var skip = 512 * 20;

        for (int i = 0; i < src.Length; i++)
        {
            var y = cell.Step(src[i], c);
            peak = Math.Max(peak, cell.Reduction);
            if (i < skip) continue;
            eIn += (double)src[i] * src[i];
            eOut += (double)y * y;
        }

        var needed = 10.0 * Math.Log10(Math.Max(eIn, 1e-20) / Math.Max(eOut, 1e-20));
        Console.WriteLine("{0,-5} crush {1,-5} peak GR {2,6:F2} dB   LEVEL needed {3,6:F2} dB   {4}",
                          mode, crush, peak, needed, needed > 24.0 ? "OVER THE +24 RAIL" : "fits");
    }

    /// RBJ peaking filter magnitude in dB at `f`, for the biquad the Bell in
    /// modules/sat/dsp/Filters.h realises.
    static double BellDb(double f, double f0, double q, double gainDb, double rate)
    {
        var A = Math.Pow(10.0, gainDb / 40.0);
        var w0 = 2.0 * Math.PI * f0 / rate;
        var alpha = Math.Sin(w0) / (2.0 * q);
        var cw = Math.Cos(w0);

        double b0 = 1 + alpha * A, b1 = -2 * cw, b2 = 1 - alpha * A;
        double a0 = 1 + alpha / A, a1 = -2 * cw, a2 = 1 - alpha / A;

        var w = 2.0 * Math.PI * f / rate;
        // |H(e^jw)| by direct evaluation.
        double nr = b0 + b1 * Math.Cos(w) + b2 * Math.Cos(2 * w);
        double ni = -(b1 * Math.Sin(w) + b2 * Math.Sin(2 * w));
        double dr = a0 + a1 * Math.Cos(w) + a2 * Math.Cos(2 * w);
        double di = -(a1 * Math.Sin(w) + a2 * Math.Sin(2 * w));

        var mag = Math.Sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
        return 20.0 * Math.Log10(Math.Max(mag, 1e-12));
    }

    /// Energy-averaged bell response across a band, which is what the band
    /// table measures -- not the response at the band's centre frequency.
    static double BellBandDb(double lo, double hi, double f0, double q, double gainDb, double rate)
    {
        double sum = 0; int n = 0;
        for (double f = lo; f <= hi; f += (hi - lo) / 64.0)
        {
            sum += Math.Pow(10.0, BellDb(f, f0, q, gainDb, rate) / 10.0);
            n++;
        }
        return 10.0 * Math.Log10(sum / n);
    }

    /// Searches f0/Q/gain for the bell that best closes the measured gap to
    /// the reference, band by band.
    static void Bell(string[] args)
    {
        double rate = 44100.0;
        var bands = new[] { new[] { 3000.0, 5000 }, new[] { 5000.0, 7000 }, new[] { 7000.0, 9000 },
                            new[] { 9000.0, 12000 }, new[] { 12000.0, 16000 }, new[] { 16000.0, 20000 } };
        // target minus ours, from the corrected band table.
        var needed = new[] { -0.29, 0.89, 1.54, -1.13, -1.82, 1.27 };
        // 16-20k carries ~20 dB less energy than the bands below it and is the
        // least trustworthy row, so it barely votes.
        var weight = new[] { 0.8, 1.0, 1.0, 1.0, 1.0, 0.2 };

        Console.WriteLine("current bell 7000 Hz / Q 0.90 / +11.0 dB");
        Console.Write("  band response:");
        for (int b = 0; b < bands.Length; b++)
            Console.Write("  {0:F2}", BellBandDb(bands[b][0], bands[b][1], 7000, 0.90, 11.0, rate));
        Console.WriteLine();
        Console.WriteLine("  needed change:  " + string.Join("  ", needed.Select(v => v.ToString("F2"))));
        Console.WriteLine();

        double bestErr = double.MaxValue; double bf = 0, bq = 0, bg = 0;
        for (double f0 = 5500; f0 <= 8500; f0 += 100)
            for (double q = 0.7; q <= 3.0; q += 0.05)
                for (double g = 7.0; g <= 15.0; g += 0.25)
                {
                    double err = 0;
                    for (int b = 0; b < bands.Length; b++)
                    {
                        var now = BellBandDb(bands[b][0], bands[b][1], 7000, 0.90, 11.0, rate);
                        var cand = BellBandDb(bands[b][0], bands[b][1], f0, q, g, rate);
                        var d = (cand - now) - needed[b];
                        err += weight[b] * d * d;
                    }
                    if (err < bestErr) { bestErr = err; bf = f0; bq = q; bg = g; }
                }

        Console.WriteLine("best fit: {0:F0} Hz / Q {1:F2} / {2:F2} dB   (residual {3:F3})", bf, bq, bg, bestErr);
        Console.Write("  delivers:     ");
        for (int b = 0; b < bands.Length; b++)
            Console.Write("  {0:F2}", BellBandDb(bands[b][0], bands[b][1], bf, bq, bg, rate)
                                     - BellBandDb(bands[b][0], bands[b][1], 7000, 0.90, 11.0, rate));
        Console.WriteLine();
    }

    static int Main(string[] args)
    {
        if (args.Length < 2) { Console.Error.WriteLine("usage: Analyze env|band ..."); return 2; }
        try
        {
            if (args[0] == "env") Env(args);
            else if (args[0] == "band") Band(args);
            else if (args[0] == "sim") Sim(args);
            else if (args[0] == "hold") Hold(args);
            else if (args[0] == "preset") Preset(args);
            else if (args[0] == "bell") Bell(args);
            else { Console.Error.WriteLine("unknown mode " + args[0]); return 2; }
        }
        catch (Exception e) { Console.Error.WriteLine("error: " + e.Message); return 1; }
        return 0;
    }
}
