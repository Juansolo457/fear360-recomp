# How changes get into master

`master` is what releases are tagged from, so nothing reaches it except through a reviewed pull
request. GitHub enforces this for everyone, the maintainer included: no direct pushes, no force
pushes, and the branch can't be deleted.

## The process

1. **Branch from `master`**, one branch per piece of work, named for it (`native-renderer`,
   `version-badge`). Contributors without write access fork the repository and branch there.
2. **Commit on the branch.** Keep commits focused, with messages that say what changed and why.
   Never commit game material: no game files, executables, archives, generated translations of the
   game's code or logs (see [Legal](README.md#legal)). Run `python -I tools/audit_repo.py` before
   pushing.
3. **Open a pull request into `master`.** Its description says what changed, what was built
   (which games), and what was tested in play and what wasn't.
4. **Before it can merge:**
   - the `audit` check passes (`.github/workflows/audit.yml`, the same script);
   - the branch is up to date with `master`;
   - every review conversation is resolved. CodeRabbit reviews each pull request. Fix what holds
     up; answer what doesn't with the reason; then resolve the thread.
5. **Merge with a merge commit** (the "Create a merge commit" button), so a pull request's
   history stays together in `master`.

## Releases

1. A pull request sets the release number in `VERSION`; the games show it with the build's commit
   (`port-nx/src/fear_version.h`).
2. Once it's merged, the release is tagged on `master` (`vX.Y.Z`) and published on GitHub.
3. **Releases carry the built program, never game data.** From 1.5 on, a release attaches the
   zips `python -I tools/steamdeck/make_package.py --release` (Windows) and `--release --linux`
   (Linux, from `tools/build-deck.sh`) make from the tagged commit: F.E.A.R.'s program and its
   libraries, the launcher, a README and `tools/xdvdfs.py`. Players unpack their own disc into it
   ([tools/release](tools/release)). Never attach game files or the private test packages the
   script's other modes make (they hold game data). Building from source stays documented in
   [docs/SETUP.md](docs/SETUP.md).
