// No `juce::Image` with static storage in the plugin's UI code.
//
// This suite exists for a single fault, found on ICE QUEEN on 2026-10-02. Three
// function-local statics in core/ui/LookAndFeel.cpp -- the two Textured plate
// tiles and the Textured knob's layer cache -- held native images. A native
// image keeps the graphics framework's shared device objects alive, so with
// the Textured surface on they outlived every editor, and releasing them at
// DLL unload hung the host: a validator run in-process printed SUCCESS and then
// never exited (exit 124 under a 60 s timeout, against 4-5 s with the surface
// set to Simple). An image belongs to something whose lifetime ends with the
// last editor -- a component, the look and feel, or a holder behind
// `juce::SharedResourcePointer` -- never to the process.
//
// The fault needs a loaded plugin to show, which ctest cannot do, so it is held
// here at the source: every file under core/ui, core/product, core/rack,
// products and modules/*/panel is read, and the suite fails on
//
//   - a `static` (or `thread_local`) variable whose declaration names an
//     image or an owner of images, at any scope -- `static juce::Image tile`,
//     and equally a static container of them,
//     `static std::map<juce::String, juce::Image>`;
//   - a namespace-scope variable whose declaration names one, static or not.
//
// The owners are the ones that would carry an image past the last editor
// just as well: `juce::SharedResourcePointer` (whatever it shares), the
// holder `MaterialImages`, `BmoLookAndFeel`, any `juce::LookAndFeel*`, and
// every class in the scanned sources that derives from a look and feel.
//
// A data member of a class, a local, a class's own declaration, and a
// function that returns or takes one are all fine. This is a reader, not a
// compiler: it strips comments and literals, tracks which braces are
// namespaces, and takes `name (` at namespace or class scope to be a function
// unless an argument is an expression -- a literal, a call, a string, member
// access or an operator. What it still cannot see:
//
//   - through `auto`. It reads the declared type, left of any `=`, never the
//     initialiser, so `static auto tile = make();` and
//     `static auto look = std::make_unique<BmoLookAndFeel>();` both pass.
//     Do not write either.
//   - a namespace-scope direct-init whose arguments are bare names,
//     `juce::Image tile (other);` -- that is a function declaration to a
//     reader that does not know whether `other` is a type.
//   - any spelling but the ones above: `juce::Image` only, since nothing in
//     the tree uses `using namespace juce`; and an owner of some other kind,
//     a struct with an image member, held in a static.
//
// The self-test below pins what it does and does not flag, so a reader that
// silently stopped matching fails here rather than passing every tree.
//
// JUCE-free on purpose: a source scan needs nothing but the standard library,
// and builds in seconds wherever the suite does.

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

int failures = 0;

void check (bool ok, const std::string& what)
{
    if (ok)
        return;

    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

/** The source with comments, string and character literals and preprocessor
    lines blanked out, newlines kept so line numbers still hold. */
std::string strip (const std::string& in)
{
    std::string out;
    out.reserve (in.size());

    const auto n = in.size();
    auto atLineStart = true;

    for (size_t i = 0; i < n; ++i)
    {
        const auto c = in[i];
        const auto next = i + 1 < n ? in[i + 1] : '\0';

        if (c == '\n')
        {
            out += c;
            atLineStart = true;
            continue;
        }

        // A preprocessor line, with its continuations.
        if (atLineStart && c == '#')
        {
            while (i < n && ! (in[i] == '\n' && in[i - 1] != '\\'))
            {
                if (in[i] == '\n')
                    out += '\n';
                ++i;
            }

            --i;
            continue;
        }

        if (c != ' ' && c != '\t' && c != '\r')
            atLineStart = false;

        if (c == '/' && next == '/')
        {
            while (i < n && in[i] != '\n')
                ++i;

            --i;
            continue;
        }

        if (c == '/' && next == '*')
        {
            i += 2;

            while (i + 1 < n && ! (in[i] == '*' && in[i + 1] == '/'))
            {
                if (in[i] == '\n')
                    out += '\n';
                ++i;
            }

            ++i;
            out += ' ';
            continue;
        }

        // A digit separator, 1'000, is not a character literal.
        if (c == '\'' && i > 0 && std::isalnum ((unsigned char) in[i - 1]) && std::isalnum ((unsigned char) next))
            continue;

        if (c == '"' || c == '\'')
        {
            for (++i; i < n && in[i] != c; ++i)
            {
                if (in[i] == '\\')
                    ++i;
                else if (in[i] == '\n')
                    out += '\n';
            }

            out += c;
            out += c;
            continue;
        }

        out += c;
    }

    return out;
}

enum class Scope { space, type, body };

/** Every class in the scanned sources that derives from a look and feel,
    learned as the files are read (`learnLooks`). A static instance of one
    holds whatever images it holds as surely as a static image does. */
std::set<std::string> derivedLooks;

/** The types whose static instance outlives the last editor with images in
    it: the image; the holder a look and feel shares its images through, and
    that holder's own type; and every look and feel, JUCE's, ours, and any
    class derived from one. As types, not scopes: not juce::Image::ARGB, not
    BmoLookAndFeel::textured, and not juce::ImageCache. */
std::regex ownerPattern()
{
    std::string names = R"(juce::Image|juce::SharedResourcePointer|juce::LookAndFeel\w*|BmoLookAndFeel|MaterialImages)";

    for (const auto& name : derivedLooks)
        names += "|" + name;

    return std::regex ("\\b(" + names + ")\\b(?!\\s*::)");
}

std::regex owners = ownerPattern();

void learnLooks (const std::string& stripped)
{
    static const std::regex derived (R"(\b(?:class|struct)\s+(\w+)(?:\s+final)?\s*:[^;{]*LookAndFeel)");
    const auto before = derivedLooks.size();

    for (std::sregex_iterator it (stripped.begin(), stripped.end(), derived), end; it != end; ++it)
        derivedLooks.insert ((*it)[1].str());

    if (derivedLooks.size() != before)
        owners = ownerPattern();
}

bool hasWord (const std::string& s, const char* word)
{
    return std::regex_search (s, std::regex (std::string ("\\b") + word + "\\b"));
}

/** Whether a declaration at namespace or class scope is a function rather
    than a variable: it has `name (`, and nothing between the parentheses is
    an expression, which a parameter list never holds -- a bare literal, a
    call, a string, member access or an operator. Default arguments never get
    this far: the declaration is cut at the first `=`. */
bool declaresFunction (const std::string& decl)
{
    static const std::regex call (R"(([A-Za-z_]\w*|operator\s*\S+)\s*\()");
    std::smatch m;

    if (! std::regex_search (decl, m, call))
        return false;

    const auto paren = (size_t) (m.position (0) + m.length (0) - 1);
    const auto close = decl.find (')', paren);
    std::stringstream args (decl.substr (paren + 1, close == std::string::npos ? std::string::npos : close - paren - 1));
    static const std::regex literal (R"(^\s*([-+]?[0-9][\w.']*|true|false|nullptr|""|'')\s*$)");
    static const std::regex expression (R"([("'+\-/%|^?]|\w\.\w)");

    for (std::string piece; std::getline (args, piece, ',');)
        if (std::regex_match (piece, literal) || std::regex_search (piece, expression))
            return false;

    return true;
}

bool offends (const std::string& stmt, Scope scope)
{
    // What is declared is left of the first `=`; the initialiser is not read,
    // so a lambda at namespace scope that takes an image is not an image.
    const auto decl = stmt.substr (0, stmt.find ('='));

    if (! std::regex_search (decl, owners))
        return false;

    // A class's own declaration or definition -- `class MaterialImages;`,
    // `struct Look final : juce::LookAndFeel_V4 {` -- declares no variable.
    static const std::regex typeDecl (R"(^\s*((public|private|protected)\s*:\s*)*(class|struct|union|enum|template)\b)");

    if (std::regex_search (decl, typeDecl))
        return false;

    const auto isStatic = hasWord (decl, "static") || hasWord (decl, "thread_local");

    if (scope == Scope::body)
        return isStatic;

    if (hasWord (decl, "using") || hasWord (decl, "typedef") || hasWord (decl, "friend"))
        return false;

    if (declaresFunction (decl))
        return false;

    return scope == Scope::space || isStatic;
}

struct Finding
{
    int line;
    std::string text;
};

std::vector<Finding> scan (const std::string& source)
{
    const auto text = strip (source);
    learnLooks (text);
    std::vector<Finding> found;
    std::vector<Scope> scopes;
    std::string stmt;
    int line = 1, stmtLine = 1;

    const auto current = [&] { return scopes.empty() ? Scope::space : scopes.back(); };

    const auto judge = [&]
    {
        if (offends (stmt, current()))
        {
            auto shown = std::regex_replace (stmt, std::regex (R"(\s+)"), " ");
            shown.erase (shown.find_last_not_of (' ') + 1);
            found.push_back ({ stmtLine, shown.substr (shown.find_first_not_of (' ')) });
        }
    };

    for (const auto c : text)
    {
        if (c == '\n')
            ++line;

        if (c == ';')
        {
            judge();
            stmt.clear();
            continue;
        }

        if (c == '{')
        {
            judge();

            auto kind = Scope::body;

            if (hasWord (stmt, "namespace") || hasWord (stmt, "extern"))
                kind = Scope::space;
            else if (current() != Scope::body
                     && stmt.find ('(') == std::string::npos && stmt.find ('=') == std::string::npos
                     && (hasWord (stmt, "class") || hasWord (stmt, "struct")
                         || hasWord (stmt, "union") || hasWord (stmt, "enum")))
                kind = Scope::type;

            scopes.push_back (kind);
            stmt.clear();
            continue;
        }

        if (c == '}')
        {
            if (! scopes.empty())
                scopes.pop_back();

            stmt.clear();
            continue;
        }

        if (stmt.find_first_not_of (" \t\r\n") == std::string::npos && ! std::isspace ((unsigned char) c))
            stmtLine = line;

        stmt += c;
    }

    return found;
}

std::string readFile (const fs::path& p)
{
    std::ifstream in (p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/** What the reader flags and what it leaves alone, each pinned by count. */
void selfTest()
{
    const auto saved = derivedLooks;
    const auto flags = [] (const char* what, const std::string& src, size_t expected)
    {
        const auto got = scan (src).size();
        check (got == expected, std::string ("self-test, ") + what + ": flagged " + std::to_string (got)
                                    + ", expected " + std::to_string (expected));
    };

    // The three forms that hung the host.
    flags ("static local tile", "namespace m { const juce::Image& tile() { static const juce::Image t = [] { "
                                "juce::Image img (juce::Image::ARGB, 4, 4, true); return img; }(); return t; } }", 1);
    flags ("static local cache", "void C::paint() { static std::map<juce::String, juce::Image> cache; }", 1);
    flags ("static local, direct-init", "void f() { static juce::Image t (juce::Image::ARGB, 1, 1, true); }", 1);

    // Namespace scope and static members.
    flags ("namespace-scope image", "namespace a { namespace { juce::Image tile; } }", 1);
    flags ("namespace-scope, direct-init", "juce::Image tile (juce::Image::ARGB, 1, 1, true);", 1);
    flags ("namespace-scope, brace-init", "const juce::Image tile { };", 1);
    flags ("namespace-scope container", "std::vector<juce::Image> tiles;", 1);
    flags ("static data member", "class C { static juce::Image tile; };", 1);
    flags ("static member, defined", "juce::Image C::tile;", 1);
    flags ("thread_local", "void f() { thread_local juce::Image t; }", 1);
    flags ("namespace-scope, direct-init from a call", "juce::Image tile (makeTile());", 1);
    flags ("namespace-scope, direct-init from a member", "const juce::Image tile (source.tile);", 1);

    // The owners of images: the shared holder, its type, and the look and feel.
    flags ("static holder pointer", "void f() { static juce::SharedResourcePointer<MaterialImages> keep; }", 1);
    flags ("namespace-scope holder pointer", "namespace { juce::SharedResourcePointer<Holder> keep; }", 1);
    flags ("static holder", "static MaterialImages images;", 1);
    flags ("static look and feel", "namespace a { static bmo::ui::BmoLookAndFeel look; }", 1);
    flags ("static JUCE look and feel", "void f() { static juce::LookAndFeel_V4 look; }", 1);
    flags ("static derived look and feel", "struct SelfTestLook final : juce::LookAndFeel_V4 { int x; }; "
                                           "void f() { static SelfTestLook look; }", 1);

    // What is fine.
    flags ("data member", "class C : public juce::Component { juce::Image plateCache; std::map<int, juce::Image> m; };", 0);
    flags ("local", "void f() { juce::Image img (juce::Image::ARGB, 1, 1, true); auto g = juce::Image(); }", 0);
    flags ("function returning one", "static juce::Image make (int w, int h); juce::Image draw() { return {}; }", 0);
    flags ("static member function", "struct S { static juce::Image render (const S&); static const juce::Image& tile(); };", 0);
    flags ("default argument", "static juce::Image make (int w = 4);", 0);
    flags ("lambda taking one", "namespace a { const auto f = [] (const juce::Image& i) { return i.getWidth(); }; }", 0);
    flags ("format constant", "void f() { static constexpr auto fmt = juce::Image::ARGB; }", 0);
    flags ("alias", "using Layers = std::map<juce::String, juce::Image>;", 0);
    flags ("other types", "static juce::ImageCache* c; static juce::String imageName;", 0);
    flags ("in a comment", "// static juce::Image tile;\n/* static juce::Image t; */", 0);
    flags ("in a string", "static const char* s = \"static juce::Image tile;\";", 0);
    flags ("behind a preprocessor line", "#define X static juce::Image tile;\nint y;", 0);
    flags ("holder as a member", "class SelfTestHolderLook : public juce::LookAndFeel_V4 { juce::SharedResourcePointer<MaterialImages> materials; };", 0);
    flags ("holder as a local", "void p() { const juce::SharedResourcePointer<MaterialImages> images; }", 0);
    flags ("look and feel as a member", "class E { ui::BmoLookAndFeel lookAndFeel; std::unique_ptr<juce::LookAndFeel_V4> menu; };", 0);
    flags ("the holder's own declarations", "class MaterialImages; class MaterialImages final { std::map<int, juce::Image> m; };", 0);
    flags ("the look and feel's own definitions", "BmoLookAndFeel::BmoLookAndFeel() { } bool BmoLookAndFeel::textured() { return true; }", 0);
    flags ("a function taking a look and feel", "static void paint (juce::Graphics&, BmoLookAndFeel& lf);", 0);

    // The self-test's own derived look and feel must not leak into the tree.
    derivedLooks = saved;
    owners = ownerPattern();
}

} // namespace

int main (int argc, char** argv)
{
    selfTest();

    if (argc < 2)
    {
        std::cerr << "usage: ui_static_image_tests <source root>\n";
        return 2;
    }

    const fs::path root (argv[1]);
    // Every directory that holds UI code: the shared controls and the look and
    // feel, both editors (core/product's and core/rack's), the products and
    // every module's panel.
    std::vector<fs::path> dirs { root / "core" / "ui", root / "core" / "product", root / "core" / "rack",
                                 root / "products" };

    for (const auto& module : fs::directory_iterator (root / "modules"))
        if (module.is_directory() && fs::is_directory (module.path() / "panel"))
            dirs.push_back (module.path() / "panel");

    std::vector<std::pair<std::string, std::string>> sources;   // relative path, text

    for (const auto& dir : dirs)
    {
        check (fs::is_directory (dir), "missing directory " + dir.generic_string());

        if (! fs::is_directory (dir))
            continue;

        for (const auto& entry : fs::recursive_directory_iterator (dir))
        {
            const auto ext = entry.path().extension().string();

            if (entry.is_regular_file() && (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".mm"))
                sources.emplace_back (fs::relative (entry.path(), root).generic_string(), readFile (entry.path()));
        }
    }

    // Two passes: every derived look and feel is learned before any file is
    // judged, so a static one is caught whichever file declares the class.
    for (const auto& source : sources)
        learnLooks (strip (source.second));

    const auto files = (int) sources.size();
    auto sawLookAndFeel = false, sawRackEditor = false;

    for (const auto& [rel, text] : sources)
    {
        sawLookAndFeel = sawLookAndFeel || rel == "core/ui/LookAndFeel.cpp";
        sawRackEditor  = sawRackEditor  || rel == "core/rack/RackEditor.h";

        for (const auto& f : scan (text))
            check (false, rel + ":" + std::to_string (f.line) + ": static or namespace-scope image or image owner: " + f.text);
    }

    // Absolutes, so a scan that found nothing because it read nothing fails.
    check (sawLookAndFeel, "core/ui/LookAndFeel.cpp was not scanned");
    check (sawRackEditor, "core/rack/RackEditor.h was not scanned");
    check (files >= 60, "only " + std::to_string (files) + " source files scanned, expected at least 60");

    std::cout << files << " files scanned, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
