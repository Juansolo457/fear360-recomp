// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Project8Recomp's generic Xenos Vulkan renderer (GPL-3.0),
// itself based on nfsmw-nx.
//
// Public entry points for F.E.A.R. NX's app-owned Xenos graphics system.
#pragma once

#include <cstdint>
#include <memory>

#include <rex/system/interfaces/graphics.h>

namespace fear_nx::native {

// Whether the nx_renderer cvar asks for the native renderer.
bool Enabled();

// Swaps the native renderer has presented since startup (0 when disabled).
uint64_t SwapsPresented();

// The 256-entry gamma ramp the game set (DC_LUT_*: 10 bits per channel, red in bits 20-29), for
// the D3D12 renderer's presenter (port-nx/src/d3d12); null before the graphics system starts.
// The ring thread writes it as it runs the guest's register writes: a ramp read during an update
// shows for one frame. The PWL ramp is not tracked.
const uint32_t* GammaRamp256();

std::unique_ptr<rex::system::IGraphicsSystem> CreateGraphicsSystem();

}  // namespace fear_nx::native
