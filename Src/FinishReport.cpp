// Ведомость отделки -> листы Excel
#include "FinishReport.hpp"

#include <algorithm>
#include <cmath>

namespace otd {

static const double kMinArea = 0.05;	// меньше — считаем, что стен такого типа нет («-»)

xlsx::Sheet MakeVedomostSheet (const Sheet& sh)
{
	xlsx::Sheet x;
	x.name = L"Ведомость";
	const int lastCol = 2 + 2 + 2 * kWallColumns;	// A..K
	x.colWidth = { { 0, 6 }, { 1, 24 }, { 2, 22 }, { 3, 9 }, { 4, 20 }, { 5, 9 }, { 6, 20 }, { 7, 9 }, { 8, 20 }, { 9, 9 }, { 10, 12 } };

	x.Set (0, 0, sh.title, xlsx::Title);
	x.Merge (0, 0, 0, lastCol);
	x.rowHeight[0] = 24;

	x.Set (1, 0, L"Наименование помещений", xlsx::Head);
	x.Merge (1, 0, 2, 1);
	x.Set (1, 2, L"ПОТОЛОК", xlsx::Head);
	x.Merge (1, 2, 1, 3);
	x.Set (1, 4, L"СТЕНА ( вид отделки )", xlsx::Head);
	x.Merge (1, 4, 1, 3 + 2 * kWallColumns);
	x.Set (1, lastCol, L"Примеч.", xlsx::Head);
	x.Merge (1, lastCol, 2, lastCol);
	x.Set (2, 2, L"Вид отделки", xlsx::Head);
	x.Set (2, 3, L"кол-во, м²", xlsx::Head);
	for (int c = 1; c <= kWallColumns; ++c) {
		x.Set (2, 2 + 2 * c, sh.columnTitle[c], xlsx::Head);
		x.Set (2, 3 + 2 * c, L"кол-во, м²", xlsx::Head);
	}
	x.rowHeight[2] = 66;

	int r = 3;
	size_t i = 0;
	while (i < sh.rows.size ()) {
		const std::wstring& g = sh.rows[i].group;
		const int first = r;
		while (i < sh.rows.size () && sh.rows[i].group == g) {
			const SheetRow& row = sh.rows[i];
			x.Set (r, 1, row.title, xlsx::Text);
			x.Set (r, 2, row.ceilFinish.empty () ? L"-" : row.ceilFinish, xlsx::Text);
			if (row.ceilArea >= kMinArea) x.SetNum (r, 3, row.ceilArea, xlsx::Num1);
			else x.Set (r, 3, L"-", xlsx::Text);
			for (int c = 1; c <= kWallColumns; ++c) {
				if (row.wallArea[c] >= kMinArea) {
					x.Set (r, 2 + 2 * c, row.wallFinish.empty () ? L"?" : row.wallFinish, xlsx::Text);
					x.SetNum (r, 3 + 2 * c, row.wallArea[c], xlsx::Num1);
				} else {
					x.Set (r, 2 + 2 * c, L"-", xlsx::Text);
					x.Set (r, 3 + 2 * c, L"-", xlsx::Text);
				}
			}
			std::wstring note;
			if (row.wallArea[0] >= kMinArea)
				note = L"Стены без столбца: " + FormatNum (row.wallArea[0], 1) + L" м² (см. лист «Расчёт»)";
			x.Set (r, lastCol, note, xlsx::Text);
			// высота строки — по самому длинному тексту (≈ 22 символа в строке ячейки)
			size_t longest = std::max (row.title.size (), std::max (row.ceilFinish.size (), row.wallFinish.size ()));
			longest = std::max (longest, note.size () / 2);
			x.rowHeight[r] = std::max (26.0, 13.0 * std::ceil ((double) longest / 22.0) + 4.0);
			++r;
			++i;
		}
		x.Set (first, 0, g, xlsx::Vert);
		// вертикальное название группы должно поместиться (две строки текста в узком столбце)
		double h = 0;
		for (int k = first; k < r; ++k) h += x.rowHeight[k];
		const double need = 3.2 * (double) g.size ();
		if (h < need)
			for (int k = first; k < r; ++k) x.rowHeight[k] += (need - h) / (double) (r - first);
		if (r - 1 > first)
			x.Merge (first, 0, r - 1, 0);
	}
	return x;
}


xlsx::Sheet MakeDetailsSheet (const std::vector<ZoneData>& zones, const Rules& rules)
{
	xlsx::Sheet x;
	x.name = L"Расчёт";
	const wchar_t* head[] = {
		L"Этаж", L"№ зоны", L"Категория зоны", L"Группа", L"Строка ведомости", L"Потолок (в зоне)", L"Стены (в зоне)",
		L"Площадь потолка, м²", L"Толщина пола, м", L"Толщина пола — откуда",
		L"ID элемента", L"Конструкция / материал", L"Столбец", L"Длина, м", L"Высота элемента, м", L"Высота отделки, м",
		L"Проёмов, шт", L"Проёмы, м²", L"Площадь отделки, м²"
	};
	const int nHead = (int) (sizeof (head) / sizeof (head[0]));
	for (int c = 0; c < nHead; ++c) {
		x.Set (0, c, head[c], xlsx::Bold);
		x.colWidth[c] = 12;
	}
	x.colWidth[2] = 20; x.colWidth[4] = 28; x.colWidth[5] = 16; x.colWidth[6] = 16; x.colWidth[9] = 22; x.colWidth[11] = 36;
	x.landscape = true;

	int r = 1;
	double total[kWallColumns + 1] = { 0, 0, 0, 0 };
	double totalCeil = 0;
	for (const ZoneData& z : zones) {
		const bool excl = IsExcluded (z, rules);
		const std::wstring g = excl ? std::wstring (L"не входит в ведомость (код " + CatCodeOf (z) + L")") : GroupOf (z, rules);
		auto zoneCols = [&] (int row) {
			x.SetNum (row, 0, z.floor, xlsx::Default);
			x.Set (row, 1, z.number, xlsx::Plain);
			x.Set (row, 2, z.name + (z.roomName.empty () || z.roomName == z.name ? L"" : L" (" + z.roomName + L")"), xlsx::Plain);
			x.Set (row, 3, g, xlsx::Plain);
			x.Set (row, 4, excl ? std::wstring () : RowTitleOf (z, g, rules), xlsx::Plain);
			x.Set (row, 5, z.ceilFinishRaw, xlsx::Plain);
			x.Set (row, 6, z.wallFinishRaw, xlsx::Plain);
		};
		zoneCols (r);
		x.SetNum (r, 7, z.ceilArea, xlsx::Num2);
		x.SetNum (r, 8, z.floorThick, xlsx::Num2);
		x.Set (r, 9, z.floorSource, xlsx::Plain);
		if (!excl) totalCeil += z.ceilArea;
		if (z.pieces.empty ()) {
			x.Set (r, 11, L"(стены у зоны не найдены)", xlsx::Plain);
			++r;
			continue;
		}
		for (const WallPiece& w : z.pieces) {
			if (r > 0 && &w != &z.pieces.front ())
				zoneCols (r);
			x.Set (r, 10, w.elemId, xlsx::Plain);
			x.Set (r, 11, (w.isColumn ? L"Колонна: " : L"") + w.construction, xlsx::Plain);
			if (w.column >= 1) x.SetNum (r, 12, w.column, xlsx::Default);
			else x.Set (r, 12, L"не определён", xlsx::Plain);
			x.SetNum (r, 13, w.length, xlsx::Num2);
			x.SetNum (r, 14, w.elemHeight, xlsx::Num2);
			x.SetNum (r, 15, w.finishHeight, xlsx::Num2);
			x.SetNum (r, 16, w.nOpenings, xlsx::Default);
			x.SetNum (r, 17, w.openings, xlsx::Num2);
			x.SetNum (r, 18, w.Area (), xlsx::Num2);
			if (!excl) total[(w.column >= 1 && w.column <= kWallColumns) ? w.column : 0] += w.Area ();
			++r;
		}
	}
	++r;
	x.Set (r, 6, L"Итого потолки:", xlsx::Bold);
	x.SetNum (r, 7, totalCeil, xlsx::Num2);
	++r;
	for (int c = 1; c <= kWallColumns; ++c, ++r) {
		x.Set (r, 6, L"Итого стены, столбец " + std::to_wstring (c) + L":", xlsx::Bold);
		x.SetNum (r, 18, total[c], xlsx::Num2);
	}
	if (total[0] > 0) {
		x.Set (r, 6, L"Стены без столбца:", xlsx::Bold);
		x.SetNum (r, 18, total[0], xlsx::Num2);
	}
	return x;
}

}	// namespace otd
