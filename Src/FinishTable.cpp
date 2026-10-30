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


static TableLayout Render (const std::vector<GCell>& cells, int nRows, int headerRows, const std::vector<double>& colW,
						   const TableStyle& st, const std::wstring& title);


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
	return Render (cells, r, 2, st.colW, st, sh.title);
}


static TableLayout Render (const std::vector<GCell>& cells, int nRows, int headerRows, const std::vector<double>& colW,
						   const TableStyle& st, const std::wstring& title)
{
	const int nCols = (int) colW.size ();

	// ---- высоты строк
	const double lineH = st.textSize * st.lineFactor;
	auto spanW = [&colW] (const GCell& g) {
		double w = 0;
		for (int c = g.c1; c <= g.c2; ++c) w += colW[(size_t) c];
		return w;
	};
	std::vector<double> rowH ((size_t) nRows, st.minRow);
	for (int k = 0; k < headerRows && k < nRows; ++k) rowH[(size_t) k] = 7;
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
	for (int c = 0; c < nCols; ++c) xs[(size_t) c + 1] = xs[(size_t) c] + colW[(size_t) c];
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
				tx.text = lines[li]; tx.size = st.textSize; tx.bold = g.bold;
				t.texts.push_back (tx);
			}
		}
	}
	// заголовок над таблицей
	{
		TText tx;
		tx.x = t.width / 2;
		tx.y = st.titleSize * 1.5;
		tx.text = title; tx.size = st.titleSize; tx.bold = true;
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


void ShiftLayout (TableLayout& t, double dx, double dy)
{
	for (TSeg& g : t.segs) { g.x1 += dx; g.x2 += dx; g.y1 += dy; g.y2 += dy; }
	for (TText& x : t.texts) { x.x += dx; x.y += dy; }
}


void AppendLayout (TableLayout& to, const TableLayout& from, double gap)
{
	TableLayout f = from;
	ShiftLayout (f, to.width + gap, 0);
	to.segs.insert (to.segs.end (), f.segs.begin (), f.segs.end ());
	to.texts.insert (to.texts.end (), f.texts.begin (), f.texts.end ());
	to.width += gap + f.width;
	to.height = std::max (to.height, f.height);
}


std::vector<CheckRow> CheckRows (const std::vector<ZoneData>& zones, const Rules& r)
{
	std::vector<CheckRow> res;
	for (const ZoneData& z : zones) {
		CheckRow c;
		c.floor = z.floor;
		c.floorName = z.floorName.empty () ? std::to_wstring (z.floor) + L" эт." : z.floorName;
		c.number = z.number;
		c.zoneName = ZoneTitle (z);
		c.category = z.name;
		c.code = CatCodeOf (z);
		c.area = z.ceilArea;
		c.excluded = IsExcluded (z, r);
		c.group = c.excluded ? std::wstring (L"не входит") : GroupOf (z, r);
		const std::wstring cf = CleanFinish (z.ceilFinishRaw, r), wf = CleanFinish (z.wallFinishRaw, r);
		c.row = (cf.empty () ? L"-" : cf) + L" / " + (wf.empty () ? L"-" : wf);
		res.push_back (c);
	}
	auto groupIdx = [&r] (const CheckRow& c) {
		if (c.excluded) return (int) r.groupOrder.size () + 1;
		for (size_t i = 0; i < r.groupOrder.size (); ++i)
			if (SameText (r.groupOrder[i], c.group)) return (int) i;
		return (int) r.groupOrder.size ();
	};
	auto numLess = [] (const std::wstring& a, const std::wstring& b) {
		if (a.size () != b.size ()) return a.size () < b.size ();		// «2» раньше «10»
		return a < b;
	};
	std::stable_sort (res.begin (), res.end (), [&] (const CheckRow& a, const CheckRow& b) {
		if (a.floor != b.floor) return a.floor < b.floor;
		const int ga = groupIdx (a), gb = groupIdx (b);
		if (ga != gb) return ga < gb;
		if (a.number != b.number) return numLess (a.number, b.number);
		return a.zoneName < b.zoneName;
	});
	return res;
}


TableLayout LayoutCheck (const std::vector<ZoneData>& zones, const Rules& r, const std::wstring& title, const TableStyle& st)
{
	// Этаж | № зоны | Имя зоны | Категория зоны | Код | Площадь, м² | Подкатегория | Строка ведомости
	const std::vector<double> colW = { 12, 14, 44, 44, 14, 18, 34, 60 };
	std::vector<GCell> cells;
	auto add = [&cells] (int r1, int c1, int r2, int c2, const std::wstring& t, bool vert = false, bool bold = false) {
		GCell g; g.r1 = r1; g.c1 = c1; g.r2 = r2; g.c2 = c2; g.text = t; g.vertical = vert; g.bold = bold;
		cells.push_back (g);
	};
	const wchar_t* head[] = { L"Этаж", L"№ зоны", L"Имя зоны", L"Категория зоны", L"Код", L"Площадь, м²", L"Подкатегория ведомости", L"Отделка: потолок / стены" };
	for (int c = 0; c < 8; ++c)
		add (0, c, 0, c, head[c]);

	const std::vector<CheckRow> rows = CheckRows (zones, r);
	int row = 1;
	size_t i = 0;
	while (i < rows.size ()) {
		const int floor = rows[i].floor, first = row;
		const std::wstring fname = rows[i].floorName;
		while (i < rows.size () && rows[i].floor == floor) {
			const CheckRow& c = rows[i];
			add (row, 1, row, 1, c.number.empty () ? L"-" : c.number);
			add (row, 2, row, 2, c.zoneName.empty () ? L"-" : c.zoneName);
			add (row, 3, row, 3, c.category);
			add (row, 4, row, 4, c.code.empty () ? L"-" : c.code);
			add (row, 5, row, 5, FormatNum (c.area, 1));
			add (row, 6, row, 6, c.group);
			add (row, 7, row, 7, c.row);
			++row; ++i;
		}
		add (first, 0, row - 1, 0, fname, true);
	}

	// итоги по подкатегориям — сверка с потолками основной таблицы
	std::vector<std::pair<std::wstring, std::pair<int, double>>> sums;
	for (const CheckRow& c : rows) {
		if (c.excluded) continue;
		auto it = std::find_if (sums.begin (), sums.end (), [&c] (const auto& kv) { return kv.first == c.group; });
		if (it == sums.end ()) { sums.push_back ({ c.group, { 0, 0.0 } }); it = sums.end () - 1; }
		it->second.first += 1;
		it->second.second += c.area;
	}
	std::stable_sort (sums.begin (), sums.end (), [&r] (const auto& a, const auto& b) {
		auto idx = [&r] (const std::wstring& g) {
			for (size_t k = 0; k < r.groupOrder.size (); ++k) if (SameText (r.groupOrder[k], g)) return (int) k;
			return (int) r.groupOrder.size ();
		};
		return idx (a.first) < idx (b.first);
	});
	if (!sums.empty ()) {
		add (row, 0, row, 7, L"Итого по подкатегориям (площадь = потолок в ведомости)", false, true);
		++row;
		int nIn = 0, nOut = 0;
		double aIn = 0;
		for (const auto& kv : sums) {
			add (row, 0, row, 5, kv.first);
			add (row, 6, row, 6, FormatNum (kv.second.second, 1) + L" м²");
			add (row, 7, row, 7, L"зон: " + std::to_wstring (kv.second.first));
			nIn += kv.second.first; aIn += kv.second.second;
			++row;
		}
		for (const CheckRow& c : rows) if (c.excluded) ++nOut;
		add (row, 0, row, 5, L"Всего в ведомости", false, true);
		add (row, 6, row, 6, FormatNum (aIn, 1) + L" м²", false, true);
		add (row, 7, row, 7, L"зон: " + std::to_wstring (nIn) + (nOut > 0 ? L", не входит: " + std::to_wstring (nOut) : L""), false, true);
		++row;
	}
	TableStyle s2 = st;
	s2.minRow = 6;
	return Render (cells, row, 1, colW, s2, title);
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
