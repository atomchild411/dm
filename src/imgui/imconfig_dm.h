#pragma once

// Dear ImGui's settings for this program (IMGUI_USER_CONFIG).

// ImGui packs a colour into 32 bits with red in the low byte, so that the
// bytes are R, G, B, A in a little-endian CPU's memory, as OpenGL reads
// vertex colours and RGBA textures.  On a big-endian CPU (IRIX on MIPS) the
// same order needs red in the high byte.
#if defined(__BIG_ENDIAN__) || defined(__MIPSEB__) || \
	(defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define IM_COL32_R_SHIFT    24
#define IM_COL32_G_SHIFT    16
#define IM_COL32_B_SHIFT    8
#define IM_COL32_A_SHIFT    0
#define IM_COL32_A_MASK     0x000000FF
#endif
