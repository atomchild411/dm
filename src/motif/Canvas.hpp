#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <X11/Xlib.h>

// Colours are 0x00RRGGBB.
typedef uint32_t Rgb;

inline Rgb MakeRgb(int r, int g, int b) { return (Rgb) ((r << 16) | (g << 8) | b); }
inline int RgbR(Rgb c) { return (c >> 16) & 0xff; }
inline int RgbG(Rgb c) { return (c >> 8) & 0xff; }
inline int RgbB(Rgb c) { return c & 0xff; }
Rgb LerpRgb(Rgb a, Rgb b, int num, int den);

// How client-side pixels become pixels of one X visual: any TrueColor
// layout, or a colour cube in a PseudoColor colormap (ordered dithering).
class PixelFormat
{
public:
	bool Init(Display* dpy, Visual* visual, int depth, Colormap cmap);

	Display* GetDisplay() const { return m_dpy; }
	Visual* GetVisual() const { return m_visual; }
	int GetDepth() const { return m_depth; }

	// Converts w x h pixels (stride in pixels) into a new XImage, dithered
	// when the visual needs it.  Free with XDestroyImage.
	XImage* MakeImage(const Rgb* px, int stride, int w, int h, int originX, int originY) const;
	// The same into the top left of an image of this visual.
	void Convert(XImage* img, const Rgb* px, int stride, int w, int h, int originX, int originY) const;

private:
	Display* m_dpy = nullptr;
	Visual* m_visual = nullptr;
	int m_depth = 0;
	bool m_trueColor = false;
	int m_shift[3] = { 0, 0, 0 }; // left shift of the 8-bit value's top bit
	int m_bits[3] = { 0, 0, 0 };
	unsigned long m_mask[3] = { 0, 0, 0 };
	// colour cube for PseudoColor
	int m_levels = 0;
	std::vector<unsigned long> m_cube;
	// [channel][dither threshold 0..15][value 0..255]: a TrueColor pixel's
	// bits for that channel, or a PseudoColor cube index's share
	std::vector<uint32_t> m_dither;
	void MakeDitherTables();
};

// A client-side RGB image that the UI draws into, then shows in a window.
class Canvas
{
public:
	void Resize(int w, int h);
	int Width() const { return m_w; }
	int Height() const { return m_h; }

	// Drawing is limited to the clip rectangle.
	void SetClip(int x, int y, int w, int h);
	void ClearClip();

	void Fill(int x, int y, int w, int h, Rgb c);
	void FillRounded(int x, int y, int w, int h, int radius, Rgb c);
	void Frame(int x, int y, int w, int h, Rgb c);
	void HLine(int x, int y, int w, Rgb c) { Fill(x, y, w, 1, c); }
	void VLine(int x, int y, int h, Rgb c) { Fill(x, y, 1, h, c); }

	// An 8-bit coverage mask (stride bytes per row) drawn in colour c.
	void BlendMask(int x, int y, const uint8_t* mask, int w, int h, int stride, Rgb c);

	// ARGB pixels (alpha in the top byte) blended over the canvas.
	void BlendArgb(int x, int y, const uint32_t* px, int w, int h, int stride);
	// The same inside a circle of the image's size (avatars).
	void BlendArgbCircle(int x, int y, const uint32_t* px, int w, int h, int stride);
	// A disc of one colour filling w x h, at the given opacity (0..255).
	void FillCircle(int x, int y, int w, int h, Rgb c, int alpha = 255);

	// Moves the whole picture up by dy rows (down when dy < 0); the rows
	// it leaves are not cleared.
	void Scroll(int dy);

	// Shows the rectangle (x, y, w, h) of the canvas at (dx, dy) in a
	// drawable.  A dithered visual's pattern is anchored ditherDy rows
	// above the drawable's top (a scrolled view passes its scroll offset,
	// so pixels it moves keep matching pixels drawn afresh).
	void Present(const PixelFormat& fmt, Drawable d, GC gc, int x, int y, int w, int h, int dx, int dy, int ditherDy = 0) const;

private:
	bool ClipRect(int& x, int& y, int& w, int& h) const;

	// The image Present converts into: in memory shared with the X server
	// (MIT-SHM) when it can be, so the pixels need not travel the socket.
	struct Upload;
	mutable std::shared_ptr<Upload> m_upload;

	int m_w = 0, m_h = 0;
	std::vector<Rgb> m_px;
	int m_cx0 = 0, m_cy0 = 0, m_cx1 = 0, m_cy1 = 0;
};
