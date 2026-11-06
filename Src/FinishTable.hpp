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
	bool			left = false;		// x — левый край строки (иначе центр)
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

// Ячейка сетки таблицы: строки r1..r2, столбцы c1..c2 (с 0)
struct GridCell {
	int				r1 = 0, c1 = 0, r2 = 0, c2 = 0;
	std::wstring	text;
	bool			vertical = false;
	bool			bold = false;
	bool			left = false;		// по левому краю (отступ indent, мм)
	double			indent = 0;
	std::wstring	tag;
};

// Таблица из ячеек: высоты строк по тексту, линии между разными ячейками, заголовок над таблицей
TableLayout		RenderGrid (const std::vector<GridCell>& cells, int nRows, int headerRows, const std::vector<double>& colW,
							const TableStyle& st, const std::wstring& title);

// Перенос по словам в ширину widthMm (длинное слово режется)
std::vector<std::wstring>	WrapText (const std::wstring& text, double widthMm, double size, double charFactor);

TableLayout		LayoutSheet (const Sheet& sh, const TableStyle& st = TableStyle ());

// Таблица проверки: каждая зона — этаж, номер, имя, категория, площадь, подкатегория и строка ведомости
struct CheckRow {
	int				floor = 0;
	std::wstring	floorName, number, zoneName, category, code, group, row, guid, note;
	double			area = 0;
	double			wall[kWallColumns + 1] = { 0, 0, 0, 0 };	// площадь стен по столбцам, м² ([0] — не определён)
	double			wallLen = 0, openings = 0;	// суммарная длина граней стен и колонн, м; вычтенные проёмы, м²
	double			len[kWallColumns + 1] = { 0, 0, 0, 0 };	// длина по столбцам, м
	double			height = 0, minH = 0;		// высота отделки зоны; наименьшая у элементов (ниже зоны), м
	bool			excluded = false;
};
std::vector<CheckRow>	CheckRows (const std::vector<ZoneData>& zones, const Rules& r);
TableLayout		LayoutCheck (const std::vector<ZoneData>& zones, const Rules& r, const std::wstring& title,
							 const TableStyle& st = TableStyle ());
void			ShiftLayout (TableLayout& t, double dx, double dy);
// Приставить таблицу справа через промежуток gap, мм
void			AppendLayout (TableLayout& to, const TableLayout& from, double gap);

// ---- Проверка зон на плане: свойства зоны (группа «ПСО и отделка») и графическая замена по ним
extern const wchar_t* const kPropGroup;			// группа свойств и группа правил графической замены
extern const wchar_t* const kPropSubcat;		// «Отделка - подкатегория»: подкатегория ведомости / «Не входит»
extern const wchar_t* const kPropCheck;			// «Отделка - проверка»: одно из значений kChk*
extern const wchar_t* const kPropNote;			// «Отделка - замечание»: что не так (текст)
extern const wchar_t* const kChkOk;				// «Верно»
extern const wchar_t* const kChkNotes;			// «Замечания» — в расчёте, но проверить
extern const wchar_t* const kChkNoFinish;		// «Ошибка: нет отделки»
extern const wchar_t* const kChkNoGroup;		// «Ошибка: нет подкатегории»
extern const wchar_t* const kChkExcluded;		// «Не входит» (8, 9)
extern const wchar_t* const kChkOtherLib;		// «Другой объект зоны»

struct ZoneCheck {
	std::wstring	subcat, check, note;
};
ZoneCheck		CheckZone (const ZoneData& z, const Rules& r);
ZoneCheck		OtherLibCheck (const std::wstring& libName);

struct OverrideRuleDef {
	std::wstring	name, prop, value;
	double			rgb[3] = { 1, 1, 1 };		// цвет фона штриховки зоны 0..1
};
struct OverrideComboDef {
	std::wstring				name;
	std::vector<std::wstring>	rules;			// имена правил по порядку (верхнее главнее)
};
std::vector<OverrideRuleDef>	OverrideRules (const Rules& r);
std::vector<OverrideComboDef>	OverrideCombos (const Rules& r);
// Критерий правила: «Тип элемента = Зона» И «свойство prop (группа kPropGroup) = value» (формат Archicad 27)
std::wstring	CriterionXml (const std::wstring& prop, const std::wstring& value);

// SVG для проверки на Linux
std::string		LayoutSvg (const TableLayout& t);

}	// namespace otd

#endif
