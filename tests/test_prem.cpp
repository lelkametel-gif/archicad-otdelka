// Тест ведомости помещений (Excel): g++ -O2 -std=c++17 -ISrc tests/test_prem.cpp Src/PremisesReport.cpp Src/TepCore.cpp
//   Src/FinishCore.cpp Src/XlsxWriter.cpp Src/FinishTable.cpp && ./a.out prem.xlsx
#include "PremisesReport.hpp"
#include "FinishCore.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>

using namespace tep;

static int fails = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Room R (const wchar_t* sec, int story, const wchar_t* apt, const wchar_t* code, const wchar_t* name, double area, const wchar_t* num = L"", double h = 2.61)
{
	Room r;
	r.section = sec; r.story = story; r.apartment = apt; r.catCode = code; r.name = name; r.area = area; r.number = num; r.height = h;
	r.storyName = story < 0 ? L"Подвал" : std::to_wstring (story) + L"-й этаж";
	r.underground = story < 0;
	return r;
}

static const xlsx::Cell* At (const xlsx::Sheet& sh, int r, int c)
{
	for (const auto& x : sh.cells) if (x.row == r && x.col == c) return &x;
	return nullptr;
}

int main (int argc, char** argv)
{
	std::vector<std::wstring> warn;
	const Config c = ParseConfig (DefaultConfigText (), &warn);
	std::vector<Room> rooms = {
		// секция 1, этаж 2: кв. 2 — 2К с балконом и лоджией
		R (L"1", 2, L"2", L"1010", L"Комната", 12.04), R (L"1", 2, L"2", L"1010", L"Комната", 10.21),
		R (L"1", 2, L"2", L"1022", L"Кухня-столовая", 16.0), R (L"1", 2, L"2", L"1030", L"Прихожая", 3.8),
		R (L"1", 2, L"2", L"1050", L"Совмещенный с/у", 4.1),
		R (L"1", 2, L"2", L"1060", L"Балкон", 2.9), R (L"1", 2, L"2", L"1070", L"Лоджия", 2.5),
		// кв. 10 — студия с зоной кухни
		R (L"1", 2, L"10", L"1010", L"Комната", 18.1), R (L"1", 2, L"10", L"1021", L"Зона кухни", 5.8),
		R (L"1", 2, L"10", L"1030", L"Прихожая", 3.2), R (L"1", 2, L"10", L"1050", L"Совмещенный с/у", 4.7),
		// общедомовые
		R (L"1", -1, L"", L"3070", L"Подвальное помещение", 429.9, L"001"), R (L"1", -1, L"", L"3030", L"Коридор", 47.0, L"002"),
		R (L"1", 1, L"", L"2012", L"Тамбур", 6.4, L"101"), R (L"1", 3, L"", L"2030", L"Лестничная клетка", 10.9, L"301"),
		R (L"2", 2, L"", L"2020", L"Коридор", 37.0),
		// встроенное помещение 1001 и кладовая 2001
		R (L"1", 1, L"1001", L"5010", L"Административное помещение", 164.8, L"", 4.12), R (L"1", 1, L"1001", L"5030", L"С/у (КУИ)", 4.9, L"", 4.12),
		R (L"2", 1, L"2001", L"4010", L"Индивидуальная кладовая", 5.8, L"", 3.69),
	};
	const auto apts = BuildApartments (rooms, c, &warn);
	const auto prem = BuildPremises (rooms, c);
	prem::Info info;
	const auto sheets = prem::PremisesSheets (rooms, apts, prem, c, &info);
	CHECK (sheets.size () == 4);		// квартиры, общедомовые 1 и 2 секции, нежилые
	CHECK (info.apartments == 2 && info.common == 5 && info.offices == 1 && info.storages == 1);
	const xlsx::Sheet& a = sheets[0];
	// кв. 2 (по номеру раньше 10): строки 4..5 (2 комнаты, 3 вспомогательных -> 3 строки)
	const auto* A = At (a, 4, 0);
	CHECK (A && A->isNum && A->num == 2);
	const auto* G = At (a, 4, 6);
	CHECK (G && G->formula == "SUM(J5:J7)" && std::fabs (G->num - 22.2) < 1e-9);
	const auto* E = At (a, 4, 4);
	CHECK (E && std::fabs (E->num - (22.2 + 16.0 + 3.8 + 4.1 + 0.9 + 1.3)) < 1e-9);	// балкон 2,9×0,3=0,9; лоджия 2,5×0,5=1,3
	const auto* Q = At (a, 4, 16);
	CHECK (Q && std::fabs (Q->num - (22.2 + 16.0 + 3.8 + 4.1 + 2.9 + 2.5)) < 1e-9);
	// студия — строка 7: J = G + K
	const auto* B = At (a, 7, 1);
	CHECK (B && B->text == L"Квартира-студия");
	const auto* J = At (a, 7, 9);
	CHECK (J && J->formula == "SUM(G8,K8)" && std::fabs (J->num - 23.9) < 1e-9);
	const auto* F = At (a, 7, 5);
	CHECK (F && F->formula == "SUM(M8:M9)+J8" && std::fabs (F->num - 31.8) < 1e-9);
	// общедомовые 1 секции: подвал техническое / коридор подвала общественное / кровля
	const xlsx::Sheet& s1 = sheets[1];
	CHECK (s1.name == L"Общедомовые 1 секция");
	CHECK (At (s1, 4, 2)->text == L"подвальное помещение" && At (s1, 4, 3)->text == L"Техническое");
	CHECK (At (s1, 5, 3)->text == L"Общественное");
	CHECK (At (s1, 6, 2)->text == L"1 этаж");
	CHECK (At (s1, 7, 2)->text == L"Выход на кровлю");		// этаж 3 выше последнего этажа с квартирами (2)
	const xlsx::Sheet& n = sheets[3];
	bool office = false, storage = false;
	for (const auto& x : n.cells) {
		if (x.isNum && x.formula == "SUM(G5:G6)" && std::fabs (x.num - 169.7) < 1e-9) office = true;
		if (x.text == L"Индивидуальная кладовая") storage = true;
	}
	CHECK (office && storage);
	CHECK (prem::FloorNo (L"4-й этаж", 7) == 4 && prem::FloorNo (L"Подвал", -1) == -1 && prem::FloorNo (L"Этаж -1", 0) == -1);
	CHECK (prem::NumLess (L"2", L"10") && !prem::NumLess (L"10", L"2") && prem::NumLess (L"12", L"12а"));
	if (argc > 1) {
		std::ofstream f (argv[1], std::ios::binary);
		const std::string b = xlsx::BuildXlsx (sheets);
		f.write (b.data (), (std::streamsize) b.size ());
	}
	std::printf ("%d проверок, ошибок: %d\n", checks, fails);
	return fails ? 1 : 0;
}
