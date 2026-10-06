# 0.2.6 installed on ICE QUEEN — run 37388687496 (tag v0.2.6-rc1)

Installed on **ICE QUEEN** on 2026-10-05 at 20:50 local, over the thirteen
bundles that were there, bundle by bundle with `Copy-Item -Force` per file,
and every bundle verified by SHA-256 against the artifact after the copy.

**This is the release build of commit 8e26f44**, which carries both tags
`v0.2.6-rc1` and `v0.2.6`. The artifact installed is `BMO-Windows` from the
rc1 tag's run (link-time optimisation on, as every tag build), downloaded on
2026-10-05 with the owner's approval; the `v0.2.6` tag's own run was still
building at the time of the install and its artifact is to be compared with
this one by hash when it lands (same source, same configuration). `Version`
in every `moduleinfo.json` reads **0.2.6** (39 of 39 files).

**What the bundles that were there were:** not 0.2.5. Twelve of the thirteen
had been overwritten by review builds between 2026-10-03 15:46 and 20:09
(see `testing-notes/` and the install-after-build change in #43); only BMO
Tune RT still carried its 0.2.5 hash. The owner chose not to restore 0.2.5
and to test 0.2.6 directly.

| bundle | SHA-256 of `Contents/x86_64-win/<name>.vst3` |
|---|---|
| BMO CEQ | `209bf2b5cbf9b25dd8e459ff8230c37531302c794fcfc6081a636fad2fd041be` |
| BMO DEQ | `e150a73f66529ec6c6d9f9bbdc88ea2054082dd30d39e53c4869af137c0185af` |
| BMO Defang | `4a264e0e3e1c4e0ee06effe05067e322dfccf9f1c8c800e8b05a22a595f0e9f1` |
| BMO Dimension | `f3828e854a37517fc57471885f7b00c54cbc80e994caa8db7ca43c4da77664f9` |
| BMO Dwell | `7737bd380e001f758465857d217bbc1136aea7ed8c7bd6766bd6aafad573246d` |
| BMO FET | `2ad2eb38d8d0415a99dec0b812b20c1e1b86efbc3b9296cc9284672f84d8b51e` |
| BMO Linger | `56dc8cba8eebef33319e037c2f05f86d589013c2c7552e3879032a602ceff637` |
| BMO Mix Rack | `7f10320bfb5647524f0e70d807d830b3b22af18dc125856fbc6961b94ea1b3e2` |
| BMO Opto | `c0b6e0a725648d39db8ceed402bef753a9e9f15a611a6d3ac50d3796ad4a4dc8` |
| BMO Saturator | `e88160375390c424e9fc770e380fb4703751f7674847ac4825ea638e45659e3a` |
| BMO Tune RT | `36a4f11090332ac8fe9c60f1b6d092cfca55c509e3823dfbc28fe76fe0d6fcd2` |
| BMO Util | `2818ba5a3b7a1e8e1f41b36b8ecdd89494cb4e4ee42e405c82eb734e1f13640` |
| LTV Comp | `1abe2aeb98a92653984ae19840cd9d79c1ef21c0b3a5be3ab0227afc5d0c987a` |

VST3 only, as before: the artifact's thirteen Standalone builds were not
installed.

## How it was done, and why not with install.ps1

The host was running during the install (the owner was away from the
machine with it open), so the package's `install.ps1` was not used: it
removes each bundle folder before copying, and a bundle the host holds open
cannot be removed. Copying file by file over the existing bundles and then
hashing every bundle answers the same question the installer would — is the
right binary in place — and did: thirteen of thirteen match. **The host has
to be restarted before anything is heard**, since a plugin it had loaded
stays the old binary in memory until then.

## What the tag build is

Both tags sit on 8e26f44, `main` after #50 set the version. The rc1 run
(`37388687496`) was green on all four jobs; the Windows job took 72 minutes
and the macOS job 55 (the first tag builds since 0.2.5). Its audio
fingerprint, read from the job log, matches the non-optimised build of the
same source on ICE QUEEN in every product, configuration, latency and peak
figure and every pass-through flag (108 of 108 lines); 12 of the 108 hashes
are identical and 96 differ in the last bits, which is what link-time
optimisation does (`testing-notes/lto-null-2026-10-01.md`).

## What this is for

The 0.2.6 listening pass on ICE QUEEN, by the owner, from the checklist he
holds outside the repository. The build does not go to beta testers: the
owner decided on 2026-10-05 that nothing is distributed before the 0.3.0
release, once BMO Linger is complete and every module has been heard.
