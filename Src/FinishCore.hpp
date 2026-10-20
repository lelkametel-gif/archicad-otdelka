// Ведомость отделки помещений — логика без API Archicad (проверяется тестами на Linux)
//   правила (столбцы стен по материалу, группы и строки помещений, названия отделки),
//   разбор номеров этажей, расчёт площадей и сборка строк ведомости.
#ifndef FINISHCORE_HPP
#define FINISHCORE_HPP

#include <map>
#include <set>
#include <string>
#include <vector>

namespace otd {

const int kWallColumns = 3;			// столбцы стен: 1 монолит, 2 кладка/КБ, 3 ГКЛ

// ----------------------------------------------------------------------------- строки
std::wstring	Trim (const std::wstring& s);
std::wstring	Lower (const std::wstring& s);			// кириллица и латиница
bool			SameText (const std::wstring& a, const std::wstring& b);	// без регистра и пробелов по краям
std::string		ToUtf8 (const std::wstring& s);
std::wstring	FromUtf8 (const std::string& s);
std::wstring	FormatNum (double v, int dec);			// «1 080,8»

// ----------------------------------------------------------------------------- правила
struct MatRule {
	std::wstring	code;		// код слоя из названия конструкции: ЖБ, КБ, Кирп, ГКЛ…
	int				column = 0;	// 1..3; 0 — не учитывать
};

struct RowRule {
	std::wstring				group;	// пусто — любая группа
	std::wstring				title;	// название строки в ведомости
	std::vector<std::wstring>	names;	// имена зон
};

struct Rules {
	std::wstring					title = L"{секция} секция. Ведомость отделки помещений жилого дома";
	std::wstring					columnTitle[kWallColumns + 1];
	std::vector<MatRule>			materials;			// проверяются по порядку
	int								columnsColumn = 1;	// колонны и пилоны
	std::vector<std::wstring>		groupOrder;			// порядок групп в ведомости
	std::vector<std::pair<std::wstring, std::wstring>>	groupByCode;	// начало кода категории зоны -> группа
	std::wstring					basementGroup;		// зоны на этажах с номером 0 и ниже (если задано)
	std::wstring					otherGroup = L"Прочие помещения";
	std::vector<RowRule>			rows;
	std::vector<std::pair<std::wstring, std::wstring>>	finishNames;	// как в зоне -> как в ведомости
};

std::wstring	DefaultRulesText ();
Rules			ParseRules (const std::wstring& text, std::vector<std::wstring>* warnings = nullptr);

// Коды слоёв из названия конструкции «Ст_В| Ш 15_КБ 190_Ш 15» -> {Ш, КБ, Ш}
std::vector<std::wstring>	MaterialCodes (const std::wstring& name);
// Столбец по названию конструкции / строительного материала; 0 — не определён
int				ClassifyWall (const std::wstring& name, const Rules& r);

// Название отделки: «00 Покраска» -> «Покраска» -> замена из правил
std::wstring	CleanFinish (const std::wstring& raw, const Rules& r);

// ----------------------------------------------------------------------------- этажи
// «1», «2-14», «1; 3; 5-7», «-1». false — ошибка в записи
bool			ParseFloors (const std::wstring& text, std::set<int>& out);
// {2..14} -> «2-14 эт.», {1,3} -> «1, 3 эт.»
std::wstring	FloorsLabel (const std::set<int>& floors);

// ----------------------------------------------------------------------------- площади
// Часть проёма (низ lower, высота h) внутри полосы отделки [lo, hi]
double			OpeningOverlap (double lower, double h, double lo, double hi);

struct WallPiece {
	std::wstring	elemId;			// ID стены / колонны
	std::wstring	construction;	// название конструкции или материала
	bool			isColumn = false;
	int				column = 0;		// 0 — не определён
	double			length = 0;		// м
	double			elemHeight = 0;	// высота стены, м
	double			finishHeight = 0;	// высота отделки = высота стены − пол − потолок
	double			openings = 0;	// м², вычтенные проёмы
	int				nOpenings = 0;
	double			Area () const;
};

struct ZoneData {
	std::wstring			number;
	std::wstring			name;				// наименование для ведомости (категория зоны)
	std::wstring			roomName;			// имя зоны (для листа проверки)
	int						floor = 0;
	std::wstring			catCode;			// код категории зоны (1010, 2012…)
	std::wstring			section;
	std::wstring			ceilFinishRaw, wallFinishRaw;
	double					ceilArea = 0;		// м²
	double					floorThick = 0;		// м, принятая толщина пола
	std::wstring			floorSource;		// «перекрытие …» / «из настроек»
	std::vector<WallPiece>	pieces;
};

struct SheetRow {
	std::wstring	group, title;
	std::wstring	ceilFinish;
	double			ceilArea = 0;
	std::wstring	wallFinish;
	double			wallArea[kWallColumns + 1] = { 0, 0, 0, 0 };	// [0] — не определён столбец
	std::set<int>	floors;
	int				zones = 0;
};

struct Sheet {
	std::wstring			title;
	std::wstring			columnTitle[kWallColumns + 1];
	std::vector<SheetRow>	rows;
	double					unknownArea = 0;	// стены без столбца
};

std::wstring	GroupOf (const ZoneData& z, const Rules& r);
// «МОП_ВхГ_Тамбур» -> «Тамбур» (часть после последнего «_»)
std::wstring	ShortName (const std::wstring& category);
std::wstring	RowTitleOf (const ZoneData& z, const std::wstring& group, const Rules& r);
Sheet			BuildSheet (const std::vector<ZoneData>& zones, const Rules& r, const std::wstring& section);

}	// namespace otd

#endif
