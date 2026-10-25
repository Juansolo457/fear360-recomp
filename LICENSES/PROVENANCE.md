# License texts: where they come from

The root `LICENSE` (MIT) covers this project's own files. The texts here are the third-party
licenses that apply to code this repository carries or patches; `NOTICE` says which files each
one covers.

| File | Copied verbatim from | Applies to |
|---|---|---|
| `GPL-3.0.txt` | `LICENSE` of [fear-nx](https://github.com/infriker/fear-nx) at commit `acad80d`, which fear-nx copied verbatim from [Project8Recomp](https://github.com/R-drg/project8-sw) (commit `3393f2615deb7e8ac11ce51d2cd0e9defded5669`). [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) ships the same text. | fear-nx's renderer and its lineage (Project8Recomp, nfsmw-nx), which fear-nx licenses GPL-3.0-or-later, and what is derived from it here |
| `ReXGlue-BSD-3-Clause.txt` | `LICENSE` of the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) at the pinned commit `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518` (unchanged since the SDK's initial commit; identical to fear-nx's `LICENSES/ReXGlue-BSD-3-Clause.txt`). It carries both ReXGlue's and the Xenia project's copyright notices. | ReXGlue code: `rexglue init` scaffolding, the SDK source in `patches/rexglue-sdk-nx/`, code taken from the SDK |
| `Xenia-BSD-3-Clause.txt` | `LICENSE` of [Xenia](https://github.com/xenia-project/xenia) (master, retrieved 2026-10-24) | Code that originates in Xenia, reached through ReXGlue or fear-nx (for example the texture cache and geometry shader code in `port-nx/src/native/`) |

The MIT license of pgr4-native, which applies to the D3D12 renderer in `port-nx/src/d3d12/`, sits
next to that code as `port-nx/src/d3d12/LICENSE.pgr4-native` (copied verbatim from
[pgr4-native](https://github.com/shaunsingh/pgr4-native) `LICENSE` at commit `8ea20d5`, through
GRAW 360 Recompiled's `native/LICENSE.pgr4-native`).

These are license texts and notices only. They don't change which license a file is under;
`NOTICE` and each file's own header do.
