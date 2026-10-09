#pragma once

#include <string>
#include <cstring>
#include "../models/RectAndPoint.hpp"


class String
{
private:
	std::string m_content;

public:
	void Clear() {
		m_content = "";
	}
	void Set(const std::string& text) {
		m_content = text;
	}
	const std::string& GetWrapped() const {
		return m_content;
	}
};


struct DrawingContext;

Point MdMeasureString(DrawingContext* context, const String& word, int styleFlags, bool& outWasWordWrapped, int maxWidth = 0);
int MdLineHeight(DrawingContext* context, int styleFlags);
int MdSpaceWidth(DrawingContext* context, int styleFlags);
void MdDrawString(DrawingContext* context, const Rect& rect, const String& str, int styleFlags);
void MdDrawCodeBackground(DrawingContext* context, const Rect& rect);
void MdDrawForwardBackground(DrawingContext* context, const Rect& rect);
int MdGetQuoteIndentSize();

