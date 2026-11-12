// Ведомость помещений (Excel) — как «Квартиры с площ.» / «Общедомовые N секция» / «Нежилые» в декларации:
// по зонам HimeraZone_АСП, квартиры и встроенные помещения — как в паспортах (без API, тестируется).
#ifndef PREMISESREPORT_HPP
#define PREMISESREPORT_HPP

#include "TepCore.hpp"
#include "XlsxWriter.hpp"

#include <string>
#include <vector>

namespace prem {

// Номер этажа для таблиц: первое число в названии этажа («4-й этаж» -> 4), иначе индекс этажа Archicad
int						FloorNo (const std::wstring& storyName, int story);
// Сравнение номеров «по-человечески»: 2 < 10, «12а» после «12»
bool					NumLess (const std::wstring& a, const std::wstring& b);

struct Info {
	int		apartments = 0, apartmentRows = 0, common = 0, offices = 0, storages = 0;
	std::vector<std::wstring>	sections;
};

// Листы: «Квартиры с площ.», «Общедомовые N секция» (по секциям), «Нежилые»
std::vector<xlsx::Sheet>	PremisesSheets (const std::vector<tep::Room>& rooms, const std::vector<tep::Apartment>& apts,
											const std::vector<tep::Apartment>& premises, const tep::Config& c, Info* info = nullptr);

}	// namespace prem

#endif
