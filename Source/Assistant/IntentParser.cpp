#include "IntentParser.h"
#include "StyleProfiles.h"
#include <set>

namespace velvox::assistant
{
    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

        // How a word sets the direction
        enum class Kind
        {
            noun,        // "Bass", "Hall": needs "mehr" / "weniger" (or a "zu viel" complaint)
            adjective,   // "breiter", "praesent": points in its polarity, "weniger" flips it
            complaint,   // "basslastig", "scharf", "dumpf": move AWAY from it unless told otherwise
            implied      // fixed phrases with a built-in wish ("nach vorne", "setzt sich nicht durch")
        };

        struct Entry
        {
            const char* text;   // normalised (ae/oe/ue/ss); contains a space = phrase
            Goal goal;
            int polarity;       // +1: more of the goal axis, -1: less
            Kind kind;
            bool exact = false; // token must match completely (no prefix match)
        };

        // The taxonomy's vocabulary. Longer entries win over shorter ones.
        const std::vector<Entry>& lexicon()
        {
            static const std::vector<Entry> entries = []
            {
                std::vector<Entry> e {
                    // ---- Bass / low end
                    { "tiefen", Goal::bass, 1, Kind::noun, true }, { "tiefe", Goal::bass, 1, Kind::noun, true },
                    { "tiefton", Goal::bass, 1, Kind::noun }, { "bass", Goal::bass, 1, Kind::noun }, { "baesse", Goal::bass, 1, Kind::noun },
                    { "low end", Goal::bass, 1, Kind::noun }, { "lows", Goal::bass, 1, Kind::noun, true }, { "low", Goal::bass, 1, Kind::noun, true },
                    { "basslastig", Goal::bass, 1, Kind::complaint }, { "wummer", Goal::bass, 1, Kind::complaint },
                    { "droehn", Goal::bass, 1, Kind::complaint }, { "boom", Goal::bass, 1, Kind::complaint }, { "rumpel", Goal::bass, 1, Kind::complaint },
                    { "rumble", Goal::bass, 1, Kind::complaint }, { "muddy", Goal::bass, 1, Kind::complaint }, { "matsch", Goal::bass, 1, Kind::complaint },
                    { "mulmig", Goal::bass, 1, Kind::complaint }, { "boomy", Goal::bass, 1, Kind::complaint },

                    // ---- Presence
                    { "praesenz", Goal::presence, 1, Kind::noun }, { "presence", Goal::presence, 1, Kind::noun },
                    { "praesenter", Goal::presence, 1, Kind::adjective }, { "praesent", Goal::presence, 1, Kind::adjective },
                    { "nach vorne", Goal::presence, 1, Kind::implied }, { "nach vorn", Goal::presence, 1, Kind::implied },
                    { "in den vordergrund", Goal::presence, 1, Kind::implied }, { "im vordergrund", Goal::presence, 1, Kind::implied },
                    { "vordergrund", Goal::presence, 1, Kind::implied }, { "im fokus", Goal::presence, 1, Kind::implied },
                    { "fokus", Goal::presence, 1, Kind::noun }, { "nach hinten", Goal::presence, -1, Kind::implied },
                    { "verstaendlich", Goal::presence, 1, Kind::adjective }, { "deutlicher", Goal::presence, 1, Kind::adjective, true },
                    { "glasklar", Goal::presence, 1, Kind::adjective }, { "klarheit", Goal::presence, 1, Kind::noun, true },
                    { "klar", Goal::presence, 1, Kind::adjective }, { "clarity", Goal::presence, 1, Kind::noun },
                    { "clear", Goal::presence, 1, Kind::adjective },
                    { "upfront", Goal::presence, 1, Kind::implied }, { "in front", Goal::presence, 1, Kind::implied },
                    { "nasal", Goal::presence, 1, Kind::complaint },

                    // ---- Vocal against the beat (masking)
                    { "setzt sich nicht durch", Goal::beatMasking, 1, Kind::implied }, { "setzt sich nicht", Goal::beatMasking, 1, Kind::implied },
                    { "durchsetz", Goal::beatMasking, 1, Kind::implied }, { "setzt sich durch", Goal::beatMasking, 1, Kind::implied },
                    { "geht unter", Goal::beatMasking, 1, Kind::implied }, { "untergeh", Goal::beatMasking, 1, Kind::implied },
                    { "gegen den beat", Goal::beatMasking, 1, Kind::implied }, { "an den beat", Goal::beatMasking, 1, Kind::implied },
                    { "beat", Goal::beatMasking, 1, Kind::implied }, { "instrumental", Goal::beatMasking, 1, Kind::implied },
                    { "maskier", Goal::beatMasking, 1, Kind::implied }, { "masking", Goal::beatMasking, 1, Kind::implied },
                    { "cut through", Goal::beatMasking, 1, Kind::implied },

                    // ---- Highs / air
                    { "hoehen", Goal::brightness, 1, Kind::noun }, { "highs", Goal::brightness, 1, Kind::noun }, { "air", Goal::brightness, 1, Kind::noun, true },
                    { "luft", Goal::brightness, 1, Kind::noun }, { "brillanz", Goal::brightness, 1, Kind::noun }, { "glanz", Goal::brightness, 1, Kind::noun },
                    { "hell", Goal::brightness, 1, Kind::adjective }, { "brillant", Goal::brightness, 1, Kind::adjective },
                    { "bright", Goal::brightness, 1, Kind::adjective }, { "dumpf", Goal::brightness, -1, Kind::complaint },
                    { "muffig", Goal::brightness, -1, Kind::complaint }, { "dunkler", Goal::brightness, -1, Kind::adjective },
                    { "dull", Goal::brightness, -1, Kind::complaint }, { "darker", Goal::brightness, -1, Kind::adjective },
                    { "crisp", Goal::brightness, 1, Kind::adjective }, { "spritzig", Goal::brightness, 1, Kind::adjective },
                    { "frischer", Goal::brightness, 1, Kind::adjective }, { "frisch", Goal::brightness, 1, Kind::adjective, true },
                    { "sparkle", Goal::brightness, 1, Kind::noun }, { "glitzer", Goal::brightness, 1, Kind::noun },

                    // ---- Reverb
                    { "hall", Goal::reverb, 1, Kind::noun }, { "reverb", Goal::reverb, 1, Kind::noun }, { "nachhall", Goal::reverb, 1, Kind::noun },
                    { "raum", Goal::reverb, 1, Kind::noun }, { "space", Goal::reverb, 1, Kind::noun, true }, { "room", Goal::reverb, 1, Kind::noun, true },
                    { "raeumlich", Goal::reverb, 1, Kind::adjective }, { "hallig", Goal::reverb, 1, Kind::complaint },
                    { "verhallt", Goal::reverb, 1, Kind::complaint }, { "trocken", Goal::reverb, -1, Kind::adjective },
                    { "dry", Goal::reverb, -1, Kind::adjective, true }, { "drier", Goal::reverb, -1, Kind::adjective, true },

                    // ---- Delay
                    { "delay", Goal::delay, 1, Kind::noun }, { "echo", Goal::delay, 1, Kind::noun }, { "verzoegerung", Goal::delay, 1, Kind::noun },

                    // ---- Level
                    { "lautstaerke", Goal::level, 1, Kind::noun }, { "pegel", Goal::level, 1, Kind::noun }, { "volume", Goal::level, 1, Kind::noun },
                    { "level", Goal::level, 1, Kind::noun, true }, { "lauter", Goal::level, 1, Kind::adjective, true },
                    { "laut", Goal::level, 1, Kind::adjective, true }, { "leiser", Goal::level, -1, Kind::adjective, true },
                    { "leise", Goal::level, -1, Kind::adjective, true }, { "louder", Goal::level, 1, Kind::adjective, true },
                    { "quieter", Goal::level, -1, Kind::adjective, true },

                    // ---- Width
                    { "stereobreite", Goal::width, 1, Kind::noun }, { "breite", Goal::width, 1, Kind::noun, true },
                    { "stereo", Goal::width, 1, Kind::noun }, { "width", Goal::width, 1, Kind::noun },
                    { "breiter", Goal::width, 1, Kind::adjective }, { "breit", Goal::width, 1, Kind::adjective, true },
                    { "schmaler", Goal::width, -1, Kind::adjective },
                    { "schmal", Goal::width, -1, Kind::adjective }, { "wider", Goal::width, 1, Kind::adjective, true },
                    { "wide", Goal::width, 1, Kind::adjective, true }, { "narrower", Goal::width, -1, Kind::adjective },

                    // ---- Warmth
                    { "waerme", Goal::warmth, 1, Kind::noun, true }, { "warmth", Goal::warmth, 1, Kind::noun },
                    { "koerper", Goal::warmth, 1, Kind::noun }, { "fuelle", Goal::warmth, 1, Kind::noun }, { "body", Goal::warmth, 1, Kind::noun },
                    { "waermer", Goal::warmth, 1, Kind::adjective }, { "warm", Goal::warmth, 1, Kind::adjective },
                    { "voller", Goal::warmth, 1, Kind::adjective, true }, { "fuelliger", Goal::warmth, 1, Kind::adjective },
                    { "duenn", Goal::warmth, -1, Kind::complaint }, { "kalt", Goal::warmth, -1, Kind::complaint },
                    { "kaelter", Goal::warmth, -1, Kind::adjective }, { "thin", Goal::warmth, -1, Kind::complaint },
                    { "warmer", Goal::warmth, 1, Kind::adjective, true },

                    // ---- Harshness
                    { "schaerfe", Goal::harshness, 1, Kind::noun }, { "haerte", Goal::harshness, 1, Kind::noun },
                    { "harshness", Goal::harshness, 1, Kind::noun }, { "resonanz", Goal::harshness, 1, Kind::noun },
                    { "schaerfer", Goal::harshness, 1, Kind::adjective }, { "scharf", Goal::harshness, 1, Kind::complaint },
                    { "harsch", Goal::harshness, 1, Kind::complaint }, { "harsh", Goal::harshness, 1, Kind::complaint },
                    { "schrill", Goal::harshness, 1, Kind::complaint }, { "stechend", Goal::harshness, 1, Kind::complaint },
                    { "beissend", Goal::harshness, 1, Kind::complaint }, { "spitz", Goal::harshness, 1, Kind::complaint },
                    { "kratzig", Goal::harshness, 1, Kind::complaint }, { "grell", Goal::harshness, 1, Kind::complaint },
                    { "weicher", Goal::harshness, -1, Kind::adjective }, { "sanfter", Goal::harshness, -1, Kind::adjective },
                    { "smoother", Goal::harshness, -1, Kind::adjective },

                    // ---- Sibilance
                    { "s laute", Goal::sibilance, 1, Kind::noun }, { "s laut", Goal::sibilance, 1, Kind::noun },
                    { "zischlaut", Goal::sibilance, 1, Kind::noun }, { "sibilanz", Goal::sibilance, 1, Kind::noun },
                    { "sibilance", Goal::sibilance, 1, Kind::noun }, { "esses", Goal::sibilance, 1, Kind::noun, true },
                    { "zisch", Goal::sibilance, 1, Kind::complaint }, { "sibilant", Goal::sibilance, 1, Kind::complaint },

                    // ---- Punch
                    { "druck", Goal::punch, 1, Kind::noun }, { "punch", Goal::punch, 1, Kind::noun }, { "power", Goal::punch, 1, Kind::noun },
                    { "wucht", Goal::punch, 1, Kind::noun }, { "biss", Goal::punch, 1, Kind::noun, true },
                    { "druckvoller", Goal::punch, 1, Kind::adjective }, { "punchiger", Goal::punch, 1, Kind::adjective },
                    { "knackiger", Goal::punch, 1, Kind::adjective }, { "kraeftiger", Goal::punch, 1, Kind::adjective },
                    { "fetter", Goal::punch, 1, Kind::adjective, true }, { "punchy", Goal::punch, 1, Kind::adjective },

                    // ---- Compression / dynamics control
                    { "kompress", Goal::compression, 1, Kind::noun }, { "compress", Goal::compression, 1, Kind::noun },
                    { "komprimierter", Goal::compression, 1, Kind::adjective }, { "komprimierung", Goal::compression, 1, Kind::noun },
                    { "gleichmaessiger", Goal::compression, 1, Kind::adjective }, { "konstanter", Goal::compression, 1, Kind::adjective },
                    { "dynamik", Goal::compression, -1, Kind::noun }, { "dynamics", Goal::compression, -1, Kind::noun },
                    { "lebendiger", Goal::compression, -1, Kind::adjective }, { "gequetscht", Goal::compression, 1, Kind::complaint },
                    { "zerquetscht", Goal::compression, 1, Kind::complaint }, { "squashed", Goal::compression, 1, Kind::complaint },
                    { "pumpt", Goal::compression, 1, Kind::complaint }, { "pumpen", Goal::compression, 1, Kind::complaint },
                    { "schwankt", Goal::compression, -1, Kind::complaint }, { "ungleichmaessig", Goal::compression, -1, Kind::complaint },
                };
                std::stable_sort (e.begin(), e.end(), [] (const Entry& a, const Entry& b)
                                  { return std::strlen (a.text) > std::strlen (b.text); });
                return e;
            }();
            return entries;
        }

        bool isOneOf (const juce::String& t, std::initializer_list<const char*> words)
        {
            for (auto* w : words)
                if (t == w)
                    return true;
            return false;
        }

        bool startsWithOneOf (const juce::String& t, std::initializer_list<const char*> stems)
        {
            for (auto* s : stems)
                if (t.startsWith (s))
                    return true;
            return false;
        }

        // +1 / -1 / 0
        int directionWord (const juce::String& t)
        {
            if (isOneOf (t, { "mehr", "more", "raise", "add", "plus", "staerker", "hoch", "rauf", "boost", "boosten", "anheben" })
                || startsWithOneOf (t, { "erhoeh", "verstaerk", "anheb", "hinzufueg", "increas", "verbesser" }))
                return 1;
            if (isOneOf (t, { "weniger", "less", "cut", "weg", "raus", "runter", "lower", "schwaecher", "minus" })
                || startsWithOneOf (t, { "reduzier", "verringer", "absenk", "senk", "daempf", "entfern", "zuruecknehm", "minimier", "reduc", "remov", "abschwaech" }))
                return -1;
            return 0;
        }

        bool isNegator (const juce::String& t)
        {
            return isOneOf (t, { "nicht", "kein", "keine", "keinen", "keiner", "keinesfalls", "ohne", "not", "no", "dont", "never", "nie", "niemals", "without" });
        }

        bool isPreserveWord (const juce::String& t)
        {
            return isOneOf (t, { "weiterhin", "trotzdem", "bleiben", "bleibt", "behalten", "beibehalten", "erhalten", "still", "keep", "stays", "remain" });
        }

        bool isFiller (const juce::String& t)
        {
            return isOneOf (t, { "mach", "mache", "machen", "die", "der", "das", "den", "dem", "des", "ein", "eine", "einen", "stimme", "vocal", "vocals",
                                 "voice", "gesang", "bitte", "sie", "ihr", "es", "ich", "will", "moechte", "soll", "sollte", "klingen", "klingt",
                                 "klang", "sound", "make", "the", "it", "please", "i", "want", "should", "sound", "sounds", "my", "meine", "mein",
                                 "auch", "noch", "insgesamt", "gesamt", "gib", "gebe", "geben", "ihm", "hier", "so", "wie", "dass", "sich",
                                 "passe", "anpassen", "an", "achte", "auf", "a", "an", "is", "too", "ist", "sind", "wird", "werden", "halt", "einfach" });
        }

        struct Mention
        {
            const Entry* entry;
            int first, last;   // token range
        };

        struct ClauseResult
        {
            std::vector<Intent> intents, constraints;
            std::vector<Goal> needDirection;   // nouns without a direction
            juce::StringArray needDirectionPhrases;
            int directionWord = 0;
            bool hasIntensity = false;
            Intensity intensity = Intensity::moderate;
            bool unrecognised = false;
            bool extreme = false;              // "extremer", "krasser" ...
            bool mentionsAll = false;          // "alles", "allem"
            std::vector<int> styles;           // indices into styleProfiles()
            juce::StringArray unknownStyles;   // "im Stil von <name I do not know>"
        };

        // "im Stil von Ski Aggu", "wie Tream", "a la ...": known names anywhere,
        // unknown names only after an explicit style marker
        void findStyles (const juce::StringArray& tokens, std::vector<bool>& used, ClauseResult& r)
        {
            const auto& profiles = styleProfiles();
            for (int p = 0; p < (int) profiles.size(); ++p)
            {
                for (const auto& name : profiles[(size_t) p].names)
                {
                    const auto words = juce::StringArray::fromTokens (name, " ", "");
                    for (int i = 0; i + words.size() <= tokens.size(); ++i)
                    {
                        bool match = true;
                        for (int k = 0; k < words.size() && match; ++k)
                            match = ! used[(size_t) (i + k)] && tokens[i + k] == words[k];
                        if (! match)
                            continue;
                        for (int k = 0; k < words.size(); ++k)
                            used[(size_t) (i + k)] = true;
                        if (std::find (r.styles.begin(), r.styles.end(), p) == r.styles.end())
                            r.styles.push_back (p);
                    }
                }
            }

            const auto isMarker = [] (const juce::String& t) { return isOneOf (t, { "stil", "stils", "style", "styles", "stile" }); };
            int markerEnd = -1;
            for (int i = 0; i < tokens.size(); ++i)
            {
                if (! isMarker (tokens[i]))
                    continue;
                used[(size_t) i] = true;
                if (i > 0 && isOneOf (tokens[i - 1], { "im", "in", "the", "dem" }))
                    used[(size_t) i - 1] = true;
                markerEnd = i;
                if (i + 1 < tokens.size() && isOneOf (tokens[i + 1], { "von", "of", "wie", "like" }))
                {
                    used[(size_t) i + 1] = true;
                    markerEnd = i + 1;
                }
            }

            // "wie X" / "like X" / "a la X" in front of a known name, and "oder" between names
            for (int i = 0; i < tokens.size(); ++i)
            {
                const bool nextIsName = i + 1 < tokens.size() && used[(size_t) i + 1] && ! r.styles.empty();
                if (! used[(size_t) i] && nextIsName && isOneOf (tokens[i], { "wie", "like", "la", "oder", "bzw", "or", "von" }))
                    used[(size_t) i] = true;
                if (! used[(size_t) i] && isOneOf (tokens[i], { "a" }) && i + 1 < tokens.size() && tokens[i + 1] == "la")
                    used[(size_t) i] = true;
            }

            if (markerEnd >= 0 && r.styles.empty())
            {
                juce::StringArray rest;
                for (int i = markerEnd + 1; i < tokens.size(); ++i)
                    if (! used[(size_t) i])
                    {
                        rest.add (tokens[i]);
                        used[(size_t) i] = true;
                    }
                if (! rest.isEmpty())
                    r.unknownStyles.add (rest.joinIntoString (" "));
            }
        }

        // Intensity words; consumes them (so "nicht zu viel" never reads as a negation)
        void readIntensity (juce::StringArray& tokens, std::vector<bool>& used, ClauseResult& r)
        {
            const auto mark = [&] (int i, int n, Intensity level)
            {
                for (int k = i; k < i + n; ++k)
                    used[(size_t) k] = true;
                r.hasIntensity = true;
                r.intensity = level;
            };

            for (int i = 0; i < tokens.size(); ++i)
            {
                if (used[(size_t) i])
                    continue;
                const auto& t = tokens[i];
                const auto next = i + 1 < tokens.size() ? tokens[i + 1] : juce::String();
                const auto next2 = i + 2 < tokens.size() ? tokens[i + 2] : juce::String();
                const bool afterZu = i > 0 && isOneOf (tokens[i - 1], { "zu", "too" });   // "zu viel Hall" is a complaint, not an intensity

                // "nicht zu viel", "nicht zu stark", "nicht zu sehr", "nicht uebertreiben"
                if (isOneOf (t, { "nicht", "not" }) && isOneOf (next, { "zu", "too" })
                    && isOneOf (next2, { "viel", "stark", "sehr", "much", "extrem", "krass", "heftig" }))
                {
                    mark (i, 3, Intensity::slight);
                    continue;
                }
                if (isOneOf (t, { "nicht", "ohne" }) && next.startsWith ("uebertreib"))
                {
                    mark (i, 2, Intensity::slight);
                    continue;
                }
                if (isOneOf (t, { "ein", "a" }) && isOneOf (next, { "wenig", "bisschen", "bissl", "bit", "little", "tick", "touch" }))
                {
                    mark (i, 2, Intensity::slight);
                    continue;
                }
                if (isOneOf (t, { "etwas", "bisschen", "leicht", "dezent", "minimal", "sanft", "subtil", "vorsichtig", "slightly", "slight",
                                  "subtle", "gently", "geringfuegig" }) || (t == "wenig" && ! afterZu))
                {
                    mark (i, 1, Intensity::slight);
                    continue;
                }
                if (isOneOf (t, { "moderat", "mittel", "spuerbar", "merklich", "moderately", "moderate", "somewhat" }))
                {
                    mark (i, 1, Intensity::moderate);
                    continue;
                }
                if (! afterZu && isOneOf (t, { "deutlich", "viel", "stark", "kraeftig", "sehr", "richtig", "massiv", "extrem", "much", "lot", "lots",
                                  "strongly", "heavily", "drastisch", "ordentlich", "wesentlich", "erheblich" }))
                {
                    mark (i, 1, Intensity::strong);
                    continue;
                }
                if (isOneOf (t, { "extremer", "krasser", "heftiger", "intensiver", "extremere", "more extreme" }))
                {
                    used[(size_t) i] = true;
                    r.extreme = true;
                    r.hasIntensity = true;
                    r.intensity = Intensity::strong;
                }
            }
        }

        // Finds lexicon entries in the token list (longest first, no overlaps)
        std::vector<Mention> findMentions (const juce::StringArray& tokens, std::vector<bool>& used)
        {
            std::vector<Mention> found;
            for (const auto& e : lexicon())
            {
                const auto words = juce::StringArray::fromTokens (e.text, " ", "");
                const int n = words.size();
                for (int i = 0; i + n <= tokens.size(); ++i)
                {
                    bool match = true;
                    for (int k = 0; k < n && match; ++k)
                    {
                        if (used[(size_t) (i + k)])
                            match = false;
                        else if (k == n - 1 && n > 1)
                            match = tokens[i + k].startsWith (words[k]);   // last word of a phrase may be inflected
                        else if (n == 1)
                            match = e.exact ? tokens[i] == words[0] : tokens[i].startsWith (words[0]);
                        else
                            match = tokens[i + k] == words[k];
                    }
                    if (! match)
                        continue;
                    for (int k = 0; k < n; ++k)
                        used[(size_t) (i + k)] = true;
                    found.push_back ({ &e, i, i + n - 1 });
                }
            }
            std::sort (found.begin(), found.end(), [] (const Mention& a, const Mention& b) { return a.first < b.first; });
            return found;
        }

        // "mono-kompatibel", "monotauglich" ... -> keep the vocal mono compatible
        bool findMonoConstraint (const juce::StringArray& tokens, std::vector<bool>& used)
        {
            for (int i = 0; i < tokens.size(); ++i)
            {
                if (used[(size_t) i])
                    continue;
                const auto& t = tokens[i];
                const auto next = i + 1 < tokens.size() ? tokens[i + 1] : juce::String();
                if (startsWithOneOf (t, { "monokompatib", "monotauglich", "monosicher", "monosafe", "monofest" }))
                {
                    used[(size_t) i] = true;
                    return true;
                }
                if (t == "mono" && startsWithOneOf (next, { "kompatib", "tauglich", "sicher", "safe", "compatib", "fest", "check" }))
                {
                    used[(size_t) i] = used[(size_t) i + 1] = true;
                    return true;
                }
            }
            return false;
        }

        ClauseResult parseClause (const juce::String& clause, int inheritedDirection)
        {
            ClauseResult r;
            auto tokens = juce::StringArray::fromTokens (clause, " ", "");
            tokens.removeEmptyStrings();
            if (tokens.isEmpty())
                return r;
            std::vector<bool> used ((size_t) tokens.size(), false);

            findStyles (tokens, used, r);
            readIntensity (tokens, used, r);
            const bool mono = findMonoConstraint (tokens, used);
            const auto mentions = findMentions (tokens, used);

            // Direction, negation, keep-wishes of the clause (only words not used by a mention)
            int dirWord = 0, dirPos = -1;
            bool preserve = false;
            for (int i = 0; i < tokens.size(); ++i)
            {
                if (used[(size_t) i])
                    continue;
                if (const int d = directionWord (tokens[i]); d != 0 && dirWord == 0)
                {
                    dirWord = d;
                    dirPos = i;
                }
                preserve = preserve || isPreserveWord (tokens[i]);
                r.mentionsAll = r.mentionsAll || isOneOf (tokens[i], { "alles", "allem", "everything", "all" });
            }
            r.directionWord = dirWord;

            if (mono)
            {
                Intent c;
                c.goal = Goal::width;
                c.direction = 0;
                c.constraint = true;
                c.phrase = "mono-kompatibel";
                r.constraints.push_back (c);
            }

            for (const auto& m : mentions)
            {
                const auto& e = *m.entry;
                Intent in;
                in.goal = e.goal;
                in.intensity = r.hasIntensity ? r.intensity : Intensity::moderate;
                in.phrase = tokens.joinIntoString (" ", m.first, m.last - m.first + 1);

                // "zu ..." complaints: "zu basslastig", "zu viel Hall", "zu wenig Druck"
                int zuDir = 0;
                const auto before1 = m.first >= 1 ? tokens[m.first - 1] : juce::String();
                const auto before2 = m.first >= 2 ? tokens[m.first - 2] : juce::String();
                if (isOneOf (before1, { "zu", "too" }))
                    zuDir = e.kind == Kind::noun ? -1 : -e.polarity;
                else if (isOneOf (before2, { "zu", "too" }) && isOneOf (before1, { "viel", "much", "stark", "sehr" }))
                    zuDir = -1;
                else if (isOneOf (before2, { "zu", "too" }) && isOneOf (before1, { "wenig", "little" }))
                    zuDir = 1;

                // Negation right before the target ("nicht schaerfer", "nicht mehr Hoehen", "kein Hall")
                bool negated = false;
                for (int k = std::max (0, m.first - 3); k < m.first; ++k)
                    if (! used[(size_t) k] && isNegator (tokens[k]))
                        negated = true;
                // ... but "zu" between them is a complaint, not a negation ("nicht zu dumpf" stays a constraint)

                int dir = 0;
                const int effectiveDirWord = dirWord != 0 ? dirWord : (e.kind == Kind::noun ? inheritedDirection : 0);
                if (zuDir != 0)
                    dir = zuDir;
                else if (e.kind == Kind::implied)
                    dir = e.polarity;
                else if (effectiveDirWord != 0)
                    dir = effectiveDirWord * e.polarity;
                else if (e.kind == Kind::complaint)
                    dir = -e.polarity;
                else if (e.kind == Kind::adjective)
                    dir = e.polarity;
                else
                    dir = 0;

                in.direction = dir;

                if (negated)
                {
                    in.constraint = true;
                    if (dir == 0)
                        in.direction = 1;   // "keine Hoehen" ~ do not add highs (never read as a cut)
                    r.constraints.push_back (in);
                }
                else if (preserve && e.kind != Kind::noun && dirWord == 0 && zuDir == 0)
                {
                    // "soll weiterhin klar klingen": keep it - do not reduce it
                    in.constraint = true;
                    in.direction = -1;
                    r.constraints.push_back (in);
                }
                else if (dir == 0)
                {
                    r.needDirection.push_back (in.goal);
                    r.needDirectionPhrases.add (in.phrase);
                }
                else
                {
                    r.intents.push_back (in);
                }
            }
            juce::ignoreUnused (dirPos);

            // Anything left that is not filler?
            if (mentions.empty() && ! mono && ! r.hasIntensity && r.styles.empty() && r.unknownStyles.isEmpty())
            {
                for (int i = 0; i < tokens.size(); ++i)
                    if (! used[(size_t) i] && ! isFiller (tokens[i]) && directionWord (tokens[i]) == 0 && ! isNegator (tokens[i])
                        && ! isPreserveWord (tokens[i]) && ! isOneOf (tokens[i], { "zu", "too", "alles", "allem", "everything", "all" }))
                        r.unrecognised = true;
            }
            return r;
        }

        juce::String questionFor (Goal g, const juce::String& phrase)
        {
            return de ("\xe2\x80\x9e") + phrase + de ("\xe2\x80\x9c: Soll ") + goalName (g)
                   + de (" mehr oder weniger werden? Schreib z.\xc2\xa0" "B. \xe2\x80\x9emehr ") + phrase
                   + de ("\xe2\x80\x9c oder \xe2\x80\x9eweniger ") + phrase + de ("\xe2\x80\x9c.");
        }
    }

    juce::String goalName (Goal g)
    {
        switch (g)
        {
            case Goal::bass:        return de ("die Tiefen");
            case Goal::presence:    return de ("die Pr\xc3\xa4senz");
            case Goal::brightness:  return de ("die H\xc3\xb6hen");
            case Goal::reverb:      return de ("der Hall");
            case Goal::delay:       return de ("das Echo");
            case Goal::level:       return de ("die Lautst\xc3\xa4rke");
            case Goal::width:       return de ("die Stereobreite");
            case Goal::warmth:      return de ("die W\xc3\xa4rme");
            case Goal::harshness:   return de ("die Sch\xc3\xa4rfe");
            case Goal::sibilance:   return de ("die S-Laute");
            case Goal::punch:       return de ("der Druck");
            case Goal::compression: return de ("die Kompression");
            case Goal::beatMasking: return de ("die Durchsetzungskraft gegen den Beat");
            case Goal::everything:  return de ("alles");
        }
        return {};
    }

    juce::String IntentParser::normalise (const juce::String& text)
    {
        auto s = text.toLowerCase();
        const std::pair<const char*, const char*> folds[] {
            { "\xc3\xa4", "ae" }, { "\xc3\xb6", "oe" }, { "\xc3\xbc", "ue" }, { "\xc3\x9f", "ss" },
            { "\xc3\x84", "ae" }, { "\xc3\x96", "oe" }, { "\xc3\x9c", "ue" },
            { "\xe2\x80\x9e", " " }, { "\xe2\x80\x9c", " " }, { "\xe2\x80\x9d", " " }, { "\xe2\x80\xa6", "." },
            { "\xe2\x80\x93", " " }, { "\xe2\x80\x94", " " }, { "\xc2\xa0", " " },
        };
        for (auto& [from, to] : folds)
            s = s.replace (juce::String::fromUTF8 (from), to);

        // Unify: hyphens and quotes become spaces, sentence marks become clause breaks
        juce::String out;
        for (auto c : s)
        {
            if (c == '-' || c == '"' || c == '\'' || c == '/' || c == '(' || c == ')' || c == '+' || c == '*')
                out << ' ';
            else if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == '\n' || c == '\r' || c == '\t')
                out << " | ";
            else
                out << juce::String::charToString (c);
        }
        return out;
    }

    ParsedRequest IntentParser::parse (const juce::String& text)
    {
        ParsedRequest result;
        if (text.trim().isEmpty())
            return result;
        result.empty = false;
        result.original = text;

        // Clauses: sentence marks and connectives ("aber", "und", "sowie" ...)
        juce::StringArray clauses;
        {
            juce::String current;
            for (auto& token : juce::StringArray::fromTokens (normalise (text), " ", ""))
            {
                if (token.isEmpty())
                    continue;
                if (token == "|" || isOneOf (token, { "aber", "und", "sowie", "but", "and", "jedoch", "allerdings", "dabei", "ausserdem", "also", "dann" }))
                {
                    if (current.trim().isNotEmpty())
                        clauses.add (current.trim());
                    current.clear();
                    continue;
                }
                current << token << " ";
            }
            if (current.trim().isNotEmpty())
                clauses.add (current.trim());
        }

        std::vector<Intent> intents, constraints;
        std::vector<int> styleIndices;
        int previousDirection = 0;
        bool anyExtreme = false, anyAll = false;
        size_t lastClauseStart = 0;

        for (const auto& clause : clauses)
        {
            auto r = parseClause (clause, previousDirection);
            anyExtreme = anyExtreme || r.extreme;
            anyAll = anyAll || r.mentionsAll;
            for (int s : r.styles)
                if (std::find (styleIndices.begin(), styleIndices.end(), s) == styleIndices.end())
                    styleIndices.push_back (s);
            result.unknownStyles.addArray (r.unknownStyles);
            if (r.intents.empty() && r.constraints.empty() && r.needDirection.empty() && ! r.hasIntensity
                && (! r.styles.empty() || ! r.unknownStyles.isEmpty()))
                continue;   // a clause that only names a style

            // A clause with only an intensity ("..., aber nicht zu viel") refines the wish before it
            if (r.intents.empty() && r.constraints.empty() && r.needDirection.empty() && r.hasIntensity && ! r.extreme)
            {
                for (size_t i = lastClauseStart; i < intents.size(); ++i)
                    intents[i].intensity = r.intensity;
                continue;
            }

            lastClauseStart = intents.size();
            for (auto& in : r.intents)
                intents.push_back (in);
            for (auto& c : r.constraints)
                constraints.push_back (c);
            for (size_t i = 0; i < r.needDirection.size(); ++i)
                result.ambiguous.push_back (questionFor (r.needDirection[i], r.needDirectionPhrases[(int) i]));
            if (r.unrecognised)
                result.unrecognised.push_back (clause);
            if (r.directionWord != 0)
                previousDirection = r.directionWord;
        }

        // "Mach alles extremer": no concrete target -> ask instead of guessing
        if ((anyExtreme || anyAll) && intents.empty() && result.ambiguous.empty() && styleIndices.empty())
        {
            result.ambiguous.push_back (de ("\xe2\x80\x9e" "Alles extremer\xe2\x80\x9c ist zu allgemein, um sicher etwas zu ver\xc3\xa4ndern. "
                                            "Was genau soll st\xc3\xa4rker werden \xe2\x80\x93 z.\xc2\xa0" "B. mehr Druck, mehr Hall, mehr Pr\xc3\xa4senz oder mehr Breite?"));
            result.unrecognised.clear();
        }

        // Merge duplicates, find conflicts
        std::vector<Intent> merged;
        for (auto& in : intents)
        {
            auto it = std::find_if (merged.begin(), merged.end(), [&] (const Intent& m) { return m.goal == in.goal; });
            if (it == merged.end())
            {
                merged.push_back (in);
                continue;
            }
            if (it->direction == in.direction)
            {
                it->intensity = std::max (it->intensity, in.intensity);
                continue;
            }
            if (std::find (result.conflicts.begin(), result.conflicts.end(), in.goal) == result.conflicts.end())
                result.conflicts.push_back (in.goal);
        }
        // A wish that its own constraint forbids ("mehr Hoehen, aber nicht mehr Hoehen") is a conflict too
        for (auto& c : constraints)
            for (auto& m : merged)
                if (m.goal == c.goal && c.direction != 0 && m.direction == c.direction
                    && std::find (result.conflicts.begin(), result.conflicts.end(), m.goal) == result.conflicts.end())
                    result.conflicts.push_back (m.goal);

        for (auto& m : merged)
            if (std::find (result.conflicts.begin(), result.conflicts.end(), m.goal) == result.conflicts.end())
                result.intents.push_back (m);
        result.constraints = constraints;

        // Style profiles: their traits become wishes. Typed wishes and limits
        // always win; traits on which several styles disagree are left out.
        std::vector<Intent> styleWishes;
        std::set<Goal> disagree;
        for (int s : styleIndices)
        {
            const auto& profile = styleProfiles()[(size_t) s];
            result.styles.add (profile.label);
            for (const auto& t : profile.traits)
            {
                if (disagree.count (t.goal) > 0)
                    continue;
                auto it = std::find_if (styleWishes.begin(), styleWishes.end(), [&] (const Intent& w) { return w.goal == t.goal; });
                if (it == styleWishes.end())
                {
                    Intent w;
                    w.goal = t.goal;
                    w.direction = t.direction;
                    w.intensity = t.intensity;
                    w.phrase = profile.label;
                    w.source = profile.label;
                    styleWishes.push_back (w);
                }
                else if (it->direction != t.direction)
                {
                    disagree.insert (t.goal);
                    styleWishes.erase (it);
                }
                else
                {
                    it->intensity = std::max (it->intensity, t.intensity);
                    if (! it->source.contains (profile.label))
                        it->source << " / " << profile.label;
                }
            }
        }
        for (auto g : disagree)
            result.styleConflicts.push_back (g);
        for (auto& w : styleWishes)
        {
            const bool typed = std::any_of (result.intents.begin(), result.intents.end(), [&] (const Intent& i) { return i.goal == w.goal && i.source.isEmpty(); });
            const bool conflicting = std::find (result.conflicts.begin(), result.conflicts.end(), w.goal) != result.conflicts.end();
            if (! typed && ! conflicting && ! result.hasConstraint (w.goal, w.direction))
                result.intents.push_back (w);
        }
        return result;
    }
}
