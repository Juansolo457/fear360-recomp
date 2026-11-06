// F.E.A.R. 360 - is the loaded executable the one the hooks were written for? Every game fix
// reaches the game through a hook on an address (a [[entrypoint.midasm_hook]] in
// fear_manifest.toml, or a REX_HOOK_RAW), and an address only means something in one executable:
// F.E.A.R. title 565507D9, default.xex SHA-256 e7fccdf6...2792, or F.E.A.R. Files' a51deed1...c7eb
// (tools/build-nx.ps1 has both in full). On another one the hooks would still run, on whatever
// code is at their addresses.
//
// The check: hook_sites_fear.inc and hook_sites_files.inc (tools/hook_sites.py writes them) hold,
// for every hook, its address and a hash of the 32 bytes of game code there. Run() hashes the
// same bytes of the loaded image and logs each hook whose code differs. A mismatch doesn't stop
// the game for now: the log says so, and the version badge stays up with a notice
// (fear_version.cpp). tools/hook_sites.py --check (CI) keeps the tables in step with the hooks.

#pragma once

namespace fear::image_check {

// In ReXApp::OnPostLoadXexImage: the image is in guest memory, the game hasn't started.
void Run();

// Whether Run() found a hook whose code isn't the supported executable's.
bool Mismatch();

}  // namespace fear::image_check
