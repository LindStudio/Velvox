#include "EmberRig.h"
#include "EmberRigData.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

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

    }

    // ------------------------------------------------------------------ rig data
    namespace
    {
        std::mutex rigMutex;
        std::weak_ptr<const MascotRig> rigShared;               // holds no pictures itself
        std::atomic<const MascotRig*> rigNow { nullptr };      // the one alive, for get() (lock free: drawing threads)
    }

    std::shared_ptr<const MascotRig> MascotRig::acquire()
    {
        const std::lock_guard<std::mutex> lock (rigMutex);
        if (auto s = rigShared.lock())
            return s;
        std::shared_ptr<const MascotRig> s (new MascotRig(), [] (const MascotRig* r)
        {
            const MascotRig* expected = r;
            rigNow.compare_exchange_strong (expected, nullptr);
            delete r;
        });
        rigShared = s;
        rigNow = s.get();
        return s;
    }

    const MascotRig& MascotRig::get()
    {
        if (auto* r = rigNow.load())
            return *r;
        // nobody holds it (should not happen: every user has a RigRenderer): kept alive for good rather than crash
        jassertfalse;
        return **new std::shared_ptr<const MascotRig> (acquire());
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
                // in memory, not a Direct2D image: the parts are read from the drawing thread
                part.image = juce::SoftwareImageType().convert (juce::ImageFileFormat::loadFrom (png, (size_t) n))
                                 .convertedToFormat (juce::Image::ARGB);
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
    juce::AffineTransform RigRenderer::fitTransform (juce::Rectangle<float> area, float height, float feet)
    {
        // fit (prototype: fit): the figure is 86 % of the area's height (700 px of the picture), feet at the bottom
        const auto& rig = MascotRig::get();
        const float s = area.getHeight() * height / 700.0f;
        const float ox = area.getCentreX() - 512.0f * s, oy = area.getY() + area.getHeight() * feet - (rig.feet.y + 8.0f) * s;
        return juce::AffineTransform::scale (s).translated (ox, oy);
    }

    void RigRenderer::render (juce::Graphics& g, juce::Rectangle<float> area, const RigPose& pose, float displayScale)
    {
        if (! MascotRig::get().isValid() || area.isEmpty())
            return;
        renderInto (ring, area, pose, displayScale);
        const auto t2 = juce::Time::getMillisecondCounterHiRes();
        juce::Graphics::ScopedSaveState keep (g);
        g.setOpacity (1.0f);      // images take the opacity of the last colour (the floor shadow's 28 % made him see-through)
        g.drawImage (ring, area, juce::RectanglePlacement::stretchToFit);   // outline + figure, composed by outline()
        lastCompositeMs = juce::Time::getMillisecondCounterHiRes() - t2;
    }

    void RigRenderer::renderInto (juce::Image& target, juce::Rectangle<float> area, const RigPose& pose, float displayScale)
    {
        const auto& rig = MascotRig::get();
        if (! rig.isValid() || area.isEmpty())
            return;
        rigToArea = fitTransform (area, fitHeight, fitFeet);
        const float s = rigToArea.mat00, ox = rigToArea.mat02, oy = rigToArea.mat12;

        const int w = std::max (1, juce::roundToInt (area.getWidth() * displayScale));
        const int h = std::max (1, juce::roundToInt (area.getHeight() * displayScale));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        // in memory, not on the GPU: the outline reads the figure's pixels every frame
        if (target.getWidth() != w || target.getHeight() != h)
            target = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        if (figure.getWidth() != w || figure.getHeight() != h)
            figure = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
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
            base = juce::AffineTransform::scale (s).translated (ox - area.getX(), oy - area.getY()).scaled (displayScale);
            fg.addTransform (base);
            juce::Image::BitmapData data (figure, juce::Image::BitmapData::readWrite);   // a software image: drawn at once
            pixels = &data;
            drawFigure (fg, pose);
            pixels = nullptr;
        }
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        outline (figure, target, outlineWidth * s * displayScale, juce::Colour (outlineColour));
        lastDrawMs = t1 - t0; lastOutlineMs = juce::Time::getMillisecondCounterHiRes() - t1;
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

        drawMesh (g, "tail", pose.tail * 1.3f, bodyTransform (pose));

        const auto headT = juce::AffineTransform::rotation (juce::degreesToRadians (pose.head), rig.neck.x, rig.neck.y)
                               .translated (pose.headX, pose.headY);
        const bool headMoves = std::abs (pose.head) > 0.05f || std::abs (pose.headX) > 0.05f || std::abs (pose.headY) > 0.05f;
        {
            juce::Graphics::ScopedSaveState ears (g);
            g.addTransform (headT);
            drawMesh (g, "ear_l", juce::jlimit (-30.0f, 30.0f, pose.earL), headTransform (pose));
            drawMesh (g, "ear_r", juce::jlimit (-30.0f, 30.0f, pose.earR), headTransform (pose));
        }
        const auto bodyT = bodyTransform (pose);
        if (! headMoves)
            drawPart ("body", bodyT);
        else
        {
            drawPart ("body", bodyT, neck1 - 2.0f);
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
        drawMesh (g, "arm_l", -juce::jlimit (-48.0f, 4.0f, pose.armL), bodyTransform (pose), neckPost);
        drawMesh (g, "arm_r", -juce::jlimit (-4.0f, 48.0f, pose.armR), bodyTransform (pose), neckPost);

        if (headMoves)
        {
            drawPart ("head", headTransform (pose), -1.0e6f, neck0 + 1.0f);
            const auto& hb = rig.part ("head").box;
            drawNeckBand (g, "head", neck0, hb.getBottom(), pose);
        }
        if (! headMoves)
            drawPart ("head", headTransform (pose));
        const auto te = juce::Time::getMillisecondCounterHiRes();
        drawFace (pose);
        lastFaceMs = juce::Time::getMillisecondCounterHiRes() - te;
    }

    // ------------------------------------------------------------------ the face, cached
    // Everything drawEyes / drawMouth can touch, in the picture's space (brows move up to ~15 px and tilt; generous).
    juce::Rectangle<float> RigRenderer::faceBox()
    {
        const auto& rig = MascotRig::get();
        juce::Rectangle<float> box;
        auto add = [&box] (juce::Rectangle<float> r) { box = box.isEmpty() ? r : box.getUnion (r); };
        for (const auto& e : rig.eyes)
        {
            if (e.x.empty())
                continue;
            float top = 1.0e9f, bot = -1.0e9f;
            for (size_t k = 0; k < e.x.size(); ++k)
            {
                top = std::min (top, e.top[k] - (k < e.up.size() ? e.up[k] : 0.0f));
                bot = std::max (bot, e.bot[k] + (k < e.low.size() ? e.low[k] : 0.0f));
            }
            add ({ e.x.front(), top, e.x.back() - e.x.front(), bot - top });
            for (const auto* pts : { &e.browUp, &e.browLow, &e.tail })
                for (auto q : *pts)
                    add ({ q.x, q.y, 1.0f, 1.0f });
        }
        if (! rig.smile.empty())
        {
            const float x0 = rig.smileX0, x1 = rig.smileX0 + (float) rig.smile.size() * rig.smileStep;
            const auto mm = std::minmax_element (rig.smile.begin(), rig.smile.end());
            add ({ x0, *mm.first, x1 - x0, *mm.second - *mm.first + 30.0f * 1.25f * 1.6f });   // open mouth + tongue
        }
        return box.expanded (40.0f);
    }

    RigRenderer::FaceKey RigRenderer::faceKey (const RigPose& p, float scale)
    {
        FaceKey k;
        const float v[9] = { p.lid, p.lowLid, p.lookX, p.lookY, p.pupil, p.mouth, p.mouthW, p.browY, p.browTilt };
        std::copy (std::begin (v), std::end (v), k.v);
        k.scale = scale;
        k.valid = true;
        return k;
    }

    bool RigRenderer::same (const FaceKey& a, const FaceKey& b)
    {
        if (! a.valid || ! b.valid || a.scale != b.scale)
            return false;
        // under ~1/20 px on a 2x screen: not visible
        constexpr float tol[9] = { 0.002f, 0.002f, 0.003f, 0.004f, 0.002f, 0.002f, 0.002f, 0.03f, 0.03f };
        for (int i = 0; i < 9; ++i)
            if (std::abs (a.v[i] - b.v[i]) > tol[i])
                return false;
        return true;
    }

    void RigRenderer::drawFace (const RigPose& pose)
    {
        static const auto box = faceBox();
        const float K = std::sqrt (std::abs (base.getDeterminant()));   // figure pixels per picture px
        const auto key = faceKey (pose, K);
        if (! same (key, faceFor))
        {
            const int w = std::max (1, (int) std::ceil (box.getWidth() * K)), h = std::max (1, (int) std::ceil (box.getHeight() * K));
            if (faceImage.getWidth() != w || faceImage.getHeight() != h)
                faceImage = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
            else
                faceImage.clear (faceImage.getBounds());
            juce::Graphics fg (faceImage);
            fg.addTransform (juce::AffineTransform::translation (-box.getX(), -box.getY()).scaled (K));
            drawEyes (fg, pose);
            drawMouth (fg, pose);
            faceFor = key;
        }
        // onto the head, like a part
        const juce::Point<float> a = box.getTopLeft(), b = box.getTopRight(), c = box.getBottomRight(), d = box.getBottomLeft();
        auto src = [&] (juce::Point<float> q) { return (q - a) * K; };
        const auto headSpace = headTransform (pose);
        raster (faceImage, headSpace, src (a), src (b), src (c), a, b, c);
        raster (faceImage, headSpace, src (a), src (c), src (d), a, c, d);
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

    void RigRenderer::drawPart (const juce::String& name, const juce::AffineTransform& space, float yFrom, float yTo)
    {
        const auto& p = MascotRig::get().part (name);
        if (! p.image.isValid())
            return;
        const auto& img = scaled (name);
        const float k = (float) img.getWidth() / p.box.getWidth();
        const float t = std::max (p.box.getY(), yFrom), b = std::min (p.box.getBottom(), yTo);
        if (b <= t)
            return;
        const juce::Point<float> a (p.box.getX(), t), c (p.box.getRight(), b), tr (c.x, a.y), bl (a.x, c.y), o (p.box.getX(), p.box.getY());
        raster (img, space, (a - o) * k, (tr - o) * k, (c - o) * k, a, tr, c);
        raster (img, space, (a - o) * k, (c - o) * k, (bl - o) * k, a, c, bl);
    }

    // A mesh part bends instead of turning: every grid point turns around the joint by (angle x its weight); the base
    // stays, the free end follows fully (prototype: meshPart)
    void RigRenderer::drawMesh (juce::Graphics& g, const juce::String& name, float degrees, const juce::AffineTransform& space,
                                std::function<juce::Point<float> (juce::Point<float>, float)> post)
    {
        const auto& rig = MascotRig::get();
        const auto* m = rig.mesh (name);
        const auto& p = rig.part (name);
        if (m == nullptr || ! p.image.isValid())
            return;
        if (std::abs (degrees) < 0.05f && ! post)
        {
            drawPart (name, space);
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
        // drawn on the full grid (the own rasterizer is fast enough)
        constexpr int S = 1;
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
                raster (img, space, (src (k0) - origin) * k, (src (k1) - origin) * k, (src (k2) - origin) * k, v[k0], v[k1], v[k2]);
                raster (img, space, (src (k0) - origin) * k, (src (k2) - origin) * k, (src (k3) - origin) * k, v[k0], v[k2], v[k3]);
            }
    }

    void RigRenderer::raster (const juce::Image& srcImage, const juce::AffineTransform& space,
                              juce::Point<float> s0, juce::Point<float> s1, juce::Point<float> s2,
                              juce::Point<float> d0, juce::Point<float> d1, juce::Point<float> d2)
    {
        if (pixels == nullptr)
            return;
        const auto toPx = space.followedBy (base);
        d0 = d0.transformedBy (toPx); d1 = d1.transformedBy (toPx); d2 = d2.transformedBy (toPx);
        float area2 = (d1.x - d0.x) * (d2.y - d0.y) - (d2.x - d0.x) * (d1.y - d0.y);
        if (std::abs (area2) < 1.0e-6f)
            return;
        if (area2 < 0) { std::swap (d1, d2); std::swap (s1, s2); }
        // pixel (centre) -> source picture
        const auto toSrc = juce::AffineTransform::fromTargetPoints (d0.x, d0.y, s0.x, s0.y, d1.x, d1.y, s1.x, s1.y, d2.x, d2.y, s2.x, s2.y);
        auto& dst = *pixels;
        const int x0 = std::max (0, (int) std::floor (std::min ({ d0.x, d1.x, d2.x })));
        const int x1 = std::min (dst.width - 1, (int) std::ceil (std::max ({ d0.x, d1.x, d2.x })));
        const int y0 = std::max (0, (int) std::floor (std::min ({ d0.y, d1.y, d2.y })));
        const int y1 = std::min (dst.height - 1, (int) std::ceil (std::max ({ d0.y, d1.y, d2.y })));
        // edge functions; top-left rule on ties so shared edges are drawn exactly once
        auto edge = [] (juce::Point<float> a, juce::Point<float> b, float px, float py) { return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x); };
        auto topLeft = [] (juce::Point<float> a, juce::Point<float> b) { return (a.y == b.y && b.x < a.x) || b.y < a.y; };
        const bool tl0 = topLeft (d1, d2), tl1 = topLeft (d2, d0), tl2 = topLeft (d0, d1);
        const juce::Image::BitmapData src (srcImage, juce::Image::BitmapData::readOnly);
        const int sw = src.width, sh = src.height;
        // edges and source position change linearly along a row: added up per pixel instead of worked out again
        const float e0x = -(d2.y - d1.y), e1x = -(d0.y - d2.y), e2x = -(d1.y - d0.y);
        const float sxx = toSrc.mat00, syx = toSrc.mat10;
        for (int y = y0; y <= y1; ++y)
        {
            auto* row = reinterpret_cast<juce::PixelARGB*> (dst.getLinePointer (y));
            const float py = (float) y + 0.5f, px0 = (float) x0 + 0.5f;
            float w0 = edge (d1, d2, px0, py), w1 = edge (d2, d0, px0, py), w2 = edge (d0, d1, px0, py);
            auto sp = juce::Point<float> (px0, py).transformedBy (toSrc) - juce::Point<float> (0.5f, 0.5f);
            for (int x = x0; x <= x1; ++x, w0 += e0x, w1 += e1x, w2 += e2x, sp.x += sxx, sp.y += syx)
            {
                if (! ((w0 > 0 || (w0 == 0 && tl0)) && (w1 > 0 || (w1 == 0 && tl1)) && (w2 > 0 || (w2 == 0 && tl2))))
                    continue;
                // bilinear sample of the (premultiplied) source, in integer weights
                const int ix = (int) std::floor (sp.x), iy = (int) std::floor (sp.y);
                if (ix < -1 || iy < -1 || ix >= sw || iy >= sh)
                    continue;
                const int fx = (int) ((sp.x - (float) ix) * 256.0f), fy = (int) ((sp.y - (float) iy) * 256.0f);
                juce::uint32 acc[4] = { 0, 0, 0, 0 };
                auto add = [&] (int sx, int sy, int wgt)
                {
                    if (sx < 0 || sy < 0 || sx >= sw || sy >= sh || wgt == 0)
                        return;
                    const auto& q = reinterpret_cast<const juce::PixelARGB*> (src.getLinePointer (sy))[sx];
                    acc[0] += (juce::uint32) (q.getAlpha() * wgt); acc[1] += (juce::uint32) (q.getRed() * wgt);
                    acc[2] += (juce::uint32) (q.getGreen() * wgt); acc[3] += (juce::uint32) (q.getBlue() * wgt);
                };
                add (ix, iy, (256 - fx) * (256 - fy));
                add (ix + 1, iy, fx * (256 - fy));
                add (ix, iy + 1, (256 - fx) * fy);
                add (ix + 1, iy + 1, fx * fy);
                const juce::uint32 a = acc[0] >> 16;
                if (a == 0)
                    continue;
                auto& o = row[x];
                const juce::uint32 inv = 255 - std::min<juce::uint32> (255, a);     // "over", premultiplied
                o.setARGB ((juce::uint8) std::min<juce::uint32> (255, a + ((o.getAlpha() * inv + 127) / 255)),
                           (juce::uint8) std::min<juce::uint32> (255, (acc[1] >> 16) + ((o.getRed() * inv + 127) / 255)),
                           (juce::uint8) std::min<juce::uint32> (255, (acc[2] >> 16) + ((o.getGreen() * inv + 127) / 255)),
                           (juce::uint8) std::min<juce::uint32> (255, (acc[3] >> 16) + ((o.getBlue() * inv + 127) / 255)));
            }
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
                raster (img, bodyTransform (pose), (a - origin) * k, (b - origin) * k, (c - origin) * k, neckD (a), neckD (b), neckD (c));
                raster (img, bodyTransform (pose), (a - origin) * k, (c - origin) * k, (d - origin) * k, neckD (a), neckD (c), neckD (d));
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
    // ONE outline of even width with round ends around the assembled figure (prototype: blur + threshold): the
    // exact distance of every pixel to the figure (Felzenszwalb's separable distance transform, two passes over the
    // pixels, only inside the figure's box + the radius), the outline where that distance is under radiusPx,
    // anti-aliased over one pixel. In the same pass the figure goes over it, so one picture goes to the screen.
    // (2026-11-17: the disc of taps cost 12 ms on a 2x screen.)
    void RigRenderer::outline (const juce::Image& fig, juce::Image& target, float radiusPx, juce::Colour colour)
    {
        const int w = fig.getWidth(), h = fig.getHeight();
        const juce::Image::BitmapData src (fig, juce::Image::BitmapData::readOnly);
        // the figure's box
        int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
        for (int y = 0; y < h; ++y)
        {
            auto* line = reinterpret_cast<const juce::PixelARGB*> (src.getLinePointer (y));
            for (int x = 0; x < w; ++x)
                if (line[x].getAlpha() >= 128)
                {
                    bx0 = std::min (bx0, x); bx1 = std::max (bx1, x); by0 = std::min (by0, y); by1 = std::max (by1, y);
                }
        }
        const juce::Image::BitmapData dst (target, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            std::memset (dst.getLinePointer (y), 0, (size_t) w * sizeof (juce::PixelARGB));
        if (bx1 < 0)
            return;
        const int m = (int) std::ceil (radiusPx) + 3;
        const int x0 = std::max (0, bx0 - m), x1 = std::min (w - 1, bx1 + m), y0 = std::max (0, by0 - m), y1 = std::min (h - 1, by1 + m);
        // on 2x screens (a wide outline) the distance is worked out on a half-size grid and interpolated: a quarter of
        // the work, the edge stays smooth because a distance field interpolates well
        const int q = radiusPx >= 5.0f ? 2 : 1;
        const int W = (x1 - x0) / q + 2, H = (y1 - y0) / q + 2;
        constexpr float inf = 1.0e12f;
        dist.assign ((size_t) (W * H), inf);
        for (int y = y0; y <= y1; ++y)
        {
            auto* line = reinterpret_cast<const juce::PixelARGB*> (src.getLinePointer (y));
            for (int x = x0; x <= x1; ++x)
                if (line[x].getAlpha() >= 128)
                    dist[(size_t) (((y - y0) / q) * W + (x - x0) / q)] = 0.0f;
        }
        // 1D squared distance transform (lower envelope of parabolas)
        auto edt1 = [this] (float* f, int n, int stride)
        {
            buf.resize ((size_t) n); v.resize ((size_t) n); z.resize ((size_t) n + 1);
            for (int q = 0; q < n; ++q) buf[(size_t) q] = f[q * stride];
            int k = 0; v[0] = 0; z[0] = -inf; z[1] = inf;
            for (int q = 1; q < n; ++q)
            {
                float sx;
                while (true)
                {
                    const int r = v[(size_t) k];
                    sx = ((buf[(size_t) q] + (float) (q * q)) - (buf[(size_t) r] + (float) (r * r))) / (2.0f * (float) (q - r));
                    if (sx <= z[(size_t) k] && k > 0) { --k; continue; }
                    break;
                }
                if (sx <= z[(size_t) k]) { v[0] = q; z[0] = -inf; z[1] = inf; k = 0; continue; }
                ++k; v[(size_t) k] = q; z[(size_t) k] = sx; z[(size_t) k + 1] = inf;
            }
            k = 0;
            for (int q = 0; q < n; ++q)
            {
                while (z[(size_t) k + 1] < (float) q) ++k;
                const int r = v[(size_t) k];
                f[q * stride] = (float) ((q - r) * (q - r)) + buf[(size_t) r];
            }
        };
        for (int x = 0; x < W; ++x) edt1 (dist.data() + x, H, W);
        for (int y = 0; y < H; ++y) edt1 (dist.data() + y * W, W, 1);
        for (auto& d2 : dist) d2 = std::sqrt (d2);      // once per cell, not four times per pixel
        const float far = (radiusPx + 1.0f) / (float) q + 1.5f;

        const auto c = colour.getPixelARGB();
        for (int y = y0; y <= y1; ++y)
        {
            auto* out = reinterpret_cast<juce::PixelARGB*> (dst.getLinePointer (y));
            auto* fp = reinterpret_cast<const juce::PixelARGB*> (src.getLinePointer (y));
            for (int x = x0; x <= x1; ++x)
            {
                if (fp[x].getAlpha() == 255) { out[x] = fp[x]; continue; }
                if (dist[(size_t) (((y - y0) / q) * W + (x - x0) / q)] > far) { out[x] = fp[x]; continue; }
                float d;
                if (q == 1)
                    d = dist[(size_t) ((y - y0) * W + (x - x0))];
                else
                {
                    // bilinear in the half-size grid, cell centres at (i + 0.5) * q
                    const float gx = ((float) (x - x0) + 0.5f) / (float) q - 0.5f, gy = ((float) (y - y0) + 0.5f) / (float) q - 0.5f;
                    const int ix = juce::jlimit (0, W - 2, (int) std::floor (gx)), iy = juce::jlimit (0, H - 2, (int) std::floor (gy));
                    const float fx = juce::jlimit (0.0f, 1.0f, gx - (float) ix), fy = juce::jlimit (0.0f, 1.0f, gy - (float) iy);
                    auto at = [&] (int a, int b) { return dist[(size_t) (b * W + a)]; };
                    d = (at (ix, iy) * (1 - fx) + at (ix + 1, iy) * fx) * (1 - fy) + (at (ix, iy + 1) * (1 - fx) + at (ix + 1, iy + 1) * fx) * fy;
                    d = std::max (0.0f, d * (float) q - 0.5f * (float) q);   // grid units -> pixels, a cell is "inside" up to its edge
                }
                const int a = juce::jlimit (0, 255, juce::roundToInt ((radiusPx + 0.5f - d) * 255.0f));
                const auto& f = fp[x];
                if (f.getAlpha() == 255 || a == 0)
                {
                    out[x] = f;
                    continue;
                }
                juce::PixelARGB o = c;
                o.multiplyAlpha (a);
                const int inv = 255 - f.getAlpha();           // the figure over the outline, premultiplied
                out[x].setARGB ((juce::uint8) (f.getAlpha() + ((o.getAlpha() * inv + 127) / 255)),
                                (juce::uint8) (f.getRed() + ((o.getRed() * inv + 127) / 255)),
                                (juce::uint8) (f.getGreen() + ((o.getGreen() * inv + 127) / 255)),
                                (juce::uint8) (f.getBlue() + ((o.getBlue() * inv + 127) / 255)));
            }
        }
    }
}
