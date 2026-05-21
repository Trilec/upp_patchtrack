U++_Coding_Standards.md — Norms for Readable, Safe, Idiomatic U++ (AI‑first)

VERSION BASELINE
- Target: U++ 2025.1 (build 17799+) and TheIDE.
- Applies to Core, Draw, CtrlCore, CtrlLib, standard plugins.

SCOPE
- What the code should look like: naming, layout, ownership, error policy, events/callbacks, threading, drawing & theming, serialization, networking, geometry, build hygiene.
- Rich examples to minimize ambiguity for AI. This file is normative for style; API walkthroughs live in the Cookbook.

PRINCIPLES
- Value semantics first; explicit ownership when needed.
- Deterministic lifetimes and RAII everywhere.
- GUI work only on the GUI thread; clean handoff from workers.
- Theme‑aware rendering with Chameleon; accessibility by default.
- Prefer U++ facilities over external libs; keep deps minimal.

-------------------------------------------------------------------------------
1) NAMING & PROJECT LAYOUT
-------------------------------------------------------------------------------
Naming
- Classes/structs: PascalCase (e.g., ImageCache, JsonReader).
- Methods/functions: camelCase (e.g., loadFromFile, setSizeHint).
- Variables: camelCase (e.g., bufferLen, socket).
- Constants/enum values: UPPER_SNAKE (e.g., MAX_RETRIES, ALIGN_CENTER).
- Templates/traits/types: PascalCase (e.g., OwnerMap<T>).
- Events/callbacks exposed by controls: WhenX (WhenAction, WhenDrop, WhenZoom...).
- Accessors: SetX/GetX; controls surface state with SetData/GetData.
- File names: match the primary class (MainWin.h/.cpp). Avoid multi‑class files unless tiny.

Files & Includes
- One public class per header where practical; private helpers in .cpp.
- Use #pragma once in headers (preferred).
- Public headers: include only what declarations require; forward declare otherwise.
- Examples must name canonical headers explicitly: <Core/Core.h>, <Draw/Draw.h>, <CtrlCore/CtrlCore.h>, <CtrlLib/CtrlLib.h>.

Directories (recommended)
- /lib      — library packages (no main/WinMain; omit mainconfig unless you have a specific build-system reason).
- /examples — minimal, compiling snippets for docs/tests.
- /docs     — these standards, cookbook, compressed API.
- /res      — layouts (.lay), images (.iml), fonts.

  
#### .upp Package File (quick rules)

Location: root of the package directory.
uses: list package dependencies (e.g., Core, CtrlLib, your other packages).
file: list your .cpp, .h, .lay files.
include: add extra include search roots (optional; keep it small).

#### mainconfig:

"" = "GUI" or "CONSOLE" for an EXE (main package),

Prefer omitting `mainconfig` entirely for a non-main library package.

NOTE: No comments are allowed in .upp.

7) Connecting .upp, .h, and .lay (if you use Layout Designer)

Common pitfall: LAYOUTFILE path must be relative to .upp’s include paths, not the filesystem or package name.

If your .upp has:

include
    /ui;

and your layout is ui/MyLayout.lay, then in the header:

#define LAYOUTFILE <MyLayout.lay>
#include <CtrlCore/lay.h>

Incorrect (will double-prepend):
#define LAYOUTFILE <MyPackage/ui/MyLayout.lay>

8) Build workflow tips & pitfalls

Missing package on build: check Assembly → Package nests; they must contain your repo root and uppsrc.
Do not prefix assembly name in uses (uses GalleryCtrl, not uses myassembly/GalleryCtrl).

Undefined WinMain: you built a library as if it were a main app. Either Compile it, or build a demo as the main package.

PNG/JPG not loading: add plugin/png (and plugin/jpg) to the main app uses.

THISBACK macro errors: make sure the callback target is a method of the current class and you included the header where it’s declared.

Header discovery: if you use #include <GalleryCtrl/GalleryCtrl.h>, make sure the package folder name is GalleryCtrl and it is inside a nest path.

-------------------------------------------------------------------------------
2) OWNERSHIP & LIFETIME
-------------------------------------------------------------------------------
- Default to value objects with clear scope.
- Unique heap ownership: One<T>. Do not share One<T>.
- Shared/observed lifetime: Ptr<>/Pte<>. Prefer Ptr<> when multiple owners/observers exist.
- Avoid raw new/delete in user code; prefer automatic storage, One<>, or factory helpers that return by value.
- Pass small/cheap types by value; otherwise const T&. Return by value (NRVO friendly).
- Never store references/pointers to stack temporaries or child control internals.
- Callbacks capturing this: prefer value captures; if lifetime is uncertain, capture Ptr<> to guard.
- Global objects: avoid. If unavoidable, ensure thread‑safe init and bounded lifetime.

Common lifetime patterns
- Parent owns child Ctrls (no delete required). Do not hold raw pointers to parent beyond scope.
- TimeCallback/Timer: own in the control/window; cancel in destructor; keep callbacks short; never assume they stop themselves.

-------------------------------------------------------------------------------
3) ERROR POLICY, ASSERTS & LOGGING
-------------------------------------------------------------------------------
- Routine failures: return status or error objects; reserve exceptions for unrecoverable or boundary translation.
- Use ASSERT/ASSERT_ in debug builds for programmer errors.
- Operational errors: log and surface to user (PromptOK/Exclamation as appropriate).
- Logging macros: LOG (release‑capable), DLOG/LLOG/RLOG for module‑scoped or debug. Initialize logging early (StdLogSetup in app startup).
- Destructors never throw; keep exception boundaries narrow and documented.

-------------------------------------------------------------------------------
4) THREADING & GUI BOUNDARY
-------------------------------------------------------------------------------
- Never touch GUI from worker threads.
- Handoff to GUI thread with PostCallback. If absolutely necessary to touch GUI from workers, wrap the minimal section with GuiLock.
- Own and stop timers on the GUI thread; cancel in destructors.
- Long operations: move to worker (Thread::Start/Lambda) and report progress via callbacks.

-------------------------------------------------------------------------------
5) CALLBACKS & EVENTS
-------------------------------------------------------------------------------
- Expose control actions via WhenX members (e.g., WhenAction, WhenSelect, WhenDrop).
- Bind member methods with THISBACK/THISBACK1..; ensure `typedef MyClass CLASSNAME;` is present.
- Document callback semantics: firing context (GUI thread), frequency, ownership/lifetime expectations.
- Avoid long‑running work inside callbacks; defer via PostCallback or workers.

Example — wiring a control pair
```
zoom.MinMax(0, 6).SetData(2);
zoom.WhenAction << [=]{ gallery.SetZoomIndex((int)~zoom); };
gallery.WhenZoom  << [=](int zi){ zoom <<= zi; };
```

-------------------------------------------------------------------------------
6) DRAWING, COLORS, FONTS & METRICS
-------------------------------------------------------------------------------
- Theme‑aware colors only: use SColor... / AColor...; never hard‑code RGBs that break dark mode.
- Paint must be stateless: read state, draw accordingly; no state mutation during Paint.
- Always balance Draw state: every Clipoff/Begin/Intersect has a matching End.
- Text measurement: compute Size with GetTextSize before drawing; avoid truncation; use DrawTextEllipsis or tooltips when needed.
- Image alpha discipline: before composing on a fresh ImageBuffer, reset alpha
  `Fill(~ib, RGBAZero(), ib.GetLength());`
- Prefer Painting/BufferPainter for vector/antialiased output; ImageDraw for raster composition.
- Prefer shared scroll/input primitives before custom-painted one-offs; if custom behavior is required, keep it style-tokenized and measured for performance.

-------------------------------------------------------------------------------
7) CHAMELEON THEMING (POLICY FOR CONTROLS)
-------------------------------------------------------------------------------
- Separate behavior from appearance; render via standardized Chameleon paint fns (ChPaint/ChPaintEdge/ChPaintBody).
- Define styles with CH_STYLE; styles are static (no dynamic allocation). Controls store a non‑owning pointer to style.
- Provide SetStyle(const Style&) so apps can theme at runtime; call Refresh() when style changes.
- Event handlers (MouseEnter/Leave, Got/LostFocus, LeftDown/Up) update state + Refresh() only; do not draw there.
- In Paint: resolve state (NORMAL/HOT/PUSHED/DISABLED), delegate to ChPaint, then draw custom content (text/icon) using style‑provided Font/Color.

Minimal themable control skeleton
```
class MyCtrl : public Ctrl {
public:
    typedef MyCtrl CLASSNAME;
    MyCtrl() { style = &StyleDefault(); }
    MyCtrl& SetStyle(const Style& s) { style = &s; Refresh(); return *this; }
    virtual void Paint(Draw& w) override {
        Size sz = GetSize();
        int state = !IsShowEnabled() ? CTRL_DISABLED : (HasMouse()||HasFocus() ? CTRL_HOT : CTRL_NORMAL);
        bool pushed = HasCapture();
        ChPaint(w, sz, *style, state, pushed);
        String text = "My Control";
        Size tsz = GetTextSize(text, style->font);
        w.DrawText((sz.cx - tsz.cx)/2, (sz.cy - tsz.cy)/2, text, style->font, style->ink[state]);
    }
protected:
    const Style *style = nullptr; // non‑owning
};
```
Minimal themable control skeleton
```
class MyCtrl : public Ctrl {
public:
    typedef MyCtrl CLASSNAME;
    MyCtrl() { style = &StyleDefault(); }
    MyCtrl& SetStyle(const Style& s) { style = &s; Refresh(); return *this; }
    virtual void Paint(Draw& w) override {
        Size sz = GetSize();
        int state = !IsShowEnabled() ? CTRL_DISABLED : (HasMouse()||HasFocus() ? CTRL_HOT : CTRL_NORMAL);
        bool pushed = HasCapture();
        ChPaint(w, sz, *style, state, pushed);
        String text = "My Control";
        Size tsz = GetTextSize(text, style->font);
        w.DrawText((sz.cx - tsz.cx)/2, (sz.cy - tsz.cy)/2, text, style->font, style->ink[state]);
    }
protected:
    const Style *style = nullptr; // non owning
};
```
Example for new controls requiring theaming:
```
class MyCtrl : public ParentCtrl {
public:
    typedef MyCtrl CLASSNAME;
    enum class Lock  { None, Open, Closed };
    enum class Align { Left, Center, Right };
    enum class State { Normal, Hover, Pressed, Disabled };

    // -----------------------------------------------------------------------
    // UiPalette and UiMetrics
    // -----------------------------------------------------------------------
    struct UiPalette : Moveable<UiPalette> {
        // Header / Body backgrounds can be flat (Bg1 only) or two-color (Bg1,Bg2)
        Value headerBg1, headerBg2;
        Value bodyBg1,   bodyBg2;

        // Inks
        Value headerInk;         // title ink
        Value headerInkHover;    // title ink when hot/pressed
        Value glyphInk;          // glyph ink (used only for text glyphs; Image glyphs are drawn as-is)

        // Lines/frames
        Value divider;           // header bottom divider (Null -> none)
        Value outerBorder;       // optional outside border (Null -> none fill)

        // Special fills
        Color headerHoverBg = SColorHighlight();
    };

    struct UiMetrics : Moveable<UiMetrics> {
        int   headerHeight   = -1;        // px; -1 -> derive from font
        int   paddingX       = 6;         // internal X padding in header
        int   paddingY       = 4;         // internal Y padding in header
        int   cornerRadius   = 0;         // reserved for painter use
        int   borderPx       = 0;         // reserved for painter use

        Align iconAlignDefault   = Align::Left;   // hamburger + open/close group
        Align titleAlignDefault  = Align::Left;   // title block inside text lane

        int   iconTextGap    = 6;         // gap between icons and title
        int   iconPx         = 16;        // nominal glyph cell size (for text glyphs)
        bool  iconBold       = false;     // emphasize glyphs when using text glyphs

        Font  headerFont     = StdFont();
        Font  glyphFont      = StdFont(); // used for text glyphs only
    };
```

-------------------------------------------------------------------------------
8) DRAG AND DROP & CLIPBOARD (POLICY)
-------------------------------------------------------------------------------
- Use AcceptFiles/AcceptImage/AcceptText in DragAndDrop to signal hover acceptance; set d.SetAction(DND_COPY or DND_MOVE) accordingly; call Refresh() for hover feedback.
- In Drop, read data with GetFiles/GetImage/GetString.
- For in‑process payloads, prefer InternalClip + AcceptInternal<T>/GetInternal<T>.
- Keep producers/consumers separate; avoid hidden global state.

-------------------------------------------------------------------------------
9) DATA: JSON, XML, SERIALIZATION
-------------------------------------------------------------------------------
- JSON: prefer typed Jsonize for structs/classes; only use dynamic Value/ValueMap for genuinely dynamic shapes.
- XML: build with XmlNode; parse with ParseXML/XmlParser; avoid blocking parses on GUI thread.
- Streams/Serialize: prefer U++ Stream for persistence; version serialized formats; keep DTOs small and explicit.

-------------------------------------------------------------------------------
10) NETWORKING (HTTP/TCP/POP3/XML RPC)
-------------------------------------------------------------------------------
- Keep sockets/HTTP/WebSocket code off the GUI thread.
- Use HttpRequest for client work: headers, cookies, UrlVar, Post fields/parts, progress callbacks (WhenStart/WhenContent/WhenDo).
- Minimal HTTP/SCGI server: TcpSocket.Listen → Accept loop → HttpHeader.Read → read body by ContentLength → HttpResponse(...).
- Use SocketWaitEvent (WAIT_READ/WRITE) for responsive loops; timeouts are mandatory.

-------------------------------------------------------------------------------
11) GEOMETRY & MATH
-------------------------------------------------------------------------------
- Centralize hit‑testing/layout math; avoid magic numbers.
- Prefer Rect/Point/Size helpers from Gtypes; use GetFitSize/GetRatioSize for scaling.
- Qualify the global clamp as ::clamp(...) to avoid ADL issues.
- Be explicit with numeric types; avoid mixed int/float in max/min without casts.

-------------------------------------------------------------------------------
12) IMAGE & RENDERING WORKFLOWS
-------------------------------------------------------------------------------
- Offscreen raster: ImageDraw compose → assign to Image → DrawImage in Paint.
- Antialiased vectors: BufferPainter(ImageBuffer, MODE_ANTIALIASED) for rasterized vector output.
- Recorded vectors: PaintingPainter + DrawPainting for resolution‑independent replay/print.
- Decoders: when calling StreamRaster::LoadFileAny in an EXE, add `uses plugin/png` and friends so decoders register at link time.

-------------------------------------------------------------------------------
13) FORMATTING & STYLE
-------------------------------------------------------------------------------
- Indentation: 4 spaces (no tabs).
- Max line length: 100–120 chars; break method chains.
- Brace: K&R (opening brace at end of line).
- One statement per line; prefer early returns.
- Order members: type/aliases → ctors/dtor → public API → protected → private.
- Keep helper functions `static` or in anonymous namespace in .cpp.

-------------------------------------------------------------------------------
14) DOCUMENTATION
-------------------------------------------------------------------------------
- File header: purpose, ownership notes, thread context.
- Public methods: concise summary + pre/postconditions as needed.
- Controls: document SetData/GetData semantics and WhenX events.
- Mark GUI‑thread requirements and any blocking operations.

-------------------------------------------------------------------------------
15) TESTS & EXAMPLES
-------------------------------------------------------------------------------
- Provide a minimal compiling example per non‑trivial module (in /examples or a demo app).
- Examples must list required headers and package `uses`; declare plugin decoders in EXE demos when loading images.
- Tests avoid GUI unless necessary; prefer fast, deterministic units.

-------------------------------------------------------------------------------
16) BUILD & PACKAGING HYGIENE (.upp, assemblies)
-------------------------------------------------------------------------------
- Library vs EXE packages: library has empty mainconfig ("" = ""); EXE sets "" = "GUI" (or "CONSOLE").
- Assembly → Package nests must include both your repo root and uppsrc. Missing nests → “missing package …”.
- In .upp: `uses` (packages only, not assembly prefixes), `file` list, optional `include` roots.
- Layout Designer: LAYOUTFILE path is relative to .upp include paths (not filesystem/package name).
- Don’t prefix assembly name in `uses`.
- If WinMain is undefined, you built a library as main; set a demo EXE as main package instead.

-------------------------------------------------------------------------------
17) REVIEW CHECKLIST (PASTE INTO PRS)
-------------------------------------------------------------------------------
[ ] Naming follows conventions; events are WhenX; control state via SetData/GetData.
[ ] Ownership explicit (value/One<>/Ptr<>); no raw new/delete; no hidden shared One<>.
[ ] GUI boundary respected; PostCallback/GuiLock used correctly; timers owned/cancelled.
[ ] Callbacks bound via THISBACK/THISBACK1…; CLASSNAME typedef present; safe captures.
[ ] Chameleon: styles via CH_STYLE; Paint stateless; no drawing in event handlers; SetStyle provided.
[ ] Colors/theme safe (SColor/AColor). Fonts/metrics measured; ellipsis/tooltip for truncation.
[ ] Minimal includes; #pragma once; headers light.
[ ] Logging & error handling sensible; ASSERTs for programmer errors.
[ ] Examples/tests updated; .upp `uses` correct; decoders added where needed.
[ ] ::clamp qualified; geometry code centralized and readable.

-------------------------------------------------------------------------------
18) PITFALLS TO AVOID — FIELD NOTES (WITH FIXES)
-------------------------------------------------------------------------------
Ownership & Lifetime
- Holding references/pointers to temporaries or child internals → Store values; if you must observe, use Ptr<>; never keep &refs to ephemeral state.
- Sharing One<> across owners → One<> is unique. If multiple components need access, own it in one place and expose by reference or use Ptr<>/Pte<>.
- Timer firing after teardown → KillSet() timers in destructors; clear WhenX callbacks; guard bodies with `if(GetTopWindow())` before touching UI.
- Capturing raw `this` into long‑lived callbacks → Capture Ptr<> to owner or weak guard state; disconnect in dtor.
- Global Ctrl objects → Avoid; use factories or Single<> wrappers with explicit init/shutdown.

Callbacks & Events
- Missing `typedef CLASSNAME` → THISBACK won’t bind; add `typedef MyClass CLASSNAME;` in the class.
- Doing heavy work inside WhenAction/WhenDrop → PostCallback to GUI thread for UI changes; run heavy work on workers and report back.
- Re‑entrancy surprises (callback triggers another event immediately) → Use flags or PostCallback to defer; document firing order.

GUI Threading
- Touching UI from workers → Forbidden. Use PostCallback; if absolutely necessary, wrap the minimal block with GuiLock.
- Blocking GUI with Sleep or network I/O → Move to worker; keep UI responsive.

Painting & Chameleon
- Drawing in event handlers (LeftDown/MouseMove) → Only update state + Refresh(); all drawing happens in Paint.
- Hardcoded colors/images not aware of dark mode → Use SColor.../AColor... and CH_STYLE; let Chameleon resolve.
- Unbalanced Begin/End/Clipoff → Always pair; prefer small scopes; assert with debug helpers if needed.
- Forgetting to clear alpha of fresh ImageBuffer → `Fill(~ib, RGBAZero(), ib.GetLength());` before composition.

Layouts, Packaging & Build
- LAYOUTFILE uses filesystem path → Use the `.upp` include root. See Section 1 examples.
- Prefixing assembly in `uses` → Only package names belong in `uses`.
- Building a lib as the main app (Undefined WinMain) → Set a demo EXE as main or set mainconfig to GUI/CONSOLE appropriately.
- Missing image decoders → Add plugin/png (and plugin/jpg, plugin/bmp) in the EXE `uses`.
- Package not found at build → Ensure Assembly → Package nests include both repo root and uppsrc.

Data, Streams & Serialization
- Ad hoc JSON with Value/ValueMap everywhere → Prefer typed Jsonize; dynamic only where shape truly varies.
- Blocking parse/serialize on GUI thread → Move to worker; update UI via PostCallback.
- Unversioned serialized formats → Add explicit version fields; write upgrade paths.

Strings & Encoding
- Confusing String/WString usage → Choose one per API boundary; convert explicitly at edges; avoid silent narrowing.
- Assuming String cannot contain NUL → It can; safe for binary; still document when binary is expected.

Networking
- HttpRequest without timeouts → Set Timeout(); use WhenStart/WhenContent for streaming; don’t hold large buffers in memory.
- Reading request bodies without checking ContentLength/chunked → Use HttpHeader helpers; always guard sizes.
- Touching UI from socket callbacks → Bounce to GUI thread with PostCallback.

Drag and Drop
- Accept* not called in DragAndDrop → Drop won’t fire; call AcceptFiles/AcceptText/AcceptImage and set d.SetAction().
- No hover feedback → Call Refresh() while hovering and paint an accepted state using d.IsAccepted().

Geometry & Math
- Off‑by‑one and negative sizes in Rect math → Use RectC/Sort and Size clamps; assert invariants; prefer helpers from Gtypes.
- Mixed int/float in comparisons → Cast explicitly; beware integer division.
- Unqualified clamp hitting ADL → Use `::clamp(...)`.

Images & Rendering
- Forgetting BufferPainter::Finish() before using the Image (when required) → Call Finish() or let scope end cleanly before using the raster.
- Drawing text at sub‑pixel integer positions → Use integer coordinates from GetTextSize to avoid blur.
- Relying on implicit premultiply state → Be explicit when mixing straight/premultiplied alpha.

Filesystem & Paths
- Using relative paths that depend on CWD → Normalize/RealizePath; base on known roots.
- Assuming case‑insensitivity on POSIX → Use PathIsEqual when comparing paths portably.

Performance & Memory
- Excessive copies of large containers/Images → Pass by const&; reserve capacity; prefer Vector over Array when possible for speed.
- Recomputing layouts/metrics in hot Paint without caching → Cache between Paints; invalidate on state changes only.

Testing & CI
- GUI tests with timers that bleed into next tests → Stop timers in teardown; use deterministic durations; isolate TopWindows.

Quick Triage Table
- “Drag works but drop never triggers” → Call Accept* in DragAndDrop and set d.SetAction().
- “Crash on exit after adding a timer” → KillSet() in dtor; clear callbacks.
- “Images load in Debug but not Release” → Missing `uses plugin/png/jpg/bmp` in main EXE.
- “THISBACK compile error” → Add `typedef CLASSNAME;` and ensure the method is a member.
- “UI freezes when fetching HTTP” → Move to worker; use HttpRequest events; update via PostCallback.

Here’s a **compile-clean** PackBuilder you can drop in as `main.cpp` (no placeholders). It scans `reference/` and `examples/`, filters by extensions, shows the file list, and builds grouped packs into an output directory (one pack per `{root}:{topdir}`).

```cpp
#include <CtrlLib/CtrlLib.h>

using namespace Upp;

struct Row : Moveable<Row> {
    String root;      // "reference" | "examples"
    String topdir;    // first directory under the root
    String relpath;   // path relative to root
    String abspath;   // absolute file path
    int    kb = 0;    // rounded size in KB
};

static String MakeRel(const String& path, const String& root)
{
    String p = NormalizePath(path);
    String r = NormalizePath(root);
    if(p.StartsWith(r)) {
        int i = r.GetCount();
        while(i < p.GetCount() && (p[i] == '/' || p[i] == '\\'))
            i++; // strip separator(s)
        return p.Mid(i);
    }
    return GetFileName(path); // fallback
}

static Vector<String> SplitCsvTrim(const String& s)
{
    Vector<String> out;
    for(String t : Split(s, ',')) {
        t = TrimBoth(t);
        if(!IsNull(t)) out.Add(t);
    }
    return out;
}

// exts: "cpp,h,lay" -> [".cpp",".h",".lay"]
static Vector<String> NormalizeExts(const WString& ws)
{
    Vector<String> out;
    for(String e : SplitCsvTrim(ws.ToString())) {
        e = ToLower(e);
        if(e[0] != '.') e.Insert(0, '.');
        out.Add(e);
    }
    return out;
}

static bool HasExtAllowed(const String& fn, const Vector<String>& allow)
{
    String e = ToLower(GetFileExt(fn)); // includes the dot
    for(const String& a : allow)
        if(e == a) return true;
    return false;
}

static String FirstComponent(const String& rel)
{
    int s1 = rel.Find('/');
    int s2 = rel.Find('\\');
    int i  = (s1 < 0) ? s2 : (s2 < 0 ? s1 : min(s1, s2));
    return i < 0 ? rel : rel.Mid(0, i);
}

struct PackBuilder : TopWindow {
    typedef PackBuilder CLASSNAME;

    ArrayCtrl   table;
    EditString  refDir, exDir, outDir, exts;
    Option      recurse;
    Button      scan, build, clr;
    Label       info;

    Vector<Row> rows;

    PackBuilder()
    {
        Title("U++ PackBuilder — Reference/Examples Concatenator");
        Sizeable().Zoomable();

        Add(table.HSizePos(8,8).VSizePos(140,40));
        table.AddColumn("Root",   90);
        table.AddColumn("TopDir", 160);
        table.AddColumn("RelPath",500);
        table.AddColumn("KB",      70)
             .SetConvert(Single<ConvertInt>())
             .SetDisplay(StdRightDisplay()); // right-align integers

        Add(refDir.LeftPos(8,  420).TopPos( 8, 24));
        Add(exDir .LeftPos(8,  420).TopPos(36, 24));
        Add(outDir.LeftPos(8,  420).TopPos(64, 24));

        Add(exts.RightPos(8,  280).TopPos( 8, 24));
        Add(recurse.RightPos(8, 280).TopPos(36, 24));
        recurse.SetLabel("Recurse subdirs").Set(true);

        Add(scan .RightPos(296, 188).BottomPos(8, 28));
        Add(build.RightPos(100,  92).BottomPos(8, 28));
        Add(clr  .RightPos(8,    92).BottomPos(8, 28));
        Add(info .LeftPos(8,    600).BottomPos(8, 28));

        refDir  <<= GetHomeDirFile("dev/ultimatepp/reference");
        exDir   <<= GetHomeDirFile("dev/ultimatepp/examples");
        outDir  <<= GetHomeDirFile("packs_out");
        exts    <<= "cpp,c,h,lay,iml,icpp,sch,usc,usc2";

        scan  << [=]{ DoScan();  };
        build << [=]{ DoBuild(); };
        clr   << [=]{ rows.Clear(); table.Clear(); info.SetText(""); };
    }

    void DoScan()
    {
        table.Clear();
        rows.Clear();

        Vector<String> allow = NormalizeExts(exts.GetText());

        ScanRoot(TrimBoth(refDir.GetText().ToString()), "reference", allow, (bool)~recurse);
        ScanRoot(TrimBoth(exDir .GetText().ToString()), "examples",  allow, (bool)~recurse);

        Sort(rows, [](const Row& a, const Row& b){
            int c = SgnCompare(a.root,   b.root);   if(c) return c < 0;
                c = SgnCompare(a.topdir, b.topdir); if(c) return c < 0;
            return SgnCompare(a.relpath, b.relpath) < 0;
        });

        for(const Row& r : rows)
            table.Add(r.root, r.topdir, r.relpath, r.kb);

        info.SetText(Format("%d files", rows.GetCount()));
    }

    void ScanRoot(const String& rootPath, const String& rootName, const Vector<String>& allow, bool do_recurse)
    {
        if(IsNull(rootPath) || !DirectoryExists(rootPath))
            return;

        Vector<String> stack;
        stack.Add(rootPath);

        while(!stack.IsEmpty()) {
            String cur = stack.Top();
            stack.Drop();

            FindFile ff;
            if(!ff.Search(AppendFileName(cur, "*")))
                continue;

            do {
                if(ff.IsFolder()) {
                    if(do_recurse)
                        stack.Add(ff.GetPath());
                }
                else if(ff.IsFile()) {
                    String ap = ff.GetPath();
                    if(!HasExtAllowed(ap, allow))
                        continue;

                    Row r;
                    r.root    = rootName;
                    r.relpath = MakeRel(ap, rootPath);
                    r.topdir  = FirstComponent(r.relpath);
                    r.abspath = ap;
                    int64 len = ff.GetLength();
                    r.kb      = (int)((len + 1023) / 1024);
                    rows.Add(pick(r));
                }
            } while(ff.Next());
        }
    }

    void DoBuild()
    {
        String outRoot = TrimBoth(outDir.GetText().ToString());
        if(IsNull(outRoot) || (!DirectoryExists(outRoot) && !DirectoryCreate(outRoot))) {
            Exclamation("Output directory is invalid.");
            return;
        }

        VectorMap<String, Vector<int>> groups; // key = "root::topdir"
        for(int i = 0; i < rows.GetCount(); ++i) {
            String k = rows[i].root + "::" + rows[i].topdir;
            int gi = groups.Find(k);
            if(gi < 0) {
                gi = groups.Add(k, Vector<int>());
            }
            groups[gi].Add(i);
        }

        int made = 0;
        for(int gi = 0; gi < groups.GetCount(); ++gi) {
            const String& k = groups.GetKey(gi);
            String key = k; key.Replace("::", "_");
            String out = AppendFileName(outRoot, key + ".txt");

            FileOut fo(out);
            if(!fo.IsOpen()) {
                Exclamation(Format("Cannot write: %s", out));
                continue;
            }

            fo.PutLine("// ===== PACK: " + key + " =====");
            fo.PutLine("// Generated by U++ PackBuilder");
            fo.PutLine("");

            const Vector<int>& idx = groups[gi];
            for(int ii = 0; ii < idx.GetCount(); ++ii) {
                const Row& r = rows[idx[ii]];
                fo.PutLine("// --- " + r.relpath + " ---");
                String data = LoadFile(r.abspath);
                fo.Put(data);
                if(data.GetCount() && data.Last() != '\n')
                    fo.Put("\n");
                fo.PutLine("\n");
            }

            fo.Close();
            ++made;
        }

        info.SetText(Format("Built %d pack(s) into: %s", made, outRoot));
        PromptOK(Format("Built %d pack(s).", made));
    }
};

GUI_APP_MAIN
{
    PackBuilder().Run();
}
```

---

## Anti-patterns & Pitfalls (compressed, generic, with fixes)

* **Non-Moveable types in U++ containers**
  *Symptom:* `static_assert(is_upp_guest<T>)` when using `Vector<T>/Array<T>`.
  *Fix:* make your type Moveable.

  ```cpp
  struct Item : Moveable<Item> { String a; int b = 0; };
  Vector<Item> v; v.Add({ "x", 1 });
  ```

* **Calling `TopWindow` APIs on a non-TopWindow**
  *Symptom:* `no known conversion ... for Title()/Add()/Sizeable()`.
  *Fix:* derive the window from `TopWindow`.

  ```cpp
  struct MainWin : TopWindow {
      typedef MainWin CLASSNAME;
      MainWin() { Title("App"); Sizeable().Zoomable(); }
  };
  ```

* **Using the wrong widget APIs**
  *Symptom:* `Set(bool)` on a `Button`, missing integer spinner types.
  *Fix:* `Option` is for checkboxes; integer spinners are `EditIntSpin`.

  ```cpp
  Option chk; chk.Set(true);
  EditIntSpin spin; spin.MinMax(0, 1000);
  ```

* **Column alignment in `ArrayCtrl`**
  *Symptom:* `Column.AlignRight()` not found.
  *Fix:* assign a right-align display for numeric text.

  ```cpp
  table.AddColumn("KB").SetConvert(Single<ConvertInt>()).SetDisplay(StdRightDisplay());
  ```

* **Sorting `ArrayCtrl` vs sorting your model**
  *Symptom:* Chaining `table.Sort(0, true).Sort(1, ...)` fails.
  *Fix:* sort your data (model) with a comparator, then load into the control.

  ```cpp
  Sort(items, [](const Item& a, const Item& b){ return a.key < b.key; });
  ```

* **`WString` → `String` conversion**
  *Symptom:* “no viable conversion from ‘WString’ to ‘String’”, or using `ToString(x)` free function.
  *Fix:* use `GetText().ToString()` or cast `~ctrl` to `String`.

  ```cpp
  String s = edit.GetText().ToString();
  // or: String s = (String)~edit;   // if edit stores a String Value
  ```

* **Single-character `String` construction**
  *Symptom:* `String('.')` fails.
  *Fix:* use a literal or `Insert`.

  ```cpp
  String s = ".";           // OK
  String t = "png"; t.Insert(0, '.'); // ".png"
  ```

* **Finding values in `Vector<T>`**
  *Symptom:* `vec.Find(x)` not found.
  *Fix:* use a loop, `FindIndex` helper (if available), or `Index<T>`.

  ```cpp
  int pos = -1; for(int i=0;i<v.GetCount();++i) if(v[i]==x) { pos=i; break; }
  ```

* **String replacement API confusion**
  *Symptom:* calling free `Replace(s, "a", "b")` with 3 args.
  *Fix:* use the **member** `String::Replace` for a single pattern.

  ```cpp
  String key = "root::sub"; key.Replace("::", "_"); // "root_sub"
  ```

* **Relative paths aren’t automatic**
  *Symptom:* using nonexistent `RelPath`.
  *Fix:* compute relative by trimming the normalized root prefix.

  ```cpp
  String rel = NormalizePath(full);
  String r   = NormalizePath(root);
  if(rel.StartsWith(r)) rel = TrimLeft(rel.Mid(r.GetCount()), "/\\");
  ```

* **Drag-and-drop hover not updating**
  *Symptom:* no visual feedback during drag.
  *Fix:* call `d.SetAction(...)` in `DragAndDrop` and `Refresh()`.

  ```cpp
  void DragAndDrop(Point, PasteClip& d) override {
      if(AcceptFiles(d)) { d.SetAction(DND_COPY); Refresh(); }
  }
  ```

* **Image decoding fails in EXE**
  *Symptom:* loaded images are Null.
  *Fix:* add decoders to the **EXE** package `uses`: `plugin/png`, `plugin/jpg`, `plugin/bmp`.

* **THISBACK binding errors**
  *Symptom:* cannot bind member to callback.
  *Fix:* ensure `typedef MyClass CLASSNAME;` is present, and bind a real member.

  ```cpp
  struct W : TopWindow { typedef W CLASSNAME; void Go(); W(){ btn <<= THISBACK(Go); } };
  ```

* **Touching GUI from worker threads**
  *Symptom:* random crashes or hangs.
  *Fix:* only touch GUI on the main thread; use `PostCallback` to hand off.

  ```cpp
  Thread t; t.Run([=]{ DoWork(); PostCallback([=]{ UpdateUI(); }); });
  ```

* **Timers firing after object destruction**
  *Symptom:* callback uses dead `this`.
  *Fix:* own the `TimeCallback` and kill it in the destructor.

  ```cpp
  struct C : Ctrl { TimeCallback tc; ~C(){ tc.KillSet(); } };
  ```

* **Unbalanced Draw state (clips/transforms)**
  *Symptom:* painting artifacts.
  *Fix:* always pair each `Begin`/`Clipoff` with `End`. Keep Paint stateless.

  ```cpp
  w.Begin(); /* draw */ w.End();
  ```

* **Layout include path pitfalls**
  *Symptom:* LAYOUTFILE path not found.
  *Fix:* path is **relative to .upp include roots**, not package name.

  ```cpp
  #define LAYOUTFILE <MyLayout.lay>
  #include <CtrlCore/lay.h>
  ```

* **`::clamp` ADL ambiguity**
  *Symptom:* ambiguous `clamp(...)`.
  *Fix:* qualify the global clamp.

  ```cpp
  int v = ::clamp(x, 0, 100);
  ```

You’re right—those misses belong in the guide. Here’s a **compile-clean** `main.cpp` with the fixes applied (no placeholders).

```cpp
#include <CtrlLib/CtrlLib.h>
using namespace Upp;

struct Row : Moveable<Row> {
    String root;      // "reference" | "examples"
    String topdir;    // first directory under the root
    String relpath;   // path relative to root
    String abspath;   // absolute file path
    int    kb = 0;    // rounded size in KB
};

static String MakeRel(const String& path, const String& root)
{
    String p = NormalizePath(path);
    String r = NormalizePath(root);
    if(p.StartsWith(r)) {
        int i = r.GetCount();
        while(i < p.GetCount() && (p[i] == '/' || p[i] == '\\'))
            i++;
        return p.Mid(i);
    }
    return GetFileName(path);
}

static Vector<String> SplitCsvTrim(const String& s)
{
    Vector<String> out;
    for(String t : Split(s, ',')) {
        t = TrimBoth(t);
        if(!IsNull(t))
            out.Add(t);
    }
    return out;
}

// exts: "cpp,h,lay" -> [".cpp",".h",".lay"]
static Vector<String> NormalizeExts(const WString& ws)
{
    Vector<String> out;
    for(String e : SplitCsvTrim(ws.ToString())) {
        e = ToLower(e);
        if(e[0] != '.')
            e.Insert(0, '.');
        out.Add(e);
    }
    return out;
}

static bool HasExtAllowed(const String& fn, const Vector<String>& allow)
{
    String e = ToLower(GetFileExt(fn)); // includes the dot
    for(const String& a : allow)
        if(e == a) return true;
    return false;
}

static String FirstComponent(const String& rel)
{
    int s1 = rel.Find('/'), s2 = rel.Find('\\');
    int i  = (s1 < 0) ? s2 : (s2 < 0 ? s1 : min(s1, s2));
    return i < 0 ? rel : rel.Mid(0, i);
}

struct PackBuilder : TopWindow {
    typedef PackBuilder CLASSNAME;

    ArrayCtrl   table;
    EditString  refDir, exDir, outDir, exts;
    Option      recurse;
    Button      scan, build, clr;
    Label       info;

    Vector<Row> rows;

    PackBuilder()
    {
        Title("U++ PackBuilder — Reference/Examples Concatenator");
        Sizeable().Zoomable();

        Add(table.HSizePos(8,8).VSizePos(140,40));
        table.AddColumn("Root",   90);
        table.AddColumn("TopDir", 160);
        table.AddColumn("RelPath",500);
        table.AddColumn("KB",      70)
             .SetConvert(Single<ConvertInt>())
             .SetDisplay(StdRightDisplay()); // right-align ints

        Add(refDir.LeftPos(8,  420).TopPos( 8, 24));
        Add(exDir .LeftPos(8,  420).TopPos(36, 24));
        Add(outDir.LeftPos(8,  420).TopPos(64, 24));

        Add(exts.RightPos(8,  280).TopPos( 8, 24));
        Add(recurse.RightPos(8, 280).TopPos(36, 24));
        recurse.SetLabel("Recurse subdirs");
        recurse = true; // don’t chain; SetLabel returns Pusher&

        Add(scan .RightPos(296, 188).BottomPos(8, 28));
        Add(build.RightPos(100,  92).BottomPos(8, 28));
        Add(clr  .RightPos(8,    92).BottomPos(8, 28));
        Add(info .LeftPos(8,    600).BottomPos(8, 28));

        refDir  <<= GetHomeDirFile("dev/ultimatepp/reference");
        exDir   <<= GetHomeDirFile("dev/ultimatepp/examples");
        outDir  <<= GetHomeDirFile("packs_out");
        exts    <<= "cpp,c,h,lay,iml,icpp,sch,usc,usc2";

        scan  << [=]{ DoScan();  };
        build << [=]{ DoBuild(); };
        clr   << [=]{ rows.Clear(); table.Clear(); info.SetText(""); };
    }

    void DoScan()
    {
        table.Clear();
        rows.Clear();

        Vector<String> allow = NormalizeExts(exts.GetText());

        ScanRoot(TrimBoth(refDir.GetText().ToString()), "reference", allow, (bool)~recurse);
        ScanRoot(TrimBoth(exDir .GetText().ToString()), "examples",  allow, (bool)~recurse);

        Sort(rows, [](const Row& a, const Row& b){
            int c = SgnCompare(a.root,   b.root);   if(c) return c < 0;
                c = SgnCompare(a.topdir, b.topdir); if(c) return c < 0;
            return SgnCompare(a.relpath, b.relpath) < 0;
        });

        for(const Row& r : rows)
            table.Add(r.root, r.topdir, r.relpath, r.kb);

        info.SetText(Format("%d files", rows.GetCount()));
    }

    void ScanRoot(const String& rootPath, const String& rootName,
                  const Vector<String>& allow, bool do_recurse)
    {
        if(IsNull(rootPath) || !DirectoryExists(rootPath))
            return;

        Vector<String> stack; stack.Add(rootPath);

        while(!stack.IsEmpty()) {
            String cur = stack.Top(); stack.Drop();

            FindFile ff;
            if(!ff.Search(AppendFileName(cur, "*")))
                continue;

            do {
                if(ff.IsFolder()) {
                    if(do_recurse) stack.Add(ff.GetPath());
                }
                else if(ff.IsFile()) {
                    String ap = ff.GetPath();
                    if(!HasExtAllowed(ap, allow))
                        continue;

                    Row r;
                    r.root    = rootName;
                    r.relpath = MakeRel(ap, rootPath);
                    r.topdir  = FirstComponent(r.relpath);
                    r.abspath = ap;
                    int64 len = ff.GetLength();
                    r.kb      = (int)((len + 1023) / 1024);
                    rows.Add(pick(r));
                }
            } while(ff.Next());
        }
    }

    void DoBuild()
    {
        String outRoot = TrimBoth(outDir.GetText().ToString());
        if(IsNull(outRoot) || (!DirectoryExists(outRoot) && !DirectoryCreate(outRoot))) {
            Exclamation("Output directory is invalid.");
            return;
        }

        VectorMap<String, Vector<int>> groups; // key = "root::topdir"
        for(int i = 0; i < rows.GetCount(); ++i) {
            String k = rows[i].root + "::" + rows[i].topdir;
            groups.GetAdd(k).Add(i); // avoid Add(...) return-type confusion
        }

        int made = 0;
        for(int gi = 0; gi < groups.GetCount(); ++gi) {
            const String& k = groups.GetKey(gi);
            String key = k; key.Replace("::", "_");
            String out = AppendFileName(outRoot, key + ".txt");

            FileOut fo(out);
            if(!fo.IsOpen()) {
                Exclamation(Format("Cannot write: %s", out));
                continue;
            }

            fo.PutLine("// ===== PACK: " + key + " =====");
            fo.PutLine("// Generated by U++ PackBuilder");
            fo.PutLine("");

            const Vector<int>& idx = groups[gi];
            for(int ii = 0; ii < idx.GetCount(); ++ii) {
                const Row& r = rows[idx[ii]];
                fo.PutLine("// --- " + r.relpath + " ---");
                String data = LoadFile(r.abspath);
                fo.Put(data);
                if(!data.EndsWith("\n")) // robust end-line check
                    fo.PutLine("");
                fo.PutLine("");
            }

            fo.Close();
            ++made;
        }

        info.SetText(Format("Built %d pack(s) into: %s", made, outRoot));
        PromptOK(Format("Built %d pack(s).", made));
    }
};

GUI_APP_MAIN
{
    PackBuilder().Run();
}
```

---

## Anti-patterns & Pitfalls (compressed, generic, with tiny fixes)

* **Chaining after API that returns a base type**

  * *Why:* `SetLabel()` returns `Pusher&`, which doesn’t have `Set(...)`.
  * *Fix:* split the calls.

  ```cpp
  Option o; o.SetLabel("Enable"); o = true;    // not: o.SetLabel(...).Set(true);
  ```

* **Wrong “add” API on `VectorMap` / `ArrayMap`**

  * *Why:* Overloads differ; some return a value ref, not an index.
  * *Fix:* use `GetAdd(key)` to get the value and then mutate.

  ```cpp
  VectorMap<String, Vector<int>> m;
  m.GetAdd("group").Add(file_index);
  ```

* **Comparing a last character incorrectly**

  * *Why:* Type/encoding surprises with `Last()`; or buffer is empty.
  * *Fix:* prefer `EndsWith`.

  ```cpp
  if(!s.EndsWith("\n")) out.PutLine("");
  ```

* **Non-`Moveable` types in U++ containers**

  * *Why:* `Vector<T>` needs `T` to be Moveable unless it’s trivially moveable.
  * *Fix:* mark data structs `Moveable<T>`.

  ```cpp
  struct Row : Moveable<Row> { String a; int b{}; };
  ```

* **Calling `TopWindow` methods on non-windows**

  * *Why:* `Title/Sizeable/Zoomable` exist on `TopWindow`.
  * *Fix:* derive UI roots from `TopWindow` (or use a `WithLayout<TopWindow>`).

  ```cpp
  struct Main : TopWindow { Main(){ Title("App"); Sizeable().Zoomable(); } };
  ```

* **WString ↔ String confusion**

  * *Why:* `GetText()` is `WString`; implicit conversions aren’t assumed.
  * *Fix:* call `.ToString()` explicitly.

  ```cpp
  String path = edit.GetText().ToString();
  ```

* **Building single-char `String` the wrong way**

  * *Why:* `String('.')` is not a valid ctor in U++.
  * *Fix:* use literals or `Insert`.

  ```cpp
  String e = "png"; e.Insert(0, '.');   // -> ".png"
  ```

* **Sorting a view instead of the model**

  * *Why:* `ArrayCtrl::Sort` overloads can be confusing; model stays unsorted.
  * *Fix:* sort your container, then reload the control.

  ```cpp
  Sort(rows, [](const Row& a, const Row& b){ return a.relpath < b.relpath; });
  ```

* **Using GUI APIs off the UI thread**

  * *Why:* U++ GUI is single-threaded by design.
  * *Fix:* hand off with `PostCallback`.

  ```cpp
  Thread t; t.Run([=]{ /* work */ PostCallback([=]{ ctrl.SetText("done"); }); });
  ```

* **Timers firing after destruction**

  * *Why:* `TimeCallback` keeps calling your lambda.
  * *Fix:* kill timers in destructors.

  ```cpp
  struct C : Ctrl { TimeCallback tc; ~C(){ tc.KillSet(); } };
  ```

* **Unbalanced drawing state**

  * *Why:* Missing `End()` for a `Begin()` / `Clipoff()`.
  * *Fix:* always pair them; keep `Paint` stateless.

  ```cpp
  w.Begin(); /* draw */ w.End();
  ```

* **Layout include path mistakes**

  * *Why:* `LAYOUTFILE` is relative to `.upp` `include` roots, not package name.
  * *Fix:*

  ```cpp
  #define LAYOUTFILE <MyLayout.lay>
  #include <CtrlCore/lay.h>
  ```

* **Image decoders missing in EXE**

  * *Why:* Decoders register at link time.
  * *Fix:* add `plugin/png`, `plugin/jpg`, etc. to the **main app** `uses`.

* **`::clamp` ADL ambiguity**

  * *Why:* Conflicts with std/other namespaces.
  * *Fix:* qualify the global.

  ```cpp
  int v = ::clamp(x, 0, 100);
  ```













You’re right—those misses belong in the guide. Here’s a **compile-clean** `main.cpp` with the fixes applied (no placeholders).

```cpp
#include <CtrlLib/CtrlLib.h>
using namespace Upp;

struct Row : Moveable<Row> {
    String root;      // "reference" | "examples"
    String topdir;    // first directory under the root
    String relpath;   // path relative to root
    String abspath;   // absolute file path
    int    kb = 0;    // rounded size in KB
};

static String MakeRel(const String& path, const String& root)
{
    String p = NormalizePath(path);
    String r = NormalizePath(root);
    if(p.StartsWith(r)) {
        int i = r.GetCount();
        while(i < p.GetCount() && (p[i] == '/' || p[i] == '\\'))
            i++;
        return p.Mid(i);
    }
    return GetFileName(path);
}

static Vector<String> SplitCsvTrim(const String& s)
{
    Vector<String> out;
    for(String t : Split(s, ',')) {
        t = TrimBoth(t);
        if(!IsNull(t))
            out.Add(t);
    }
    return out;
}

// exts: "cpp,h,lay" -> [".cpp",".h",".lay"]
static Vector<String> NormalizeExts(const WString& ws)
{
    Vector<String> out;
    for(String e : SplitCsvTrim(ws.ToString())) {
        e = ToLower(e);
        if(e[0] != '.')
            e.Insert(0, '.');
        out.Add(e);
    }
    return out;
}

static bool HasExtAllowed(const String& fn, const Vector<String>& allow)
{
    String e = ToLower(GetFileExt(fn)); // includes the dot
    for(const String& a : allow)
        if(e == a) return true;
    return false;
}

static String FirstComponent(const String& rel)
{
    int s1 = rel.Find('/'), s2 = rel.Find('\\');
    int i  = (s1 < 0) ? s2 : (s2 < 0 ? s1 : min(s1, s2));
    return i < 0 ? rel : rel.Mid(0, i);
}

struct PackBuilder : TopWindow {
    typedef PackBuilder CLASSNAME;

    ArrayCtrl   table;
    EditString  refDir, exDir, outDir, exts;
    Option      recurse;
    Button      scan, build, clr;
    Label       info;

    Vector<Row> rows;

    PackBuilder()
    {
        Title("U++ PackBuilder — Reference/Examples Concatenator");
        Sizeable().Zoomable();

        Add(table.HSizePos(8,8).VSizePos(140,40));
        table.AddColumn("Root",   90);
        table.AddColumn("TopDir", 160);
        table.AddColumn("RelPath",500);
        table.AddColumn("KB",      70)
             .SetConvert(Single<ConvertInt>())
             .SetDisplay(StdRightDisplay()); // right-align ints

        Add(refDir.LeftPos(8,  420).TopPos( 8, 24));
        Add(exDir .LeftPos(8,  420).TopPos(36, 24));
        Add(outDir.LeftPos(8,  420).TopPos(64, 24));

        Add(exts.RightPos(8,  280).TopPos( 8, 24));
        Add(recurse.RightPos(8, 280).TopPos(36, 24));
        recurse.SetLabel("Recurse subdirs");
        recurse = true; // don’t chain; SetLabel returns Pusher&

        Add(scan .RightPos(296, 188).BottomPos(8, 28));
        Add(build.RightPos(100,  92).BottomPos(8, 28));
        Add(clr  .RightPos(8,    92).BottomPos(8, 28));
        Add(info .LeftPos(8,    600).BottomPos(8, 28));

        refDir  <<= GetHomeDirFile("dev/ultimatepp/reference");
        exDir   <<= GetHomeDirFile("dev/ultimatepp/examples");
        outDir  <<= GetHomeDirFile("packs_out");
        exts    <<= "cpp,c,h,lay,iml,icpp,sch,usc,usc2";

        scan  << [=]{ DoScan();  };
        build << [=]{ DoBuild(); };
        clr   << [=]{ rows.Clear(); table.Clear(); info.SetText(""); };
    }

    void DoScan()
    {
        table.Clear();
        rows.Clear();

        Vector<String> allow = NormalizeExts(exts.GetText());

        ScanRoot(TrimBoth(refDir.GetText().ToString()), "reference", allow, (bool)~recurse);
        ScanRoot(TrimBoth(exDir .GetText().ToString()), "examples",  allow, (bool)~recurse);

        Sort(rows, [](const Row& a, const Row& b){
            int c = SgnCompare(a.root,   b.root);   if(c) return c < 0;
                c = SgnCompare(a.topdir, b.topdir); if(c) return c < 0;
            return SgnCompare(a.relpath, b.relpath) < 0;
        });

        for(const Row& r : rows)
            table.Add(r.root, r.topdir, r.relpath, r.kb);

        info.SetText(Format("%d files", rows.GetCount()));
    }

    void ScanRoot(const String& rootPath, const String& rootName,
                  const Vector<String>& allow, bool do_recurse)
    {
        if(IsNull(rootPath) || !DirectoryExists(rootPath))
            return;

        Vector<String> stack; stack.Add(rootPath);

        while(!stack.IsEmpty()) {
            String cur = stack.Top(); stack.Drop();

            FindFile ff;
            if(!ff.Search(AppendFileName(cur, "*")))
                continue;

            do {
                if(ff.IsFolder()) {
                    if(do_recurse) stack.Add(ff.GetPath());
                }
                else if(ff.IsFile()) {
                    String ap = ff.GetPath();
                    if(!HasExtAllowed(ap, allow))
                        continue;

                    Row r;
                    r.root    = rootName;
                    r.relpath = MakeRel(ap, rootPath);
                    r.topdir  = FirstComponent(r.relpath);
                    r.abspath = ap;
                    int64 len = ff.GetLength();
                    r.kb      = (int)((len + 1023) / 1024);
                    rows.Add(pick(r));
                }
            } while(ff.Next());
        }
    }

    void DoBuild()
    {
        String outRoot = TrimBoth(outDir.GetText().ToString());
        if(IsNull(outRoot) || (!DirectoryExists(outRoot) && !DirectoryCreate(outRoot))) {
            Exclamation("Output directory is invalid.");
            return;
        }

        VectorMap<String, Vector<int>> groups; // key = "root::topdir"
        for(int i = 0; i < rows.GetCount(); ++i) {
            String k = rows[i].root + "::" + rows[i].topdir;
            groups.GetAdd(k).Add(i); // avoid Add(...) return-type confusion
        }

        int made = 0;
        for(int gi = 0; gi < groups.GetCount(); ++gi) {
            const String& k = groups.GetKey(gi);
            String key = k; key.Replace("::", "_");
            String out = AppendFileName(outRoot, key + ".txt");

            FileOut fo(out);
            if(!fo.IsOpen()) {
                Exclamation(Format("Cannot write: %s", out));
                continue;
            }

            fo.PutLine("// ===== PACK: " + key + " =====");
            fo.PutLine("// Generated by U++ PackBuilder");
            fo.PutLine("");

            const Vector<int>& idx = groups[gi];
            for(int ii = 0; ii < idx.GetCount(); ++ii) {
                const Row& r = rows[idx[ii]];
                fo.PutLine("// --- " + r.relpath + " ---");
                String data = LoadFile(r.abspath);
                fo.Put(data);
                if(!data.EndsWith("\n")) // robust end-line check
                    fo.PutLine("");
                fo.PutLine("");
            }

            fo.Close();
            ++made;
        }

        info.SetText(Format("Built %d pack(s) into: %s", made, outRoot));
        PromptOK(Format("Built %d pack(s).", made));
    }
};

GUI_APP_MAIN
{
    PackBuilder().Run();
}
```

---

## Anti-patterns & Pitfalls (compressed, generic, with tiny fixes)

* **Chaining after API that returns a base type**

  * *Why:* `SetLabel()` returns `Pusher&`, which doesn’t have `Set(...)`.
  * *Fix:* split the calls.

  ```cpp
  Option o; o.SetLabel("Enable"); o = true;    // not: o.SetLabel(...).Set(true);
  ```

* **Wrong “add” API on `VectorMap` / `ArrayMap`**

  * *Why:* Overloads differ; some return a value ref, not an index.
  * *Fix:* use `GetAdd(key)` to get the value and then mutate.

  ```cpp
  VectorMap<String, Vector<int>> m;
  m.GetAdd("group").Add(file_index);
  ```

* **Comparing a last character incorrectly**

  * *Why:* Type/encoding surprises with `Last()`; or buffer is empty.
  * *Fix:* prefer `EndsWith`.

  ```cpp
  if(!s.EndsWith("\n")) out.PutLine("");
  ```

* **Non-`Moveable` types in U++ containers**

  * *Why:* `Vector<T>` needs `T` to be Moveable unless it’s trivially moveable.
  * *Fix:* mark data structs `Moveable<T>`.

  ```cpp
  struct Row : Moveable<Row> { String a; int b{}; };
  ```

* **Calling `TopWindow` methods on non-windows**

  * *Why:* `Title/Sizeable/Zoomable` exist on `TopWindow`.
  * *Fix:* derive UI roots from `TopWindow` (or use a `WithLayout<TopWindow>`).

  ```cpp
  struct Main : TopWindow { Main(){ Title("App"); Sizeable().Zoomable(); } };
  ```

* **WString ↔ String confusion**

  * *Why:* `GetText()` is `WString`; implicit conversions aren’t assumed.
  * *Fix:* call `.ToString()` explicitly.

  ```cpp
  String path = edit.GetText().ToString();
  ```

* **Building single-char `String` the wrong way**

  * *Why:* `String('.')` is not a valid ctor in U++.
  * *Fix:* use literals or `Insert`.

  ```cpp
  String e = "png"; e.Insert(0, '.');   // -> ".png"
  ```

* **Sorting a view instead of the model**

  * *Why:* `ArrayCtrl::Sort` overloads can be confusing; model stays unsorted.
  * *Fix:* sort your container, then reload the control.

  ```cpp
  Sort(rows, [](const Row& a, const Row& b){ return a.relpath < b.relpath; });
  ```

* **Using GUI APIs off the UI thread**

  * *Why:* U++ GUI is single-threaded by design.
  * *Fix:* hand off with `PostCallback`.

  ```cpp
  Thread t; t.Run([=]{ /* work */ PostCallback([=]{ ctrl.SetText("done"); }); });
  ```

* **Timers firing after destruction**

  * *Why:* `TimeCallback` keeps calling your lambda.
  * *Fix:* kill timers in destructors.

  ```cpp
  struct C : Ctrl { TimeCallback tc; ~C(){ tc.KillSet(); } };
  ```

* **Unbalanced drawing state**

  * *Why:* Missing `End()` for a `Begin()` / `Clipoff()`.
  * *Fix:* always pair them; keep `Paint` stateless.

  ```cpp
  w.Begin(); /* draw */ w.End();
  ```

* **Layout include path mistakes**

  * *Why:* `LAYOUTFILE` is relative to `.upp` `include` roots, not package name.
  * *Fix:*

  ```cpp
  #define LAYOUTFILE <MyLayout.lay>
  #include <CtrlCore/lay.h>
  ```

* **Image decoders missing in EXE**

  * *Why:* Decoders register at link time.
  * *Fix:* add `plugin/png`, `plugin/jpg`, etc. to the **main app** `uses`.

* **`::clamp` ADL ambiguity**

  * *Why:* Conflicts with std/other namespaces.
  * *Fix:* qualify the global.

  ```cpp
  int v = ::clamp(x, 0, 100);
  ```


..........



Got it—let’s make the app self-explanatory and easier to drive. Here’s your **updated, compile-clean `main.cpp`** with:

* a bottom-left **[Help]** button (full usage guide),
* **[…]** browse buttons for each directory field (directory chooser),
* labeled **Scan**, **Build Packs**, **Clear**, **OK**, **Cancel** buttons,
* labels for all inputs.

```cpp
#include <CtrlLib/CtrlLib.h>
using namespace Upp;

struct Row : Moveable<Row> {
    String root;      // "reference" | "examples"
    String topdir;    // first directory under the root
    String relpath;   // path relative to root
    String abspath;   // absolute file path
    int    kb = 0;    // rounded size in KB
};

static String MakeRel(const String& path, const String& root)
{
    String p = NormalizePath(path);
    String r = NormalizePath(root);
    if(p.StartsWith(r)) {
        int i = r.GetCount();
        while(i < p.GetCount() && (p[i] == '/' || p[i] == '\\'))
            i++;
        return p.Mid(i);
    }
    return GetFileName(path);
}

static Vector<String> SplitCsvTrim(const String& s)
{
    Vector<String> out;
    for(String t : Split(s, ',')) {
        t = TrimBoth(t);
        if(!IsNull(t))
            out.Add(t);
    }
    return out;
}

// "cpp,h,lay" -> [".cpp",".h",".lay"]
static Vector<String> NormalizeExts(const WString& ws)
{
    Vector<String> out;
    for(String e : SplitCsvTrim(ws.ToString())) {
        e = ToLower(e);
        if(e[0] != '.')
            e.Insert(0, '.');
        out.Add(e);
    }
    return out;
}

static bool HasExtAllowed(const String& fn, const Vector<String>& allow)
{
    String e = ToLower(GetFileExt(fn)); // includes the dot
    for(const String& a : allow)
        if(e == a) return true;
    return false;
}

static String FirstComponent(const String& rel)
{
    int s1 = rel.Find('/'), s2 = rel.Find('\\');
    int i  = (s1 < 0) ? s2 : (s2 < 0 ? s1 : min(s1, s2));
    return i < 0 ? rel : rel.Mid(0, i);
}

static String HelpQtf()
{
    return
        "[1 U++ PackBuilder — Help]\n\n"
        "[*Purpose:] Concatenate source files from the U++ [*reference/] and [*examples/] "
        "trees into per-folder “packs” (one output .txt per {root:topdir}).\n\n"
        "[*Fields]\n"
        " • [*Reference dir:] top folder that contains the “reference/” subtrees.\n"
        " • [*Examples dir:]  top folder that contains the “examples/” subtrees.\n"
        " • [*Output dir:]    where the generated .txt packs are written.\n"
        " • [*Extensions:]    comma-separated list (e.g., cpp,c,h,lay,iml,icpp,sch,usc,usc2).\n"
        " • [*Recurse subdirs:] include nested directories when scanning.\n\n"
        "[*Workflow]\n"
        " 1. Set the three directories (use […] to browse).\n"
        " 2. Adjust extensions if needed.\n"
        " 3. Click [*Scan] to list matching files.\n"
        " 4. Click [*Build Packs] to write packs into the Output dir.\n\n"
        "[*Output]\n"
        "Creates files named [*reference_<topdir>.txt] and/or [*examples_<topdir>.txt]. "
        "Each pack contains a series of sections:\n"
        "    // --- relative\\path\\to\\file ---\n"
        "    <file contents>\n\n"
        "[*Notes]\n"
        " • Sorting is by Root → TopDir → RelPath.\n"
        " • You can re-scan safely—output files are overwritten on Build.\n"
        " • ‘OK’/‘Cancel’ just close the window (they don’t build).\n";
}

struct PackBuilder : TopWindow {
    typedef PackBuilder CLASSNAME;

    // Labels
    Label lblRef, lblEx, lblOut, lblExts;

    // Inputs
    EditString  refDir, exDir, outDir, exts;
    Button      brRef, brEx, brOut;        // [...] browse buttons
    Option      recurse;

    // Table and status
    ArrayCtrl   table;
    Label       info;

    // Actions
    Button      help, scan, build, clr, ok, cancel;

    Vector<Row> rows;

    PackBuilder()
    {
        Title("U++ PackBuilder — Reference/Examples Concatenator");
        Sizeable().Zoomable();

        // ---- Labels
        lblRef.SetText("Reference dir:");
        lblEx .SetText("Examples dir:");
        lblOut.SetText("Output dir:");
        lblExts.SetText("Extensions:");

        Add(lblRef.LeftPos(8, 120).TopPos( 8, 24));
        Add(lblEx .LeftPos(8, 120).TopPos(36, 24));
        Add(lblOut.LeftPos(8, 120).TopPos(64, 24));
        Add(lblExts.RightPos(8+280, 100).TopPos( 8, 24)); // aligns near the exts field

        // ---- Inputs
        Add(refDir.LeftPos(130, 420).TopPos( 8, 24));
        Add(exDir .LeftPos(130, 420).TopPos(36, 24));
        Add(outDir.LeftPos(130, 420).TopPos(64, 24));

        Add(exts.RightPos(8, 280).TopPos( 8, 24));

        // ---- Browse […]
        brRef.SetLabel("..."); brEx.SetLabel("..."); brOut.SetLabel("...");
        Add(brRef.LeftPos(130+420+4, 28).TopPos( 8, 24));
        Add(brEx .LeftPos(130+420+4, 28).TopPos(36, 24));
        Add(brOut.LeftPos(130+420+4, 28).TopPos(64, 24));

        // ---- Recurse
        Add(recurse.RightPos(8, 280).TopPos(36, 24));
        recurse.SetLabel("Recurse subdirs");
        recurse = true;

        // ---- Table
        Add(table.HSizePos(8,8).VSizePos(140,48));
        table.AddColumn("Root",   90);
        table.AddColumn("TopDir", 160);
        table.AddColumn("RelPath",500);
        table.AddColumn("KB",      70)
             .SetConvert(Single<ConvertInt>())
             .SetDisplay(StdRightDisplay());

        // ---- Bottom buttons
        help .SetLabel("Help");
        scan .SetLabel("Scan");
        build.SetLabel("Build Packs");
        clr  .SetLabel("Clear");
        ok   .SetLabel("OK");
        cancel.SetLabel("Cancel");

        Add(help .LeftPos(8,   80).BottomPos(8, 28));
        Add(scan .RightPos(296,188).BottomPos(8, 28));
        Add(build.RightPos(100, 92).BottomPos(8, 28));
        Add(clr  .RightPos(8,   92).BottomPos(8, 28));
        Add(ok   .RightPos(8+92+8+92+8, 80).BottomPos(8, 28));   // place OK before Cancel group
        Add(cancel.RightPos(8+92+8, 80).BottomPos(8, 28));
        Add(info .LeftPos(8,  600).BottomPos(8, 28));

        // ---- Defaults
        refDir <<= GetHomeDirFile("dev/ultimatepp/reference");
        exDir  <<= GetHomeDirFile("dev/ultimatepp/examples");
        outDir <<= GetHomeDirFile("packs_out");
        exts   <<= "cpp,c,h,lay,iml,icpp,sch,usc,usc2";

        // ---- Wiring
        help   << [=]{ PromptOK(HelpQtf()); };
        scan   << [=]{ DoScan();  };
        build  << [=]{ DoBuild(); };
        clr    << [=]{ rows.Clear(); table.Clear(); info.SetText(""); };

        ok     << [=]{ Close(); };
        cancel << [=]{ Close(); };

        brRef  << [=]{ BrowseDir(refDir);  };
        brEx   << [=]{ BrowseDir(exDir);   };
        brOut  << [=]{ BrowseDir(outDir);  };
    }

    void BrowseDir(EditString& target)
    {
        FileSel fs;
        fs.Dir();
        fs.ActiveDir(target.GetText().ToString());
        if(fs.ExecuteOpen())
            target <<= ~fs; // selected directory path
    }

    void DoScan()
    {
        table.Clear();
        rows.Clear();

        Vector<String> allow = NormalizeExts(exts.GetText());

        ScanRoot(TrimBoth(refDir.GetText().ToString()), "reference", allow, (bool)~recurse);
        ScanRoot(TrimBoth(exDir .GetText().ToString()), "examples",  allow, (bool)~recurse);

        Sort(rows, [](const Row& a, const Row& b){
            int c = SgnCompare(a.root,   b.root);   if(c) return c < 0;
                c = SgnCompare(a.topdir, b.topdir); if(c) return c < 0;
            return SgnCompare(a.relpath, b.relpath) < 0;
        });

        for(const Row& r : rows)
            table.Add(r.root, r.topdir, r.relpath, r.kb);

        info.SetText(Format("%d files", rows.GetCount()));
    }

    void ScanRoot(const String& rootPath, const String& rootName,
                  const Vector<String>& allow, bool do_recurse)
    {
        if(IsNull(rootPath) || !DirectoryExists(rootPath))
            return;

        Vector<String> stack; stack.Add(rootPath);

        while(!stack.IsEmpty()) {
            String cur = stack.Top(); stack.Drop();

            FindFile ff;
            if(!ff.Search(AppendFileName(cur, "*")))
                continue;

            do {
                if(ff.IsFolder()) {
                    if(do_recurse) stack.Add(ff.GetPath());
                }
                else if(ff.IsFile()) {
                    String ap = ff.GetPath();
                    if(!HasExtAllowed(ap, allow))
                        continue;

                    Row r;
                    r.root    = rootName;
                    r.relpath = MakeRel(ap, rootPath);
                    r.topdir  = FirstComponent(r.relpath);
                    r.abspath = ap;
                    int64 len = ff.GetLength();
                    r.kb      = (int)((len + 1023) / 1024);
                    rows.Add(pick(r));
                }
            } while(ff.Next());
        }
    }

    void DoBuild()
    {
        String outRoot = TrimBoth(outDir.GetText().ToString());
        if(IsNull(outRoot) || (!DirectoryExists(outRoot) && !DirectoryCreate(outRoot))) {
            Exclamation("Output directory is invalid.");
            return;
        }

        VectorMap<String, Vector<int>> groups; // key = "root::topdir"
        for(int i = 0; i < rows.GetCount(); ++i) {
            String k = rows[i].root + "::" + rows[i].topdir;
            groups.GetAdd(k).Add(i);
        }

        int made = 0;
        for(int gi = 0; gi < groups.GetCount(); ++gi) {
            const String& k = groups.GetKey(gi);
            String key = k; key.Replace("::", "_");
            String out = AppendFileName(outRoot, key + ".txt");

            FileOut fo(out);
            if(!fo.IsOpen()) {
                Exclamation(Format("Cannot write: %s", out));
                continue;
            }

            fo.PutLine("// ===== PACK: " + key + " =====");
            fo.PutLine("// Generated by U++ PackBuilder");
            fo.PutLine("");

            const Vector<int>& idx = groups[gi];
            for(int ii = 0; ii < idx.GetCount(); ++ii) {
                const Row& r = rows[idx[ii]];
                fo.PutLine("// --- " + r.relpath + " ---");
                String data = LoadFile(r.abspath);
                fo.Put(data);
                if(!data.EndsWith("\n"))
                    fo.PutLine("");
                fo.PutLine("");
            }
            fo.Close();
            ++made;
        }

        info.SetText(Format("Built %d pack(s) into: %s", made, outRoot));
        PromptOK(Format("Built %d pack(s).", made));
    }
};

GUI_APP_MAIN
{
    PackBuilder().Run();
}
```

---

## Anti-patterns & Pitfalls (compressed, generic)

* **Chaining after a method that returns `Pusher&`**

  * *Symptom:* Trying `.Set(true)` after `.SetLabel(...)` on `Option/Button`.
  * *Fix:* Split calls.

  ```cpp
  Option chk; chk.SetLabel("Recurse"); chk = true;
  ```

* **Directory selection done with a file dialog mode**

  * *Symptom:* Can’t pick folders reliably.
  * *Fix:* Use `FileSel` with `Dir()` (or your platform’s directory picker).

  ```cpp
  FileSel fs; fs.Dir(); if(fs.ExecuteOpen()) edit <<= ~fs;
  ```

* **WString ↔ String confusion**

  * *Symptom:* “no viable conversion from ‘WString’ to ‘String’”.
  * *Fix:* `GetText().ToString()` when reading from `EditString`.

  ```cpp
  String s = edit.GetText().ToString();
  ```

* **Single-char `String` construction**

  * *Symptom:* `String('.')` fails.
  * *Fix:* use literal or `Insert`.

  ```cpp
  String e = "png"; e.Insert(0, '.');
  ```

* **Sorting the view instead of the model**

  * *Symptom:* `ArrayCtrl::Sort` overuse or wrong overload.
  * *Fix:* sort the data container, then load the control.

  ```cpp
  Sort(rows, [](const Row& a, const Row& b){ return a.relpath < b.relpath; });
  ```

* **Member returns don’t chain**

  * *Symptom:* `SetLabel(...).Set(...)`.
  * *Fix:* check return type; split statements.

* **Map “add” overload confusion**

  * *Symptom:* assigning `int` to a `Vector<int>` or vice-versa.
  * *Fix:* `GetAdd(key).Add(value)` to mutate the value in place.

  ```cpp
  VectorMap<String, Vector<int>> g; g.GetAdd("group").Add(i);
  ```

* **Comparing last character incorrectly**

  * *Symptom:* pointer/int mismatch on `Last()`.
  * *Fix:* prefer `EndsWith`.

  ```cpp
  if(!s.EndsWith("\n")) out.PutLine("");
  ```

* **Containers with non-Moveable payloads**

  * *Symptom:* `static_assert(is_upp_guest<T>)`.
  * *Fix:* mark data pods as `Moveable<T>`.

  ```cpp
  struct Row : Moveable<Row> { String a; int b; };
  ```

* **GUI work off the UI thread**

  * *Symptom:* UI glitches/crashes.
  * *Fix:* hand off with `PostCallback`.

  ```cpp
  Thread t; t.Run([=]{ /* work */ PostCallback([=]{ ctrl.SetText("done"); }); });
  ```

* **Unbalanced Draw state**

  * *Symptom:* artifacts after painting.
  * *Fix:* always pair `Begin/End`, `Clipoff/End`.

  ```cpp
  w.Begin(); /* draw */ w.End();
  ```


...........



-------------------------------------------------------------------------------
19) CODE SNIPPET APPENDIX (CANONICAL PATTERNS) (CANONICAL PATTERNS)
-------------------------------------------------------------------------------
A) CLASSNAME + THISBACK
```
struct ImageView : TopWindow {
    typedef ImageView CLASSNAME;
    Splitter splitter; ArrayCtrl files; ImageCtrl img; Button dirUp;
    String dir;

    void DoDir(); void Enter(); void DirUpClick();

    ImageView() {
        Title("Image viewer"); Sizeable().Zoomable();
        splitter.Horz(files, img).SetPos(2600);
        Add(splitter.SizePos());
        files.WhenEnterItem  = THISBACK(Enter);
        files.WhenLeftDouble = THISBACK(DoDir);
        dirUp <<= THISBACK(DirUpClick);
    }
};
```

B) Safe timer pattern
```
struct Worker : Ctrl {
    TimeCallback tick;
    void Start() {
        tick.Set(-1, [=]{ if(GetTopWindow()) Step(); }); // periodic; guard owner
    }
    void Stop() { tick.KillSet(); }
    void Step() { /* do small unit; PostCallback updates UI */ }
    ~Worker() { Stop(); }
};
```

C) DnD accept + drop
```
virtual void DragAndDrop(Point p, PasteClip& d) override {
    if(AcceptFiles(d) || AcceptText(d) || AcceptImage(d)) {
        d.SetAction(DND_COPY);
        Refresh();
    }
}
virtual void Drop(Point p, PasteClip& d) override {
    if(IsAvailableFiles(d))
        for(const String& fn : GetFiles(d)) UseFile(fn);
    else if(AcceptImage(d)) SetImage(GetImage(d));
    else if(AcceptText(d))  SetText(GetString(d));
}
```

D) ImageBuffer alpha reset before composition
```
ImageBuffer ib(sz);
Fill(~ib, RGBAZero(), ib.GetLength());
BufferPainter bp(ib, MODE_ANTIALIASED);
// ... draw ...
Image img = ib;
```

E) HTTP download to file (streaming)
```
void HttpToFile(const String& url, const String& out) {
    FileOut fo(out);
    HttpRequest r(url);
    r.WhenContent = [&](const void *ptr, int sz){ fo.Put((const char*)ptr, sz); };
    r.Execute();
}
```

TITLE
U++_Coding_Standards.txt — Norms for Readable, Safe, Idiomatic U++ (AI‑first)

VERSION BASELINE

* Target: U++ 2025.1 (build 17799+) and TheIDE.
* Applies to Core, Draw, CtrlCore, CtrlLib, standard plugins.

SCOPE

* What the code should look like: naming, layout, ownership, error policy, events/callbacks, threading, drawing & theming, serialization, networking, geometry, build hygiene.
* Rich examples to minimize ambiguity for AI. This file is normative for style; API walkthroughs live in the Cookbook.

PRINCIPLES

* Value semantics first; explicit ownership when needed.
* Deterministic lifetimes and RAII everywhere.
* GUI work only on the GUI thread; clean handoff from workers.
* Theme‑aware rendering with Chameleon; accessibility by default.
* Prefer U++ facilities over external libs; keep deps minimal.

---

1. NAMING & PROJECT LAYOUT

---

Naming

* Classes/structs: PascalCase (e.g., ImageCache, JsonReader).
* Methods/functions: camelCase (e.g., loadFromFile, setSizeHint).
* Variables: camelCase (e.g., bufferLen, socket).
* Constants/enum values: UPPER_SNAKE (e.g., MAX_RETRIES, ALIGN_CENTER).
* Templates/traits/types: PascalCase (e.g., OwnerMap<T>).
* Events/callbacks exposed by controls: WhenX (WhenAction, WhenDrop, WhenZoom...).
* Accessors: SetX/GetX; controls surface state with SetData/GetData.
* File names: match the primary class (MainWin.h/.cpp). Avoid multi‑class files unless tiny.

Files & Includes

* One public class per header where practical; private helpers in .cpp.
* Use #pragma once in headers (preferred).
* Public headers: include only what declarations require; forward declare otherwise.
* Examples must name canonical headers explicitly: <Core/Core.h>, <Draw/Draw.h>, <CtrlCore/CtrlCore.h>, <CtrlLib/CtrlLib.h>.

Directories (recommended)

* /lib      — library packages (no main/WinMain; omit mainconfig unless you have a specific build-system reason).
* /examples — minimal, compiling snippets for docs/tests.
* /docs     — these standards, cookbook, compressed API.
* /res      — layouts (.lay), images (.iml), fonts.

#### Repo Directory Example for a Control (eg:upp_GalleryCtrl)

upp_GalleryCtrl/
└─ GalleryCtrl/
├─ GalleryCtrl.h
├─ GalleryCtrl.cpp
└─ GalleryCtrl.upp
└─ examples/
	└─ GalleryDemo/
	├─ GalleryDemo.upp      ← MAIN APP
	└─ main.cpp

#### Repo Directory Example for multi-package (app) with its own controlls used in the app (eg:upp_GalleryCtrl)

FontStudio/                    ← repo root (in your Package nests)
├─ FontStudio.upp              ← MAIN APP (EXE)
├─ main.cpp
└─ GalleryCtrl/                ← LIB package lives here during dev
	├─ GalleryCtrl.h
	├─ GalleryCtrl.cpp
	└─ GalleryCtrl.upp

#### .upp Package File (quick rules)

Location: root of the package directory.
uses: list package dependencies (e.g., Core, CtrlLib, your other packages).
file: list your .cpp, .h, .lay files.
include: add extra include search roots (optional; keep it small).

#### mainconfig:

"" = "GUI" or "CONSOLE" for an EXE (main package),

Prefer omitting `mainconfig` entirely for a non-main library package.

NOTE: No comments are allowed in .upp.

7. Connecting .upp, .h, and .lay (if you use Layout Designer)

Common pitfall: LAYOUTFILE path must be relative to .upp’s include paths, not the filesystem or package name.

If your .upp has:

include
/ui;

and your layout is ui/MyLayout.lay, then in the header:

#define LAYOUTFILE <MyLayout.lay>
#include <CtrlCore/lay.h>

Incorrect (will double-prepend):
#define LAYOUTFILE <MyPackage/ui/MyLayout.lay>

8. Build workflow tips & pitfalls

Missing package on build: check Assembly → Package nests; they must contain your repo root and uppsrc.
Do not prefix assembly name in uses (uses GalleryCtrl, not uses myassembly/GalleryCtrl).

Undefined WinMain: you built a library as if it were a main app. Either Compile it, or build a demo as the main package.

PNG/JPG not loading: add plugin/png (and plugin/jpg) to the main app uses.

THISBACK macro errors: make sure the callback target is a method of the current class and you included the header where it’s declared.

Header discovery: if you use #include <GalleryCtrl/GalleryCtrl.h>, make sure the package folder name is GalleryCtrl and it is inside a nest path.

---

2. OWNERSHIP & LIFETIME

---

* Default to value objects with clear scope.
* Unique heap ownership: One<T>. Do not share One<T>.
* Shared/observed lifetime: Ptr<>/Pte<>. Prefer Ptr<> when multiple owners/observers exist.
* Avoid raw new/delete in user code; prefer automatic storage, One<>, or factory helpers that return by value.
* Pass small/cheap types by value; otherwise const T&. Return by value (NRVO friendly).
* Never store references/pointers to stack temporaries or child control internals.
* Callbacks capturing this: prefer value captures; if lifetime is uncertain, capture Ptr<> to guard.
* Global objects: avoid. If unavoidable, ensure thread‑safe init and bounded lifetime.

Common lifetime patterns

* Parent owns child Ctrls (no delete required). Do not hold raw pointers to parent beyond scope.
* TimeCallback/Timer: own in the control/window; cancel in destructor; keep callbacks short; never assume they stop themselves.

---

3. ERROR POLICY, ASSERTS & LOGGING

---

* Routine failures: return status or error objects; reserve exceptions for unrecoverable or boundary translation.
* Use ASSERT/ASSERT_ in debug builds for programmer errors.
* Operational errors: log and surface to user (PromptOK/Exclamation as appropriate).
* Logging macros: LOG (release‑capable), DLOG/LLOG/RLOG for module‑scoped or debug. Initialize logging early (StdLogSetup in app startup).
* Destructors never throw; keep exception boundaries narrow and documented.

---

4. THREADING & GUI BOUNDARY

---

* Never touch GUI from worker threads.
* Handoff to GUI thread with PostCallback. If absolutely necessary to touch GUI from workers, wrap the minimal section with GuiLock.
* Own and stop timers on the GUI thread; cancel in destructors.
* Long operations: move to worker (Thread::Start/Lambda) and report progress via callbacks.

---

5. CALLBACKS & EVENTS

---

* Expose control actions via WhenX members (e.g., WhenAction, WhenSelect, WhenDrop).
* Bind member methods with THISBACK/THISBACK1..; ensure `typedef MyClass CLASSNAME;` is present.
* Document callback semantics: firing context (GUI thread), frequency, ownership/lifetime expectations.
* Avoid long‑running work inside callbacks; defer via PostCallback or workers.

Example — wiring a control pair

```
zoom.MinMax(0, 6).SetData(2);
zoom.WhenAction << [=]{ gallery.SetZoomIndex((int)~zoom); };
gallery.WhenZoom  << [=](int zi){ zoom <<= zi; };
```

---

6. DRAWING, COLORS, FONTS & METRICS

---

* Theme‑aware colors only: use SColor... / AColor...; never hard‑code RGBs that break dark mode.
* Paint must be stateless: read state, draw accordingly; no state mutation during Paint.
* Always balance Draw state: every Clipoff/Begin/Intersect has a matching End.
* Text measurement: compute Size with GetTextSize before drawing; avoid truncation; use DrawTextEllipsis or tooltips when needed.
* Image alpha discipline: before composing on a fresh ImageBuffer, reset alpha
  `Fill(~ib, RGBAZero(), ib.GetLength());`
* Prefer Painting/BufferPainter for vector/antialiased output; ImageDraw for raster composition.

---

7. CHAMELEON THEMING (POLICY FOR CONTROLS)

---

* Separate behavior from appearance; render via standardized Chameleon paint fns (ChPaint/ChPaintEdge/ChPaintBody).
* Define styles with CH_STYLE; styles are static (no dynamic allocation). Controls store a non‑owning pointer to style.
* Provide SetStyle(const Style&) so apps can theme at runtime; call Refresh() when style changes.
* Event handlers (MouseEnter/Leave, Got/LostFocus, LeftDown/Up) update state + Refresh() only; do not draw there.
* In Paint: resolve state (NORMAL/HOT/PUSHED/DISABLED), delegate to ChPaint, then draw custom content (text/icon) using style‑provided Font/Color.

Minimal themable control skeleton

```
class MyCtrl : public Ctrl {
public:
    typedef MyCtrl CLASSNAME;
    MyCtrl() { style = &StyleDefault(); }
    MyCtrl& SetStyle(const Style& s) { style = &s; Refresh(); return *this; }
    virtual void Paint(Draw& w) override {
        Size sz = GetSize();
        int state = !IsShowEnabled() ? CTRL_DISABLED : (HasMouse()||HasFocus() ? CTRL_HOT : CTRL_NORMAL);
        bool pushed = HasCapture();
        ChPaint(w, sz, *style, state, pushed);
        String text = "My Control";
        Size tsz = GetTextSize(text, style->font);
        w.DrawText((sz.cx - tsz.cx)/2, (sz.cy - tsz.cy)/2, text, style->font, style->ink[state]);
    }
protected:
    const Style *style = nullptr; // non‑owning
};
```

---

8. DRAG‑AND‑DROP & CLIPBOARD (POLICY)

---

* Use AcceptFiles/AcceptImage/AcceptText in DragAndDrop to signal hover acceptance; set d.SetAction(DND_COPY or DND_MOVE) accordingly; call Refresh() for hover feedback.
* In Drop, read data with GetFiles/GetImage/GetString.
* For in‑process payloads, prefer InternalClip + AcceptInternal<T>/GetInternal<T>.
* Keep producers/consumers separate; avoid hidden global state.

---

9. DATA: JSON, XML, SERIALIZATION

---

* JSON: prefer typed Jsonize for structs/classes; only use dynamic Value/ValueMap for genuinely dynamic shapes.
* XML: build with XmlNode; parse with ParseXML/XmlParser; avoid blocking parses on GUI thread.
* Streams/Serialize: prefer U++ Stream for persistence; version serialized formats; keep DTOs small and explicit.

---

10. NETWORKING (HTTP/TCP/POP3/XML‑RPC)

---

* Keep sockets/HTTP/WebSocket code off the GUI thread.
* Use HttpRequest for client work: headers, cookies, UrlVar, Post fields/parts, progress callbacks (WhenStart/WhenContent/WhenDo).
* Minimal HTTP/SCGI server: TcpSocket.Listen → Accept loop → HttpHeader.Read → read body by ContentLength → HttpResponse(...).
* Use SocketWaitEvent (WAIT_READ/WRITE) for responsive loops; timeouts are mandatory.

---

11. GEOMETRY & MATH

---

* Centralize hit‑testing/layout math; avoid magic numbers.
* Prefer Rect/Point/Size helpers from Gtypes; use GetFitSize/GetRatioSize for scaling.
* Qualify the global clamp as ::clamp(...) to avoid ADL issues.
* Be explicit with numeric types; avoid mixed int/float in max/min without casts.

---

12. IMAGE & RENDERING WORKFLOWS

---

* Offscreen raster: ImageDraw compose → assign to Image → DrawImage in Paint.
* Antialiased vectors: BufferPainter(ImageBuffer, MODE_ANTIALIASED) for rasterized vector output.
* Recorded vectors: PaintingPainter + DrawPainting for resolution‑independent replay/print.
* Decoders: when calling StreamRaster::LoadFileAny in an EXE, add `uses plugin/png` and friends so decoders register at link time.

---

13. FORMATTING & STYLE

---

* Indentation: 4 spaces (no tabs).
* Max line length: 100–120 chars; break method chains.
* Brace: K&R (opening brace at end of line).
* One statement per line; prefer early returns.
* Order members: type/aliases → ctors/dtor → public API → protected → private.
* Keep helper functions `static` or in anonymous namespace in .cpp.

---

14. DOCUMENTATION

---

* File header: purpose, ownership notes, thread context.
* Public methods: concise summary + pre/postconditions as needed.
* Controls: document SetData/GetData semantics and WhenX events.
* Mark GUI‑thread requirements and any blocking operations.

---

15. TESTS & EXAMPLES

---

* Provide a minimal compiling example per non‑trivial module (in /examples or a demo app).
* Examples must list required headers and package `uses`; declare plugin decoders in EXE demos when loading images.
* Tests avoid GUI unless necessary; prefer fast, deterministic units.

---

16. BUILD & PACKAGING HYGIENE (.upp, assemblies)

---

* Library vs EXE packages: library has empty mainconfig ("" = ""); EXE sets "" = "GUI" (or "CONSOLE").
* Assembly → Package nests must include both your repo root and uppsrc. Missing nests → “missing package …”.
* In .upp: `uses` (packages only, not assembly prefixes), `file` list, optional `include` roots.
* Layout Designer: LAYOUTFILE path is relative to .upp include paths (not filesystem/package name).
* Don’t prefix assembly name in `uses`.
* If WinMain is undefined, you built a library as main; set a demo EXE as main package instead.

---

17. REVIEW CHECKLIST (PASTE INTO PRS)

---

[ ] Naming follows conventions; events are WhenX; control state via SetData/GetData.
[ ] Ownership explicit (value/One<>/Ptr<>); no raw new/delete; no hidden shared One<>.
[ ] GUI boundary respected; PostCallback/GuiLock used correctly; timers owned/cancelled.
[ ] Callbacks bound via THISBACK/THISBACK1…; CLASSNAME typedef present; safe captures.
[ ] Chameleon: styles via CH_STYLE; Paint stateless; no drawing in event handlers; SetStyle provided.
[ ] Colors/theme safe (SColor/AColor). Fonts/metrics measured; ellipsis/tooltip for truncation.
[ ] Minimal includes; #pragma once; headers light.
[ ] Logging & error handling sensible; ASSERTs for programmer errors.
[ ] Examples/tests updated; .upp `uses` correct; decoders added where needed.
[ ] ::clamp qualified; geometry code centralized and readable.

---

18. PITFALLS TO AVOID — FIELD NOTES (WITH FIXES)

---

Ownership & Lifetime

* Holding references/pointers to temporaries or child internals → Store values; if you must observe, use Ptr<>; never keep &refs to ephemeral state.
* Sharing One<> across owners → One<> is unique. If multiple components need access, own it in one place and expose by reference or use Ptr<>/Pte<>.
* Timer firing after teardown → KillSet() timers in destructors; clear WhenX callbacks; guard bodies with `if(GetTopWindow())` before touching UI.
* Capturing raw `this` into long‑lived callbacks → Capture Ptr<> to owner or weak guard state; disconnect in dtor.
* Global Ctrl objects → Avoid; use factories or Single<> wrappers with explicit init/shutdown.

Callbacks & Events

* Missing `typedef CLASSNAME` → THISBACK won’t bind; add `typedef MyClass CLASSNAME;` in the class.
* Doing heavy work inside WhenAction/WhenDrop → PostCallback to GUI thread for UI changes; run heavy work on workers and report back.
* Re‑entrancy surprises (callback triggers another event immediately) → Use flags or PostCallback to defer; document firing order.

GUI Threading

* Touching UI from workers → Forbidden. Use PostCallback; if absolutely necessary, wrap the minimal block with GuiLock.
* Blocking GUI with Sleep or network I/O → Move to worker; keep UI responsive.

Painting & Chameleon

* Drawing in event handlers (LeftDown/MouseMove) → Only update state + Refresh(); all drawing happens in Paint.
* Hardcoded colors/images not aware of dark mode → Use SColor.../AColor... and CH_STYLE; let Chameleon resolve.
* Unbalanced Begin/End/Clipoff → Always pair; prefer small scopes; assert with debug helpers if needed.
* Forgetting to clear alpha of fresh ImageBuffer → `Fill(~ib, RGBAZero(), ib.GetLength());` before composition.

Layouts, Packaging & Build

* LAYOUTFILE uses filesystem path → Use the `.upp` include root. See Section 1 examples.
* Prefixing assembly in `uses` → Only package names belong in `uses`.
* Building a lib as the main app (Undefined WinMain) → Set a demo EXE as main or set mainconfig to GUI/CONSOLE appropriately.
* Missing image decoders → Add plugin/png (and plugin/jpg, plugin/bmp) in the EXE `uses`.
* Package not found at build → Ensure Assembly → Package nests include both repo root and uppsrc.

Data, Streams & Serialization

* Ad‑hoc JSON with Value/ValueMap everywhere → Prefer typed Jsonize; dynamic only where shape truly varies.
* Blocking parse/serialize on GUI thread → Move to worker; update UI via PostCallback.
* Unversioned serialized formats → Add explicit version fields; write upgrade paths.

Strings & Encoding

* Confusing String/WString usage → Choose one per API boundary; convert explicitly at edges; avoid silent narrowing.
* Assuming String cannot contain NUL → It can; safe for binary; still document when binary is expected.

Networking

* HttpRequest without timeouts → Set Timeout(); use WhenStart/WhenContent for streaming; don’t hold large buffers in memory.
* Reading request bodies without checking ContentLength/chunked → Use HttpHeader helpers; always guard sizes.
* Touching UI from socket callbacks → Bounce to GUI thread with PostCallback.

Drag‑and‑Drop

* Accept* not called in DragAndDrop → Drop won’t fire; call AcceptFiles/AcceptText/AcceptImage and set d.SetAction().
* No hover feedback → Call Refresh() while hovering and paint an accepted state using d.IsAccepted().

Geometry & Math

* Off‑by‑one and negative sizes in Rect math → Use RectC/Sort and Size clamps; assert invariants; prefer helpers from Gtypes.
* Mixed int/float in comparisons → Cast explicitly; beware integer division.
* Unqualified clamp hitting ADL → Use `::clamp(...)`.

Images & Rendering

* Forgetting BufferPainter::Finish() before using the Image (when required) → Call Finish() or let scope end cleanly before using the raster.
* Drawing text at sub‑pixel integer positions → Use integer coordinates from GetTextSize to avoid blur.
* Relying on implicit premultiply state → Be explicit when mixing straight/premultiplied alpha.

Filesystem & Paths

* Using relative paths that depend on CWD → Normalize/RealizePath; base on known roots.
* Assuming case‑insensitivity on POSIX → Use PathIsEqual when comparing paths portably.

Performance & Memory

* Excessive copies of large containers/Images → Pass by const&; reserve capacity; prefer Vector over Array when possible for speed.
* Recomputing layouts/metrics in hot Paint without caching → Cache between Paints; invalidate on state changes only.

Testing & CI

* GUI tests with timers that bleed into next tests → Stop timers in teardown; use deterministic durations; isolate TopWindows.

Quick Triage Table

* “Drag works but drop never triggers” → Call Accept* in DragAndDrop and set d.SetAction().
* “Crash on exit after adding a timer” → KillSet() in dtor; clear callbacks.
* “Images load in Debug but not Release” → Missing `uses plugin/png/jpg/bmp` in main EXE.
* “THISBACK compile error” → Add `typedef CLASSNAME;` and ensure the method is a member.
* “UI freezes when fetching HTTP” → Move to worker; use HttpRequest events; update via PostCallback.

---

19. CODE SNIPPET APPENDIX (CANONICAL PATTERNS) (CANONICAL PATTERNS)

---

A) CLASSNAME + THISBACK

```
struct ImageView : TopWindow {
    typedef ImageView CLASSNAME;
    Splitter splitter; ArrayCtrl files; ImageCtrl img; Button dirUp;
    String dir;

    void DoDir(); void Enter(); void DirUpClick();

    ImageView() {
        Title("Image viewer"); Sizeable().Zoomable();
        splitter.Horz(files, img).SetPos(2600);
        Add(splitter.SizePos());
        files.WhenEnterItem  = THISBACK(Enter);
        files.WhenLeftDouble = THISBACK(DoDir);
        dirUp <<= THISBACK(DirUpClick);
    }
};
```

B) Safe timer pattern

```
struct Worker : Ctrl {
    TimeCallback tick;
    void Start() {
        tick.Set(-1, [=]{ if(GetTopWindow()) Step(); }); // periodic; guard owner
    }
    void Stop() { tick.KillSet(); }
    void Step() { /* do small unit; PostCallback updates UI */ }
    ~Worker() { Stop(); }
};
```

C) DnD accept + drop

```
virtual void DragAndDrop(Point p, PasteClip& d) override {
    if(AcceptFiles(d) || AcceptText(d) || AcceptImage(d)) {
        d.SetAction(DND_COPY);
        Refresh();
    }
}
virtual void Drop(Point p, PasteClip& d) override {
    if(IsAvailableFiles(d))
        for(const String& fn : GetFiles(d)) UseFile(fn);
    else if(AcceptImage(d)) SetImage(GetImage(d));
    else if(AcceptText(d))  SetText(GetString(d));
}
```

D) ImageBuffer alpha reset before composition

```
ImageBuffer ib(sz);
Fill(~ib, RGBAZero(), ib.GetLength());
BufferPainter bp(ib, MODE_ANTIALIASED);
// ... draw ...
Image img = ib;
```

E) HTTP download to file (streaming)

```
void HttpToFile(const String& url, const String& out) {
    FileOut fo(out);
    HttpRequest r(url);
    r.WhenContent = [&](const void *ptr, int sz){ fo.Put((const char*)ptr, sz); };
    r.Execute();
}
```

#### NOTES: 
Canonical Sources: Verify against uppsrc or official documentation.

Version-Neutral APIs: Show both old and new signatures if APIs change.

Memory and Ownership: Favor value semantics; use Null carefully.

Widgets: Avoid global/static Ctrl objects; use Single<> or factories.

Threading: Restrict GUI operations to the main thread with GuiLock.

Static Linking: Default to static binaries.

OOM Policy: U++ aborts on allocation failure; avoid try/catch for OOM.

Leak Detection: Use MemoryBreakpoint and MemoryIgnoreLeaksBlock avoid *new pointers

JSON: Use Core/JSON.h exclusively.

#### Official Documentation Links
Overview: https://www.ultimatepp.org/www$uppweb$overview$en-us.html
Docs Hub: https://www.ultimatepp.org/www$uppweb$documentation$en-us.html
Core Tutorial: https://www.ultimatepp.org/srcdoc$Core$Tutorial$en-us.html
GUI Tutorial: https://www.ultimatepp.org/srcdoc$CtrlLib$Tutorial$en-us.html
Containers: https://www.ultimatepp.org/srcdoc$Core$NTL_en-us.html
Caveats: https://www.ultimatepp.org/srcdoc$Core$Caveats_en-us.html
Leak Guide: https://www.ultimatepp.org/srcdoc$Core$Leaks_en-us.html
Design Decisions: https://www.ultimatepp.org/srcdoc$Core$Decisions_en-us.html
RichText (QTF): https://www.ultimatepp.org/srcdoc$RichText$QTF_en-us.html
GitHub Source Links
Master Branch: https://github.com/ultimatepp/ultimatepp/tree/master
Next 2025.1 Branch: https://github.com/ultimatepp/ultimatepp/tree/next2025_1
Key Files:
https://github.com/ultimatepp/ultimatepp/blob/master/uppsrc/CtrlCore/CtrlCore.h
https://github.com/ultimatepp/ultimatepp/blob/master/uppsrc/Core/Function.h
https://github.com/ultimatepp/ultimatepp/blob/master/uppsrc/Core/One.h
https://github.com/ultimatepp/ultimatepp/blob/master/uppsrc/Core/Ptr.h





-------------------------------------------------------------------------------
20) REPO-SPECIFIC UI CONTROL NOTES
-------------------------------------------------------------------------------
- Load UPP_GUIDES/u_new_controls_checklist.md when working on this repo's Ui* controls; it is the practical control guide, not just a merge checklist.
- Use UiLayoutCursor for lightweight shell/manual placement. Do not invent ad-hoc coordinate helpers per demo.
- For accordions inside scroll panels, make section-body sizes explicit and ensure the owning layout triggers scroll extent recomputation on toggle.
- Prefer baseline control/theme defaults in demos. Demo-local style helpers should only remain when they intentionally show a variant.
