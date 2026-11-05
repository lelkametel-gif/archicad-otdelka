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
	std::wstring	passportSep = L".";			// разделитель дробной части в паспорте
	double			perResident = 30;			// м² приведённой площади квартир на жителя
	double			coefBalcony = 0.3, coefLoggia = 0.5;
	std::wstring	studioLabel = L"С";			// обозначение студии в паспорте (остальные — «1К», «2К»…)

	CodeSet	apartment, living, kitchenDining, kitchen, balcony, loggia;
	CodeSet	building, volume, houseArea, tech, builtIn, storage, nonResidential, common, excluded;

	// [Вручную]: номер строки ТЭП -> секция -> текст («всего» — столбец ВСЕГО)
	std::map<std::wstring, std::map<std::wstring, std::wstring>>	manual;
};

const int			kConfigVersion = 1;
std::wstring		DefaultConfigText ();
Config				ParseConfig (const std::wstring& text, std::vector<std::wstring>* warnings = nullptr);

// ----------------------------------------------------------------------------- помещения и квартиры
struct Room {
	std::wstring	guid, section, apartment, catCode, name, number, storyName;
	int				story = 0;				// индекс этажа Archicad
	bool			underground = false;	// подземный этаж (индекс < 0)
	double			area = 0;				// площадь помещения, м² (как в штампе зоны)
	double			volume = 0;				// объём зоны, м³
	double			x1 = 0, y1 = 0, x2 = 0, y2 = 0;	// габарит в плане, м
	otd::Polys		polys;					// контур зоны (для поиска свободного места под паспорт)
};

struct Apartment {
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
	std::wstring		Key () const { return section + L"|" + code; }
};

double					RoundTo (double v, int dec);
std::wstring			CodeOf (const std::wstring& catCode, const std::wstring& catName);	// код или первое число имени
std::wstring			StoryKey (int story);
// Квартиры по параметру «Квартира»; warnings — зоны квартир без номера и т. п.
std::vector<Apartment>	BuildApartments (const std::vector<Room>& rooms, const Config& c, std::vector<std::wstring>* warnings = nullptr);
// Числа паспорта: жилая, площадь квартиры, общая, приведённая
std::wstring			PassportNum (double v, const Config& c);

// Место паспорта: снаружи у наружной стены квартиры. Сторона — край этажа (bx1…by2), ближайший к квартире;
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

// Ближайшее к квартире свободное место для паспорта w×h: перебираются стороны квартиры (сначала — ближайшая
// к краю этажа bx1…by2), отступ от границы квартиры от minGap до maxGap шагом step и сдвиг вдоль стороны.
// margin — зазор до занятого. false — свободного места нет (x, y — как PassportPlace).
bool					PlaceFree (const Apartment& a, double bx1, double by1, double bx2, double by2, double w, double h,
								   double minGap, double maxGap, double step, double margin, const FreeSpace& fs,
								   double& x, double& y);

// ----------------------------------------------------------------------------- ТЭП
struct TepRow {
	std::wstring				num, name, unit;
	bool						sub = false;		// «в т.ч.» — с отступом
	bool						bold = false;
	std::vector<std::wstring>	cells;				// по секциям
	std::wstring				total;
};

struct Tep {
	std::vector<std::wstring>	sections;
	std::vector<TepRow>			rows;
};

std::vector<std::wstring>	SortSections (std::vector<std::wstring> s);
Tep				BuildTep (const std::vector<Room>& rooms, const std::vector<Apartment>& apts, const Config& c);
otd::TableLayout	LayoutTep (const Tep& t, const std::wstring& title);
xlsx::Sheet		TepSheet (const Tep& t, const std::wstring& title);
xlsx::Sheet		ApartmentsSheet (const std::vector<Room>& rooms, const std::vector<Apartment>& apts, const Config& c);

}	// namespace tep

#endif
