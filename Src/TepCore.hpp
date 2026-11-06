// Паспорта квартир и ТЭП жилого дома по зонам HimeraZone (без API Archicad — проверяется тестами).
//   Квартира = зоны помещений квартир (код категории 1xxx) с одинаковым параметром зоны «Квартира»
//   (asAddOn_stApartmentCode) в одной секции на одном этаже.
//   Данные проекта (категории строк ТЭП, округление, значения вручную) — текстовый файл «ТЭП - <проект>.txt».
#ifndef TEPCORE_HPP
#define TEPCORE_HPP

#include "FinishTable.hpp"
#include "XlsxWriter.hpp"

#include <map>
#include <string>
#include <vector>

namespace tep {

// Набор кодов категорий: «1010, 1011» — точно, «5» — все 5xxx, «-5020» — кроме
struct CodeSet {
	std::vector<std::wstring>	inc, exc;
	bool	Has (const std::wstring& code) const;
	bool	Empty () const { return inc.empty (); }
};
CodeSet			ParseCodes (const std::wstring& text);
std::wstring	CodesText (const CodeSet& s);

struct Config {
	int				dec = 1;					// знаков после запятой у площади помещения (округление до суммирования)
	std::wstring	passportSep = L",";			// разделитель дробной части в паспорте
	double			perResident = 30;			// м² приведённой площади квартир на жителя
	double			coefBalcony = 0.3, coefLoggia = 0.5;
	std::wstring	studioLabel = L"С";			// обозначение студии в паспорте (остальные — «1К», «2К»…)
	std::wstring	officeLabel = L"АП";		// обозначение встроенного помещения в паспорте
	std::wstring	officeSep = L",";			// разделитель дробной части в паспортах помещений

	CodeSet	apartment, living, kitchenDining, kitchen, balcony, loggia;
	CodeSet	building, volume, houseArea, basement, tech, basementRest, builtIn, storage, nonResidential, common, excluded;

	// [Вручную]: номер строки ТЭП -> секция -> текст («всего» — столбец ВСЕГО)
	std::map<std::wstring, std::map<std::wstring, std::wstring>>	manual;
};

const int			kConfigVersion = 4;
std::wstring		DefaultConfigText ();
// Обновить файл данных прежней версии (значения по умолчанию прежней версии -> новые); true — текст изменён
bool				MigrateConfigText (std::wstring& text);
Config				ParseConfig (const std::wstring& text, std::vector<std::wstring>* warnings = nullptr);

// ----------------------------------------------------------------------------- помещения и квартиры
struct Room {
	std::wstring	guid, section, apartment, catCode, name, number, storyName;
	int				story = 0;				// индекс этажа Archicad
	bool			underground = false;	// подземный этаж (индекс < 0)
	double			area = 0;				// площадь помещения, м² (как в штампе зоны)
	double			volume = 0;				// объём зоны, м³
	double			height = 0;				// высота зоны, м
	double			x1 = 0, y1 = 0, x2 = 0, y2 = 0;	// габарит в плане, м
	otd::Polys		polys;					// контур зоны (для поиска свободного места под паспорт)
};

struct Apartment {
	bool				office = false;		// встроенное помещение (АП), не квартира
	double				height = 0;			// помещение: высота самой большой зоны, м
	std::wstring		section, code, storyName;
	int					story = 0;
	std::vector<size_t>	rooms;				// индексы в списке помещений
	int					livingRooms = 0;
	bool				hasKitchenDining = false, hasKitchen = false;
	double				living = 0, area = 0, total = 0, reduced = 0;	// жилая; квартиры; общая; приведённая
	std::wstring		label;				// «1К», «С»
	std::wstring		typeName;			// «2-х комнатных с кухней-столовой»
	int					typeOrder = 0;
	double				x1 = 1e30, y1 = 1e30, x2 = -1e30, y2 = -1e30;
	// у чего ставить паспорт: габарит самой большой жилой комнаты (нет жилых — вся квартира)
	double				ax1 = 0, ay1 = 0, ax2 = 0, ay2 = 0;
	void				AnchorAll () { ax1 = x1; ay1 = y1; ax2 = x2; ay2 = y2; }
	std::wstring		Key () const { return (office ? std::wstring (L"АП|") : std::wstring ()) + section + L"|" + code; }
};

double					RoundTo (double v, int dec);
std::wstring			CodeOf (const std::wstring& catCode, const std::wstring& catName);	// код или первое число имени
std::wstring			StoryKey (int story);
// Квартиры по параметру «Квартира»; warnings — зоны квартир без номера и т. п.
std::vector<Apartment>	BuildApartments (const std::vector<Room>& rooms, const Config& c, std::vector<std::wstring>* warnings = nullptr);
// Встроенные помещения (категории «Встроенные помещения») по тому же параметру «Квартира» (номер помещения: 1005):
// площадь — сумма зон, высота и место паспорта — по самой большой зоне
std::vector<Apartment>	BuildPremises (const std::vector<Room>& rooms, const Config& c);
std::wstring			OfficeArea (double v, const Config& c);		// «84,2 м²»
std::wstring			OfficeHeight (double v, const Config& c);	// «h=3,97м»
// Числа паспорта: жилая, площадь квартиры, общая, приведённая
std::wstring			PassportNum (double v, const Config& c);

// Место паспорта: снаружи у наружной стены самой большой жилой комнаты квартиры (габарит ax1…ay2). Сторона — край этажа (bx1…by2), ближайший к квартире;
// паспорт w×h (м) — по центру квартиры вдоль этой стороны, на расстоянии gap (м) от границы квартиры.
// Возвращает левый нижний угол.
void					PassportPlace (const Apartment& a, double bx1, double by1, double bx2, double by2,
									   double w, double h, double gap, double& x, double& y);

// Свободное место на плане этажа: занято зонами, стенами, колоннами и уже стоящими паспортами
struct Rect { double x1 = 0, y1 = 0, x2 = 0, y2 = 0; };

class FreeSpace {
public:
	void	AddShape (const std::vector<otd::Pt>& poly);	// многоугольник (контур зоны, стены)
	void	AddRect (const Rect& r);
	bool	IsFree (const Rect& r) const;
	size_t	Count () const { return shapes.size () + rects.size (); }
private:
	std::vector<std::vector<otd::Pt>>	shapes;
	std::vector<Rect>					shapeBox;
	std::vector<Rect>					rects;
};

// Ближайшее к жилой комнате квартиры свободное место для паспорта w×h: перебираются стороны комнаты (сначала — ближайшая
// к краю этажа bx1…by2), отступ от границы квартиры от minGap до maxGap шагом step и сдвиг вдоль стороны.
// margin — зазор до занятого. false — свободного места нет (x, y — как PassportPlace).
bool					PlaceFree (const Apartment& a, double bx1, double by1, double bx2, double by2, double w, double h,
								   double minGap, double maxGap, double step, double margin, const FreeSpace& fs,
								   double& x, double& y);

// ----------------------------------------------------------------------------- ТЭП
struct TepRow {
	std::wstring				key;				// номер, у «в т.ч.» — «номер/название»
	std::wstring				num, name, unit;
	bool						sub = false;		// «в т.ч.» — с отступом
	bool						bold = false;
	std::vector<std::wstring>	cells;				// по секциям
	std::wstring				total;
};

struct Tep {
	std::vector<std::wstring>	sections;
	std::vector<TepRow>			rows;
	std::vector<std::wstring>	floorsInfo;		// какие этажи вошли в этажность / подземные — для отчёта
	// таблица проверки: строка ТЭП (key) -> секция -> что учтено (строки текста)
	std::map<std::wstring, std::map<std::wstring, std::vector<std::wstring>>>	check;
};

// Подземный этаж: отметка ниже −0,5 м, отрицательный индекс или «подвал / подполье / подзем» в названии
bool			IsUnderground (int index, double level, const std::wstring& name);

std::vector<std::wstring>	SortSections (std::vector<std::wstring> s);
Tep				BuildTep (const std::vector<Room>& rooms, const std::vector<Apartment>& apts, const Config& c);
otd::TableLayout	LayoutTep (const Tep& t, const std::wstring& title);
xlsx::Sheet		TepSheet (const Tep& t, const std::wstring& title);
// Таблица проверки ТЭП: по каждой строке и секции — какие зоны (категории, кол-во, сумма), этажи, квартиры учтены
otd::TableLayout	LayoutTepCheck (const Tep& t, const std::wstring& title);
xlsx::Sheet		TepCheckSheet (const Tep& t);
xlsx::Sheet		ApartmentsSheet (const std::vector<Room>& rooms, const std::vector<Apartment>& apts, const Config& c);

}	// namespace tep

#endif
