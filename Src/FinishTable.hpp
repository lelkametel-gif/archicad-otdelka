// Ведомость отделки -> таблица из линий и однострочных текстов (для рабочего листа Archicad).
// Все размеры в мм листа; начало — левый верхний угол таблицы, ось Y вверх (строки идут вниз).
#ifndef FINISHTABLE_HPP
#define FINISHTABLE_HPP

#include "FinishCore.hpp"

#include <string>
#include <vector>

namespace otd {

struct TSeg { double x1, y1, x2, y2; };

struct TText {
	double			x = 0, y = 0;		// центр строки текста
	std::wstring	text;
	double			size = 2.5;			// высота шрифта, мм
	bool			vertical = false;	// повёрнут на 90°
	bool			bold = false;
	std::wstring	tag;				// GUID зоны строки таблицы проверки (пусто — нет)
};

struct TableStyle {
	double				textSize = 2.5;
	double				titleSize = 3.5;
	double				pad = 1.2;			// поля ячейки
	double				lineFactor = 1.55;	// шаг строк = высота шрифта × коэффициент
	double				charFactor = 0.62;	// средняя ширина буквы = высота шрифта × коэффициент
	double				minRow = 8;
	std::vector<double>	colW = { 12, 40, 36, 16, 32, 16, 32, 16, 32, 16, 22 };
};

struct TableLayout {
	std::vector<TSeg>	segs;
	std::vector<TText>	texts;
	double				width = 0, height = 0;
};

// Перенос по словам в ширину widthMm (длинное слово режется)
std::vector<std::wstring>	WrapText (const std::wstring& text, double widthMm, double size, double charFactor);

TableLayout		LayoutSheet (const Sheet& sh, const TableStyle& st = TableStyle ());

// Таблица проверки: каждая зона — этаж, номер, имя, категория, площадь, подкатегория и строка ведомости
struct CheckRow {
	int				floor = 0;
	std::wstring	floorName, number, zoneName, category, code, group, row, guid;
	double			area = 0;
	bool			excluded = false;
};
std::vector<CheckRow>	CheckRows (const std::vector<ZoneData>& zones, const Rules& r);
TableLayout		LayoutCheck (const std::vector<ZoneData>& zones, const Rules& r, const std::wstring& title,
							 const TableStyle& st = TableStyle ());
void			ShiftLayout (TableLayout& t, double dx, double dy);
// Приставить таблицу справа через промежуток gap, мм
void			AppendLayout (TableLayout& to, const TableLayout& from, double gap);

// SVG для проверки на Linux
std::string		LayoutSvg (const TableLayout& t);

}	// namespace otd

#endif
