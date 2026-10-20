// Ведомость отделки -> таблица из линий и однострочных текстов
#include "FinishTable.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace otd {

static const double kMinArea = 0.05;

std::vector<std::wstring> WrapText (const std::wstring& text, double widthMm, double size, double charFactor)
{
	std::vector<std::wstring> res;
	const double cw = std::max (0.1, size * charFactor);
	const size_t maxChars = (size_t) std::max (1.0, std::floor (widthMm / cw));
	// слова (перенос допускается после пробела и после «-», «,», «/»)
	std::vector<std::wstring> words;
	std::wstring cur;
	for (wchar_t c : text) {
		if (c == L'\n') { words.push_back (cur); words.push_back (L"\n"); cur.clear (); continue; }
		cur += c;
		if (c == L' ' || c == L',' || c == L'/') { words.push_back (cur); cur.clear (); }
	}
	if (!cur.empty ()) words.push_back (cur);

	std::wstring line;
	auto flush = [&] () {
		const std::wstring t = Trim (line);
		res.push_back (t);
		line.clear ();
	};
	for (std::wstring w : words) {
		if (w == L"\n") { flush (); continue; }
		if (Trim (line + w).size () <= maxChars) { line += w; continue; }
		if (!Trim (line).empty ()) flush ();
		while (Trim (w).size () > maxChars) {		// длинное слово — режем
			res.push_back (w.substr (0, maxChars));
			w = w.substr (maxChars);
		}
		line = w;
	}
	if (!Trim (line).empty () || res.empty ()) flush ();
	return res;
}


namespace {

struct GCell {
	int				r1, c1, r2, c2;
	std::wstring	text;
	bool			vertical = false;
	bool			bold = false;
};

}	// namespace


TableLayout LayoutSheet (const Sheet& sh, const TableStyle& st)
{
	const int nCols = (int) st.colW.size ();		// 2 + 2 + 2*3 + 1 = 11
	const int lastCol = nCols - 1;
	std::vector<GCell> cells;
	auto add = [&cells] (int r1, int c1, int r2, int c2, const std::wstring& t, bool vert = false) {
		GCell g; g.r1 = r1; g.c1 = c1; g.r2 = r2; g.c2 = c2; g.text = t; g.vertical = vert;
		cells.push_back (g);
	};

	add (0, 0, 1, 1, L"Наименование помещений");
	add (0, 2, 0, 3, L"ПОТОЛОК");
	add (0, 4, 0, 3 + 2 * kWallColumns, L"СТЕНА ( вид отделки )");
	add (0, lastCol, 1, lastCol, L"Примеч.");
	add (1, 2, 1, 2, L"Вид отделки");
	add (1, 3, 1, 3, L"кол-во, м²");
	for (int c = 1; c <= kWallColumns; ++c) {
		add (1, 2 + 2 * c, 1, 2 + 2 * c, sh.columnTitle[c]);
		add (1, 3 + 2 * c, 1, 3 + 2 * c, L"кол-во, м²");
	}

	int r = 2;
	size_t i = 0;
	while (i < sh.rows.size ()) {
		const std::wstring& g = sh.rows[i].group;
		const int first = r;
		while (i < sh.rows.size () && sh.rows[i].group == g) {
			const SheetRow& row = sh.rows[i];
			add (r, 1, r, 1, row.title);
			add (r, 2, r, 2, row.ceilFinish.empty () ? L"-" : row.ceilFinish);
			add (r, 3, r, 3, row.ceilArea >= kMinArea ? FormatNum (row.ceilArea, 1) : L"-");
			for (int c = 1; c <= kWallColumns; ++c) {
				const bool has = row.wallArea[c] >= kMinArea;
				add (r, 2 + 2 * c, r, 2 + 2 * c, has ? (row.wallFinish.empty () ? L"?" : row.wallFinish) : L"-");
				add (r, 3 + 2 * c, r, 3 + 2 * c, has ? FormatNum (row.wallArea[c], 1) : L"-");
			}
			add (r, lastCol, r, lastCol, row.wallArea[0] >= kMinArea ? L"Стены без столбца: " + FormatNum (row.wallArea[0], 1) + L" м²" : L"");
			++r; ++i;
		}
		add (first, 0, r - 1, 0, g, true);
	}
	const int nRows = r;

	// ---- высоты строк
	const double lineH = st.textSize * st.lineFactor;
	auto spanW = [&st] (const GCell& g) {
		double w = 0;
		for (int c = g.c1; c <= g.c2; ++c) w += st.colW[(size_t) c];
		return w;
	};
	std::vector<double> rowH ((size_t) nRows, st.minRow);
	for (int k = 0; k < 2 && k < nRows; ++k) rowH[(size_t) k] = 7;
	for (const GCell& g : cells) {
		if (g.r1 != g.r2 || g.vertical) continue;
		const size_t n = WrapText (g.text, spanW (g) - 2 * st.pad, st.textSize, st.charFactor).size ();
		rowH[(size_t) g.r1] = std::max (rowH[(size_t) g.r1], n * lineH + 2 * st.pad);
	}
	for (const GCell& g : cells) {
		if (g.r1 == g.r2 && !g.vertical) continue;
		double need;
		if (g.vertical) {
			const double w = spanW (g) - 2 * st.pad;
			const double maxLines = std::max (1.0, std::floor (w / lineH));
			need = (double) g.text.size () * st.textSize * st.charFactor / maxLines + 2 * st.pad + 3 * st.textSize;
		} else {
			need = WrapText (g.text, spanW (g) - 2 * st.pad, st.textSize, st.charFactor).size () * lineH + 2 * st.pad;
		}
		double have = 0;
		for (int k = g.r1; k <= g.r2; ++k) have += rowH[(size_t) k];
		if (have < need)
			for (int k = g.r1; k <= g.r2; ++k) rowH[(size_t) k] += (need - have) / (g.r2 - g.r1 + 1);
	}

	std::vector<double> xs ((size_t) nCols + 1, 0.0), ys ((size_t) nRows + 1, 0.0);
	for (int c = 0; c < nCols; ++c) xs[(size_t) c + 1] = xs[(size_t) c] + st.colW[(size_t) c];
	for (int k = 0; k < nRows; ++k) ys[(size_t) k + 1] = ys[(size_t) k] - rowH[(size_t) k];

	TableLayout t;
	t.width = xs.back ();
	t.height = -ys.back ();

	// ---- тексты
	for (const GCell& g : cells) {
		if (Trim (g.text).empty ()) continue;
		const double cx = (xs[(size_t) g.c1] + xs[(size_t) g.c2 + 1]) / 2;
		const double cy = (ys[(size_t) g.r1] + ys[(size_t) g.r2 + 1]) / 2;
		if (g.vertical) {
			const double h = ys[(size_t) g.r1] - ys[(size_t) g.r2 + 1];
			const std::vector<std::wstring> lines = WrapText (g.text, h - 2 * st.pad, st.textSize, st.charFactor);
			const double n = (double) lines.size ();
			for (size_t li = 0; li < lines.size (); ++li) {
				TText tx;
				tx.x = cx - (n - 1) / 2 * lineH + (double) li * lineH;
				tx.y = cy;
				tx.text = lines[li]; tx.size = st.textSize; tx.vertical = true;
				t.texts.push_back (tx);
			}
		} else {
			const std::vector<std::wstring> lines = WrapText (g.text, spanW (g) - 2 * st.pad, st.textSize, st.charFactor);
			const double n = (double) lines.size ();
			for (size_t li = 0; li < lines.size (); ++li) {
				TText tx;
				tx.x = cx;
				tx.y = cy + (n - 1) / 2 * lineH - (double) li * lineH;
				tx.text = lines[li]; tx.size = st.textSize;
				t.texts.push_back (tx);
			}
		}
	}
	// заголовок над таблицей
	{
		TText tx;
		tx.x = t.width / 2;
		tx.y = st.titleSize * 1.5;
		tx.text = sh.title; tx.size = st.titleSize; tx.bold = true;
		t.texts.push_back (tx);
	}

	// ---- линии: граница между единичными клетками рисуется, если клетки принадлежат разным ячейкам
	std::vector<int> owner ((size_t) (nRows * nCols), -1);
	for (size_t ci = 0; ci < cells.size (); ++ci)
		for (int rr = cells[ci].r1; rr <= cells[ci].r2; ++rr)
			for (int cc = cells[ci].c1; cc <= cells[ci].c2; ++cc)
				owner[(size_t) (rr * nCols + cc)] = (int) ci;
	auto own = [&] (int rr, int cc) { return owner[(size_t) (rr * nCols + cc)]; };

	for (int k = 0; k <= nRows; ++k) {		// горизонтальные
		int start = -1;
		for (int c = 0; c <= nCols; ++c) {
			const bool draw = c < nCols && (k == 0 || k == nRows || own (k - 1, c) != own (k, c) || own (k, c) < 0);
			if (draw && start < 0) start = c;
			if (!draw && start >= 0) {
				t.segs.push_back ({ xs[(size_t) start], ys[(size_t) k], xs[(size_t) c], ys[(size_t) k] });
				start = -1;
			}
		}
	}
	for (int c = 0; c <= nCols; ++c) {		// вертикальные
		int start = -1;
		for (int k = 0; k <= nRows; ++k) {
			const bool draw = k < nRows && (c == 0 || c == nCols || own (k, c - 1) != own (k, c) || own (k, c) < 0);
			if (draw && start < 0) start = k;
			if (!draw && start >= 0) {
				t.segs.push_back ({ xs[(size_t) c], ys[(size_t) start], xs[(size_t) c], ys[(size_t) k] });
				start = -1;
			}
		}
	}
	return t;
}


std::string LayoutSvg (const TableLayout& t)
{
	const double m = 5, s = 4;		// поле, пикселей на мм
	const double top = 12;
	char buf[512];
	std::string x;
	snprintf (buf, sizeof (buf), "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\">"
			  "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>", (t.width + 2 * m) * s, (t.height + top + 2 * m) * s);
	x += buf;
	auto X = [&] (double v) { return (v + m) * s; };
	auto Y = [&] (double v) { return (top - v + m) * s; };
	for (const TSeg& g : t.segs) {
		snprintf (buf, sizeof (buf), "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"black\" stroke-width=\"1\"/>",
				  X (g.x1), Y (g.y1), X (g.x2), Y (g.y2));
		x += buf;
	}
	for (const TText& tx : t.texts) {
		std::string esc;
		for (char ch : ToUtf8 (tx.text)) {
			if (ch == '<') esc += "&lt;"; else if (ch == '&') esc += "&amp;"; else esc += ch;
		}
		snprintf (buf, sizeof (buf), "<text x=\"%.1f\" y=\"%.1f\" font-family=\"Arial\" font-size=\"%.1f\" text-anchor=\"middle\" "
				  "dominant-baseline=\"middle\"%s%s>", X (tx.x), Y (tx.y), tx.size * s * 1.35,
				  tx.bold ? " font-weight=\"bold\"" : "", "");
		x += buf;
		if (tx.vertical) {
			snprintf (buf, sizeof (buf), "</text>");
			x.erase (x.size () - 1);
			snprintf (buf, sizeof (buf), " transform=\"rotate(-90 %.1f %.1f)\">", X (tx.x), Y (tx.y));
			x += buf;
		}
		x += esc + "</text>";
	}
	x += "</svg>";
	return x;
}

}	// namespace otd
