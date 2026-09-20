# 0.2.5 installed on ICE QUEEN — run 35309123745

Installed on **ICE QUEEN** on 2026-09-17, over the nine bundles that were
there, using the package's own `install.ps1` — the first real-world run of the
installer, and it found a fault on that run. See below.

**This is a pull-request build, not a release.** It comes from the CI run of
[#10](https://github.com/kevkloud/bmo-mix-rack/pull/10) at `00ba5c1`, which was
green on all four jobs but **was not merged at the time of this install**, and
no `v0.2.5` tag existed. It is the first build whose `Version` reads 0.2.5.

**Nothing about the sound changed.** `00ba5c1` moves exactly one line —
`CMakeLists.txt`'s `project(... VERSION ...)`. There is no difference in any
`.cpp` or `.h` against `d1e1c38`, so anything heard here is comparable to what
was heard on the previous install.

| bundle | SHA-256 of `Contents/x86_64-win/<name>.vst3` |
|---|---|
| BMO CEQ | `351ab56672a0acfed55fa712b638154df06360cb128ef907e62f1f6e2dba79c3` |
| BMO DEQ | `630e5115be693f805292eb1cf3d4208ad4bd308d245333b91ae3a4f049b341b0` |
| BMO Dimension | `2fc674fc2d3c5f0fa58f5578a419ce204567489686e2ce51fb80d1df3080521d` |
| BMO Mix Rack | `82a27fdf5c67395851d883c7c17963ccd13d26826fcc8599aa63824dbe846cf9` |
| BMO Opto | `b06aa2c9bbe4ec94cc1f255f3d70c1385395f4e0ad83010af62d1f062ce4b1c4` |
| BMO Saturator | `c5c40d2464ad0383c7ac01964ba2cdf728f464020a0f41cd6456a4b209c955cb` |
| BMO Tune RT | `2ac3f79161fd6a37c658b7d5f1bf6760fcb45f869c1ae18aa26f5b3cd72525c7` |
| BMO Util | `10ed79597d3d18936a43200036af51a460c339ba375c25695af034d701f4d2ba` |
| LTV Comp | `ea8434852f8cd1bf76cf13767d5a4fb5a8110cd353d700ec8fc0ed54d1bb3ecb` |

All nine verified byte-for-byte against the artifact after the copy. `Version`
in every `moduleinfo.json` reads **0.2.5**; it read 0.2.4 before, which named
`cbd0939` rather than this tree.

VST3 only. No standalone builds are installed on ICE QUEEN and none were added;
the artifact carries nine of them if they are ever wanted.

## What this replaces

The previous install was `BMO-Windows` from run 35290922537, whose hashes are
in `ableton-pass-handoff-2026-09-17.md` §1. **That table is superseded by this
one.** The handoff's listening list is unaffected — the six changes it names
are all still unheard, and none of them moved in `00ba5c1`.

## The fault the installer found on its first real run

`install.ps1` refused to run, saying it needed Administrator. The folder was
writable without elevation on this machine.

The check asked whether the user was in the Administrators role, which is a
*proxy* for the real question and not the same as it: `C:\Program Files\Common
Files\VST3` is writable without elevation on a machine whose permissions have
been set that way, and refusing there sends someone to open an elevated prompt
they never needed. It now probes the directory with a temporary file and only
prints the elevation message when that fails.

This is the same mistake the macOS script had already been corrected for, in
the quarantine step — inferring a condition from something adjacent instead of
testing it. Worth remembering as a class: **ask the question you actually mean.**

Both installers were written on 2026-09-17 and neither had been run against a
real plug-in folder until now; the sandbox tests covered removal, the guard,
nesting and idempotence, but not the permission gate, because the sandbox was
always writable.
