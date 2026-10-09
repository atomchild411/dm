#pragma once

// OpenGL's declarations: the core profile header on macOS, gl.h elsewhere
// (OpenGL 1.1 on Windows and IRIX; what this file's users call is in that).
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#if defined(_WIN32)
#include <windows.h>
#endif
#include <GL/gl.h>
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F // OpenGL 1.2 (Windows' gl.h has 1.1)
#endif
#ifndef GL_CLAMP
#define GL_CLAMP 0x2900 // OpenGL 1.1's (only that path uses it)
#endif
