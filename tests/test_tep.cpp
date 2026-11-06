// Проверки паспортов квартир и ТЭП:
//   g++ -std=c++17 -ISrc -o /tmp/tt tests/test_tep.cpp Src/TepCore.cpp Src/FinishCore.cpp Src/XlsxWriter.cpp Src/FinishTable.cpp && /tmp/tt [tep.svg] [tep.xlsx]
#include "TepCore.hpp"
#include "FinishCore.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>

static int fails = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b) CHECK (std::fabs ((a) - (b)) < 1e-6)

using namespace tep;
static std::string U (const std::wstring& w) { return otd::ToUtf8 (w); }

static Room R (const wchar_t* sec, int story, const wchar_t* apt, const wchar_t* code, const wchar_t* name, double area,
			   double x1 = 0, double y1 = 0, double x2 = 1, double y2 = 1)
{
	Room r;
	r.section = sec; r.story = story; r.apartment = apt; r.catCode = code; r.name = name; r.area = area;
	r.storyName = story < 0 ? L"Подвал" : L"Этаж " + std::to_wstring (story);
	r.underground = story < 0;
	r.x1 = x1; r.y1 = y1; r.x2 = x2; r.y2 = y2;
	return r;
}

int main (int argc, char** argv)
{
	std::vector<std::wstring> warn;
	const Config c = ParseConfig (DefaultConfigText (), &warn);
	for (const auto& w : warn) std::printf ("%s\n", U (w).c_str ());
	CHECK (warn.empty ());
	CHECK (c.dec == 1);
	CHECK (c.living.Has (L"1010") && c.living.Has (L"1011") && !c.living.Has (L"1022"));
	CHECK (c.builtIn.Has (L"5010") && !c.builtIn.Has (L"5020") && c.storage.Has (L"5020"));
	CHECK (c.apartment.Has (L"1070") && !c.apartment.Has (L"2010"));
	CHECK (c.common.Has (L"2050") && c.common.Has (L"6020") && !c.common.Has (L"3030"));
	NEAR (c.coefBalcony, 0.3); NEAR (c.coefLoggia, 0.5);
	CHECK (c.manual.empty ());
	CHECK (CodeOf (L"", L"1022 Кухня-столовая") == L"1022");
	CHECK (CodeOf (L" 2050 ", L"x") == L"2050");
	NEAR (RoundTo (2.45, 1), 2.5);
	NEAR (RoundTo (1.2, 1), 1.2);

	{	// свой файл: значения вручную и изменённые категории
		std::vector<std::wstring> w2;
		const Config m = ParseConfig (L"[Площади]\nЗнаков после запятой = 2\n[Категории]\nЖилые комнаты = 1010\n"
									  L"[Вручную]\n18 = 1: 22; 2: 7; всего: 29\n2 = 1: 15\nпривет\n[Чужое]\nx = 1\n", &w2);
		CHECK (m.dec == 2);
		CHECK (!m.living.Has (L"1011"));
		CHECK (m.kitchenDining.Has (L"1022"));		// остальное — по умолчанию
		CHECK (m.manual.at (L"18").at (L"2") == L"7");
		CHECK (m.manual.at (L"18").at (L"всего") == L"29");
		CHECK (w2.size () == 2);
	}

	// ---- секция 2, этаж 2: 2К с лоджией (как на плане), 1К с кухней-столовой, студия; секция 1 — 2К без кухни-столовой
	std::vector<Room> rooms = {
		R (L"2", 2, L"1", L"1010", L"Комната", 10.9, 0, 10, 4, 14),
		R (L"2", 2, L"1", L"1010", L"Комната", 10.3, 4, 10, 7, 14),
		R (L"2", 2, L"1", L"1022", L"Кухня-столовая", 12.3, 7, 10, 10, 14),
		R (L"2", 2, L"1", L"1030", L"Прихожая", 4.1),
		R (L"2", 2, L"1", L"1040", L"Коридор", 16.76),		// округляется до 16,8
		R (L"2", 2, L"1", L"1050", L"Совм. с/у", 2.9),
		R (L"2", 2, L"1", L"1051", L"С/у", 2.6),
		R (L"2", 2, L"1", L"1070", L"Лоджия", 2.4, 0, 9, 4, 10),
		R (L"2", 2, L"2", L"1010", L"Комната", 10.8, 0, 0, 4, 4),
		R (L"2", 2, L"2", L"1022", L"Кухня-столовая", 19.4, 4, 0, 8, 4),
		R (L"2", 2, L"2", L"1030", L"Прихожая", 5.0),
		R (L"2", 2, L"2", L"1050", L"Совм. с/у", 3.0),
		R (L"2", 2, L"3", L"1010", L"Комната", 18.3),
		R (L"2", 2, L"3", L"1050", L"С/у", 3.5),
		R (L"2", 2, L"3", L"1060", L"Балкон", 3.0),
		R (L"2", 2, L"", L"1010", L"Комната", 9.0),				// без номера квартиры
		R (L"2", 2, L"", L"2050", L"Лифтовой холл", 37.0),
		R (L"2", 2, L"9", L"2040", L"Коридор", 5.5),			// номер квартиры у МОП
		R (L"2", -1, L"", L"4050", L"Электрощитовая", 25.1),
		R (L"2", -1, L"", L"3030", L"Коридор", 21.2),
		R (L"2", -1, L"", L"3060", L"Кладовая", 2.5),
		R (L"2", -1, L"", L"3060", L"Кладовая", 2.5),
		R (L"2", 1, L"", L"5010", L"Офис", 120.25),
		R (L"2", 1, L"", L"5020", L"Кладовая №1", 6.1),
		R (L"2", 1, L"", L"8020", L"Площадь застройки", 594.3),
		R (L"2", 1, L"", L"8010", L"Строительный объем", 0),
		R (L"1", 3, L"5", L"1010", L"Комната", 12.0),
		R (L"1", 3, L"5", L"1010", L"Комната", 14.0),
		R (L"1", 3, L"5", L"1020", L"Кухня", 9.0),
		R (L"1", 4, L"5", L"1010", L"Комната", 12.0),			// та же «5» на другом этаже — другая квартира (студия)
	};
	rooms[25].volume = 1000; rooms[25].underground = false;
	{
		Room v = R (L"2", -1, L"", L"8010", L"Строительный объем", 0); v.volume = 461.6;
		rooms.push_back (v);
	}
	warn.clear ();
	const std::vector<Apartment> apts = BuildApartments (rooms, c, &warn);
	for (const auto& w : warn) std::printf ("  %s\n", U (w).c_str ());
	CHECK (apts.size () == 5);
	CHECK (warn.size () == 2);		// зона без номера; номер квартиры у МОП
	const Apartment* a1 = nullptr; const Apartment* a2 = nullptr; const Apartment* a3 = nullptr;
	for (const auto& a : apts) {
		if (a.section == L"2" && a.code == L"1") a1 = &a;
		if (a.section == L"2" && a.code == L"2") a2 = &a;
		if (a.section == L"2" && a.code == L"3") a3 = &a;
	}
	CHECK (apts.front ().section == L"1");
	CHECK (a1 && a2 && a3);
	if (a1) {
		NEAR (a1->living, 21.2);
		NEAR (a1->area, 59.9);
		NEAR (a1->total, 62.3);
		NEAR (a1->reduced, 61.1);
		CHECK (a1->label == L"2К");
		CHECK (a1->typeName == L"2-х комнатных с кухней-столовой");
		CHECK (PassportNum (a1->reduced, c) == L"61.1");
		NEAR (a1->x1, 0); NEAR (a1->y2, 14);
		NEAR (a1->ax1, 0); NEAR (a1->ax2, 4); NEAR (a1->ay1, 10);		// у самой большой жилой комнаты (10,9)
		double x, y;
		PassportPlace (*a1, 0, 0, 20, 14, 2, 1.4, 0.8, x, y);		// у верхнего края этажа -> над ней
		NEAR (y, 14.8); NEAR (x, 2 - 1);
		Apartment b = *a1; b.y1 = 9; b.y2 = 12.5; b.x1 = 2; b.x2 = 10; b.AnchorAll ();			// фасад секции отступает от края этажа — паспорт у стены квартиры
		PassportPlace (b, 0, 0, 20, 14, 2, 1.4, 0.8, x, y);
		NEAR (y, 13.3);
		Apartment s = *a1; s.x1 = 18; s.x2 = 19.6; s.y1 = 6; s.y2 = 8; s.AnchorAll ();	// у правого края -> справа
		PassportPlace (s, 0, 0, 20, 14, 2, 1.4, 0.8, x, y);
		NEAR (x, 20.4); NEAR (y, 7 - 0.7);
	}
	if (a1) {	// свободное место: сверху занято (соседняя секция), ближайшее свободное — снизу? нет: левее/правее по стороне
		FreeSpace fs;
		Apartment q = *a1; q.x1 = 0; q.x2 = 10; q.y1 = 9; q.y2 = 14; q.AnchorAll ();
		fs.AddShape ({ { 0, 9 }, { 10, 9 }, { 10, 14 }, { 0, 14 } });			// сама квартира
		double x, y;
		CHECK (PlaceFree (q, 0, 0, 20, 14, 2, 1.4, 0.2, 6, 0.2, 0.1, fs, x, y));
		NEAR (y, 14.2); NEAR (x, 4);											// снаружи над серединой
		fs.AddShape ({ { -5, 14.3 }, { 6, 14.3 }, { 6, 20 }, { -5, 20 } });		// над левой частью — зона соседней секции
		CHECK (PlaceFree (q, 0, 0, 20, 14, 2, 1.4, 0.2, 6, 0.2, 0.1, fs, x, y));
		CHECK (x >= 6.1 - 1e-6 && y >= 14.2 - 1e-6 && y < 15);					// сдвинут вправо вдоль стороны
		CHECK (!fs.IsFree ({ 2, 10, 3, 11 }));									// внутри квартиры
		CHECK (fs.IsFree ({ 11, 10, 12, 11 }));
		fs.AddRect ({ 6, 14, 12, 16 });											// там уже паспорт
		CHECK (PlaceFree (q, 0, 0, 20, 14, 2, 1.4, 0.2, 6, 0.2, 0.1, fs, x, y));
		CHECK (!(x < 12 && x + 2 > 6 && y < 16 && y + 1.4 > 14));
	}
	if (a2) {
		CHECK (a2->label == L"1К");
		CHECK (a2->typeName == L"1-комнатных с кухней-столовой");
		NEAR (a2->area, 38.2); NEAR (a2->total, 38.2); NEAR (a2->living, 10.8);
		double x, y;
		PassportPlace (*a2, 0, 0, 20, 14, 2, 1.4, 0.8, x, y);		// у нижнего края -> под комнатой
		NEAR (y, -0.8 - 1.4); NEAR (x, 2 - 1);
	}
	{
		std::vector<Room> rr = {
			R (L"2", 2, L"115", L"1022", L"Кухня-столовая", 19.4, 0, 8, 4, 14),
			R (L"2", 2, L"115", L"1030", L"Прихожая", 5.1, 4, 5, 6, 8),
			R (L"2", 2, L"115", L"1010", L"Комната", 10.9, 0, 0, 3.6, 4),
			R (L"2", 2, L"115", L"1010", L"Комната", 10.3, 3.6, 0, 6.6, 4),
			R (L"2", 2, L"115", L"1070", L"Лоджия", 2.4, 3.6, -1.2, 6, 0) };
		const auto aa = BuildApartments (rr, c);
		CHECK (aa.size () == 1);
		if (!aa.empty ()) {
			FreeSpace fs;
			for (const auto& r : rr) fs.AddShape ({ { r.x1, r.y1 }, { r.x2, r.y1 }, { r.x2, r.y2 }, { r.x1, r.y2 } });
			double x = 0, y = 0;
			CHECK (PlaceFree (aa[0], 0, -1.2, 6.6, 14, 1.8, 1.4, 0.2, 6, 0.15, 0.08, fs, x, y));
			CHECK (y + 1.4 <= 0 + 1e-6);					// внизу, под комнатой 10,9
			CHECK (x + 1.8 <= 3.6 + 0.9 + 1e-6);			// не под лоджией
		}
	}
	{	// встроенные помещения: номер 1005 в параметре «Квартира»
		std::vector<Room> rr = {
			R (L"1", 1, L"1005", L"5010", L"Административное помещение", 78.7, 0, 0, 9, 10),
			R (L"1", 1, L"1005", L"5030", L"С/у (КУИ)", 5.5, 3, 2, 5, 4),
			R (L"1", 1, L"1006", L"5010", L"Административное помещение", 94.7, 9, 0, 18, 10),
			R (L"1", 1, L"1006", L"5030", L"С/у (КУИ)", 4.0),
			R (L"1", 1, L"", L"5010", L"Офис без номера", 10.0) };
		rr[0].height = 3.97;
		std::vector<std::wstring> w3;
		const auto pr = BuildPremises (rr, c);
		CHECK (BuildApartments (rr, c, &w3).empty () && w3.empty ());
		CHECK (pr.size () == 2);
		if (pr.size () == 2) {
			CHECK (pr[0].office && pr[0].code == L"1005" && pr[0].label == L"АП");
			NEAR (pr[0].area, 84.2); NEAR (pr[1].area, 98.7);
			CHECK (OfficeArea (pr[0].area, c) == L"84,2 м²");
			CHECK (OfficeHeight (pr[0].height, c) == L"h=3,97м");
			CHECK (pr[0].Key () == L"АП|1|1005");
			NEAR (pr[0].ax2, 9);
		}
	}
	if (a3) {
		CHECK (a3->label == L"С");
		CHECK (a3->typeName == L"1-комнатных (студий)");
		NEAR (a3->total, 24.8); NEAR (a3->reduced, 22.7);		// балкон 3,0 × 0,3 = 0,9
	}

	// ---- ТЭП
	const Tep t = BuildTep (rooms, apts, c);
	CHECK (t.sections.size () == 2 && t.sections[0] == L"1" && t.sections[1] == L"2");
	auto find = [&t] (const wchar_t* num, const wchar_t* name = nullptr) -> const TepRow* {
		for (const auto& r : t.rows)
			if (r.num == num && (name == nullptr || r.name == name)) return &r;
		return nullptr;
	};
	const TepRow* r5 = find (L"5");
	CHECK (r5 && r5->cells[1] == L"51,3" && r5->cells[0] == L"-");
	const TepRow* tech = find (L"", L"в т.ч. площадь тех. помещений:");
	CHECK (tech && tech->cells[1] == L"25,1");
	const TepRow* r6 = find (L"6");
	CHECK (r6 && r6->cells[1] == L"120,3");
	const TepRow* r11 = find (L"11");
	CHECK (r11 && r11->cells[1] == L"6,1");
	const TepRow* r12 = find (L"12"); const TepRow* r13 = find (L"13");
	CHECK (r12 && r12->cells[1] == L"5,0" && r13 && r13->cells[1] == L"2");
	const TepRow* r14 = find (L"14");
	CHECK (r14 && r14->cells[1] == L"42,5");		// 37,0 + 5,5 (МОП); подвал не входит
	const TepRow* r3 = find (L"3");
	CHECK (r3 && r3->cells[1] == L"1 461,6");
	const TepRow* r2 = find (L"2"); const TepRow* r21 = find (L"2.1");
	// секция 2: подвал (-1), 1 этаж — офисы, 2 — квартиры -> этажность 2 (1–2 этажи), этажей 3 (+ подвал)
	// секция 1: 3 и 4 этажи с квартирами -> 2 и 2
	CHECK (r2 && r2->cells[1] == L"2" && r2->cells[0] == L"2");
	CHECK (r21 && r21->cells[1] == L"3" && r21->cells[0] == L"2" && r21->total == L"перем.");
	CHECK (IsUnderground (0, -3.3, L"Этаж 0") && IsUnderground (0, 0, L"Подвал") && IsUnderground (-1, 0, L"x"));
	CHECK (!IsUnderground (0, 0, L"Этаж 1") && !IsUnderground (3, 6.6, L"Этаж 4"));
	CHECK (t.floorsInfo.size () == 2);
	for (const auto& f : t.floorsInfo) std::printf ("  %s\n", U (f).c_str ());
	{	// этаж только с ЛК выше последнего жилого не считается ни в этажность, ни в количество этажей
		std::vector<Room> r2r = rooms;
		r2r.push_back (R (L"2", 11, L"", L"2010", L"Лестничная клетка", 15.7));
		const Tep t3 = BuildTep (r2r, apts, c);
		for (const auto& r : t3.rows) {
			if (r.num == L"2") CHECK (r.cells[1] == L"2");
			if (r.num == L"2.1") CHECK (r.cells[1] == L"3");
		}
	}
	const TepRow* r16 = find (L"16");
	CHECK (r16 && r16->cells[1] == L"3" && r16->total == L"5 (100%)");
	const TepRow* st = find (L"", L"1-комнатных (студий)");
	CHECK (st && st->total == L"2 (40%)");
	CHECK (t.rows.size () > 20);
	const TepRow* r1 = find (L"1");
	CHECK (r1 && r1->cells[1] == L"594,3" && r1->total == L"594,3");
	const TepRow* r17 = find (L"17");
	CHECK (r17 != nullptr);

	{	// значения вручную
		Config m = c;
		m.manual[L"18"][L"1"] = L"22";
		m.manual[L"18"][L"2"] = L"7";
		m.manual[L"2"][L"всего"] = L"15";
		const Tep t2 = BuildTep (rooms, apts, m);
		for (const auto& r : t2.rows) {
			if (r.num == L"18") CHECK (r.cells[0] == L"22" && r.total == L"29");
			if (r.num == L"2") CHECK (r.total == L"15");
		}
	}

	const otd::TableLayout lay = LayoutTep (t, L"Технико-экономические показатели");
	CHECK (!lay.segs.empty () && lay.width > 150);
	if (argc > 1) { std::ofstream f (argv[1], std::ios::binary); f << otd::LayoutSvg (lay); }
	if (argc > 2) {
		std::ofstream f (argv[2], std::ios::binary);
		f << xlsx::BuildXlsx ({ TepSheet (t, L"Технико-экономические показатели"), ApartmentsSheet (rooms, apts, c) });
	}
	for (const auto& r : t.rows) {
		std::string s = U (r.num) + " | " + U (r.name).substr (0, 60) + " | " + U (r.unit) + " | " + U (r.total);
		for (const auto& v : r.cells) s += " | " + U (v);
		std::printf ("  %s\n", s.c_str ());
	}
	std::printf ("%d проверок, ошибок: %d\n", checks, fails);
	return fails == 0 ? 0 : 1;
}
