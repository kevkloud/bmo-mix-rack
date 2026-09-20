# Display faces

Two faces from Tom Gordon Design, licensed to Frosty and Kevin. Every product
in the suite embeds the same two, so the whole rack reads as one panel:

    TG - Minerva Black Black    labels: legends, switches, headers, presets
    TG - Blender                captions under knobs (INPUT, DRIVE, WIDTH ...)

## They are not in this repository, and this directory stays empty

The `.otf` files are gitignored on purpose. They are licensed, not open, and
this repository is public: committing them would redistribute the font files
to everyone who clones it, which no ordinary font licence permits. Embedding
a face *inside a compiled binary* is what a font licence is bought for;
publishing the file is not.

The licence is held by two named individuals. So the files should not sit in
a working copy at all, where one `git add -f`, one stray archive of the source
tree, or one clone handed to a collaborator is enough to break it. **Keep them
in a folder of your own, outside this repository, and point the build at it.**

## Pointing the build at your font folder

Once per working copy:

    scripts/set-font-dir.sh /path/to/your/fonts

That checks both faces are present and records the location in `.bmo-fontdir`
at the repository root, which is gitignored and per-machine. Every build in
that working copy then reads the faces from there. To see what is recorded,
run it with no arguments; `--clear` forgets it.

Nothing in this repository ever writes to your font folder. No build, script
or clean step touches it. The only reason to open it is to add a newly
licensed face for a future build.

CMake resolves the directory in this order, first hit wins:

    1. -DBMO_FONT_DIR=<path>   one-off; it then sticks in that build cache
    2. $BMO_FONT_DIR           a shell or a CI environment
    3. .bmo-fontdir            what set-font-dir.sh wrote
    4. assets/fonts/           this directory, which CI fills from secrets

If none of them yields both faces, CMake stops with a message naming the
missing file and the directory it looked in, rather than quietly falling back
to whatever face the machine happens to have installed.

### Note for Kevin, on merging this

Nothing you have to do, and nothing breaks. This is what changes when the
`local-font-dir` branch lands.

Your current setup — the two `.otf` files sitting in this directory — keeps
working exactly as it does today. `assets/fonts/` is still the last rung of
the search, so an existing working copy and an existing build tree both carry
on. CI is untouched: the secret-restore step writes into this directory and is
deliberately exempted from the warning below, so the Actions logs stay clean.

The one thing you will notice is a CMake warning on your next configure,
saying you are building from the in-repo copy. It is a nudge, not an error;
the build proceeds.

To clear it, and to get the licensed files out of the working copy:

    mkdir -p ~/Fonts/TG                       # anywhere outside the repo
    mv assets/fonts/*.otf ~/Fonts/TG/
    scripts/set-font-dir.sh ~/Fonts/TG

Then re-run your configure. The status line should read:

    -- BMO fonts: /Users/<you>/Fonts/TG (.bmo-fontdir)

Treat that folder as a constant: leave it alone except to drop in a newly
licensed face when we buy one. Nothing in this repository writes to it.

Why bother, given the gitignore already worked: the licence is held by the two
of us as individuals, not by the project, and this repository is public. With
the files inside the tree, the only thing between a licensed font and
publication was one ignore line — one `git add -f`, one `git archive` of the
source, one clone handed to somebody helping out. Outside the tree, no git
operation in this repository can reach them at all. Both of us doing it means
that is true on both machines.

If you would rather not move anything, do nothing. It will keep building.

## CI

CI has no such folder, so it restores the faces into this directory from two
repository secrets before the configure step:

    FONT_TG_MINERVA_BLACK_B64
    FONT_TG_BLENDER_B64

Each is gzip then base64 of the corresponding `.otf` (a secret is capped at
48 KB and the plain base64 of one of these is over it). To set or replace one,
from your own font folder:

    gzip -c "$FONTS/TG-Blender.otf" | base64 | gh secret set FONT_TG_BLENDER_B64
    gzip -c "$FONTS/TG-MinervaBlack-Black.otf" | base64 | gh secret set FONT_TG_MINERVA_BLACK_B64

### A pull request from a fork always fails these two jobs

**This is not a broken secret and there is nowhere to point one.** GitHub does
not pass repository secrets to a pull request whose head branch lives in a
fork, by design, so that untrusted code cannot read them. They can be set
perfectly well in `kevkloud/bmo-mix-rack` and still arrive empty.

The restore step says so and prints the proof -- if `this repo` and `head repo`
differ in its output, that is the whole explanation:

    this repo: kevkloud/bmo-mix-rack
    head repo: badmixesonly/bmo-mix-rack-333

Only the **plugin** matrix needs the faces. **DSP** and **Each side alone**
never touch them and pass normally, so a fork PR is not unverified -- it is
verified by everything that does not need a licensed font, plus whatever the
author ran locally. Say in the PR body what the local `ctest` came back as.

Two ways to get a green plugin build, and the choice is not a technical one:

1. **Push the branch to `kevkloud/bmo-mix-rack` and open the pull request from
   there.** A same-repo PR gets the secrets. This is what the restore step
   recommends and it needs only push rights, not admin. The cost is that the
   branch then lives in Kevin's repository rather than in the fork, which is a
   working-practice question rather than a CI one -- see the two-remote flag
   in `docs/ui-workflow-brief.md`.

2. **Merge on the other jobs.** DSP, Each side alone and the author's local
   suite. Reasonable for a change that cannot affect a plugin build, and not
   reasonable for one that can.

`pull_request_target` would also hand the secrets over and **should not be
used**: it runs the base branch's workflow with secrets in scope while
checking out the fork's code, which is the standard way this goes wrong.

## Where they are named

`CMakeLists.txt` at the root resolves the directory, lists the two files and
compiles them into the `BmoAssets` binary-data target; `core/ui/Fonts.cpp`
loads them from there. Nothing else in the tree refers to the files by name.
