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
// here at the source: every file under core/ui, core/product, products and
// modules/*/panel is read, and the suite fails on
//
//   - a `static` (or `thread_local`) variable whose declaration names
//     `juce::Image`, at any scope -- `static juce::Image tile`, and equally a
//     static container of them, `static std::map<juce::String, juce::Image>`;
//   - a namespace-scope variable whose declaration names `juce::Image`,
//     static or not.
//
// A data member of a class, a local, and a function that returns an image are
// all fine. This is a reader, not a compiler: it strips comments and literals,
// tracks which braces are namespaces, and takes `name (` at namespace or class
// scope to be a function unless an argument is a bare literal. It matches the
// spelling `juce::Image` only -- nothing in the tree uses `using namespace
// juce` -- and it reads the declared type, left of any `=`, never the
// initialiser, so it cannot see through `auto`: `static auto tile = make();`
// passes. Do not write that either. The self-test below pins what it does and
// does not flag, so a reader that silently stopped matching fails here rather
// than passing every tree.
//
// JUCE-free on purpose: a source scan needs nothing but the standard library,
// and builds in seconds wherever the suite does.

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
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

const std::regex& imageType()
{
    // juce::Image as a type: not juce::Image::ARGB, not juce::ImageCache.
    static const std::regex re (R"(\bjuce::Image\b(?!\s*::))");
    return re;
}

bool hasWord (const std::string& s, const char* word)
{
    return std::regex_search (s, std::regex (std::string ("\\b") + word + "\\b"));
}

/** Whether a declaration at namespace or class scope is a function rather
    than a variable: it has `name (`, and nothing between the parentheses is a
    bare literal, which a parameter list never is. */
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

    for (std::string piece; std::getline (args, piece, ',');)
        if (std::regex_match (piece, literal))
            return false;

    return true;
}

bool offends (const std::string& stmt, Scope scope)
{
    // What is declared is left of the first `=`; the initialiser is not read,
    // so a lambda at namespace scope that takes an image is not an image.
    const auto decl = stmt.substr (0, stmt.find ('='));

    if (! std::regex_search (decl, imageType()))
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
    std::vector<fs::path> dirs { root / "core" / "ui", root / "core" / "product", root / "products" };

    for (const auto& module : fs::directory_iterator (root / "modules"))
        if (module.is_directory() && fs::is_directory (module.path() / "panel"))
            dirs.push_back (module.path() / "panel");

    int files = 0;
    auto sawLookAndFeel = false;

    for (const auto& dir : dirs)
    {
        check (fs::is_directory (dir), "missing directory " + dir.generic_string());

        if (! fs::is_directory (dir))
            continue;

        for (const auto& entry : fs::recursive_directory_iterator (dir))
        {
            const auto ext = entry.path().extension().string();

            if (! entry.is_regular_file() || (ext != ".h" && ext != ".hpp" && ext != ".cpp" && ext != ".mm"))
                continue;

            ++files;
            const auto rel = fs::relative (entry.path(), root).generic_string();
            sawLookAndFeel = sawLookAndFeel || rel == "core/ui/LookAndFeel.cpp";

            for (const auto& f : scan (readFile (entry.path())))
                check (false, rel + ":" + std::to_string (f.line) + ": static or namespace-scope juce::Image: " + f.text);
        }
    }

    // Absolutes, so a scan that found nothing because it read nothing fails.
    check (sawLookAndFeel, "core/ui/LookAndFeel.cpp was not scanned");
    check (files >= 60, "only " + std::to_string (files) + " source files scanned, expected at least 60");

    std::cout << files << " files scanned, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
