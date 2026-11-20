#include "SpeechEngine.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace ihm
{
namespace
{
using Str = std::u32string;
using Toks = std::vector<std::string>;
using Seq = std::vector<Segment>;

Str decodeUtf8 (const std::string& s)
{
    Str out;
    size_t i = 0;
    while (i < s.size())
    {
        unsigned char c = (unsigned char) s[i];
        char32_t cp = 0;
        int len = 1;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
        else { ++i; continue; }
        for (int k = 1; k < len; ++k)
        {
            if (i + (size_t) k >= s.size()) { cp = 0; break; }
            cp = (cp << 6) | ((unsigned char) s[i + (size_t) k] & 0x3F);
        }
        i += (size_t) len;
        if (cp) out.push_back (cp);
    }
    return out;
}

char32_t lowerC (char32_t c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 0x20;
    if (c == 0x401) return 0x451;
    return c;
}
bool isCyr (char32_t c) { return (c >= 0x430 && c <= 0x44F) || c == 0x451; }
bool isLat (char32_t c) { return c >= 'a' && c <= 'z'; }

Segment mk (float f1, float f2, float f3, float e1, float e2, float e3,
            float voice, float noise, float nf, float nbw, bool asp, float ms)
{
    return Segment { f1, f2, f3, e1, e2, e3, voice, noise, nf, nbw, asp, ms };
}

const std::unordered_map<std::string, Seq>& table()
{
    static const std::unordered_map<std::string, Seq> t = []
    {
        std::unordered_map<std::string, Seq> m;
        auto vow = [&] (const char* n, float a, float b, float c, float ms, float voice = 1.f)
        { m[n] = { mk (a, b, c, a, b, c, voice, 0, 0, 0, false, ms) }; };
        auto dip = [&] (const char* n, float a, float b, float c, float d, float e, float f, float ms)
        { m[n] = { mk (a, b, c, d, e, f, 1, 0, 0, 0, false, ms) }; };

        vow ("IY", 270, 2290, 3010, 115); vow ("IH", 390, 1990, 2550, 85);
        vow ("EH", 530, 1840, 2480, 100); vow ("AE", 660, 1720, 2410, 120);
        vow ("AA", 730, 1090, 2440, 125); vow ("AO", 570, 840, 2410, 125);
        vow ("UH", 440, 1020, 2240, 85);  vow ("UW", 300, 870, 2240, 115);
        vow ("AH", 640, 1190, 2390, 90);  vow ("ER", 490, 1350, 1690, 120);
        vow ("RY", 330, 1500, 2400, 100);
        dip ("AY", 730, 1090, 2440, 300, 2200, 3000, 185);
        dip ("AW", 730, 1090, 2440, 320, 900, 2300, 185);
        dip ("EY", 530, 1840, 2480, 300, 2200, 3000, 165);
        dip ("OW", 500, 900, 2400, 320, 850, 2300, 170);
        dip ("OY", 570, 840, 2410, 300, 2200, 3000, 195);
        vow ("W", 290, 610, 2150, 70);  vow ("Y", 260, 2070, 3020, 65);
        vow ("L", 360, 1300, 2700, 75); vow ("R", 310, 1060, 1380, 80);
        vow ("M", 250, 1100, 2200, 80, 0.5f); vow ("N", 250, 1600, 2500, 75, 0.5f);
        vow ("NG", 250, 2000, 2600, 85, 0.5f);

        auto fric = [&] (const char* n, float voice, float noise, float nf, float nbw, float ms,
                         float a, float b, float c)
        { m[n] = { mk (a, b, c, a, b, c, voice, noise, nf, nbw, false, ms) }; };
        fric ("S", 0, 0.8f, 6500, 2200, 110, 500, 1500, 2500);
        fric ("SH", 0, 0.8f, 3200, 1400, 110, 500, 1500, 2500);
        fric ("F", 0, 0.25f, 4500, 4000, 100, 500, 1500, 2500);
        fric ("TH", 0, 0.2f, 5500, 4000, 95, 500, 1500, 2500);
        fric ("X", 0, 0.5f, 2500, 2000, 100, 500, 1500, 2500);
        fric ("Z", 0.6f, 0.35f, 6500, 2200, 95, 250, 1700, 2600);
        fric ("ZH", 0.6f, 0.35f, 3200, 1400, 95, 250, 1700, 2300);
        fric ("V", 0.6f, 0.12f, 4500, 4000, 80, 250, 1100, 2200);
        fric ("DH", 0.6f, 0.1f, 5500, 4000, 70, 250, 1500, 2500);
        m["HH"] = { mk (500, 1500, 2500, 500, 1500, 2500, 0, 0.55f, 0, 0, true, 70) };

        auto plos = [&] (const char* n, bool voiced, float nf, float nbw)
        {
            Seq s;
            s.push_back (mk (250, 1200, 2300, 250, 1200, 2300, voiced ? 0.35f : 0.f, 0, 0, 0, false, 50));
            s.push_back (mk (500, 1500, 2500, 500, 1500, 2500, 0, 0.9f, nf, nbw, false, 14));
            if (! voiced) s.push_back (mk (500, 1500, 2500, 500, 1500, 2500, 0, 0.4f, 0, 0, true, 28));
            else          s.push_back (mk (500, 1500, 2500, 500, 1500, 2500, 0.5f, 0, 0, 0, false, 6));
            m[n] = s;
        };
        plos ("P", false, 1500, 2500); plos ("B", true, 1500, 2500);
        plos ("T", false, 4500, 2500); plos ("D", true, 4500, 2500);
        plos ("K", false, 2200, 1500); plos ("G", true, 2200, 1500);

        auto closure = [] (bool voiced, float ms)
        { return mk (250, 1200, 2300, 250, 1200, 2300, voiced ? 0.35f : 0.f, 0, 0, 0, false, ms); };
        m["CH"] = { closure (false, 45), mk (500, 1500, 2500, 500, 1500, 2500, 0, 0.8f, 3200, 1400, false, 85) };
        m["JH"] = { closure (true, 45),  mk (250, 1700, 2300, 250, 1700, 2300, 0.5f, 0.45f, 3200, 1400, false, 70) };
        m["TS"] = { closure (false, 40), mk (500, 1500, 2500, 500, 1500, 2500, 0, 0.8f, 6500, 2200, false, 85) };
        return m;
    }();
    return t;
}

void appendPhoneme (const std::string& tok, Seq& out)
{
    auto& t = table();
    auto it = t.find (tok);
    if (it != t.end()) out.insert (out.end(), it->second.begin(), it->second.end());
}

Toks splitTokens (const std::string& s)
{
    Toks r;
    std::string cur;
    for (char c : s)
    {
        if (c == ' ') { if (! cur.empty()) { r.push_back (cur); cur.clear(); } }
        else cur.push_back (c);
    }
    if (! cur.empty()) r.push_back (cur);
    return r;
}

// ------------------------------------------------------------------ English
const std::unordered_map<std::string, std::string>& enDict()
{
    static const std::unordered_map<std::string, std::string> d = {
        { "infiniti", "IH N F IH N IH T IY" }, { "hype", "HH AY P" }, { "machine", "M AH SH IY N" },
        { "welcome", "W EH L K AH M" }, { "hello", "HH AH L OW" }, { "jealous", "JH EH L AH S" },
        { "the", "DH AH" }, { "a", "AH" }, { "of", "AH V" }, { "to", "T UW" }, { "you", "Y UW" },
        { "your", "Y AO R" }, { "are", "AA R" }, { "is", "IH Z" }, { "was", "W AA Z" },
        { "one", "W AH N" }, { "two", "T UW" }, { "three", "TH R IY" }, { "four", "F AO R" },
        { "five", "F AY V" }, { "six", "S IH K S" }, { "seven", "S EH V AH N" }, { "eight", "EY T" },
        { "nine", "N AY N" }, { "ten", "T EH N" }, { "zero", "Z IY R OW" },
        { "love", "L AH V" }, { "have", "HH AE V" }, { "give", "G IH V" }, { "come", "K AH M" },
        { "some", "S AH M" }, { "what", "W AH T" }, { "who", "HH UW" }, { "do", "D UW" },
        { "said", "S EH D" }, { "they", "DH EY" }, { "this", "DH IH S" }, { "that", "DH AE T" },
        { "with", "W IH DH" }, { "there", "DH EH R" }, { "their", "DH EH R" }, { "where", "W EH R" },
        { "here", "HH IY R" }, { "were", "W ER" }, { "from", "F R AH M" }, { "then", "DH EH N" },
        { "them", "DH EH M" }, { "these", "DH IY Z" }, { "those", "DH OW Z" }, { "get", "G EH T" },
        { "girl", "G ER L" }, { "gift", "G IH F T" }, { "now", "N AW" }, { "how", "HH AW" },
        { "down", "D AW N" }, { "town", "T AW N" }, { "brown", "B R AW N" }, { "cow", "K AW" },
        { "power", "P AW ER" }, { "flower", "F L AW ER" }, { "laugh", "L AE F" }, { "enough", "IH N AH F" },
        { "through", "TH R UW" }, { "though", "DH OW" }, { "music", "M Y UW Z IH K" },
        { "beat", "B IY T" }, { "bass", "B EY S" }, { "drop", "D R AA P" }, { "synth", "S IH N TH" },
        { "FL", "EH F EH L" }, { "studio", "S T UW D IY OW" }, { "robot", "R OW B AA T" },
        { "voice", "V OY S" }, { "speak", "S P IY K" }, { "word", "W ER D" }, { "world", "W ER L D" },
        { "love", "L AH V" }, { "me", "M IY" }, { "he", "HH IY" }, { "she", "SH IY" }, { "we", "W IY" },
        { "be", "B IY" }, { "my", "M AY" }, { "by", "B AY" }, { "no", "N OW" }, { "yes", "Y EH S" },
        { "go", "G OW" }, { "so", "S OW" }, { "or", "AO R" }, { "and", "AE N D" }, { "for", "F AO R" },
        { "night", "N AY T" }, { "light", "L AY T" }, { "fight", "F AY T" }, { "right", "R AY T" },
        { "people", "P IY P AH L" }, { "very", "V EH R IY" }, { "every", "EH V R IY" },
    };
    return d;
}

bool isVow (char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }

Toks englishG2P (const std::string& s)
{
    auto& dict = enDict();
    auto f = dict.find (s);
    if (f != dict.end()) return splitTokens (f->second);

    const size_t n = s.size();
    Toks out;
    auto at = [&] (size_t k) -> char { return k < n ? s[k] : '\0'; };
    auto starts = [&] (size_t k, const char* p) { return k < n && s.compare (k, std::strlen (p), p) == 0; };
    auto cons = [&] (size_t k) { return k < n && ! isVow (s[k]); };

    int groups = 0;
    for (size_t k = 0; k < n; ++k)
        if (isVow (s[k]) && (k == 0 ? s[k] != 'y' : true) && (k == 0 || ! isVow (s[k - 1]))) ++groups;
    if (groups == 0) groups = 1;

    for (size_t i = 0; i < n;)
    {
        const char c = s[i];
        const char prev = i > 0 ? s[i - 1] : '\0';
        if (! isVow (c) && at (i + 1) == c && c != 'c') { ++i; continue; }

        const bool magic = cons (i + 1) && at (i + 2) == 'e'
                           && (i + 3 >= n || (i + 3 == n - 1 && (at (i + 3) == 's' || at (i + 3) == 'd')));
        const bool openSingle = (groups == 1 && i == n - 1 && i > 0 && ! isVow (prev));

        switch (c)
        {
            case 'a':
                if (starts (i, "ai") || starts (i, "ay")) { out.push_back ("EY"); i += 2; }
                else if (starts (i, "au") || starts (i, "aw")) { out.push_back ("AO"); i += 2; }
                else if (starts (i, "all")) { out.push_back ("AO"); ++i; }
                else if (starts (i, "ar")) { out.push_back ("AA"); out.push_back ("R"); i += 2; }
                else if (magic || openSingle) { out.push_back ("EY"); ++i; }
                else if (i == n - 1 && groups > 1) { out.push_back ("AH"); ++i; }
                else { out.push_back ("AE"); ++i; }
                break;
            case 'e':
                if (starts (i, "ee") || starts (i, "ea")) { out.push_back ("IY"); i += 2; }
                else if (starts (i, "ei")) { out.push_back ("EY"); i += 2; }
                else if (starts (i, "ew") || starts (i, "eu")) { out.push_back ("Y"); out.push_back ("UW"); i += 2; }
                else if (starts (i, "er")) { out.push_back ("ER"); i += 2; }
                else if (i == n - 1) { if (groups == 1) out.push_back ("IY"); ++i; }
                else if (i == n - 2 && at (i + 1) == 'd' && i >= 2 && cons (i - 1))
                { if (prev == 't' || prev == 'd') out.push_back ("IH"); ++i; }
                else if (i == n - 2 && at (i + 1) == 's' && i >= 2 && cons (i - 1)
                         && std::strchr ("sxzch", prev)) { out.push_back ("IH"); ++i; }
                else if (i == n - 2 && at (i + 1) == 's' && i >= 2 && cons (i - 1)) { ++i; }
                else if (magic || openSingle) { out.push_back ("IY"); ++i; }
                else { out.push_back ("EH"); ++i; }
                break;
            case 'i':
                if (starts (i, "igh")) { out.push_back ("AY"); i += 3; }
                else if (starts (i, "ie")) { out.push_back ("IY"); i += 2; }
                else if (starts (i, "ir")) { out.push_back ("ER"); i += 2; }
                else if (magic || openSingle) { out.push_back ("AY"); ++i; }
                else if (i == n - 1) { out.push_back ("IY"); ++i; }
                else { out.push_back ("IH"); ++i; }
                break;
            case 'o':
                if (starts (i, "oo")) { out.push_back ("UW"); i += 2; }
                else if (starts (i, "oa")) { out.push_back ("OW"); i += 2; }
                else if (starts (i, "ou")) { out.push_back ("AW"); i += 2; }
                else if (starts (i, "ow")) { out.push_back ("OW"); i += 2; }
                else if (starts (i, "oi") || starts (i, "oy")) { out.push_back ("OY"); i += 2; }
                else if (starts (i, "or")) { out.push_back ("AO"); out.push_back ("R"); i += 2; }
                else if (magic || openSingle || i == n - 1) { out.push_back ("OW"); ++i; }
                else { out.push_back ("AA"); ++i; }
                break;
            case 'u':
                if (starts (i, "ue") && i + 2 == n) { out.push_back ("UW"); i += 2; }
                else if (starts (i, "ui")) { out.push_back ("UW"); i += 2; }
                else if (starts (i, "ur")) { out.push_back ("ER"); i += 2; }
                else if (magic) { out.push_back ("UW"); ++i; }
                else { out.push_back ("AH"); ++i; }
                break;
            case 'y':
                if (i == 0) out.push_back ("Y");
                else if (magic) out.push_back ("AY");
                else if (i == n - 1) out.push_back (groups <= 1 ? "AY" : "IY");
                else out.push_back ("IH");
                ++i;
                break;
            case 'b': out.push_back ("B"); ++i; break;
            case 'c':
                if (starts (i, "ch")) { out.push_back ("CH"); i += 2; }
                else if (starts (i, "ck")) { out.push_back ("K"); i += 2; }
                else if (at (i + 1) == 'e' || at (i + 1) == 'i' || at (i + 1) == 'y') { out.push_back ("S"); ++i; }
                else { out.push_back ("K"); ++i; }
                break;
            case 'd':
                if (starts (i, "dg")) { out.push_back ("JH"); i += 2; }
                else { out.push_back ("D"); ++i; }
                break;
            case 'f': out.push_back ("F"); ++i; break;
            case 'g':
                if (starts (i, "gh")) { if (i == 0) out.push_back ("G"); i += 2; }
                else if (at (i + 1) == 'e' || at (i + 1) == 'i' || at (i + 1) == 'y') { out.push_back ("JH"); ++i; }
                else { out.push_back ("G"); ++i; }
                break;
            case 'h': out.push_back ("HH"); ++i; break;
            case 'j': out.push_back ("JH"); ++i; break;
            case 'k':
                if (i == 0 && at (1) == 'n') { out.push_back ("N"); i += 2; }
                else { out.push_back ("K"); ++i; }
                break;
            case 'l':
                if (i == n - 2 && at (i + 1) == 'e' && i >= 2 && cons (i - 1))
                { out.push_back ("AH"); out.push_back ("L"); i += 2; }
                else { out.push_back ("L"); ++i; }
                break;
            case 'm': out.push_back ("M"); ++i; break;
            case 'n':
                if (starts (i, "ng")) { out.push_back ("NG"); i += 2; }
                else if (starts (i, "nk")) { out.push_back ("NG"); out.push_back ("K"); i += 2; }
                else { out.push_back ("N"); ++i; }
                break;
            case 'p':
                if (starts (i, "ph")) { out.push_back ("F"); i += 2; }
                else { out.push_back ("P"); ++i; }
                break;
            case 'q':
                if (starts (i, "qu")) { out.push_back ("K"); out.push_back ("W"); i += 2; }
                else { out.push_back ("K"); ++i; }
                break;
            case 'r': out.push_back ("R"); ++i; break;
            case 's':
                if (starts (i, "sh")) { out.push_back ("SH"); i += 2; }
                else if (starts (i, "sion")) { out.push_back ("ZH"); out.push_back ("AH"); out.push_back ("N"); i += 4; }
                else if (i == n - 1 && i > 0 && (isVow (prev) || std::strchr ("bdglmnrv", prev))) { out.push_back ("Z"); ++i; }
                else if (i > 0 && isVow (prev) && i + 1 < n && isVow (s[i + 1])) { out.push_back ("Z"); ++i; }
                else { out.push_back ("S"); ++i; }
                break;
            case 't':
                if (starts (i, "tch")) { out.push_back ("CH"); i += 3; }
                else if (starts (i, "tion")) { out.push_back ("SH"); out.push_back ("AH"); out.push_back ("N"); i += 4; }
                else if (starts (i, "th")) { out.push_back ("TH"); i += 2; }
                else { out.push_back ("T"); ++i; }
                break;
            case 'v': out.push_back ("V"); ++i; break;
            case 'w':
                if (starts (i, "wh")) { out.push_back ("W"); i += 2; }
                else if (i == 0 && at (1) == 'r') { out.push_back ("R"); i += 2; }
                else { out.push_back ("W"); ++i; }
                break;
            case 'x': out.push_back ("K"); out.push_back ("S"); ++i; break;
            case 'z': out.push_back ("Z"); ++i; break;
            default: ++i; break;
        }
    }
    return out;
}

// ------------------------------------------------------------------ Russian
Toks russianG2P (const Str& w)
{
    Toks out;
    const size_t n = w.size();
    for (size_t i = 0; i < n; ++i)
    {
        const char32_t c = w[i];
        const char32_t prev = i ? w[i - 1] : 0;
        const bool hard = prev == 0x436 || prev == 0x448 || prev == 0x446; // ж ш ц
        const bool last = (i + 1 == n);
        switch (c)
        {
            case 0x430: out.push_back ("AA"); break;                                   // а
            case 0x43E: out.push_back ("AO"); break;                                   // о
            case 0x443: out.push_back ("UW"); break;                                   // у
            case 0x44B: out.push_back ("RY"); break;                                   // ы
            case 0x44D: out.push_back ("EH"); break;                                   // э
            case 0x438: out.push_back (hard ? "RY" : "IY"); break;                     // и
            case 0x44F: out.push_back ("Y"); out.push_back ("AA"); break;              // я
            case 0x44E: out.push_back ("Y"); out.push_back ("UW"); break;              // ю
            case 0x435: if (! hard) out.push_back ("Y"); out.push_back ("EH"); break;  // е
            case 0x451: if (! hard) out.push_back ("Y"); out.push_back ("AO"); break;  // ё
            case 0x431: out.push_back (last ? "P" : "B"); break;                       // б
            case 0x432: out.push_back (last ? "F" : "V"); break;                       // в
            case 0x433: out.push_back (last ? "K" : "G"); break;                       // г
            case 0x434: out.push_back (last ? "T" : "D"); break;                       // д
            case 0x436: out.push_back (last ? "SH" : "ZH"); break;                     // ж
            case 0x437: out.push_back (last ? "S" : "Z"); break;                       // з
            case 0x439: out.push_back ("Y"); break;                                    // й
            case 0x43A: out.push_back ("K"); break;                                    // к
            case 0x43B: out.push_back ("L"); break;                                    // л
            case 0x43C: out.push_back ("M"); break;                                    // м
            case 0x43D: out.push_back ("N"); break;                                    // н
            case 0x43F: out.push_back ("P"); break;                                    // п
            case 0x440: out.push_back ("R"); break;                                    // р
            case 0x441: out.push_back ("S"); break;                                    // с
            case 0x442: out.push_back ("T"); break;                                    // т
            case 0x444: out.push_back ("F"); break;                                    // ф
            case 0x445: out.push_back ("X"); break;                                    // х
            case 0x446: out.push_back ("TS"); break;                                   // ц
            case 0x447: out.push_back ("CH"); break;                                   // ч
            case 0x448: out.push_back ("SH"); break;                                   // ш
            case 0x449: out.push_back ("SH"); out.push_back ("SH"); break;             // щ
            default: break;                                                            // ь ъ и прочее
        }
    }
    return out;
}

const char32_t* enDigit[10] = { U"zero", U"one", U"two", U"three", U"four", U"five", U"six", U"seven", U"eight", U"nine" };
const char32_t* ruDigit[10] = { U"ноль", U"один", U"два", U"три", U"четыре", U"пять", U"шесть", U"семь", U"восемь", U"девять" };

std::string toAscii (const Str& s)
{
    std::string r;
    for (auto c : s) if (c < 128) r.push_back ((char) c);
    return r;
}
} // namespace

std::shared_ptr<const Utterance> buildUtterance (const std::string& utf8, std::string* debug)
{
    Str txt = decodeUtf8 (utf8);
    bool anyCyr = false;
    for (auto& c : txt) { c = lowerC (c); if (isCyr (c)) anyCyr = true; }

    auto u = std::make_shared<Utterance>();
    auto dbg = [&] (const Toks& t, const char* tag)
    {
        if (! debug) return;
        for (auto& x : t) { *debug += x; *debug += ' '; }
        *debug += tag;
    };
    auto pause = [&] (float ms)
    {
        if (! u->segs.empty())
            u->segs.push_back (mk (500, 1500, 2500, 500, 1500, 2500, 0, 0, 0, 0, false, ms));
    };
    auto addWord = [&] (const Toks& t)
    {
        u->wordStart.push_back (u->segs.size());
        for (auto& x : t) appendPhoneme (x, u->segs);
        pause (45);
        dbg (t, "| ");
    };

    size_t i = 0;
    while (i < txt.size())
    {
        const char32_t c = txt[i];
        if (isLat (c) || isCyr (c) || c == '\'')
        {
            Str w;
            bool hasCyr = false;
            size_t j = i;
            while (j < txt.size() && (isLat (txt[j]) || isCyr (txt[j]) || txt[j] == '\''))
            {
                if (txt[j] != '\'') { w.push_back (txt[j]); if (isCyr (txt[j])) hasCyr = true; }
                ++j;
            }
            Toks t;
            if (hasCyr) { Str cy; for (auto ch : w) if (isCyr (ch)) cy.push_back (ch); t = russianG2P (cy); }
            else t = englishG2P (toAscii (w));
            addWord (t);
            i = j;
            continue;
        }
        if (c >= '0' && c <= '9')
        {
            Str w = anyCyr ? Str (ruDigit[c - '0']) : Str (enDigit[c - '0']);
            addWord (anyCyr ? russianG2P (w) : englishG2P (toAscii (w)));
            ++i;
            continue;
        }
        if (c == '{')
        {
            size_t j = i + 1;
            std::string cur;
            Toks t;
            while (j < txt.size() && txt[j] != '}')
            {
                char32_t ch = txt[j];
                if (ch >= 'a' && ch <= 'z') cur.push_back ((char) (ch - 32));
                else if (! cur.empty()) { t.push_back (cur); cur.clear(); }
                ++j;
            }
            if (! cur.empty()) t.push_back (cur);
            addWord (t);
            i = (j < txt.size()) ? j + 1 : j;
            continue;
        }
        if (c == ',' || c == ';' || c == ':') pause (130);
        else if (c == '.' || c == '!' || c == '?') pause (240);
        ++i;
    }
    u->wordStart.push_back (u->segs.size());
    return u;
}
} // namespace ihm
