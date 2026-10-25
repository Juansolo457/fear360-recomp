// D3D12 renderer: F.E.A.R.'s guest Xbox 360 D3D (default.xex, August 2006 XDK). Function
// addresses, device fields and object layouts, from the generated code; found by pairing each of
// GRAW's D3D functions (graw360-recomp native/graw_d3d.h) with its twin here and aligning the two
// instruction by instruction (docs/NATIVE_RENDERER.md). F.E.A.R. Files has its own addresses (not
// mapped yet).
//
// Against GRAW's D3D: the push cursor is at +40 (GRAW +0, PGR4 +48), the five dirty masks at +0
// (GRAW +16), the register shadow blocks 1,216 bytes lower, the constant shadows 128 lower, and
// the resource header is 24 bytes (GRAW 12, PGR4 24). The draw functions flush their dirty state
// inline (GRAW calls sub_824D9E98), and there are two EndVertices, one after each Begin.
#pragma once

#include <cstdint>

namespace fear::d3d12 {

// Guest D3D functions the native path hooks (generated sub_X; r3 = the device unless noted).
namespace fn {
// Draws.
constexpr uint32_t kBeginVertices = 0x829873D8;         // (prim, vertex count, stride) -> space
constexpr uint32_t kEndVertices = 0x82987890;           // commits BeginVertices' packets
constexpr uint32_t kEndIndexedVertices = 0x82987E60;    // the same, after BeginIndexedVertices
constexpr uint32_t kDrawVerticesUP = 0x829878B0;        // (prim, count, data, stride)
constexpr uint32_t kBeginIndexedVertices = 0x82987910;  // (prim, base, vertices, indices, format,
                                                        //  stride, out pointers) -> HRESULT
constexpr uint32_t kDrawVertices = 0x82987F38;          // (prim, start vertex, vertex count)
constexpr uint32_t kDrawIndexedVertices = 0x82988340;   // (prim, base vertex, start, count)
// Clears, resolves, Swap, tiling.
constexpr uint32_t kClearF = 0x82986F00;  // (flags, rect, color float4, z f1, stencil r8)
constexpr uint32_t kClear = 0x829872B0;   // (count, rects, flags, D3DCOLOR, z f1, stencil r9)
constexpr uint32_t kResolve = 0x82984718;  // (flags, rect, texture, point, level, slice, color,
                                           //  z f1; stencil and params on the stack)
constexpr uint32_t kSwap = 0x82974598;     // (front buffer texture, params)
constexpr uint32_t kBeginTiling = 0x82979BD0;  // (flags, count, rects, color, z f1, stencil r9)
constexpr uint32_t kEndTiling = 0x8297A0C8;    // (flags, rects, texture, color, z f1, stencil r9,
                                               //  params r10)
// Push buffer: the functions that move the cursor (device+40) to another place by assignment.
constexpr uint32_t kMakeSpace = 0x8298A988;     // submits the segment (sub_8298A7E8), starts the
                                                // next one 32-byte aligned after it
constexpr uint32_t kWrapSegment = 0x8298A620;   // back to the buffer's start
constexpr uint32_t kRestoreSegment = 0x82989998;  // the segment saved at device+13040
}  // namespace fn

// Device layout (byte offsets from the guest D3D device pointer).
namespace dev {
constexpr uint32_t kDirtyMasks = 0;      // 5 x u64, big-endian (kDirtyBits)
constexpr uint32_t kPush = 40;           // last written push-buffer dword
constexpr uint32_t kPushEnd = 44;        // end of the current segment
constexpr uint32_t kPushLimit = 48;      // past it, the next write calls MakeSpace
constexpr uint32_t kFetchConstants = 1024;  // 0x4800, 32 x 6 dwords (sub_82994650)
constexpr uint32_t kVsConstants = 1792;     // 0x4000, 256 x vec4
constexpr uint32_t kPsConstants = 5888;     // 0x4400, 256 x vec4
constexpr uint32_t kBoolLoop = 9984;        // 0x4900, 40 dwords
constexpr uint32_t kShadow2000 = 10240;     // register shadow blocks, as the draws flush them
constexpr uint32_t kShadow2100 = 10316;
constexpr uint32_t kShadow2180 = 10400;
constexpr uint32_t kShadow2200 = 10420;
constexpr uint32_t kShadow2280 = 10468;
constexpr uint32_t kShadow2300 = 10552;
constexpr uint32_t kShadow2380 = 10704;
constexpr uint32_t kRingReadPtr = 10768;    // pointer to the GPU's write-back of its read pointer
constexpr uint32_t kFlags = 10808;          // u8 (GRAW's +10432); kFlagTiling
constexpr uint8_t kFlagTiling = 0x20;       // in kFlags: from BeginTiling to EndTiling
// Not GPU registers: the window scissor SetScissorRect (sub_8296CDA8) keeps in the two dwords
// after block 0x2000's flushed ones (x | y << 16, 15-bit signed fields), as in GRAW's D3D.
constexpr uint32_t kScissorTL = 10308;
constexpr uint32_t kScissorBR = 10312;
constexpr uint32_t kIndexBuffer = 12164;    // guest D3D index buffer object
constexpr uint32_t kColorSurfaces = 12168;  // 4 x guest D3D surface object
constexpr uint32_t kDepthSurface = 12184;
constexpr uint32_t kVertexStride = 12256;   // u8: BeginVertices' vertex stride in dwords
constexpr uint32_t kTileCount = 12472;      // BeginTiling's tiles
constexpr uint32_t kTileRects = 12476;      // 15 x {x1, y1, x2, y2}
constexpr uint32_t kTileOrigins = 12716;    // 15 x the top left rounded to 32 pixels {x, y}
                                            // (8 bytes each, as in PGR4's D3D; GRAW keeps 16)
constexpr uint32_t kPendingPush = 13076;    // BeginVertices' cursor, committed by EndVertices
}  // namespace dev

// The bits of the five dirty masks (device+0, +8, +16, +24, +32) the draws' inline flush tests
// (sub_829873D8 and the other draw functions), by mask, host order.
constexpr uint64_t kDirtyBits[5] = {
    ~0ull,                 // +0: VS float constants
    ~0ull,                 // +8: PS float constants
    0x001E000000000000ull  // +16: the shader load (sub_82995558)
        | 0x0001F00000000000ull  //  register block 0x2180
        | 0x00000FFFFFFFF000ull  //  fetch constants (sub_82994650)
        | 0xFFFull,              //  register block 0x2200
    0x0FFFFF8000000000ull  // +24: register block 0x2280
        | 0x0000007FFF800000ull  //  0x2000
        | 0x00000000007FFFFCull  //  0x2100
        | 0x2ull,                //  bool/loop constants
    0x007E000000000000ull  // +32: sub_829944D0
        | 0x00003FC000000000ull  //  register block 0x2380
        | 0x0000003FFFFFFFFFull, //  0x2300
};

// Resource objects: F.E.A.R.'s D3DResource header is 24 bytes (GRAW's: 12), as in PGR4's XDK.
constexpr uint32_t kTextureFetchOffset = 28;  // D3DBaseTexture: GPU fetch constant, 6 dwords
                                              // (Swap copies it from there)
constexpr uint32_t kIndexBufferFormat = 0;    // D3DIndexBuffer: bit 31 = 32-bit indices
constexpr uint32_t kIndexBufferAddress = 24;  // virtual address of the indices

}  // namespace fear::d3d12
