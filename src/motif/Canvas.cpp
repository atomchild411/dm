#include "Canvas.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include "Perf.hpp"

Rgb LerpRgb(Rgb a, Rgb b, int num, int den)
{
	int r = RgbR(a) + (RgbR(b) - RgbR(a)) * num / den;
	int g = RgbG(a) + (RgbG(b) - RgbG(a)) * num / den;
	int bl = RgbB(a) + (RgbB(b) - RgbB(a)) * num / den;
	return MakeRgb(r, g, bl);
}

// 4x4 ordered dither thresholds, 0..15
static const int g_bayer[4][4] = {
	{  0,  8,  2, 10 },
	{ 12,  4, 14,  6 },
	{  3, 11,  1,  9 },
	{ 15,  7, 13,  5 },
};

static int HighBit(unsigned long m)
{
	int b = -1;
	while (m) { b++; m >>= 1; }
	return b;
}

static int BitCount(unsigned long m)
{
	int n = 0;
	while (m) { n += m & 1; m >>= 1; }
	return n;
}

bool PixelFormat::Init(Display* dpy, Visual* visual, int depth, Colormap cmap)
{
	m_dpy = dpy;
	m_visual = visual;
	m_depth = depth;
	m_trueColor = visual->c_class == TrueColor || visual->c_class == DirectColor;

	if (m_trueColor)
	{
		unsigned long masks[3] = { visual->red_mask, visual->green_mask, visual->blue_mask };
		for (int i = 0; i < 3; i++) {
			m_mask[i] = masks[i];
			m_bits[i] = BitCount(masks[i]);
			m_shift[i] = HighBit(masks[i]); // position of the top bit
		}
		MakeDitherTables();
		return true;
	}

	// PseudoColor / StaticColor / GrayScale: a colour cube, as big as the
	// colormap lets us have.
	for (int levels = 6; levels >= 2; levels--)
	{
		std::vector<unsigned long> cube;
		bool ok = true;
		for (int r = 0; r < levels && ok; r++)
		for (int g = 0; g < levels && ok; g++)
		for (int b = 0; b < levels && ok; b++)
		{
			XColor xc;
			xc.red   = (unsigned short) (r * 65535 / (levels - 1));
			xc.green = (unsigned short) (g * 65535 / (levels - 1));
			xc.blue  = (unsigned short) (b * 65535 / (levels - 1));
			xc.flags = DoRed | DoGreen | DoBlue;
			if (!XAllocColor(dpy, cmap, &xc))
				ok = false;
			else
				cube.push_back(xc.pixel);
		}
		if (ok) {
			m_levels = levels;
			m_cube.swap(cube);
			MakeDitherTables();
			return true;
		}
		if (!cube.empty())
			XFreeColors(dpy, cmap, cube.data(), (int) cube.size(), 0);
	}

	// Nothing could be allocated: black and white.
	m_levels = 2;
	m_cube.assign(8, BlackPixel(dpy, DefaultScreen(dpy)));
	m_cube[7] = WhitePixel(dpy, DefaultScreen(dpy));
	MakeDitherTables();
	return false;
}

void PixelFormat::MakeDitherTables()
{
	m_dither.assign(3 * 16 * 256, 0);
	for (int i = 0; i < 3; i++)
	for (int t = 0; t < 16; t++)
	for (int q = 0; q < 256; q++)
	{
		uint32_t v;
		if (m_trueColor) {
			int bits = m_bits[i];
			if (bits < 8) {
				// ordered dither between the two nearest levels
				int top = (1 << bits) - 1;
				int f = q * top * 16 / 255;
				int l = f / 16 + ((f & 15) > t ? 1 : 0);
				if (l > top) l = top;
				v = (uint32_t) (((unsigned long) l << (m_shift[i] - bits + 1)) & m_mask[i]);
			}
			else {
				int sft = m_shift[i] - 7;
				v = (uint32_t) ((sft >= 0 ? (unsigned long) q << sft : (unsigned long) q >> -sft) & m_mask[i]);
			}
		}
		else {
			// scale to 0..(n-1)*16, then threshold; weighted for the cube
			int n = m_levels;
			int f = q * (n - 1) * 16 / 255;
			int l = f / 16 + ((f & 15) > t ? 1 : 0);
			if (l > n - 1) l = n - 1;
			v = (uint32_t) (l * (i == 0 ? n * n : i == 1 ? n : 1));
		}
		m_dither[(i * 16 + t) * 256 + q] = v;
	}
}

unsigned long PixelFormat::PixelOf(Rgb c) const
{
	int v[3] = { RgbR(c), RgbG(c), RgbB(c) };
	if (m_trueColor) {
		unsigned long p = 0;
		for (int i = 0; i < 3; i++) {
			int s = m_shift[i] - 7;
			unsigned long x = s >= 0 ? (unsigned long) v[i] << s : (unsigned long) v[i] >> -s;
			p |= x & m_mask[i];
		}
		return p;
	}
	int n = m_levels;
	int r = (v[0] * (n - 1) + 127) / 255, g = (v[1] * (n - 1) + 127) / 255, b = (v[2] * (n - 1) + 127) / 255;
	return m_cube[(r * n + g) * n + b];
}

XImage* PixelFormat::MakeImage(const Rgb* px, int stride, int w, int h, int originX, int originY) const
{
	XImage* img = XCreateImage(m_dpy, m_visual, m_depth, ZPixmap, 0, NULL, w, h, 32, 0);
	if (!img)
		return NULL;
	img->data = (char*) malloc((size_t) img->bytes_per_line * h);
	if (!img->data) {
		XDestroyImage(img);
		return NULL;
	}
	Convert(img, px, stride, w, h, originX, originY);
	return img;
}

void PixelFormat::Convert(XImage* img, const Rgb* px, int stride, int w, int h, int originX, int originY) const
{
	bool hostMSB;
	{
		uint16_t probe = 1;
		hostMSB = *(uint8_t*) &probe == 0;
	}
	bool nativeOrder = (img->byte_order == MSBFirst) == hostMSB;

	if (m_trueColor && m_bits[0] >= 8 && m_bits[1] >= 8 && m_bits[2] >= 8 &&
		img->bits_per_pixel == 32 && nativeOrder)
	{
		// 24-bit colour: no dithering, one store per pixel.
		int sr = m_shift[0] - 7, sg = m_shift[1] - 7, sb = m_shift[2] - 7;
		for (int y = 0; y < h; y++) {
			const Rgb* s = px + (size_t) y * stride;
			uint32_t* d = (uint32_t*) (img->data + (size_t) y * img->bytes_per_line);
			for (int x = 0; x < w; x++) {
				Rgb c = s[x];
				uint32_t r = RgbR(c), g = RgbG(c), b = RgbB(c);
				d[x] = (sr >= 0 ? r << sr : r >> -sr) | (sg >= 0 ? g << sg : g >> -sg) | (sb >= 0 ? b << sb : b >> -sb);
			}
		}
		return;
	}

	// Other layouts: each pixel's value worked out, then stored directly
	// for 8, 16 and 32 bits a pixel (XPutPixel, a call a pixel, otherwise).
	int bpp = img->bits_per_pixel;
	bool msb = img->byte_order == MSBFirst;
	for (int y = 0; y < h; y++)
	{
		const Rgb* s = px + (size_t) y * stride;
		uint8_t* row = (uint8_t*) img->data + (size_t) y * img->bytes_per_line;
		const int* bayer = g_bayer[(y + originY) & 3];
		for (int x = 0; x < w; x++)
		{
			Rgb c = s[x];
			int t = bayer[(x + originX) & 3]; // 0..15
			const uint32_t* d = &m_dither[t * 256];
			uint32_t a = d[RgbR(c)], b = d[16 * 256 + RgbG(c)], e = d[32 * 256 + RgbB(c)];
			unsigned long p = m_trueColor ? (unsigned long) (a | b | e) : m_cube[a + b + e];
			switch (bpp) {
				case 8:
					row[x] = (uint8_t) p;
					break;
				case 16: {
					uint8_t* q = row + 2 * x;
					q[msb ? 0 : 1] = (uint8_t) (p >> 8);
					q[msb ? 1 : 0] = (uint8_t) p;
					break;
				}
				case 32: {
					uint8_t* q = row + 4 * x;
					for (int k = 0; k < 4; k++)
						q[msb ? k : 3 - k] = (uint8_t) (p >> (24 - 8 * k));
					break;
				}
				default:
					XPutPixel(img, x, y, p);
			}
		}
	}
}

void Canvas::Resize(int w, int h)
{
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	m_w = w;
	m_h = h;
	m_px.assign((size_t) w * h, 0xffffff);
	ClearClip();
}

void Canvas::SetClip(int x, int y, int w, int h)
{
	m_cx0 = std::max(0, x);
	m_cy0 = std::max(0, y);
	m_cx1 = std::min(m_w, x + w);
	m_cy1 = std::min(m_h, y + h);
}

void Canvas::ClearClip()
{
	m_cx0 = m_cy0 = 0;
	m_cx1 = m_w;
	m_cy1 = m_h;
}

bool Canvas::ClipRect(int& x, int& y, int& w, int& h) const
{
	int x1 = std::min(x + w, m_cx1), y1 = std::min(y + h, m_cy1);
	x = std::max(x, m_cx0);
	y = std::max(y, m_cy0);
	w = x1 - x;
	h = y1 - y;
	return w > 0 && h > 0;
}

void Canvas::Fill(int x, int y, int w, int h, Rgb c)
{
	if (!ClipRect(x, y, w, h))
		return;
	for (int j = 0; j < h; j++) {
		Rgb* d = &m_px[(size_t) (y + j) * m_w + x];
		std::fill(d, d + w, c);
	}
}

void Canvas::FillRounded(int x, int y, int w, int h, int radius, Rgb c)
{
	if (radius * 2 > h) radius = h / 2;
	if (radius * 2 > w) radius = w / 2;
	for (int j = 0; j < h; j++)
	{
		int inset = 0;
		int dy = j < radius ? radius - j - 1 : (j >= h - radius ? j - (h - radius) : -1);
		if (dy >= 0) {
			// how far the circle's edge is from the corner at this row
			int r2 = radius * radius, k = 0;
			while (k < radius && (radius - k - 1) * (radius - k - 1) + dy * dy >= r2)
				k++;
			inset = k;
		}
		Fill(x + inset, y + j, w - 2 * inset, 1, c);
	}
}

void Canvas::Frame(int x, int y, int w, int h, Rgb c)
{
	Fill(x, y, w, 1, c);
	Fill(x, y + h - 1, w, 1, c);
	Fill(x, y, 1, h, c);
	Fill(x + w - 1, y, 1, h, c);
}

static inline Rgb Blend(Rgb dst, Rgb src, int a)
{
	if (a >= 255) return src;
	if (a <= 0) return dst;
	int ia = 255 - a;
	int r = (RgbR(src) * a + RgbR(dst) * ia + 127) / 255;
	int g = (RgbG(src) * a + RgbG(dst) * ia + 127) / 255;
	int b = (RgbB(src) * a + RgbB(dst) * ia + 127) / 255;
	return MakeRgb(r, g, b);
}

void Canvas::BlendMask(int x, int y, const uint8_t* mask, int w, int h, int stride, Rgb c)
{
	int cx = x, cy = y, cw = w, ch = h;
	if (!ClipRect(cx, cy, cw, ch))
		return;
	for (int j = 0; j < ch; j++) {
		const uint8_t* m = mask + (size_t) (cy - y + j) * stride + (cx - x);
		Rgb* d = &m_px[(size_t) (cy + j) * m_w + cx];
		for (int i = 0; i < cw; i++)
			if (m[i])
				d[i] = Blend(d[i], c, m[i]);
	}
}

void Canvas::BlendArgb(int x, int y, const uint32_t* px, int w, int h, int stride)
{
	int cx = x, cy = y, cw = w, ch = h;
	if (!ClipRect(cx, cy, cw, ch))
		return;
	for (int j = 0; j < ch; j++) {
		const uint32_t* s = px + (size_t) (cy - y + j) * stride + (cx - x);
		Rgb* d = &m_px[(size_t) (cy + j) * m_w + cx];
		for (int i = 0; i < cw; i++)
			d[i] = Blend(d[i], s[i] & 0xffffff, (int) (s[i] >> 24));
	}
}

// The coverage (0..16) of each pixel of a circle inscribed in w x h, 4x4
// supersampled; made once for each size.
static const uint8_t* CircleCoverage(int w, int h)
{
	static std::map<std::pair<int, int>, std::vector<uint8_t>> cache;
	auto it = cache.find(std::make_pair(w, h));
	if (it != cache.end())
		return it->second.data();
	std::vector<uint8_t>& cov = cache[std::make_pair(w, h)];
	cov.resize((size_t) w * h);
	float rx = w / 2.0f, ry = h / 2.0f;
	for (int sy = 0; sy < h; sy++)
		for (int sx = 0; sx < w; sx++) {
			int inside = 0;
			for (int sj = 0; sj < 4; sj++)
			for (int si = 0; si < 4; si++) {
				float fx = (sx + (si + 0.5f) / 4 - rx) / rx;
				float fy = (sy + (sj + 0.5f) / 4 - ry) / ry;
				inside += fx * fx + fy * fy <= 1.0f;
			}
			cov[(size_t) sy * w + sx] = (uint8_t) inside;
		}
	return cov.data();
}

void Canvas::BlendArgbCircle(int x, int y, const uint32_t* px, int w, int h, int stride)
{
	int cx = x, cy = y, cw = w, ch = h;
	if (!ClipRect(cx, cy, cw, ch))
		return;
	const uint8_t* cov = CircleCoverage(w, h);
	for (int j = 0; j < ch; j++) {
		int sy = cy - y + j;
		const uint32_t* s = px + (size_t) sy * stride + (cx - x);
		const uint8_t* k = cov + (size_t) sy * w + (cx - x);
		Rgb* d = &m_px[(size_t) (cy + j) * m_w + cx];
		for (int i = 0; i < cw; i++)
			if (k[i])
				d[i] = Blend(d[i], s[i] & 0xffffff, (int) (s[i] >> 24) * k[i] / 16);
	}
}

void Canvas::FillCircle(int x, int y, int w, int h, Rgb c, int alpha)
{
	int cx = x, cy = y, cw = w, ch = h;
	if (!ClipRect(cx, cy, cw, ch))
		return;
	const uint8_t* cov = CircleCoverage(w, h);
	for (int j = 0; j < ch; j++) {
		const uint8_t* k = cov + (size_t) (cy - y + j) * w + (cx - x);
		Rgb* d = &m_px[(size_t) (cy + j) * m_w + cx];
		for (int i = 0; i < cw; i++)
			if (k[i])
				d[i] = Blend(d[i], c, alpha * k[i] / 16);
	}
}

struct Canvas::Upload
{
	Display* dpy = nullptr;
	Visual* visual = nullptr;
	XImage* img = nullptr;
	XShmSegmentInfo shm;
	bool attached = false;

	~Upload()
	{
		if (!img)
			return;
		if (attached) {
			XShmDetach(dpy, &shm);
			XSync(dpy, False);
		}
		if (attached || shm.shmaddr) {
			shmdt(shm.shmaddr);
			img->data = NULL; // not malloc'd: XDestroyImage must not free it
		}
		XDestroyImage(img);
	}
};

// MIT-SHM: -1 not tried yet, 0 not available (or DM_NO_SHM), 1 working.
static int g_shm = -1;
static bool g_shmFailed;

static int ShmErrorHandler(Display*, XErrorEvent*)
{
	g_shmFailed = true;
	return 0;
}

// A w x h image in memory shared with the X server, or null when the server
// cannot use it (another machine's display, no extension).
static XImage* MakeShmImage(Display* dpy, Visual* visual, int depth, int w, int h, XShmSegmentInfo& shm, bool& attached)
{
	attached = false;
	shm.shmaddr = NULL;
	if (g_shm < 0)
		g_shm = !getenv("DM_NO_SHM") && XShmQueryExtension(dpy) ? 1 : 0;
	if (!g_shm)
		return NULL;

	XImage* img = XShmCreateImage(dpy, visual, depth, ZPixmap, NULL, &shm, w, h);
	if (!img)
		return NULL;
	shm.shmid = shmget(IPC_PRIVATE, (size_t) img->bytes_per_line * h, IPC_CREAT | 0600);
	if (shm.shmid < 0) {
		XDestroyImage(img);
		return NULL;
	}
	shm.shmaddr = img->data = (char*) shmat(shm.shmid, NULL, 0);
	shm.readOnly = False;
	if (shm.shmaddr == (char*) -1) {
		shmctl(shm.shmid, IPC_RMID, NULL);
		shm.shmaddr = NULL;
		img->data = NULL;
		XDestroyImage(img);
		return NULL;
	}

	// a server on another machine answers the attach with an error
	XSync(dpy, False);
	g_shmFailed = false;
	XErrorHandler old = XSetErrorHandler(ShmErrorHandler);
	XShmAttach(dpy, &shm);
	XSync(dpy, False);
	XSetErrorHandler(old);
	shmctl(shm.shmid, IPC_RMID, NULL); // freed once both sides detach
	if (g_shmFailed) {
		g_shm = 0;
		shmdt(shm.shmaddr);
		shm.shmaddr = NULL;
		img->data = NULL;
		XDestroyImage(img);
		return NULL;
	}
	attached = true;
	return img;
}

void Canvas::Present(const PixelFormat& fmt, Drawable dr, GC gc, int x, int y, int w, int h, int dx, int dy) const
{
	if (x < 0) { w += x; dx -= x; x = 0; }
	if (y < 0) { h += y; dy -= y; y = 0; }
	if (x + w > m_w) w = m_w - x;
	if (y + h > m_h) h = m_h - y;
	if (w <= 0 || h <= 0)
		return;
	Perf::Scope perf(Perf::PRESENT);
	Display* dpy = fmt.GetDisplay();

	// The canvas-sized shared image, made again when the canvas grew.
	if (g_shm != 0 && (!m_upload || m_upload->dpy != dpy || m_upload->visual != fmt.GetVisual() ||
		m_upload->img->width < w || m_upload->img->height < h))
	{
		m_upload.reset();
		std::shared_ptr<Upload> u(new Upload);
		u->dpy = dpy;
		u->visual = fmt.GetVisual();
		u->img = MakeShmImage(dpy, fmt.GetVisual(), fmt.GetDepth(), std::max(w, m_w), std::max(h, m_h), u->shm, u->attached);
		if (u->img)
			m_upload = u;
	}
	if (m_upload) {
		fmt.Convert(m_upload->img, &m_px[(size_t) y * m_w + x], m_w, w, h, dx, dy);
		XShmPutImage(dpy, dr, gc, m_upload->img, 0, 0, dx, dy, w, h, False);
		XSync(dpy, False); // the server has read the pixels before they change again
		return;
	}

	// In bands, so a full-window update never needs one huge image.
	const int band = 64;
	for (int by = 0; by < h; by += band)
	{
		int bh = std::min(band, h - by);
		XImage* img = fmt.MakeImage(&m_px[(size_t) (y + by) * m_w + x], m_w, w, bh, dx, dy + by);
		if (!img)
			return;
		XPutImage(fmt.GetDisplay(), dr, gc, img, 0, 0, dx, dy + by, w, bh);
		XDestroyImage(img);
	}
}
