// BMO render inspection harness.
//
//   Inspect scan  <png> row <y> [x0 x1]     run-length along one row
//   Inspect scan  <png> col <x> [y0 y1]     run-length down one column
//   Inspect hist  <png> <x> <y> <w> <h> [n] exact-colour histogram over a box
//   Inspect crop  <png> <x> <y> <w> <h> <scale> <out.png>
//   Inspect sheet <out.png> <png> <label> [<png> <label> ...]
//   Inspect hash  <png> [<png> ...]         SHA-256 of the pixels
//   Inspect ratio <a> <b> [...]             contrast and L* between colours
//   Inspect gaps  <png> [min] [header] [scale]  bands of bare plate down a panel
//
// These are the tools that settled the visual questions on the ui-editor
// branch. They existed as throwaway PowerShell in a scratchpad and died with
// it; this is them written down so the next session does not rebuild them.
//
// Local-only, and **deliberately not wired into tools/CMakeLists.txt**, for
// the same reason tools/measure/renders is not -- see its README. Nothing in
// here should ever be able to fail a plugin build.
//
//   csc -out:Inspect.exe Inspect.cs        # Framework64/v4.0.30319/csc.exe
//
// The lessons each mode exists to enforce are on the modes themselves.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;

static class Program
{
    //== Pixels ================================================================

    /// A bitmap flattened to straight BGRA bytes, so every mode reads pixels
    /// the same way regardless of what the PNG was encoded as.
    sealed class Pixels
    {
        public int Width, Height;
        public byte[] Data;                       // 4 bytes per pixel, BGRA

        public int Argb(int x, int y)
        {
            var o = (y * Width + x) * 4;
            return (Data[o + 3] << 24) | (Data[o + 2] << 16) | (Data[o + 1] << 8) | Data[o];
        }

        public string Hex(int x, int y)
        {
            var o = (y * Width + x) * 4;
            return string.Format("#{0:x2}{1:x2}{2:x2}", Data[o + 2], Data[o + 1], Data[o]);
        }
    }

    static Pixels Read(string path)
    {
        if (!File.Exists(path)) throw new Exception("no such file: " + path);

        using (var src = new Bitmap(path))
        using (var bmp = new Bitmap(src.Width, src.Height, PixelFormat.Format32bppArgb))
        {
            using (var g = Graphics.FromImage(bmp))
            {
                g.InterpolationMode = InterpolationMode.NearestNeighbor;
                g.PixelOffsetMode = PixelOffsetMode.Half;
                g.DrawImage(src, 0, 0, src.Width, src.Height);
            }

            var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
            var bits = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var data = new byte[bmp.Width * bmp.Height * 4];

            for (int y = 0; y < bmp.Height; y++)
                System.Runtime.InteropServices.Marshal.Copy(
                    bits.Scan0 + y * bits.Stride, data, y * bmp.Width * 4, bmp.Width * 4);

            bmp.UnlockBits(bits);
            return new Pixels { Width = bmp.Width, Height = bmp.Height, Data = data };
        }
    }

    /// Refuses to overwrite. A stale read of a path that had only been
    /// re-rendered in the reader's head cost a full debugging cycle chasing a
    /// bug that did not exist, so the rule from that session -- render to a
    /// unique filename -- is enforced here rather than remembered.
    static void GuardOutput(string path)
    {
        if (File.Exists(path))
            throw new Exception("output exists, pick another name: " + path
                                + "\n  (a stale render read as a fresh one is the most expensive"
                                + " mistake this harness can make)");
    }

    //== scan ==================================================================

    /// Prints the runs of identical colour along one row or column: start,
    /// end, width, exact hex.
    ///
    /// This is the mode that matters most. Squinting at a magnified crop
    /// cannot tell you that two inks are the same to within 1.01:1 -- the dark
    /// band selector "ring" looked like a ring and scanned as #8d8d98,
    /// #8e8e93, #8d8d98 running together as one 21 px slab. No amount of
    /// looking produces that number.
    static void Scan(string[] a)
    {
        var px = Read(a[0]);
        var axis = a[1].ToLowerInvariant();
        var at = int.Parse(a[2], CultureInfo.InvariantCulture);

        var alongMax = axis == "row" ? px.Width : px.Height;
        var from = a.Length > 3 ? int.Parse(a[3], CultureInfo.InvariantCulture) : 0;
        var to = a.Length > 4 ? int.Parse(a[4], CultureInfo.InvariantCulture) : alongMax - 1;

        if (axis != "row" && axis != "col") throw new Exception("axis is row or col, got " + a[1]);
        var acrossMax = axis == "row" ? px.Height : px.Width;
        if (at < 0 || at >= acrossMax) throw new Exception(a[1] + " " + at + " is outside 0.." + (acrossMax - 1));
        from = Math.Max(0, from);
        to = Math.Min(alongMax - 1, to);

        Console.WriteLine("{0} {1} of {2}  ({3}x{4}), {5}..{6}",
                          axis, at, Path.GetFileName(a[0]), px.Width, px.Height, from, to);
        Console.WriteLine("  {0,5} {1,5} {2,5}  {3}", "from", "to", "width", "colour");

        int runStart = from;
        int prev = axis == "row" ? px.Argb(from, at) : px.Argb(at, from);

        for (int i = from + 1; i <= to + 1; i++)
        {
            int cur = i > to ? ~prev
                    : (axis == "row" ? px.Argb(i, at) : px.Argb(at, i));

            if (cur != prev)
            {
                var hex = axis == "row" ? px.Hex(runStart, at) : px.Hex(at, runStart);
                Console.WriteLine("  {0,5} {1,5} {2,5}  {3}", runStart, i - 1, i - runStart, hex);
                runStart = i;
                prev = cur;
            }
        }
    }

    //== hist ==================================================================

    /// Exact-colour counts over a box, most common first.
    ///
    /// Use this, not "the darkest pixel in the box", to identify what colour
    /// something is. Two inks 128 apart in hex can be two apart in luminance
    /// -- #497450 and #317290 measure 104 and 102 -- so darkest-pixel is a
    /// coin flip between a caption and the rule beside it.
    static void Hist(string[] a)
    {
        var px = Read(a[0]);
        int x = int.Parse(a[1], CultureInfo.InvariantCulture);
        int y = int.Parse(a[2], CultureInfo.InvariantCulture);
        int w = int.Parse(a[3], CultureInfo.InvariantCulture);
        int h = int.Parse(a[4], CultureInfo.InvariantCulture);
        int top = a.Length > 5 ? int.Parse(a[5], CultureInfo.InvariantCulture) : 20;

        var box = Rectangle.Intersect(new Rectangle(x, y, w, h),
                                      new Rectangle(0, 0, px.Width, px.Height));
        if (box.IsEmpty) throw new Exception("box is entirely outside the image");
        if (box.Width != w || box.Height != h)
            Console.WriteLine("note: box clipped to {0},{1} {2}x{3}",
                              box.X, box.Y, box.Width, box.Height);

        var counts = new Dictionary<int, int>();
        for (int yy = box.Top; yy < box.Bottom; yy++)
            for (int xx = box.Left; xx < box.Right; xx++)
            {
                var c = px.Argb(xx, yy);
                int n;
                counts[c] = counts.TryGetValue(c, out n) ? n + 1 : 1;
            }

        var total = box.Width * box.Height;
        Console.WriteLine("{0}  box {1},{2} {3}x{4} = {5} px, {6} distinct colours",
                          Path.GetFileName(a[0]), box.X, box.Y, box.Width, box.Height,
                          total, counts.Count);
        Console.WriteLine("  {0,8} {1,7}  {2,-9} {3,6}", "count", "share", "colour", "L*");

        foreach (var kv in counts.OrderByDescending(k => k.Value).Take(top))
        {
            var r = (kv.Key >> 16) & 0xff;
            var g = (kv.Key >> 8) & 0xff;
            var b = kv.Key & 0xff;
            Console.WriteLine("  {0,8} {1,6:0.0}%  {2,-9} {3,6:0.0}",
                              kv.Value, 100.0 * kv.Value / total,
                              string.Format("#{0:x2}{1:x2}{2:x2}", r, g, b),
                              Lstar(Luminance(r, g, b)));
        }

        if (counts.Count > top) Console.WriteLine("  ... {0} more", counts.Count - top);
    }

    //== crop ==================================================================

    /// Crop and magnify, nearest-neighbour so a pixel stays a pixel. Smoothing
    /// here would invent colours that are not in the render, which is the one
    /// thing an inspection tool must not do.
    static void Crop(string[] a)
    {
        var srcPath = a[0];
        int x = int.Parse(a[1], CultureInfo.InvariantCulture);
        int y = int.Parse(a[2], CultureInfo.InvariantCulture);
        int w = int.Parse(a[3], CultureInfo.InvariantCulture);
        int h = int.Parse(a[4], CultureInfo.InvariantCulture);
        int scale = int.Parse(a[5], CultureInfo.InvariantCulture);
        var outPath = a[6];

        if (scale < 1) throw new Exception("scale is a positive integer, got " + scale);
        GuardOutput(outPath);

        using (var src = new Bitmap(srcPath))
        {
            var box = Rectangle.Intersect(new Rectangle(x, y, w, h),
                                          new Rectangle(0, 0, src.Width, src.Height));
            if (box.IsEmpty) throw new Exception("box is entirely outside the image");

            using (var dst = new Bitmap(box.Width * scale, box.Height * scale, PixelFormat.Format32bppArgb))
            {
                using (var g = Graphics.FromImage(dst))
                {
                    g.InterpolationMode = InterpolationMode.NearestNeighbor;
                    g.PixelOffsetMode = PixelOffsetMode.Half;
                    g.SmoothingMode = SmoothingMode.None;
                    g.DrawImage(src, new Rectangle(0, 0, dst.Width, dst.Height), box, GraphicsUnit.Pixel);
                }
                dst.Save(outPath, ImageFormat.Png);
            }

            Console.WriteLine("{0}  {1},{2} {3}x{4} at {5}x -> {6} ({7}x{8})",
                              Path.GetFileName(srcPath), box.X, box.Y, box.Width, box.Height,
                              scale, outPath, box.Width * scale, box.Height * scale);
        }
    }

    //== sheet =================================================================

    /// Side by side with labels, aligned at the top on a mid grey.
    ///
    /// Two panels in one image is the cheap answer to "is this the same as
    /// that", and to showing candidate layouts rather than describing them.
    /// The ground is deliberately neither the light plate nor the dark one, so
    /// it cannot be mistaken for part of either render.
    static void Sheet(string[] a)
    {
        var outPath = a[0];
        var rest = a.Skip(1).ToArray();
        if (rest.Length == 0 || rest.Length % 2 != 0)
            throw new Exception("sheet takes an output then <png> <label> pairs");

        GuardOutput(outPath);

        const int gap = 16, pad = 16, band = 28;
        var images = new List<Bitmap>();
        var labels = new List<string>();

        try
        {
            for (int i = 0; i < rest.Length; i += 2)
            {
                if (!File.Exists(rest[i])) throw new Exception("no such file: " + rest[i]);
                images.Add(new Bitmap(rest[i]));
                labels.Add(rest[i + 1]);
            }

            var width = pad * 2 + images.Sum(im => im.Width) + gap * (images.Count - 1);
            var height = pad * 2 + band + images.Max(im => im.Height);

            using (var dst = new Bitmap(width, height, PixelFormat.Format32bppArgb))
            using (var g = Graphics.FromImage(dst))
            using (var font = new Font("Consolas", 11f, FontStyle.Regular, GraphicsUnit.Point))
            using (var ink = new SolidBrush(Color.FromArgb(0xf0, 0xf0, 0xf0)))
            {
                g.Clear(Color.FromArgb(0x50, 0x50, 0x54));
                g.InterpolationMode = InterpolationMode.NearestNeighbor;
                g.PixelOffsetMode = PixelOffsetMode.Half;

                var x = pad;
                for (int i = 0; i < images.Count; i++)
                {
                    g.DrawString(labels[i], font, ink, x, pad);
                    g.DrawImage(images[i], new Rectangle(x, pad + band, images[i].Width, images[i].Height));
                    x += images[i].Width + gap;
                }

                dst.Save(outPath, ImageFormat.Png);
            }

            Console.WriteLine("{0}  {1} panels, {2}x{3}", outPath, images.Count, width, height);
        }
        finally
        {
            foreach (var im in images) im.Dispose();
        }
    }

    //== hash ==================================================================

    /// SHA-256 over the pixels, not over the file.
    ///
    /// A refactor that claims to move nothing is a claim you can settle rather
    /// than argue: render every panel, refactor, render again, compare. Pixels
    /// rather than file bytes because a PNG encoder is free to vary everything
    /// around them and a difference there would mean nothing.
    static void Hash(string[] a)
    {
        using (var sha = SHA256.Create())
            foreach (var path in a)
            {
                var px = Read(path);
                var hex = BitConverter.ToString(sha.ComputeHash(px.Data)).Replace("-", "").ToLowerInvariant();
                Console.WriteLine("{0}  {1,5}x{2,-5}  {3}", hex.Substring(0, 16), px.Width, px.Height, path);
            }
    }

    //== gaps ==================================================================

    /// Runs of rows that are nothing but plate: the empty bands down a panel.
    ///
    /// This exists because `ui_layout_tests --dump` cannot answer the
    /// question. The dump prints control *boxes*, and a panel's boxes are
    /// very nearly contiguous -- on BMO Util the rule under VOLUME and the
    /// PAN box are one pixel apart -- while the *ink* inside them is not,
    /// because a knob box carries padding above its face and below its
    /// caption. Read from the dump alone, Util has no empty band worth the
    /// name. Rendered, its worst is 46 px against BMO EQ's 21.5, which is
    /// the comparison the UI pass actually trades in.
    ///
    /// So: boxes are what a layout test asserts, ink is what a reader sees,
    /// and only the render knows the difference.
    ///
    /// Two things to know before reading the output. The plate is detected
    /// as the commonest colour below the header rather than passed in, and
    /// is printed on the first line so a wrong guess is visible instead of
    /// silent. And a hairline rule *does* break a band, because it is a row
    /// with ink in it: Util's empty foot prints as 46 and 31 either side of
    /// the rule centred on 566, not as one run of 78.
    static void Gaps(string[] a)
    {
        var px = Read(a[0]);
        int min    = a.Length > 1 ? int.Parse(a[1], CultureInfo.InvariantCulture) : 10;
        int header = a.Length > 2 ? int.Parse(a[2], CultureInfo.InvariantCulture) : 52;
        int scale  = a.Length > 3 ? int.Parse(a[3], CultureInfo.InvariantCulture) : 2;

        if (scale < 1) throw new Exception("scale is 1 or more, got " + a[3]);

        // header and scale are properties of the renderer, not of the PNG, so
        // both are printed rather than assumed silently. The defaults are
        // tools/snapshot's: it renders at 2x (createComponentSnapshot 2.0f)
        // and a product editor puts ProductHeader::kHeight 28 plus a 24 px
        // preset strip above the panel. Pass them if either ever moves.
        int top = header * scale;

        if (top >= px.Height)
            throw new Exception("header " + header + " at scale " + scale
                                + " is the whole of a " + px.Height + " px render");

        var counts = new Dictionary<int, int>();

        for (int y = top; y < px.Height; y++)
            for (int x = 0; x < px.Width; x++)
            {
                int c = px.Argb(x, y);
                int n; counts.TryGetValue(c, out n); counts[c] = n + 1;
            }

        int plate = counts.OrderByDescending(kv => kv.Value).First().Key;
        int pr = (plate >> 16) & 0xff, pg = (plate >> 8) & 0xff, pb = plate & 0xff;

        Console.WriteLine("{0}  ({1}x{2})  plate #{3:x2}{4:x2}{5:x2}  scale {6}, header {7}",
                          Path.GetFileName(a[0]), px.Width, px.Height, pr, pg, pb, scale, header);
        Console.WriteLine("  panel is render rows {0}..{1}; bands under {2} px not listed",
                          top, px.Height - 1, min);
        Console.WriteLine("  {0,10}  {1,5}   panel-local design px", "design-y", "px");

        int widest = 0;
        string widestAt = "none";
        int y2 = top;

        while (y2 < px.Height)
        {
            if (! BareRow(px, y2, plate)) { y2++; continue; }

            int start = y2;
            while (y2 < px.Height && BareRow(px, y2, plate)) y2++;

            int run = (y2 - start) / scale;

            if (run >= min)
            {
                Console.WriteLine("  {0,4}..{1,-4}  {2,5}",
                                  start / scale - header, (y2 - 1) / scale - header, run);

                if (run > widest)
                {
                    widest = run;
                    widestAt = (start / scale - header) + ".." + ((y2 - 1) / scale - header);
                }
            }
        }

        Console.WriteLine("  largest {0} px at {1}", widest, widestAt);
    }

    static bool BareRow(Pixels px, int y, int plate)
    {
        for (int x = 0; x < px.Width; x++)
            if (px.Argb(x, y) != plate) return false;

        return true;
    }

    //== ratio =================================================================

    static double Channel(int c)
    {
        var s = c / 255.0;
        return s <= 0.03928 ? s / 12.92 : Math.Pow((s + 0.055) / 1.055, 2.4);
    }

    static double Luminance(int r, int g, int b)
    {
        return 0.2126 * Channel(r) + 0.7152 * Channel(g) + 0.0722 * Channel(b);
    }

    /// CIE L*, which is how the dark set's structural greys are spaced. Below
    /// about L* 20 a contrast ratio stops discriminating -- from the dark
    /// plate the most contrast available by going darker, all the way to
    /// black, is 1.29:1 -- so lightness is the measure that still works there.
    static double Lstar(double y)
    {
        const double e = 216.0 / 24389.0, k = 24389.0 / 27.0;
        return y > e ? 116.0 * Math.Pow(y, 1.0 / 3.0) - 16.0 : k * y;
    }

    /// A colour argument is either a hex -- #8d8d98 or 8d8d98 -- or a pixel
    /// picked out of a render, as file.png:x,y.
    static void ParseColour(string s, out int r, out int g, out int b, out string shown)
    {
        var at = s.LastIndexOf(':');
        if (at > 1 && s.IndexOf(',') > at)
        {
            var path = s.Substring(0, at);
            var xy = s.Substring(at + 1).Split(',');
            var px = Read(path);
            int x = int.Parse(xy[0], CultureInfo.InvariantCulture);
            int y = int.Parse(xy[1], CultureInfo.InvariantCulture);
            if (x < 0 || y < 0 || x >= px.Width || y >= px.Height)
                throw new Exception(s + " is outside the image (" + px.Width + "x" + px.Height + ")");
            var c = px.Argb(x, y);
            r = (c >> 16) & 0xff; g = (c >> 8) & 0xff; b = c & 0xff;
            shown = string.Format("#{0:x2}{1:x2}{2:x2}  {3}", r, g, b, s);
            return;
        }

        var hex = s.TrimStart('#');
        if (hex.Length != 6) throw new Exception("expected #rrggbb or file.png:x,y, got " + s);
        r = int.Parse(hex.Substring(0, 2), NumberStyles.HexNumber);
        g = int.Parse(hex.Substring(2, 2), NumberStyles.HexNumber);
        b = int.Parse(hex.Substring(4, 2), NumberStyles.HexNumber);
        shown = "#" + hex.ToLowerInvariant();
    }

    /// Every pair, both ways round, because a ratio without a named ground is
    /// not a measurement. "The marker is 4.67:1" was true and useless; against
    /// ringFace rather than the plate it was the whole story.
    static void Ratio(string[] a)
    {
        if (a.Length < 2) throw new Exception("ratio takes at least two colours");

        var rs = new int[a.Length]; var gs = new int[a.Length]; var bs = new int[a.Length];
        var names = new string[a.Length]; var ys = new double[a.Length];

        for (int i = 0; i < a.Length; i++)
        {
            ParseColour(a[i], out rs[i], out gs[i], out bs[i], out names[i]);
            ys[i] = Luminance(rs[i], gs[i], bs[i]);
            Console.WriteLine("  {0,-28}  L* {1,5:0.0}", names[i], Lstar(ys[i]));
        }

        Console.WriteLine();
        Console.WriteLine("  {0,-13} {1,-13} {2,8}  {3,6}", "ink", "on ground", "ratio", "dL*");

        for (int i = 0; i < a.Length; i++)
            for (int j = 0; j < a.Length; j++)
            {
                if (i == j) continue;
                var hi = Math.Max(ys[i], ys[j]);
                var lo = Math.Min(ys[i], ys[j]);
                var ratio = (hi + 0.05) / (lo + 0.05);
                Console.WriteLine("  {0,-13} {1,-13} {2,7:0.00}:1  {3,6:0.0}",
                                  Short(names[i]), Short(names[j]), ratio,
                                  Math.Abs(Lstar(ys[i]) - Lstar(ys[j])));
            }
    }

    static string Short(string shown)
    {
        var sp = shown.IndexOf(' ');
        return sp < 0 ? shown : shown.Substring(0, sp);
    }

    //== main ==================================================================

    static int Main(string[] argv)
    {
        if (argv.Length == 0) { Usage(); return 2; }

        var mode = argv[0].ToLowerInvariant();
        var rest = argv.Skip(1).ToArray();

        try
        {
            switch (mode)
            {
                case "scan":  Need(rest, 3, "scan <png> row|col <n> [from to]"); Scan(rest); break;
                case "hist":  Need(rest, 5, "hist <png> <x> <y> <w> <h> [top]"); Hist(rest); break;
                case "crop":  Need(rest, 7, "crop <png> <x> <y> <w> <h> <scale> <out.png>"); Crop(rest); break;
                case "sheet": Need(rest, 3, "sheet <out.png> <png> <label> ..."); Sheet(rest); break;
                case "hash":  Need(rest, 1, "hash <png> [<png> ...]"); Hash(rest); break;
                case "ratio": Need(rest, 2, "ratio <a> <b> [...]"); Ratio(rest); break;
                case "gaps":  Need(rest, 1, "gaps <png> [min] [header] [scale]"); Gaps(rest); break;
                default:
                    // Refuse what is not understood rather than doing something
                    // plausible with it, the way tools/snapshot's realValueFor
                    // now does: a mistyped mode that quietly did nothing would
                    // read as an answer.
                    Console.Error.WriteLine("unknown mode: " + argv[0]);
                    Usage();
                    return 2;
            }
        }
        catch (Exception e)
        {
            Console.Error.WriteLine(mode + ": " + e.Message);
            return 1;
        }

        return 0;
    }

    static void Need(string[] a, int n, string form)
    {
        if (a.Length < n) throw new Exception("expected " + form);
    }

    static void Usage()
    {
        Console.Error.WriteLine(
            "Inspect scan  <png> row <y> [x0 x1]     run-length along one row\n" +
            "Inspect scan  <png> col <x> [y0 y1]     run-length down one column\n" +
            "Inspect hist  <png> <x> <y> <w> <h> [n] exact-colour histogram over a box\n" +
            "Inspect crop  <png> <x> <y> <w> <h> <scale> <out.png>\n" +
            "Inspect sheet <out.png> <png> <label> [<png> <label> ...]\n" +
            "Inspect hash  <png> [<png> ...]         SHA-256 of the pixels\n" +
            "Inspect ratio <a> <b> [...]             contrast and L*; a is #rrggbb or file.png:x,y\n" +
            "Inspect gaps  <png> [min] [header] [scale]  bands of bare plate down a panel");
    }
}
