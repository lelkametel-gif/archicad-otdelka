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
		if (c == L' ' || c == L'/') { words.push_back (cur); cur.clear (); }
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


TableLayout LayoutSheet (const Sheet& sh, const TableStyle& st)
{
	const int nCols = (int) st.colW.size ();		// 2 + 2 + 2*3 + 1 = 11
	const int lastCol = nCols - 1;
	std::vector<GridCell> cells;
	auto add = [&cells] (int r1, int c1, int r2, int c2, const std::wstring& t, bool vert = false) {
		GridCell g; g.r1 = r1; g.c1 = c1; g.r2 = r2; g.c2 = c2; g.text = t; g.vertical = vert;
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
			if (row.wallSpan > 0) {
				const int r2 = r + row.wallSpan - 1;
				for (int c = 1; c <= kWallColumns; ++c) {
					const bool has = row.wallSum[c] >= kMinArea;
					add (r, 2 + 2 * c, r2, 2 + 2 * c, has ? (row.wallFinish.empty () ? L"?" : row.wallFinish) : L"-");
					add (r, 3 + 2 * c, r2, 3 + 2 * c, has ? FormatNum (row.wallSum[c], 1) : L"-");
				}
				add (r, lastCol, r2, lastCol, row.wallSum[0] >= kMinArea ? L"Стены без столбца: " + FormatNum (row.wallSum[0], 1) + L" м²" : L"");
			}
			++r; ++i;
		}
		add (first, 0, r - 1, 0, g, true);
	}
	return RenderGrid (cells, r, 2, st.colW, st, sh.title);
}


TableLayout RenderGrid (const std::vector<GridCell>& cells, int nRows, int headerRows, const std::vector<double>& colW,
						   const TableStyle& st, const std::wstring& title)
{
	const int nCols = (int) colW.size ();

	// ---- высоты строк
	const double lineH = st.textSize * st.lineFactor;
	auto spanW = [&colW] (const GridCell& g) {
		double w = 0;
		for (int c = g.c1; c <= g.c2; ++c) w += colW[(size_t) c];
		return w;
	};
	std::vector<double> rowH ((size_t) nRows, st.minRow);
	for (int k = 0; k < headerRows && k < nRows; ++k) rowH[(size_t) k] = 7;
	for (const GridCell& g : cells) {
		if (g.r1 != g.r2 || g.vertical) continue;
		const size_t n = WrapText (g.text, (spanW (g) - 2 * st.pad) * (g.bold ? 0.88 : 1.0), st.textSize, st.charFactor).size ();
		rowH[(size_t) g.r1] = std::max (rowH[(size_t) g.r1], n * lineH + 2 * st.pad);
	}
	for (const GridCell& g : cells) {
		if (g.r1 == g.r2 && !g.vertical) continue;
		double need;
		if (g.vertical) {
			const double w = spanW (g) - 2 * st.pad;
			const double maxLines = std::max (1.0, std::floor (w / lineH));
			need = (double) g.text.size () * st.textSize * st.charFactor / maxLines + 2 * st.pad + 3 * st.textSize;
		} else {
			need = WrapText (g.text, (spanW (g) - 2 * st.pad) * (g.bold ? 0.88 : 1.0), st.textSize, st.charFactor).size () * lineH + 2 * st.pad;
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
	for (const GridCell& g : cells) {
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
			const std::vector<std::wstring> lines = WrapText (g.text, (spanW (g) - 2 * st.pad - g.indent) * (g.bold ? 0.88 : 1.0), st.textSize, st.charFactor);
			const double n = (double) lines.size ();
			for (size_t li = 0; li < lines.size (); ++li) {
				TText tx;
				tx.x = g.left ? xs[(size_t) g.c1] + st.pad + g.indent : cx;
				tx.left = g.left;
				tx.y = cy + (n - 1) / 2 * lineH - (double) li * lineH;
				tx.text = lines[li]; tx.size = st.textSize; tx.bold = g.bold; tx.tag = g.tag;
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
		c.guid = z.guid;
		std::wstring plan, low;
		for (const auto& n : z.notes) plan += (plan.empty () ? L"" : L"; ") + n;
		c.height = z.finishH;
		c.minH = z.finishH > 0.01 ? z.finishH : 1e9;
		for (const WallPiece& w : z.pieces) {
			const int col = (w.column >= 1 && w.column <= kWallColumns) ? w.column : 0;
			c.wall[col] += w.Area ();
			c.wallLen += w.length;
			c.len[col] += w.length;
			c.openings += w.openings;
			if (z.finishH < 0.01) c.height = std::max (c.height, w.finishHeight);		// высота зоны неизвестна — по элементам
			if (w.finishHeight < c.minH - 0.005) c.minH = w.finishHeight;
			if (w.lowBy > 0.02)
				low += (low.empty () ? L"" : L"; ") + (w.elemId.empty () ? std::wstring (L"без ID") : w.elemId) +
					   L" на " + FormatNum (w.lowBy, 2) + L" м";
		}
		auto addNote = [&c] (const std::wstring& t) { if (!t.empty ()) c.note += (c.note.empty () ? L"" : L"\n") + t; };
		if (!plan.empty ()) addNote (L"не связаны с зоной, взяты по плану (м): " + plan);
		if (!low.empty ()) addNote (L"ниже зоны, проверить высоту: " + low);
		if (c.wall[0] >= 0.05) addNote (L"стены без столбца: " + FormatNum (c.wall[0], 1) + L" м²");
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
	// Этаж | № | Имя | Категория | Код | Потолок | Длина: монолит, КБ, ГКЛ | Высота отделки | Проёмы
	//   | Площадь: монолит, КБ, ГКЛ | Подкатегория | Отделка | Примечание
	const std::vector<double> colW = { 12, 14, 40, 40, 12, 17, 16, 16, 16, 18, 15, 17, 17, 17, 30, 54, 80 };
	const int cL = 6, cH = 9, cO = 10, cW = 11, cGr = 14, cFin = 15;	// длины, высота, проёмы, площади, подкатегория, отделка
	const int last = (int) colW.size () - 1;
	std::vector<GridCell> cells;
	auto add = [&cells] (int r1, int c1, int r2, int c2, const std::wstring& t, bool vert = false, bool bold = false) {
		GridCell g; g.r1 = r1; g.c1 = c1; g.r2 = r2; g.c2 = c2; g.text = t; g.vertical = vert; g.bold = bold;
		cells.push_back (g);
	};
	auto num = [] (double v) { return v >= 0.05 ? FormatNum (v, 1) : std::wstring (L"-"); };
	const wchar_t* head[] = { L"Этаж", L"№ зоны", L"Имя зоны", L"Категория зоны", L"Код", L"Потолок, м²",
							  L"Длина стен: монолит, м", L"Длина стен: кладка/КБ, м", L"Длина стен: ГКЛ, м", L"Высота отделки, м", L"Проёмы, м²",
							  L"Стены: монолит, м²", L"Стены: кладка/КБ, м²", L"Стены: ГКЛ, м²",
							  L"Подкатегория ведомости", L"Отделка: потолок / стены", L"Примечание (проверить)" };
	for (int c = 0; c <= last; ++c)
		add (0, c, 0, c, head[c]);

	const std::vector<CheckRow> rows = CheckRows (zones, r);
	int row = 1;
	size_t i = 0;
	while (i < rows.size ()) {
		const int floor = rows[i].floor, first = row;
		const std::wstring fname = rows[i].floorName;
		while (i < rows.size () && rows[i].floor == floor) {
			const CheckRow& c = rows[i];
			const size_t firstCell = cells.size ();
			add (row, 1, row, 1, c.number.empty () ? L"-" : c.number);
			add (row, 2, row, 2, c.zoneName.empty () ? L"-" : c.zoneName);
			add (row, 3, row, 3, c.category);
			add (row, 4, row, 4, c.code.empty () ? L"-" : c.code);
			add (row, 5, row, 5, FormatNum (c.area, 1));
			for (int k = 1; k <= kWallColumns; ++k)
				add (row, cL - 1 + k, row, cL - 1 + k, c.len[k] >= 0.01 ? FormatNum (c.len[k], 2) : std::wstring (L"-"));
			add (row, cH, row, cH, c.height < 0.01 ? std::wstring (L"-") :
				 FormatNum (c.height, 2) + (c.minH < c.height - 0.005 ? L" (до " + FormatNum (c.minH, 2) + L")" : L""));
			add (row, cO, row, cO, num (c.openings));
			for (int k = 1; k <= kWallColumns; ++k)
				add (row, cW - 1 + k, row, cW - 1 + k, num (c.wall[k]));
			add (row, cGr, row, cGr, c.group);
			add (row, cFin, row, cFin, c.row);
			add (row, last, row, last, c.note, false, !c.note.empty ());
			for (size_t k = firstCell; k < cells.size (); ++k)
				cells[k].tag = c.guid;		// любой текст строки -> зона на плане
			++row; ++i;
		}
		add (first, 0, row - 1, 0, fname, true);
	}

	// итоги по подкатегориям — сверка с основной таблицей (потолок и стены по столбцам)
	struct Sum { std::wstring group; int n = 0; double ceil = 0, open = 0; double len[kWallColumns + 1] = { 0, 0, 0, 0 }, wall[kWallColumns + 1] = { 0, 0, 0, 0 }; };
	std::vector<Sum> sums;
	for (const CheckRow& c : rows) {
		if (c.excluded) continue;
		auto it = std::find_if (sums.begin (), sums.end (), [&c] (const Sum& s) { return s.group == c.group; });
		if (it == sums.end ()) { Sum s; s.group = c.group; sums.push_back (s); it = sums.end () - 1; }
		it->n += 1;
		it->ceil += c.area;
		for (int k = 0; k <= kWallColumns; ++k) it->len[k] += c.len[k];
		it->open += c.openings;
		for (int k = 0; k <= kWallColumns; ++k) it->wall[k] += c.wall[k];
	}
	std::stable_sort (sums.begin (), sums.end (), [&r] (const Sum& a, const Sum& b) {
		auto idx = [&r] (const std::wstring& g) {
			for (size_t k = 0; k < r.groupOrder.size (); ++k) if (SameText (r.groupOrder[k], g)) return (int) k;
			return (int) r.groupOrder.size ();
		};
		return idx (a.group) < idx (b.group);
	});
	if (!sums.empty ()) {
		add (row, 0, row, last, L"Итого по подкатегориям (= суммы подраздела в ведомости)", false, true);
		++row;
		Sum all;
		int nOut = 0;
		for (const auto& s : sums) {
			add (row, 0, row, 4, s.group);
			add (row, 5, row, 5, num (s.ceil));
			for (int k = 1; k <= kWallColumns; ++k)
				add (row, cL - 1 + k, row, cL - 1 + k, s.len[k] >= 0.01 ? FormatNum (s.len[k], 2) : std::wstring (L"-"));
			add (row, cH, row, cH, L"");
			add (row, cO, row, cO, num (s.open));
			for (int k = 1; k <= kWallColumns; ++k) add (row, cW - 1 + k, row, cW - 1 + k, num (s.wall[k]));
			add (row, cGr, row, last, L"зон: " + std::to_wstring (s.n) + (s.wall[0] >= 0.05 ? L"; стены без столбца: " + FormatNum (s.wall[0], 1) + L" м²" : L""));
			all.n += s.n; all.ceil += s.ceil; all.open += s.open;
			for (int k = 0; k <= kWallColumns; ++k) all.len[k] += s.len[k];
			for (int k = 0; k <= kWallColumns; ++k) all.wall[k] += s.wall[k];
			++row;
		}
		for (const CheckRow& c : rows) if (c.excluded) ++nOut;
		add (row, 0, row, 4, L"Всего в ведомости", false, true);
		add (row, 5, row, 5, num (all.ceil), false, true);
		for (int k = 1; k <= kWallColumns; ++k)
			add (row, cL - 1 + k, row, cL - 1 + k, all.len[k] >= 0.01 ? FormatNum (all.len[k], 2) : std::wstring (L"-"), false, true);
		add (row, cH, row, cH, L"");
		add (row, cO, row, cO, num (all.open), false, true);
		for (int k = 1; k <= kWallColumns; ++k) add (row, cW - 1 + k, row, cW - 1 + k, num (all.wall[k]), false, true);
		add (row, cGr, row, last, L"зон: " + std::to_wstring (all.n) + (nOut > 0 ? L", не входит: " + std::to_wstring (nOut) : L""), false, true);
		++row;
	}
	TableStyle s2 = st;
	s2.minRow = 6;
	return RenderGrid (cells, row, 1, colW, s2, title);
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
		snprintf (buf, sizeof (buf), "<text x=\"%.1f\" y=\"%.1f\" font-family=\"Arial Narrow\" font-size=\"%.1f\" text-anchor=\"%s\" "
				  "dominant-baseline=\"middle\"%s%s>", X (tx.x), Y (tx.y), tx.size * s * 1.35, tx.left ? "start" : "middle",
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

// ============================================================================= проверка зон на плане

const wchar_t* const kPropGroup		= L"ПСО и отделка";
const wchar_t* const kPropSubcat	= L"Отделка - подкатегория";
const wchar_t* const kPropCheck		= L"Отделка - проверка";
const wchar_t* const kPropNote		= L"Отделка - замечание";
const wchar_t* const kChkOk			= L"Верно";
const wchar_t* const kChkNotes		= L"Замечания";
const wchar_t* const kChkNoFinish	= L"Ошибка: нет отделки";
const wchar_t* const kChkNoGroup	= L"Ошибка: нет подкатегории";
const wchar_t* const kChkHeader		= L"Ошибка: категория-заголовок";
const wchar_t* const kChkExcluded	= L"Не входит";
const wchar_t* const kChkOtherLib	= L"Другой объект зоны";


// Категории-разделители в списке («1 Квартиры», «2100 ---ЭКСПЛУАТАЦИОННЫЕ…», «7 ---ЧЕРДАК---»): зонам не назначаются
bool IsHeaderCategory (const ZoneData& z)
{
	const std::wstring code = Trim (z.catCode);
	if (z.name.find (L"---") != std::wstring::npos)
		return true;
	if (code.size () == 1 && code[0] >= L'0' && code[0] <= L'9')
		return true;
	return code.size () == 4 && code.compare (2, 2, L"00") == 0;
}


ZoneCheck CheckZone (const ZoneData& z, const Rules& r)
{
	ZoneCheck c;
	const std::wstring code = CatCodeOf (z);
	if (IsExcluded (z, r)) {
		c.subcat = kChkExcluded;
		c.check = kChkExcluded;
		c.note = L"код категории " + code + L" — в ведомость отделки не входит (раздел [Группы], «-»)";
		return c;
	}
	c.subcat = GroupOf (z, r);
	bool matched = false;
	for (const auto& kv : r.groupByCode) {
		const std::wstring pre = Trim (kv.first);
		if (!pre.empty () && code.compare (0, pre.size (), pre) == 0) { matched = true; break; }
	}
	std::vector<std::wstring> errs, notes;
	const bool header = IsHeaderCategory (z);
	if (header)
		errs.push_back (L"категория «" + z.name + L"» — заголовок раздела, назначьте зоне категорию с полным кодом");
	bool noFinish = false;
	if (CleanFinish (z.ceilFinishRaw, r).empty ()) { errs.push_back (L"не задана отделка потолка"); noFinish = true; }
	if (CleanFinish (z.wallFinishRaw, r).empty ()) { errs.push_back (L"не задана отделка стен"); noFinish = true; }
	if (!matched) {
		const std::wstring what = code.empty () ? std::wstring (L"у категории зоны нет кода")
												: L"код категории " + code + L" не найден в разделе [Группы] правил";
		if (z.floor <= 0 && !r.basementGroup.empty ())
			notes.push_back (what + L" — взята подкатегория подвала");
		else
			errs.push_back (what + L" — зона попала в «" + c.subcat + L"»");
	}
	if (z.pieces.empty ())
		notes.push_back (L"стены зоны не найдены");
	{	// замечания для раскраски: пилоны «по плану» — норма (Archicad не связывает колонны с зоной),
		// стена ниже зоны меньше чем на 10 см — тоже не замечание; в таблице проверки они по-прежнему видны
		std::wstring plan, low;
		double noCol = 0;
		// торцы стен и перегородок (до 15 см), примыкающие к зоне, тоже не замечание
		for (const WallPiece& w : z.pieces)
			if (w.byContour && !w.isColumn && w.length >= 0.15)
				plan += (plan.empty () ? L"" : L", ") + (w.elemId.empty () ? std::wstring (L"без ID") : w.elemId) + L" " + FormatNum (w.length, 2);
		for (const WallPiece& w : z.pieces) {
			if (w.lowBy >= 0.10)
				low += (low.empty () ? L"" : L", ") + (w.elemId.empty () ? std::wstring (L"без ID") : w.elemId) +
					   L" на " + FormatNum (w.lowBy, 2) + L" м";
			if (w.column < 1 || w.column > kWallColumns) noCol += w.Area ();
		}
		if (!plan.empty ()) notes.push_back (L"стены не связаны с зоной, взяты по плану (м): " + plan);
		if (!low.empty ()) notes.push_back (L"ниже зоны, проверить высоту: " + low);
		if (noCol >= 0.05) notes.push_back (L"стены без столбца: " + FormatNum (noCol, 1) + L" м²");
	}
	if (!errs.empty ())
		c.check = header ? kChkHeader : noFinish ? kChkNoFinish : kChkNoGroup;
	else
		c.check = notes.empty () ? kChkOk : kChkNotes;
	for (const auto& s : errs) c.note += (c.note.empty () ? L"" : L"; ") + s;
	for (const auto& s : notes) c.note += (c.note.empty () ? L"" : L"; ") + s;
	return c;
}


ZoneCheck OtherLibCheck (const std::wstring& libName)
{
	ZoneCheck c;
	c.subcat = kChkExcluded;
	c.check = kChkOtherLib;
	c.note = L"объект зоны «" + (libName.empty () ? std::wstring (L"без объекта") : libName) + L"» — в расчёт не берётся";
	return c;
}


static std::wstring SubcatRuleName (const std::wstring& g)	{ return L"Отделка подкатегория - " + g; }
static std::wstring CheckRuleName (const std::wstring& v)	{ return L"Отделка проверка - " + v; }


std::vector<OverrideRuleDef> OverrideRules (const Rules& r)
{
	std::vector<OverrideRuleDef> res;
	auto add = [&res] (const std::wstring& name, const std::wstring& prop, const std::wstring& value, double cr, double cg, double cb) {
		OverrideRuleDef d;
		d.name = name; d.prop = prop; d.value = value;
		d.rgb[0] = cr; d.rgb[1] = cg; d.rgb[2] = cb;
		res.push_back (d);
	};
	add (CheckRuleName (kChkHeader), kPropCheck, kChkHeader,		0.70, 0.10, 0.10);	// тёмно-красный
	add (CheckRuleName (kChkNoFinish), kPropCheck, kChkNoFinish,	1.00, 0.25, 0.25);	// красный
	add (CheckRuleName (kChkNoGroup), kPropCheck, kChkNoGroup,		0.90, 0.30, 0.85);	// малиновый
	add (CheckRuleName (kChkOtherLib), kPropCheck, kChkOtherLib,	0.55, 0.55, 0.55);	// тёмно-серый
	add (CheckRuleName (kChkExcluded), kPropCheck, kChkExcluded,	0.85, 0.85, 0.85);	// светло-серый
	add (CheckRuleName (kChkNotes), kPropCheck, kChkNotes,			1.00, 0.85, 0.20);	// жёлтый
	add (CheckRuleName (kChkOk), kPropCheck, kChkOk,				0.55, 0.85, 0.45);	// зелёный
	// подкатегории — светлые цвета по порядку групп в правилах
	static const double pal[][3] = {
		{ 0.70, 0.78, 0.92 },	// голубовато-серый
		{ 1.00, 0.78, 0.55 },	// оранжевый
		{ 0.72, 0.90, 0.66 },	// зелёный
		{ 0.84, 0.74, 0.96 },	// сиреневый
		{ 1.00, 0.95, 0.55 },	// жёлтый
		{ 0.62, 0.84, 1.00 },	// голубой
		{ 0.86, 0.76, 0.62 },	// бежево-коричневый
		{ 1.00, 0.74, 0.84 },	// розовый
		{ 0.66, 0.92, 0.88 },	// бирюзовый
		{ 0.92, 0.88, 0.70 },	// песочный
	};
	const size_t nPal = sizeof (pal) / sizeof (pal[0]);
	size_t i = 0;
	for (const auto& g : r.groupOrder) {
		if (g.empty () || g == kExcludedGroup) continue;
		const double* c = pal[i++ % nPal];
		add (SubcatRuleName (g), kPropSubcat, g, c[0], c[1], c[2]);
	}
	return res;
}


std::vector<OverrideComboDef> OverrideCombos (const Rules& r)
{
	OverrideComboDef sub, chk;
	sub.name = L"Отделка - подкатегории";
	chk.name = L"Отделка - проверка зон";
	for (const wchar_t* v : { kChkHeader, kChkNoFinish, kChkNoGroup, kChkOtherLib, kChkExcluded }) {
		sub.rules.push_back (CheckRuleName (v));
		chk.rules.push_back (CheckRuleName (v));
	}
	for (const auto& g : r.groupOrder)
		if (!g.empty () && g != kExcludedGroup) sub.rules.push_back (SubcatRuleName (g));
	chk.rules.push_back (CheckRuleName (kChkNotes));
	chk.rules.push_back (CheckRuleName (kChkOk));
	return { sub, chk };
}


static std::wstring XmlEsc (const std::wstring& s)
{
	std::wstring o;
	for (wchar_t ch : s) {
		switch (ch) {
			case L'&': o += L"&amp;"; break;
			case L'<': o += L"&lt;"; break;
			case L'>': o += L"&gt;"; break;
			case L'"': o += L"&quot;"; break;
			default: o += ch;
		}
	}
	return o;
}


std::wstring CriterionXml (const std::wstring& prop, const std::wstring& value)
{
	// как в файле графической замены, сохранённом из Archicad 27 (тип элемента «Зона» + свойство «равно»)
	return
		L"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>"
		L"<CriterionExpression Mv=\"2\" Sv=\"1\">"
			L"<TextMatchType>1</TextMatchType>"
			L"<Size>1</Size>"
			L"<ClassGuid>C6EBD1BD-7702-46FF-8ED9-9CC37648A7C7</ClassGuid>"
			L"<CompositeCriterion Mv=\"2\" Sv=\"1\">"
				L"<LogicalOperator>2</LogicalOperator>"
				L"<CriteriaCount>2</CriteriaCount>"
				L"<ClassGuid>C6EBD1BD-7702-46FF-8ED9-9CC37648A7C7</ClassGuid>"
				L"<CompositeCriterion Mv=\"2\" Sv=\"1\">"
					L"<LogicalOperator>1</LogicalOperator>"
					L"<CriteriaCount>1</CriteriaCount>"
					L"<ClassGuid>B4B7B134-EC56-4D40-8D4C-71D7C5A2493A</ClassGuid>"
					L"<ElemTypeCriterion Mv=\"2\" Sv=\"3\">"
						L"<VBEF::CritToolType Mv=\"1\" Sv=\"0\">"
							L"<ElemRegistryRefId>1380929357</ElemRegistryRefId>"
							L"<ElemCreatorToolUnID>"
								L"<MainGuid>A73622DE-E40A-4E63-ADB7-52D13422BD06</MainGuid>"
								L"<RevGuid>C2FE0BB3-ACF0-41F9-80ED-F98DDAEEA090</RevGuid>"
							L"</ElemCreatorToolUnID>"
						L"</VBEF::CritToolType>"
						L"<LogicalOperator>1</LogicalOperator>"
					L"</ElemTypeCriterion>"
				L"</CompositeCriterion>"
				L"<ClassGuid>C6EBD1BD-7702-46FF-8ED9-9CC37648A7C7</ClassGuid>"
				L"<CompositeCriterion Mv=\"2\" Sv=\"1\">"
					L"<LogicalOperator>1</LogicalOperator>"
					L"<CriteriaCount>1</CriteriaCount>"
					L"<ClassGuid>58E4905F-AD57-45F5-8D26-0100000F60BF</ClassGuid>"
					L"<PropertyCriterion Mv=\"5\" Sv=\"0\">"
						L"<VBEF::ConditionIO Mv=\"1\" Sv=\"0\">"
							L"<PropertyDefinitionUserId Version=\"2\">"
								L"<PrimaryId>1</PrimaryId>"
								L"<HasGuidId>false</HasGuidId>"
								L"<Guid>00000000-0000-0000-0000-000000000000</Guid>"
								L"<HasNameId>true</HasNameId>"
								L"<Name>" + XmlEsc (prop) + L"</Name>"
								L"<PropertyDefinitionGroupUserId Version=\"2\">"
									L"<PrimaryId>1</PrimaryId>"
									L"<HasGuidId>false</HasGuidId>"
									L"<Guid>00000000-0000-0000-0000-000000000000</Guid>"
									L"<HasNameId>true</HasNameId>"
									L"<Name>" + XmlEsc (kPropGroup) + L"</Name>"
								L"</PropertyDefinitionGroupUserId>"
							L"</PropertyDefinitionUserId>"
							L"<CriteriaOperatorEnum>6</CriteriaOperatorEnum>"
							L"<Value>"
								L"<Variant Type=\"StringVariant\">"
									L"<Status>Normal</Status>"
									L"<Value>" + XmlEsc (value) + L"</Value>"
								L"</Variant>"
							L"</Value>"
						L"</VBEF::ConditionIO>"
					L"</PropertyCriterion>"
				L"</CompositeCriterion>"
			L"</CompositeCriterion>"
		L"</CriterionExpression>";
}

}	// namespace otd
