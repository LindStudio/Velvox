#include "EmberSeal.h"
#include <array>
#include <cmath>

namespace ember::seal
{
    namespace
    {
        constexpr float tau = juce::MathConstants<float>::twoPi;

        // Elder Futhark, each rune as polylines in a 0.6 x 1 box
        using Line = std::vector<juce::Point<float>>;
        const std::array<std::vector<Line>, 24>& runeLines()
        {
            static const std::array<std::vector<Line>, 24> r {{
                { { {0,0},{0,1} }, { {0,.15f},{.5f,0} }, { {0,.4f},{.5f,.22f} } },                       // fehu
                { { {0,1},{0,0},{.5f,.3f},{.5f,1} } },                                                       // uruz
                { { {0,0},{0,1} }, { {0,.25f},{.45f,.5f},{0,.75f} } },                                       // thurisaz
                { { {0,0},{0,1} }, { {0,.1f},{.45f,.3f} }, { {0,.35f},{.45f,.55f} } },                       // ansuz
                { { {0,1},{0,0},{.45f,.22f},{0,.45f},{.5f,1} } },                                            // raidho
                { { {.45f,.1f},{0,.35f},{.45f,.6f} } },                                                      // kaunan
                { { {0,0},{.6f,1} }, { {.6f,0},{0,1} } },                                                    // gebo
                { { {0,1},{0,0},{.42f,.2f},{0,.4f} } },                                                      // wunjo
                { { {0,0},{0,1} }, { {.5f,0},{.5f,1} }, { {0,.35f},{.5f,.6f} } },                            // hagalaz
                { { {.3f,0},{.3f,1} }, { {.05f,.35f},{.55f,.6f} } },                                         // naudiz
                { { {.3f,0},{.3f,1} } },                                                                     // isaz
                { { {.28f,.08f},{0,.33f},{.28f,.58f} }, { {.32f,.42f},{.6f,.67f},{.32f,.92f} } },            // jera
                { { {.3f,0},{.3f,1} }, { {.3f,0},{.56f,.2f} }, { {.3f,1},{.04f,.8f} } },                     // eihwaz
                { { {.4f,.32f},{.4f,.25f},{0,0},{0,1},{.4f,.75f},{.4f,.68f} } },                             // perthro
                { { {.3f,1},{.3f,0} }, { {0,.1f},{.3f,.4f},{.6f,.1f} } },                                    // algiz
                { { {.5f,0},{.05f,.35f},{.5f,.65f},{.05f,1} } },                                             // sowilo
                { { {.3f,1},{.3f,0} }, { {0,.3f},{.3f,0},{.6f,.3f} } },                                      // tiwaz
                { { {0,0},{0,1},{.45f,.75f},{0,.5f},{.45f,.25f},{0,0} } },                                   // berkanan
                { { {0,1},{0,0},{.3f,.35f},{.6f,0},{.6f,1} } },                                              // ehwaz
                { { {0,1},{0,0},{.6f,.45f} }, { {.6f,1},{.6f,0},{0,.45f} } },                                // mannaz
                { { {0,1},{0,0},{.45f,.25f} } },                                                             // laguz
                { { {.3f,.25f},{.6f,.5f},{.3f,.75f},{0,.5f},{.3f,.25f} } },                                  // ingwaz
                { { {0,0},{0,1},{.6f,0},{.6f,1},{0,0} } },                                                   // dagaz
                { { {.6f,1},{.05f,.3f},{.3f,0},{.55f,.3f},{0,1} } },                                         // othala
            }};
            return r;
        }

        juce::Point<float> onEllipse (juce::Point<float> c, float rx, float ry, float a, float k = 1.0f)
        {
            return { c.x + std::cos (a) * rx * k, c.y + std::sin (a) * ry * k };
        }

        juce::Path ellipsePath (juce::Point<float> c, float rx, float ry)
        {
            juce::Path p;
            p.addEllipse (c.x - rx, c.y - ry, 2.0f * rx, 2.0f * ry);
            return p;
        }

        // a stroke with a soft halo: wide and faint first, then thin and bright
        void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour col, float width, float alpha)
        {
            g.setColour (col.withAlpha (alpha * 0.16f));
            g.strokePath (p, juce::PathStrokeType (width * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (col.withAlpha (alpha * 0.3f));
            g.strokePath (p, juce::PathStrokeType (width * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (col.withAlpha (alpha));
            g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        float hash (int i) { const float s = std::sin ((float) i * 12.9898f) * 43758.5453f; return s - std::floor (s); }
    }

    const juce::Path& rune (int index)
    {
        static const std::array<juce::Path, 24> paths = []
        {
            std::array<juce::Path, 24> out;
            for (size_t i = 0; i < 24; ++i)
                for (const auto& line : runeLines()[i])
                {
                    out[i].startNewSubPath (line[0]);
                    for (size_t k = 1; k < line.size(); ++k)
                        out[i].lineTo (line[k]);
                }
            return out;
        }();
        return paths[(size_t) (((index % 24) + 24) % 24)];
    }

    void drawRune (juce::Graphics& g, int index, juce::Point<float> c, float height, juce::Colour col, float alpha)
    {
        auto p = rune (index);
        p.applyTransform (juce::AffineTransform::translation (-0.3f, -0.5f).scaled (height).translated (c));
        glowStroke (g, p, col, juce::jmax (1.0f, height * 0.085f), alpha);
    }

    void RuneAtlas::setScale (float s)
    {
        if (std::abs (s - scale) > 1.0e-3f)
        {
            scale = s;
            sprites.clear();
        }
    }

    void RuneAtlas::draw (juce::Graphics& g, int index, juce::Point<float> c, float height, float alpha)
    {
        const int h = juce::jlimit (4, 64, juce::roundToInt (height));
        const int idx = ((index % 24) + 24) % 24, key = idx * 100 + h;
        auto it = sprites.find (key);
        if (it == sprites.end())
        {
            const float pad = (float) h * 0.25f + 2.0f, w = (float) h * 0.6f + 2.0f * pad, hh = (float) h + 2.0f * pad;
            juce::Image img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (w * scale)), juce::jmax (1, juce::roundToInt (hh * scale)), true, juce::SoftwareImageType());
            {
                juce::Graphics sg (img);
                sg.addTransform (juce::AffineTransform::scale (scale));
                drawRune (sg, idx, { w * 0.5f, hh * 0.5f }, (float) h, colour, 1.0f);
            }
            // drawn in memory (the same pixels as before), handed to the screen's renderer once
            it = sprites.emplace (key, screen ? juce::NativeImageType().convert (img) : img).first;
        }
        const auto& img = it->second;
        const float w = (float) img.getWidth() / scale, hh = (float) img.getHeight() / scale;
        g.setOpacity (juce::jlimit (0.0f, 1.0f, alpha));
        g.drawImage (img, juce::Rectangle<float> (c.x - w * 0.5f, c.y - hh * 0.5f, w, hh));
        g.setOpacity (1.0f);
    }

    juce::Rectangle<float> RuneAtlas::bounds (juce::Point<float> c, float height) const
    {
        // the sprite's size in draw() (rounded up generously, so a rune is never skipped by mistake)
        const float h = (float) juce::jlimit (4, 64, juce::roundToInt (height)), pad = h * 0.25f + 2.0f;
        const float w = h * 0.6f + 2.0f * pad + 2.0f, hh = h + 2.0f * pad + 2.0f;
        return { c.x - w * 0.5f, c.y - hh * 0.5f, w, hh };
    }

    void drawFloor (juce::Graphics& g, juce::Point<float> c, float rx, float ry, const Scene& s)
    {
        const float lit = s.lit(), rot = s.rotation(), heat = s.on ? juce::jmin (1.0f, s.heat) : 0.0f;
        // light pool under the circle
        g.setGradientFill (juce::ColourGradient (pink.withAlpha (0.42f * lit), c.x, c.y, pink.withAlpha (0.0f), c.x + rx * 1.08f, c.y, true));
        g.fillEllipse (c.x - rx * 1.08f, c.y - ry * 1.15f, rx * 2.16f, ry * 2.3f);

        for (auto [k, w, a] : { std::tuple { 1.0f, 2.4f, .95f }, { .953f, 1.2f, .8f }, { .873f, 2.0f, .9f }, { .653f, 1.6f, .85f }, { .4f, 1.2f, .7f } })
            glowStroke (g, ellipsePath (c, rx * k, ry * k), pink, w, a * lit);

        // the pentagram, turning slowly; a spark runs along its lines
        std::array<juce::Point<float>, 5> pts;
        for (int i = 0; i < 5; ++i)
            pts[(size_t) i] = onEllipse (c, rx, ry, -juce::MathConstants<float>::halfPi + (float) i * tau / 5.0f + rot * 0.5f, .873f);
        juce::Path star;
        star.startNewSubPath (pts[0]);
        for (int i = 1; i <= 5; ++i)
            star.lineTo (pts[(size_t) ((i * 2) % 5)]);
        glowStroke (g, star, pink, 2.0f, .95f * lit);
        for (auto p : pts)
        {
            glowStroke (g, ellipsePath (p, rx * .06f, ry * .06f), pink, 1.4f, .9f * lit);
            glowStroke (g, ellipsePath (p, rx * .033f, ry * .033f), pink, 1.0f, .8f * lit);
        }
        // light pillars from the star points while he works hard
        if (s.on && heat > 0.15f)
            for (auto p : pts)
            {
                g.setGradientFill (juce::ColourGradient (pink.withAlpha (0.0f), p.x, p.y - 260.0f, pink.withAlpha (juce::jmin (0.45f, heat * 0.4f)), p.x, p.y, false));
                g.fillRect (p.x - 5.0f, p.y - 260.0f, 10.0f, 260.0f);
            }
    }

    void drawFloorLive (juce::Graphics& g, juce::Point<float> c, float rx, float ry, const Scene& s)
    {
        const float lit = s.lit(), rot = s.rotation(), heat = s.on ? juce::jmin (1.0f, s.heat) : 0.0f;
        // energy running round the outer band (dashes that move)
        {
            juce::Path arc, dashed;
            const float off = (float) std::fmod (s.t * 0.25, (double) tau);
            arc.addCentredArc (c.x, c.y, rx * .975f, ry * .975f, 0.0f, off, off + tau, true);
            const float dashes[] { 14.0f, 10.0f };
            juce::PathStrokeType (2.0f).createDashedStroke (dashed, arc, dashes, 2);
            g.setColour (pinkLight.withAlpha ((0.45f + 0.4f * heat) * lit));
            g.fillPath (dashed);
        }
        // a spark running along the star's lines
        {
            std::array<juce::Point<float>, 5> pts;
            for (int i = 0; i < 5; ++i)
                pts[(size_t) i] = onEllipse (c, rx, ry, -juce::MathConstants<float>::halfPi + (float) i * tau / 5.0f + rot * 0.5f, .873f);
            const double run = s.t * 0.9;
            const int seg = (int) std::fmod (run, 5.0);
            const float u = (float) (run - std::floor (run));
            const auto a0 = pts[(size_t) ((seg * 2) % 5)], a1 = pts[(size_t) (((seg + 1) * 2) % 5)];
            const auto sp = a0 + (a1 - a0) * u;
            g.setGradientFill (juce::ColourGradient (pinkLight.withAlpha (lit), sp.x, sp.y, pinkLight.withAlpha (0.0f), sp.x + 12.0f, sp.y, true));
            g.fillEllipse (sp.x - 12.0f, sp.y - 12.0f, 24.0f, 24.0f);
        }
        // the wave after each swallowed S runs outward
        if (const float w = s.wave(); w > 0.0f && s.on)
        {
            const float k = .4f + (1.0f - w) * .67f;
            g.setColour (pinkLight.withAlpha (0.3f * w));
            g.strokePath (ellipsePath (c, rx * k, ry * k), juce::PathStrokeType (3.0f + 6.0f * w));
            g.setColour (pinkLight.withAlpha (w));
            g.strokePath (ellipsePath (c, rx * k, ry * k), juce::PathStrokeType (1.0f + 2.0f * w));
        }
    }

    void drawFloatingRunes (juce::Graphics& g, RuneAtlas& atlas, juce::Point<float> c, float rx, float ry, const Scene& s, bool front)
    {
        struct Ring { int n; float r, h, size, dir; int off; };
        const Ring rings[] { { 24, .93f, 40.0f, 21.0f, 1.0f, 0 }, { 14, .7f, 22.0f, 14.0f, -1.7f, 9 } };
        const float lit = s.lit(), rot = s.rotation(), jump = s.on ? s.jump() : 0.0f;
        for (const auto& ring : rings)
            for (int i = 0; i < ring.n; ++i)
            {
                const float a = rot * ring.dir + (float) i * tau / (float) ring.n, depth = std::sin (a);
                if ((depth >= 0.0f) != front)
                    continue;
                const float near = (depth + 1.0f) * 0.5f, bob = s.calm ? 0.0f : std::sin ((float) s.t * 1.6f + (float) i * 1.3f) * 4.0f;
                const auto floor = onEllipse (c, rx, ry, a, ring.r);
                const juce::Point<float> at (floor.x, floor.y - ring.h * (1.0f + jump * 0.9f) - bob);
                const float size = ring.size * (0.7f + 0.5f * near), alpha = lit * (0.35f + 0.65f * near);
                g.setColour (pink.withAlpha (alpha * 0.2f));   // a thread of light down to the floor
                g.fillRect (at.x - 0.5f, at.y + size * 0.55f, 1.0f, juce::jmax (0.0f, floor.y - at.y - size * 0.55f));
                atlas.draw (g, i * 5 + ring.off, at, size, alpha);
            }
    }

    void drawBrazier (juce::Graphics& g, juce::Point<float> top, const Scene& s, int seed)
    {
        if (! s.on)
            return;
        const float t = (float) s.t, heat = juce::jmin (1.0f, s.heat);
        g.setGradientFill (juce::ColourGradient (pink.withAlpha (0.35f), top.x, top.y - 10.0f, pink.withAlpha (0.0f), top.x + 24.0f, top.y - 10.0f, true));
        g.fillEllipse (top.x - 24.0f, top.y - 70.0f, 48.0f, 90.0f);
        for (int k = 0; k < 6; ++k)
        {
            const float ph = hash (seed * 31 + k) * 6.0f, flick = 0.75f + 0.25f * std::sin (t * (7.0f + (float) k) + ph);
            const float h = (26.0f + 8.0f * (float) (k % 3) + 18.0f * heat) * flick, w = 9.0f + (float) (k % 2) * 4.0f;
            const float x = top.x + (hash (seed * 17 + k) - 0.5f) * 30.0f + std::sin (t * 3.0f + ph) * 2.5f;
            juce::Path f;
            f.startNewSubPath (x - w * 0.5f, top.y);
            f.quadraticTo (x - w * 0.6f, top.y - h * 0.5f, x + std::sin (t * 5.0f + ph) * 3.0f, top.y - h);
            f.quadraticTo (x + w * 0.6f, top.y - h * 0.5f, x + w * 0.5f, top.y);
            f.closeSubPath();
            g.setGradientFill (juce::ColourGradient (pinkLight.withAlpha (0.85f), x, top.y, pink.withAlpha (0.0f), x, top.y - h, false));
            g.fillPath (f);
        }
    }

    void drawRuneBorder (juce::Graphics& g, RuneAtlas& atlas, juce::Rectangle<float> w, const Scene& s)
    {
        const float lit = s.on ? 0.7f + 0.3f * juce::jmin (1.0f, s.heat) : 0.15f;
        const float per = 2.0f * (w.getWidth() + w.getHeight()) - 40.0f, step = 15.0f;
        const double t = s.calm ? 0.0 : s.t;   // calm: the border stands still
        const float off = (float) std::fmod (t * 22.0, (double) step);
        // only the runes in the part being repainted: the border is repainted in strips, and every other repaint of
        // the window (Ember 60 times a second, the meters) passes through this overlay too
        int i = 0;
        for (float d = off; d < per; d += step, ++i)
        {
            const float W = w.getWidth() - 20.0f, H = w.getHeight() - 20.0f;
            juce::Point<float> p;
            if (d < W)                p = { w.getX() + 10.0f + d, w.getY() + 6.0f };
            else if (d < W + H)       p = { w.getRight() - 6.0f, w.getY() + 10.0f + d - W };
            else if (d < 2 * W + H)   p = { w.getRight() - 10.0f - (d - W - H), w.getBottom() - 6.0f };
            else                      p = { w.getX() + 6.0f, w.getBottom() - 10.0f - (d - 2 * W - H) };
            if (g.clipRegionIntersects (atlas.bounds (p, 8.0f).getSmallestIntegerContainer()))
                atlas.draw (g, i * 5, p, 8.0f, lit * (0.35f + 0.3f * std::sin ((float) t * 2.0f + (float) i * 0.7f)));
        }
    }

    void drawMiniSeal (juce::Graphics& g, RuneAtlas& atlas, juce::Point<float> c, float r, const Scene& s)
    {
        const float lit = s.on ? 0.55f + 0.35f * juce::jmin (1.0f, s.heat) : 0.12f, rot = s.rotation() * 2.0f;
        g.setColour (pink.withAlpha (0.5f * lit));
        g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.2f);
        g.drawEllipse (c.x - r * .87f, c.y - r * .87f, 2 * r * .87f, 2 * r * .87f, 1.0f);
        juce::Path star;
        for (int i = 0; i <= 5; ++i)
        {
            const float a = -juce::MathConstants<float>::halfPi + (float) ((i * 2) % 5) * tau / 5.0f + rot;
            const juce::Point<float> p (c.x + std::cos (a) * r * .87f, c.y + std::sin (a) * r * .87f);
            i == 0 ? star.startNewSubPath (p) : star.lineTo (p);
        }
        g.setColour (pink.withAlpha (0.45f * lit));
        g.strokePath (star, juce::PathStrokeType (1.0f));
        for (int i = 0; i < 12; ++i)
        {
            const float a = rot * 0.5f + (float) i * tau / 12.0f;
            atlas.draw (g, i * 7, { c.x + std::cos (a) * r * 0.935f, c.y + std::sin (a) * r * 0.935f }, 7.0f, 0.6f * lit);
        }
    }

    void drawChamber (juce::Graphics& g, juce::Rectangle<float> w, juce::Rectangle<float> stage)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff060106), 0, w.getY(), juce::Colour (0xff1c0613), 0, w.getBottom() * 0.62f, false));
        g.fillRect (w);
        // bricks
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        for (int row = 0; row * 22 < (int) w.getHeight(); ++row)
            for (int col = -1; col * 46 < (int) w.getWidth(); ++col)
                g.drawRect (juce::Rectangle<float> ((float) col * 46.0f + (float) (row % 2) * 23.0f, (float) row * 22.0f, 46.0f, 22.0f), 1.0f);
        // floor of the stage in perspective
        const float floorY = stage.getY() + stage.getHeight() * 0.74f, cx = stage.getCentreX();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff16070f), 0, floorY, juce::Colour (0xff040205), 0, w.getBottom(), false));
        g.fillRect (w.withTop (floorY));
        g.setColour (juce::Colour (0xffff78aa).withAlpha (0.05f));
        for (int i = -14; i < 22; ++i)
            g.drawLine (cx + (float) i * 30.0f, floorY, cx + (float) i * 95.0f, w.getBottom(), 1.0f);
        for (float y = floorY + 8.0f, st = 9.0f; y < w.getBottom(); y += st, st *= 1.28f)
            g.drawLine (w.getX(), y, w.getRight(), y, 1.0f);
        // pillars at the stage's sides, chains between
        for (float px : { stage.getX() + 24.0f, stage.getRight() - 28.0f })
        {
            juce::ColourGradient gg (juce::Colour (0xff07030a), px - 28.0f, 0, juce::Colour (0xff07030a), px + 28.0f, 0, false);
            gg.addColour (0.5, juce::Colour (0xff2a0e1c));
            g.setGradientFill (gg);
            g.fillRect (px - 24.0f, stage.getY() + 100.0f, 48.0f, stage.getHeight() * 0.74f - 100.0f);
            g.setColour (juce::Colour (0xff140810));
            g.fillRect (px - 31.0f, stage.getY() + 90.0f, 62.0f, 14.0f);
            g.setColour (juce::Colour (0xff1c1016));
            juce::Path bowl;
            bowl.addQuadrilateral (px - 28.0f, stage.getY() + 70.0f, px + 28.0f, stage.getY() + 70.0f, px + 16.0f, stage.getY() + 90.0f, px - 16.0f, stage.getY() + 90.0f);
            g.fillPath (bowl);
        }
        g.setColour (juce::Colour (0xff1e1a1e));
        for (float chx : { stage.getX() + stage.getWidth() * 0.24f, stage.getX() + stage.getWidth() * 0.78f })
            for (float y = stage.getY() - 50.0f, k = 0; y < stage.getY() + 130.0f; y += 18.0f, ++k)
                g.drawEllipse (chx - ((int) k % 2 ? 2.5f : 6.0f), y - 9.0f, (int) k % 2 ? 5.0f : 12.0f, 18.0f, 2.5f);
        // vignette
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, stage.getCentreX(), stage.getCentreY() + 40.0f,
                                                 juce::Colours::black.withAlpha (0.78f), stage.getCentreX() + w.getWidth() * 0.62f, stage.getCentreY(), true));
        g.fillRect (w);
    }
}
