// Ведомость отделки -> листы Excel (без API Archicad)
#ifndef FINISHREPORT_HPP
#define FINISHREPORT_HPP

#include "FinishCore.hpp"
#include "XlsxWriter.hpp"

namespace otd {

// Лист «Ведомость» в форме листа 63 тома АР
xlsx::Sheet		MakeVedomostSheet (const Sheet& sh);
// Лист «Расчёт» — каждая стена каждой зоны, для проверки
xlsx::Sheet		MakeDetailsSheet (const std::vector<ZoneData>& zones, const Rules& r);

}	// namespace otd

#endif
