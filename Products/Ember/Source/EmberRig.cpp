#include "EmberRig.h"
#include "EmberRigData.h"
#include <cmath>

// The drawing follows the browser prototype line by line (tools/mascot/ember_live_prototype_template.html, v17), so
// the plugin shows exactly what Jakob approved there. Comments name the prototype's function where it helps.
namespace ember
{
    namespace
    {
        constexpr float neck0 = 492.0f, neck1 = 582.0f;          // the neck band: head turns fully above, not below

        juce::Colour rgb (const juce::var& v)
        {
            auto c = [&] (int i) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt ((double) v[i] * 255.0)); };
            return juce::Colour (c (0), c (1), c (2));
        }

        std::vector<float> floats (const juce::var& v)
        {
            std::vector<float> out;
            if (auto* a = v.getArray())
                for (auto& x : *a)
                    out.push_back ((float) (double) x);
            return out;
        }

        std::vector<juce::Point<float>> points (const juce::var& v)
        {
            std::vector<juce::Point<float>> out;
            if (auto* a = v.getArray())
                for (auto& p : *a)
                    out.push_back ({ (float) (double) p[0], (float) (double) p[1] });
            return out;
        }

        juce::Point<float> point (const juce::var& v) { return { (float) (double) v[0], (float) (double) v[1] }; }

        const char* resource (const juce::String& originalName, int& size)
        {
            for (int i = 0; i < EmberRigData::namedResourceListSize; ++i)
                if (originalName == EmberRigData::originalFilenames[i])
                    return EmberRigData::getNamedResource (EmberRigData::namedResourceList[i], size);
            size = 0;
            return nullptr;
        }

        juce::Point<float> rotateAround (juce::Point<float> p, juce::Point<float> c, float radians)
        {
            const float co = std::cos (radians), si = std::sin (radians), dx = p.x - c.x, dy = p.y - c.y;
            return { c.x + co * dx - si * dy, c.y + si * dx + co * dy };
        }

        float smooth01 (float u) { u = juce::jlimit (0.0f, 1.0f, u); return u * u * (3.0f - 2.0f * u); }

        // one textured triangle: clip to the target (grown 0.9 px so no hairline shows between triangles), map the
        // picture with the affine that takes the source corners to the target corners (prototype: tri)
        float triangleGrow = 0.9f;   // rig units; set per render so it is ~1.2 screen px

        void triangle (juce::Graphics& g, const juce::Image& img, juce::Point<float> imgOrigin, float k,
                       juce::Point<float> s0, juce::Point<float> s1, juce::Point<float> s2,
                       juce::Point<float> d0, juce::Point<float> d1, juce::Point<float> d2)
        {
            const auto m = (d0 + d1 + d2) / 3.0f;
            auto grow = [&] (juce::Point<float> p) { auto d = p - m; const float l = std::max (1.0e-3f, d.getDistanceFromOrigin()); return p + d * (triangleGrow / l); };
            juce::Path t;
            t.addTriangle (grow (d0), grow (d1), grow (d2));
            const auto a = (s0 - imgOrigin) * k, b = (s1 - imgOrigin) * k, c = (s2 - imgOrigin) * k;   // into the scaled picture
            const auto tf = juce::AffineTransform::fromTargetPoints (a.x, a.y, d0.x, d0.y, b.x, b.y, d1.x, d1.y, c.x, c.y, d2.x, d2.y);
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (t);
            g.drawImageTransformed (img, tf);
        }
    }

    // ------------------------------------------------------------------ rig data
    const MascotRig& MascotRig::get()
    {
        static const MascotRig rig;
        return rig;
    }

    MascotRig::MascotRig()
    {
        int size = 0;
        auto* json = resource ("rig.json", size);
        if (json == nullptr)
            return;
        const auto root = juce::JSON::parse (juce::String::fromUTF8 (json, size));
        if (auto* obj = root["parts"].getDynamicObject())
            for (auto& p : obj->getProperties())
            {
                int n = 0;
                auto* png = resource ("part_" + p.name.toString() + ".png", n);
                if (png == nullptr)
                    return;
                Part part;
                part.image = juce::ImageFileFormat::loadFrom (png, (size_t) n).convertedToFormat (juce::Image::ARGB);
                part.box = { (float) p.value["x"], (float) p.value["y"], (float) p.value["w"], (float) p.value["h"] };
                parts[p.name.toString()] = part;
            }
        if (auto* obj = root["meshes"].getDynamicObject())
            for (auto& p : obj->getProperties())
            {
                Mesh m;
                m.x = (float) p.value["x"]; m.y = (float) p.value["y"]; m.step = (float) p.value["step"];
                m.cols = (int) p.value["cols"]; m.rows = (int) p.value["rows"];
                m.weight = floats (p.value["w"]);
                for (auto q : floats (p.value["q"]))
                    m.quad.push_back ((uint8_t) (q > 0.5f));
                m.pivot = point (p.value["pivot"]);
                meshes[p.name.toString()] = m;
            }
        const auto j = root["joints"];
        neck = point (j["neck"]); tailJoint = point (j["tail"]); earL = point (j["ear_l"]); earR = point (j["ear_r"]);
        shoulderL = point (j["shoulder_l"]); shoulderR = point (j["shoulder_r"]); feet = point (j["feet"]);
        smileX0 = (float) root["smile"]["x0"]; smileStep = (float) root["smile"]["step"]; smile = floats (root["smile"]["pts"]);
        const auto e = root["eyes"];
        const auto c = e["colors"];
        line = rgb (c["line"]); pupil = rgb (c["pupil"]); pupilRim = rgb (c["pupil_rim"]);
        yellowHi = rgb (c["yellow_hi"]); yellow = rgb (c["yellow"]); yellowLo = rgb (c["yellow_lo"]);
        shade = rgb (c["shade"]); glint = rgb (c["glint"]);
        if (auto* a = e["eyes"].getArray())
            for (auto& v : *a)
            {
                Eye eye;
                eye.x = floats (v["x"]); eye.top = floats (v["top"]); eye.bot = floats (v["bot"]);
                eye.up = floats (v["up"]); eye.low = floats (v["low"]);
                auto pu = floats (v["pupil"]); auto gl = floats (v["glint"]);
                for (int k = 0; k < 5 && k < (int) pu.size(); ++k) eye.pupil[k] = pu[(size_t) k];
                for (int k = 0; k < 4 && k < (int) gl.size(); ++k) eye.glint[k] = gl[(size_t) k];
                eye.browUp = points (v["brow"]["up"]); eye.browLow = points (v["brow"]["low"]);
                eye.tail = points (v["tail"]);
                eye.cx = (float) v["cx"];
                eyes.push_back (std::move (eye));
            }
        valid = parts.count ("head") && parts.count ("body") && eyes.size() == 2;
    }

    const MascotRig::Part& MascotRig::part (const juce::String& name) const
    {
        auto it = parts.find (name);
        return it != parts.end() ? it->second : empty;
    }

    const MascotRig::Mesh* MascotRig::mesh (const juce::String& name) const
    {
        auto it = meshes.find (name);
        return it != meshes.end() ? &it->second : nullptr;
    }

    // ------------------------------------------------------------------ renderer
    void RigRenderer::render (juce::Graphics& g, juce::Rectangle<float> area, const RigPose& pose, float displayScale)
    {
        const auto& rig = MascotRig::get();
        if (! rig.isValid() || area.isEmpty())
            return;
        // fit (prototype: fit): the figure is 86 % of the area's height (700 px of the picture), feet at the bottom
        const float s = area.getHeight() * 0.86f / 700.0f;
        const float ox = area.getCentreX() - 512.0f * s, oy = area.getY() + area.getHeight() * 0.97f - (rig.feet.y + 8.0f) * s;
        rigToArea = juce::AffineTransform::scale (s).translated (ox, oy);

        const int w = std::max (1, juce::roundToInt (area.getWidth() * displayScale));
        const int h = std::max (1, juce::roundToInt (area.getHeight() * displayScale));
        if (figure.getWidth() != w || figure.getHeight() != h)
        {
            // in memory, not on the GPU: the outline reads the figure's pixels every frame
            figure = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
            ring = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        }
        else
            figure.clear (figure.getBounds());
        {
            juce::Graphics fg (figure);
            fg.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            if (std::abs (scaledFor - s * displayScale) > 1.0e-4f)
            {
                scaledParts.clear();
                scaledFor = s * displayScale;
            }
            fg.addTransform (juce::AffineTransform::scale (s).translated (ox - area.getX(), oy - area.getY()).scaled (displayScale));
            triangleGrow = std::max (0.9f, 1.2f / (s * displayScale));
            drawFigure (fg, pose);
        }
        outline (figure, outlineWidth * s * displayScale, juce::Colour (outlineColour));
        // the outline (into 'ring'), then the figure over it
        juce::Graphics::ScopedSaveState keep (g);
        g.setOpacity (1.0f);      // images take the opacity of the last colour (the floor shadow's 28 % made him see-through)
        g.drawImage (ring, area, juce::RectanglePlacement::stretchToFit);
        g.drawImage (figure, area, juce::RectanglePlacement::stretchToFit);
    }

    juce::AffineTransform RigRenderer::bodyTransform (const RigPose& pose)
    {
        const auto F = MascotRig::get().feet;
        const float sq = juce::jlimit (-0.05f, 0.06f, pose.squash);
        return juce::AffineTransform::translation (0.0f, pose.bob)
                   .translated (-F.x, -F.y)
                   .scaled (1.0f + sq * 0.6f, 1.0f - sq)
                   .rotated (juce::degreesToRadians (pose.lean))
                   .translated (F.x, F.y);
    }

    juce::AffineTransform RigRenderer::headTransform (const RigPose& pose)
    {
        const auto& rig = MascotRig::get();
        return juce::AffineTransform::rotation (juce::degreesToRadians (pose.head), rig.neck.x, rig.neck.y)
                   .translated (pose.headX, pose.headY)
                   .followedBy (bodyTransform (pose));
    }

    // The whole figure in the picture's space (prototype: draw)
    void RigRenderer::drawFigure (juce::Graphics& g, const RigPose& pose)
    {
        const auto& rig = MascotRig::get();
        juce::Graphics::ScopedSaveState body (g);
        g.addTransform (bodyTransform (pose));

        drawMesh (g, "tail", pose.tail * 1.3f);

        const auto headT = juce::AffineTransform::rotation (juce::degreesToRadians (pose.head), rig.neck.x, rig.neck.y)
                               .translated (pose.headX, pose.headY);
        const bool headMoves = std::abs (pose.head) > 0.05f || std::abs (pose.headX) > 0.05f || std::abs (pose.headY) > 0.05f;
        {
            juce::Graphics::ScopedSaveState ears (g);
            g.addTransform (headT);
            drawMesh (g, "ear_l", juce::jlimit (-30.0f, 30.0f, pose.earL));
            drawMesh (g, "ear_r", juce::jlimit (-30.0f, 30.0f, pose.earR));
        }
        if (! headMoves)
            drawPart (g, "body");
        else
        {
            {
                juce::Graphics::ScopedSaveState below (g);
                g.reduceClipRegion (juce::Rectangle<float> (-2000.0f, neck1 - 2.0f, 5000.0f, 4000.0f).toNearestIntEdges());
                drawPart (g, "body");
            }
            drawNeckBand (g, "body", rig.part ("body").box.getY(), neck1, pose);
        }

        // arms: negative = left arm out, positive = right arm out; out freely, in only a little. The shoulders bend
        // with the neck.
        std::function<juce::Point<float> (juce::Point<float>, float)> neckPost;
        if (headMoves)
            neckPost = [&rig, pose] (juce::Point<float> p, float restY)
            {
                const float wN = 1.0f - smooth01 ((restY - neck0) / (neck1 - neck0));
                auto q = rotateAround (p, rig.neck, juce::degreesToRadians (pose.head) * wN);
                return q + juce::Point<float> (pose.headX * wN, pose.headY * wN);
            };
        drawMesh (g, "arm_l", -juce::jlimit (-48.0f, 4.0f, pose.armL), neckPost);
        drawMesh (g, "arm_r", -juce::jlimit (-4.0f, 48.0f, pose.armR), neckPost);

        if (headMoves)
        {
            {
                juce::Graphics::ScopedSaveState above (g);
                g.addTransform (headT);
                g.reduceClipRegion (juce::Rectangle<float> (-2000.0f, -2000.0f, 5000.0f, 2000.0f + neck0 + 1.0f).toNearestIntEdges());
                drawPart (g, "head");
            }
            const auto& hb = rig.part ("head").box;
            drawNeckBand (g, "head", neck0, hb.getBottom(), pose);
        }
        juce::Graphics::ScopedSaveState face (g);
        g.addTransform (headT);
        if (! headMoves)
            drawPart (g, "head");
        drawEyes (g, pose);
        drawMouth (g, pose);
    }

    const juce::Image& RigRenderer::scaled (const juce::String& name)
    {
        auto it = scaledParts.find (name);
        if (it != scaledParts.end())
            return it->second;
        const auto& p = MascotRig::get().part (name);
        const int w = std::max (1, juce::roundToInt (p.box.getWidth() * scaledFor));
        const int h = std::max (1, juce::roundToInt (p.box.getHeight() * scaledFor));
        return scaledParts[name] = juce::SoftwareImageType().convert (p.image).rescaled (w, h, juce::Graphics::highResamplingQuality);
    }

    void RigRenderer::drawPart (juce::Graphics& g, const juce::String& name)
    {
        const auto& p = MascotRig::get().part (name);
        if (! p.image.isValid())
            return;
        const auto& img = scaled (name);
        g.drawImageTransformed (img, juce::AffineTransform::scale (p.box.getWidth() / (float) img.getWidth(), p.box.getHeight() / (float) img.getHeight())
                                         .translated (p.box.getX(), p.box.getY()));
    }

    // A mesh part bends instead of turning: every grid point turns around the joint by (angle x its weight); the base
    // stays, the free end follows fully (prototype: meshPart)
    void RigRenderer::drawMesh (juce::Graphics& g, const juce::String& name, float degrees,
                                std::function<juce::Point<float> (juce::Point<float>, float)> post)
    {
        const auto& rig = MascotRig::get();
        const auto* m = rig.mesh (name);
        const auto& p = rig.part (name);
        if (m == nullptr || ! p.image.isValid())
            return;
        if (std::abs (degrees) < 0.05f && ! post)
        {
            drawPart (g, name);
            return;
        }
        const float a = juce::degreesToRadians (degrees);
        std::vector<juce::Point<float>> v ((size_t) (m->cols * m->rows));
        for (int jy = 0; jy < m->rows; ++jy)
            for (int ix = 0; ix < m->cols; ++ix)
            {
                const auto k = (size_t) (jy * m->cols + ix);
                const juce::Point<float> src (m->x + (float) ix * m->step, m->y + (float) jy * m->step);
                auto q = rotateAround (src, m->pivot, a * m->weight[k]);
                if (post)
                    q = post (q, src.y);
                v[k] = q;
            }
        const juce::Point<float> origin (p.box.getX(), p.box.getY());
        const auto& img = scaled (name);
        const float k = (float) img.getWidth() / p.box.getWidth();
        // drawn on every 2nd grid point: a quarter of the triangles, the bend stays smooth
        constexpr int S = 2;
        auto hasPixels = [&] (int ix, int jy)
        {
            for (int b = jy; b < std::min (jy + S, m->rows - 1); ++b)
                for (int a2 = ix; a2 < std::min (ix + S, m->cols - 1); ++a2)
                {
                    const auto c = (size_t) (b * (m->cols - 1) + a2);
                    if (c < m->quad.size() && m->quad[c])
                        return true;
                }
            return false;
        };
        for (int jy = 0; jy < m->rows - 1; jy += S)
            for (int ix = 0; ix < m->cols - 1; ix += S)
            {
                if (! hasPixels (ix, jy))
                    continue;
                const int ix1 = std::min (ix + S, m->cols - 1), jy1 = std::min (jy + S, m->rows - 1);
                const auto k0 = (size_t) (jy * m->cols + ix), k1 = (size_t) (jy * m->cols + ix1),
                           k3 = (size_t) (jy1 * m->cols + ix), k2 = (size_t) (jy1 * m->cols + ix1);
                auto src = [&] (size_t k) { return juce::Point<float> (m->x + (float) (k % (size_t) m->cols) * m->step,
                                                                      m->y + (float) (k / (size_t) m->cols) * m->step); };
                triangle (g, img, origin, k, src (k0), src (k1), src (k2), v[k0], v[k1], v[k2]);
                triangle (g, img, origin, k, src (k0), src (k2), src (k3), v[k0], v[k2], v[k3]);
            }
    }

    // The neck: the drawing has none, so head and body are one bending sheet there (prototype: bandMesh / neckD)
    void RigRenderer::drawNeckBand (juce::Graphics& g, const juce::String& name, float y0, float y1, const RigPose& pose)
    {
        const auto& rig = MascotRig::get();
        const auto& p = rig.part (name);
        if (! p.image.isValid())
            return;
        auto neckD = [&] (juce::Point<float> q)
        {
            const float wN = 1.0f - smooth01 ((q.y - neck0) / (neck1 - neck0));
            auto r = rotateAround (q, rig.neck, juce::degreesToRadians (pose.head) * wN);
            return r + juce::Point<float> (pose.headX * wN, pose.headY * wN);
        };
        const float st = 24.0f;
        std::vector<float> xs, ys;
        for (float x = p.box.getX() - 2.0f; x < p.box.getRight() + st; x += st) xs.push_back (x);
        for (float y = y0; y < y1; y += st) ys.push_back (y);
        ys.push_back (y1);
        const juce::Point<float> origin (p.box.getX(), p.box.getY());
        const auto& img = scaled (name);
        const float k = (float) img.getWidth() / p.box.getWidth();
        for (size_t j = 0; j + 1 < ys.size(); ++j)
            for (size_t i = 0; i + 1 < xs.size(); ++i)
            {
                const juce::Point<float> a (xs[i], ys[j]), b (xs[i + 1], ys[j]), c (xs[i + 1], ys[j + 1]), d (xs[i], ys[j + 1]);
                triangle (g, img, origin, k, a, b, c, neckD (a), neckD (b), neckD (c));
                triangle (g, img, origin, k, a, c, d, neckD (a), neckD (c), neckD (d));
            }
    }

    // Vector eyes and brows (prototype: drawEyes): the opening between the lids, yellow with a radial shade, the slit
    // pupil and the glint cut to it, the lid strokes following the lids, the brow above
    void RigRenderer::drawEyes (juce::Graphics& g, const RigPose& pose)
    {
        const auto& rig = MascotRig::get();
        const float c = juce::jlimit (0.0f, 1.0f, pose.lid), lo = juce::jlimit (0.0f, 1.0f, pose.lowLid);
        for (const auto& e : rig.eyes)
        {
            const size_t n = e.x.size();
            if (n < 2)
                continue;
            std::vector<float> yl (n), yb (n), upT (n), loB (n), shade (n);
            for (size_t k = 0; k < n; ++k)
            {
                const float meet = e.top[k] + 0.72f * (e.bot[k] - e.top[k]);
                yl[k] = e.top[k] + c * (meet - e.top[k]) + lo * 0.1f * (e.bot[k] - e.top[k]);
                yb[k] = std::max (yl[k], e.bot[k] - std::max (c, lo) * (e.bot[k] - meet));
                upT[k] = yl[k] - e.up[k] * (1.0f - 0.5f * c);
                loB[k] = yb[k] + e.low[k];
                shade[k] = yl[k] + 0.22f * (yb[k] - yl[k]);
            }
            auto band = [&] (const std::vector<float>& a, const std::vector<float>& b, float da, float db)
            {
                juce::Path p;
                p.startNewSubPath (e.x[0], a[0] + da);
                for (size_t k = 1; k < n; ++k) p.lineTo (e.x[k], a[k] + da);
                for (size_t k = n; k-- > 0;) p.lineTo (e.x[k], b[k] + db);
                p.closeSubPath();
                return p;
            };
            if (c < 0.97f)
            {
                juce::Graphics::ScopedSaveState open (g);
                g.reduceClipRegion (band (yl, yb, 0.0f, 0.0f));
                float cy = 0.0f;
                for (size_t k = 0; k < n; ++k) cy += e.top[k] + e.bot[k];
                cy = cy / (2.0f * (float) n) + 3.0f;
                const float cx = (e.x.front() + e.x.back()) * 0.5f, R = (e.x.back() - e.x.front()) * 0.55f;
                juce::ColourGradient grad (rig.yellowHi, cx, cy + 4.0f, rig.yellowLo, cx + R, cy + 4.0f, true);
                grad.addColour (0.5, rig.yellow);
                g.setGradientFill (grad);
                g.fillRect (juce::Rectangle<float> (e.x.front() - 2.0f, cy - R, e.x.back() - e.x.front() + 4.0f, 2.0f * R));
                g.setColour (rig.shade.withAlpha (0.45f));
                g.fillPath (band (yl, shade, 0.0f, 0.0f));
                // the slit pupil looks around, the glint moves a third as far
                const float pcx = e.pupil[0], pcy = e.pupil[1], hw = e.pupil[2], hh = e.pupil[3], sl = e.pupil[4], sc = pose.pupil;
                const float px = pcx + pose.lookX * 12.0f, py = pcy + pose.lookY * 8.0f;
                juce::Path pupil;
                for (int q = 0; q <= 40; ++q)
                {
                    const float v = -1.0f + (float) q / 20.0f, wv = hw * sc * std::pow (std::max (0.0f, 1.0f - v * v), 0.55f);
                    const juce::Point<float> pt (px + sl * v * hh + wv, py + v * hh * sc);
                    q == 0 ? pupil.startNewSubPath (pt) : pupil.lineTo (pt);
                }
                for (int q = 40; q >= 0; --q)
                {
                    const float v = -1.0f + (float) q / 20.0f, wv = hw * sc * std::pow (std::max (0.0f, 1.0f - v * v), 0.55f);
                    pupil.lineTo (px + sl * v * hh - wv, py + v * hh * sc);
                }
                pupil.closeSubPath();
                g.setGradientFill (juce::ColourGradient (rig.pupil, px, py - hh, rig.pupilRim, px, py + hh, false));
                g.fillPath (pupil);
                g.setColour (rig.glint);
                juce::Path gl;
                gl.addEllipse (-e.glint[2], -e.glint[3], 2.0f * e.glint[2], 2.0f * e.glint[3]);
                g.fillPath (gl, juce::AffineTransform::rotation (-0.3f).translated (e.glint[0] + pose.lookX * 4.0f, e.glint[1] + pose.lookY * 2.5f));
            }
            g.setColour (rig.line);
            std::vector<float> ylp (n), ybm (n);
            for (size_t k = 0; k < n; ++k) { ylp[k] = yl[k] + 1.2f; ybm[k] = yb[k] - 0.6f; }
            g.fillPath (band (ylp, upT, 0.0f, 0.0f));
            g.fillPath (band (ybm, loB, 0.0f, 0.0f));
            if (e.tail.size() == 3)
            {
                std::vector<float> tx, tu, td;
                for (int q = 0; q <= 20; ++q)
                {
                    const float u = (float) q / 20.0f, a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, cc = u * u;
                    const float x = a * e.tail[0].x + b * e.tail[1].x + cc * e.tail[2].x, y = a * e.tail[0].y + b * e.tail[1].y + cc * e.tail[2].y;
                    const float wv = 3.0f * (1.0f - u) + 0.6f;
                    tx.push_back (x); tu.push_back (y - wv / 2); td.push_back (y + wv / 2);
                }
                juce::Path tp; tp.startNewSubPath (tx[0], tu[0]);
                for (size_t k = 1; k < tx.size(); ++k) tp.lineTo (tx[k], tu[k]);
                for (size_t k = tx.size(); k-- > 0;) tp.lineTo (tx[k], td[k]);
                tp.closeSubPath(); g.fillPath (tp);
            }
            // the brow: up / down and tilt (positive = cross: the inner end goes down)
            if (! e.browUp.empty())
            {
                juce::Point<float> bc;
                for (auto& q : e.browUp) bc += q;
                bc /= (float) e.browUp.size();
                const float side = e.cx < 512.0f ? 1.0f : -1.0f;
                juce::Path brow;
                brow.startNewSubPath (e.browUp[0]);
                for (auto& q : e.browUp) brow.lineTo (q);
                for (size_t k = e.browLow.size(); k-- > 0;) brow.lineTo (e.browLow[k]);
                brow.closeSubPath();
                g.fillPath (brow, juce::AffineTransform::translation (-bc.x, -bc.y)
                                      .rotated (juce::degreesToRadians (pose.browTilt) * side)
                                      .translated (bc.x, bc.y + pose.browY));
            }
        }
    }

    // The open mouth (prototype: drawMouth): only the middle of the smile line opens, corners and curl stay as drawn
    void RigRenderer::drawMouth (juce::Graphics& g, const RigPose& pose)
    {
        const auto& rig = MascotRig::get();
        const float m = std::max (0.0f, pose.mouth);
        const int n = (int) rig.smile.size();
        if (m < 0.03f || n < 8)
            return;
        const int i0 = juce::roundToInt ((float) n * 0.06f), i1 = juce::roundToInt ((float) n * 0.8f);
        const float wf = juce::jlimit (0.55f, 1.0f, pose.mouthW), mid = (float) (i0 + i1) * 0.5f, half = (float) (i1 - i0) * 0.5f;
        const float H = 30.0f * std::min (1.25f, m);
        auto X = [&] (int i) { return rig.smileX0 + (mid + ((float) i - mid) * wf) * rig.smileStep; };
        auto u = [&] (int i) { const float d = ((float) i - mid) / half; return std::max (0.0f, 1.0f - d * d); };
        auto top = [&] (int i) { const int k = juce::jlimit (0, n - 1, juce::roundToInt (mid + ((float) i - mid) * wf)); return rig.smile[(size_t) k] + (1.0f - wf) * 3.0f * u (i); };
        auto bot = [&] (int i) { return top (i) + H * std::pow (u (i), 0.7f); };
        juce::Path open;
        open.startNewSubPath (X (i0), top (i0));
        for (int i = i0 + 1; i <= i1; ++i) open.lineTo (X (i), top (i));
        for (int i = i1; i >= i0; --i) open.lineTo (X (i), bot (i));
        open.closeSubPath();
        const int im = juce::roundToInt (mid);
        const float yT = top (im);
        {
            juce::Graphics::ScopedSaveState inside (g);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c0610), 0, yT, juce::Colour (0xff561019), 0, yT + H, false));
            g.fillPath (open);
            g.reduceClipRegion (open);
            g.setColour (juce::Colour (0xffc94a4c));   // tongue
            g.fillEllipse (X (juce::roundToInt (mid + 0.15f * half)) - 20.0f * wf, yT + H * 1.08f - H * 0.5f, 40.0f * wf, H);
            if (m > 0.6f)
            {
                juce::Path teeth;
                teeth.startNewSubPath (X (i0 + 3), top (i0 + 3) - 1.0f);
                for (int i = i0 + 4; i <= i1 - 3; ++i) teeth.lineTo (X (i), top (i) - 1.0f);
                for (int i = i1 - 3; i >= i0 + 3; --i) teeth.lineTo (X (i), top (i) + 2.5f * u (i));
                teeth.closeSubPath();
                g.setColour (juce::Colour (0xfffff0e8).withAlpha (std::min (0.85f, (m - 0.6f) * 2.5f)));
                g.fillPath (teeth);
            }
        }
        juce::Path lip;
        lip.startNewSubPath (X (i0), top (i0));
        for (int i = i0 + 1; i <= i1; ++i) lip.lineTo (X (i), bot (i));
        g.setColour (rig.line);
        g.strokePath (lip, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ONE outline of even width with round ends around the assembled figure (prototype: blur + threshold filter):
    // the figure's alpha grown by a disc of radiusPx (anti-aliased), in the outline colour, into 'ring'
    void RigRenderer::outline (const juce::Image& fig, float radiusPx, juce::Colour colour)
    {
        const int w = fig.getWidth(), h = fig.getHeight();
        alpha.resize ((size_t) (w * h));
        {
            const juce::Image::BitmapData src (fig, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < h; ++y)
            {
                auto* line = reinterpret_cast<const juce::PixelARGB*> (src.getLinePointer (y));
                for (int x = 0; x < w; ++x)
                    alpha[(size_t) (y * w + x)] = line[x].getAlpha();
            }
        }
        // the disc, anti-aliased at its rim (weights 0..256)
        struct Tap { int dx, dy, weight; };
        std::vector<Tap> taps;
        const int r = (int) std::ceil (radiusPx + 1.0f);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
            {
                const float wgt = juce::jlimit (0.0f, 1.0f, radiusPx + 0.5f - std::sqrt ((float) (dx * dx + dy * dy)));
                if (wgt > 0.0f && (dx != 0 || dy != 0))
                    taps.push_back ({ dx, dy, juce::roundToInt (wgt * 256.0f) });
            }
        const juce::Image::BitmapData dst (ring, juce::Image::BitmapData::writeOnly);
        const auto c = colour.getPixelARGB();
        for (int y = 0; y < h; ++y)
        {
            auto* out = reinterpret_cast<juce::PixelARGB*> (dst.getLinePointer (y));
            for (int x = 0; x < w; ++x)
            {
                int v = alpha[(size_t) (y * w + x)];
                if (v < 255)
                    for (const auto& t : taps)
                    {
                        const int xx = x + t.dx, yy = y + t.dy;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h)
                            continue;
                        v = std::max (v, (alpha[(size_t) (yy * w + xx)] * t.weight) >> 8);
                        if (v >= 255)
                            break;
                    }
                juce::PixelARGB px = c;
                px.multiplyAlpha (v);          // premultiplied
                out[x] = px;
            }
        }
    }
}
