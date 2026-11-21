// F.E.A.R. PC - draws a controller button in a line of text (FEAR_PC_PAD). See fear_pad.h.
//
// CLTGUIString leaves room for each button character in a string and calls FearPadGlyphRender
// with the square to fill. The buttons come from one picture, Interface\FearPC\PadGlyphs.dds:
// cells of equal size, fearpc::kGlyphColumns across, one per character from U+E000, the Xbox
// set first and the PlayStation set after it (pc/core layout.h). pc/tools/make_glyphs.py draws
// it; it is our own art, not either console's.
#include "stdafx.h"

#include "fear_pad.h"

#include "iltdrawprim.h"
#include "ilttexturemgr.h"
#include "ltquaduvutils.h"
#include "fearpc/layout.h"

namespace
{
	const char* const kPicture = "Interface\\FearPC\\PadGlyphs.dds";
	// Held for the life of the game: releasing it when the DLL unloads would call into an
	// engine that may already be gone.
	HTEXTURE g_hPicture = NULL;
	bool g_bLoaded = false;
}

bool FearPadGlyphsFailed()
{
	return g_bLoaded && !g_hPicture;
}

namespace
{
bool Load()
{
	if (!g_bLoaded)
	{
		g_bLoaded = true;
		if (g_pILTTextureMgr->CreateTextureFromFile(g_hPicture, kPicture) != LT_OK)
			g_hPicture = NULL;
		if (!g_hPicture)
			g_pLTClient->CPrint("Pads: no %s; button prompts keep the key names.", kPicture);
	}
	return g_hPicture != NULL;
}
}


void FearPadGlyphRender(uint32 nGlyph, float fX, float fY, float fSize, uint32 nColor)
{
	if (!Load() || nGlyph >= (uint32)fearpc::kGlyphsPerSet)
		return;

	// A string made while one set showed can outlive it; draw whatever shows now, or the Xbox
	// set if prompts have gone back to key names.
	const uint32 nCell = nGlyph + (FearPadGlyphSet() == 2 ? fearpc::kGlyphsPerSet : 0);
	const float fCell = 1.0f / fearpc::kGlyphColumns;

	LTPoly_GT4 poly;
	DrawPrimSetXYWH(poly, fX, fY, fSize, fSize);
	// The button keeps its own colors and takes the text's fade.
	DrawPrimSetRGBA(poly, 0xFF, 0xFF, 0xFF, (uint8)(nColor >> 24));
	g_pDrawPrim->SetTexture(g_hPicture);
	SetupQuadUVs(poly, g_hPicture, (nCell % fearpc::kGlyphColumns) * fCell, (nCell / fearpc::kGlyphColumns) * fCell, fCell, fCell);
	g_pDrawPrim->DrawPrim(&poly, 1);
}
