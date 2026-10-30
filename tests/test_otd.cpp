// Проверки логики ведомости отделки:
//   g++ -std=c++17 -ISrc -o /tmp/t tests/test_otd.cpp Src/FinishCore.cpp Src/FinishReport.cpp Src/XlsxWriter.cpp Src/FinishTable.cpp && /tmp/t [out.xlsx]
#include "FinishCore.hpp"
#include "FinishReport.hpp"
#include "XlsxWriter.hpp"
#include "FinishTable.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>

static int fails = 0, checks = 0;

#define CHECK(cond) do { ++checks; if (!(cond)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b) CHECK (std::fabs ((a) - (b)) < 1e-6)

using namespace otd;

static std::string U (const std::wstring& w) { return ToUtf8 (w); }

int main (int argc, char** argv)
{
	std::vector<std::wstring> warn;
	const Rules r = ParseRules (DefaultRulesText (), &warn);
	for (const auto& w : warn) std::printf ("%s\n", U (w).c_str ());
	CHECK (warn.empty ());
	CHECK (r.columnTitle[1] == L"Монолит. ж/б стены; ж/б пилоны");
	CHECK (r.columnsColumn == 1);
	CHECK (r.basementGroup.empty ());
	CHECK (RulesVersion (DefaultRulesText ()) == kRulesVersion);
	CHECK (RulesVersion (L"[Материалы]\nКБ = 2\n") == 1);
	CHECK (ClassifyWall (L"Штук | Гипс", r) == 1);
	{
		ZoneData mz; mz.catCode = L"2020"; mz.floor = 1;
		CHECK (GroupOf (mz, r) == L"Общедомовые помещения");
	}
	CHECK (r.groupByCode.size () == 9);
	CHECK (r.groupOrder.front () == L"Тех.помещения (подвал)");
	CHECK (r.groupOrder.size () == 8);		// 7 подкатегорий + «Прочие»
	{
		auto G = [&r] (const wchar_t* code, const wchar_t* name = L"") {
			ZoneData q; q.catCode = code; q.name = name; q.floor = 1; return GroupOf (q, r);
		};
		CHECK (G (L"3010") == L"Тех.помещения (подвал)");
		CHECK (G (L"1010") == L"Квартиры");
		CHECK (G (L"2050") == L"Общедомовые помещения");
		CHECK (G (L"5020") == L"Встроенные помещения");
		CHECK (G (L"4001") == L"1 этаж");
		CHECK (G (L"6001") == L"Типовой этаж");
		CHECK (G (L"7001") == L"Чердак");
		CHECK (G (L"8001") == kExcludedGroup);
		CHECK (G (L"9100") == kExcludedGroup);
		CHECK (G (L"", L"6020 Кладовая") == L"Типовой этаж");		// код из названия категории
		ZoneData q; q.catCode = L"8001";
		CHECK (IsExcluded (q, r));
	}
	CHECK (r.groupOrder.back () == L"Прочие помещения");

	// --- коды и столбцы по названиям конструкций со скриншота
	{
		auto c = MaterialCodes (L"Ст_В| Ш 15_КБ 190_Ш 15");
		CHECK (c.size () == 3 && c[0] == L"Ш" && c[1] == L"КБ" && c[2] == L"Ш");
		c = MaterialCodes (L"Ст_П_О | ГКЛв С623_ПС50 | <10м");
		CHECK (c.size () == 2 && c[0] == L"ГКЛв" && c[1] == L"ПС");
		c = MaterialCodes (L"Ст_П| ПГП-80 | Плитка/- | <3,6м");
		CHECK (!c.empty () && c[0] == L"ПГП");
	}
	CHECK (ClassifyWall (L"Ст_В| Ш 15_КБ 190_Ш 15", r) == 2);
	CHECK (ClassifyWall (L"Ст_В| Ш 15_ЖБ 250", r) == 1);
	CHECK (ClassifyWall (L"Ст_В| Ш 10_Ут 150_ЖБ 200", r) == 1);
	CHECK (ClassifyWall (L"Ст_В| Ш 15_Кирп 120_Ш 15", r) == 2);
	CHECK (ClassifyWall (L"Ст_В| Ш 10_У 100_Кл 10_КБ 190", r) == 2);
	CHECK (ClassifyWall (L"Ст_П | ГКЛ 12,5_И 50_ГКЛ 12,5", r) == 3);
	CHECK (ClassifyWall (L"Ст_П_О | ГКЛ 15_Возд 25", r) == 3);
	CHECK (ClassifyWall (L"Ст_П_О | ГКЛв С623_ПС50 | <10м (зашивки)", r) == 3);
	CHECK (ClassifyWall (L"Ст_В| Ш 15", r) == 1);			// отделочная стенка из штукатурки -> монолит
	CHECK (ClassifyWall (L"Штукатурка", r) == 1);			// однослойная стена, строительный материал
	CHECK (ClassifyWall (L"Бетон - конструкционный", r) == 1);
	CHECK (ClassifyWall (L"Ст_Ут_Вш| Ш 10_ЭППС 30", r) == 1);	// Ш найден, ЭППС нет в правилах
	CHECK (ClassifyWall (L"Стекло", r) == 0);
	// названия из проекта начинаются с «|»
	CHECK (ClassifyWall (L"| Ст_В| ЗТ 15_ЖБ 180", r) == 1);
	CHECK (ClassifyWall (L"| Ст_В| Кирп 250_Ш 15 (стены, перегородки)", r) == 2);
	CHECK (ClassifyWall (L"| Ст_В| Ш 15_КБ 190_Ш 15", r) == 2);
	CHECK (ClassifyWall (L"| Ст_Н| Ш 15_Возд 200_УТ 150_Возд 70_Кирп 120 (Альтаир Латте)", r) == 2);
	CHECK (ClassifyWall (L"| Ст_Н| Ш 15_ГБ 400_Возд 20_Кирп 120 (Альтаир Латте)", r) == 2);
	CHECK (ClassifyWall (L"| Ст_П_О | ГКЛ 15_Возд 25", r) == 3);
	CHECK (ClassifyWall (L"Штук | Гипс", r) == 1);
	CHECK (ShortName (L"МОП_ВхГ_Тамбур") == L"Тамбур" && ShortName (L"Комната") == L"Комната");
	CHECK (ClassifyWall (L"Ст_П| ПГП-80 | Плитка/- | <3,6м", r) == 2);		// ПГП — как керамзитобетонные перегородки
	CHECK (ClassifyWall (L"Ст_В| Ш 15_У 50_Кирп 120", r) == 2);			// У — утеплитель, не влияет
	CHECK (ClassifyWall (L"Ст_Ут| У 100", r) == 0);

	// --- отделка
	CHECK (CleanFinish (L"00 Покраска", r) == L"Водно-дисперсионная окраска");
	CHECK (CleanFinish (L"02 Обои", r) == L"Оклейка обоями светлых тонов");
	CHECK (CleanFinish (L"Натяжной потолок", r) == L"Натяжной потолок");
	CHECK (CleanFinish (L"  ", r).empty ());

	// --- этажи
	std::set<int> f;
	CHECK (ParseFloors (L"1", f) && f.size () == 1 && *f.begin () == 1);
	CHECK (ParseFloors (L"2-14; 16", f) && f.size () == 14 && f.count (16) && !f.count (15));
	CHECK (ParseFloors (L"-1", f) && f.size () == 1 && *f.begin () == -1);
	CHECK (ParseFloors (L"-2..1", f) && f.size () == 4);
	CHECK (ParseFloors (L"1, 3", f) && f.size () == 2);
	CHECK (!ParseFloors (L"", f));
	CHECK (!ParseFloors (L"а", f));
	CHECK (FloorsLabel ({ 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 }) == L"2-14 эт.");
	CHECK (FloorsLabel ({ 1, 3, 4 }) == L"1, 3-4 эт.");

	// --- площади
	NEAR (OpeningOverlap (0, 2.1, 0.08, 2.9), 2.02);
	NEAR (OpeningOverlap (0.9, 1.5, 0.08, 2.9), 1.5);
	NEAR (OpeningOverlap (2.5, 1.0, 0.08, 2.9), 0.4);
	NEAR (OpeningOverlap (3.0, 1.0, 0.08, 2.9), 0.0);
	WallPiece wp;
	wp.length = 4; wp.finishHeight = 2.7; wp.openings = 1.5;
	NEAR (wp.Area (), 4 * 2.7 - 1.5);
	CHECK (FormatNum (1080.849, 1) == L"1 080,8");
	CHECK (FormatNum (57.4, 1) == L"57,4");
	CHECK (FormatNum (-3, 0) == L"-3");

	// --- ведомость
	std::vector<ZoneData> zones;
	auto zone = [&] (const wchar_t* name, int floor, const wchar_t* code, const wchar_t* ceil, const wchar_t* wall, double area,
					 std::vector<std::pair<int, double>> walls) {
		ZoneData z;
		z.name = name; z.floor = floor; z.catCode = code; z.ceilFinishRaw = ceil; z.wallFinishRaw = wall; z.ceilArea = area;
		z.number = std::to_wstring (zones.size () + 1);
		for (auto& w : walls) {
			WallPiece p;
			p.column = w.first; p.length = w.second; p.finishHeight = 2.5; p.elemHeight = 2.8;
			p.construction = L"тест"; p.elemId = L"W" + std::to_wstring (w.first);
			z.pieces.push_back (p);
		}
		zones.push_back (z);
	};
	zone (L"Комната", 2, L"1010", L"Натяжной потолок", L"02 Обои", 15.0, { { 1, 3.0 }, { 2, 4.0 }, { 3, 2.0 } });
	zones.back ().roomName = L"Жилая комната";
	zone (L"Кабинет", 3, L"1011", L"Натяжной потолок", L"02 Обои", 12.0, { { 1, 2.0 }, { 3, 4.0 } });
	zones.back ().roomName = L"Кабинет";
	zone (L"Кухня", 2, L"1020", L"Натяжной потолок", L"02 Обои", 10.0, { { 2, 3.0 } });
	zones.back ().roomName = L"Кухня-столовая";
	zone (L"Комната", 3, L"1010", L"Натяжной потолок", L"02 Обои", 14.0, { { 1, 1.0 } });
	zones.back ().roomName = L"жилая комната";		// повтор имени — не дублируется
	zone (L"МОП_Лифтовой холл", 2, L"2050", L"00 Покраска", L"00 Покраска", 6.0, { { 1, 2.0 } });
	zones.back ().roomName = L"Лифтовый холл";
	zone (L"МОП_ВхГ_Лифтовой холл", 15, L"2013", L"Подвесной потолок \"Грильято\"", L"00 Покраска", 5.0, { { 1, 2.0 } });
	zones.back ().roomName = L"Лифтовый холл";
	zone (L"ТП_Электрощитовая", -1, L"3050", L"00 Покраска", L"00 Покраска", 8.0, { { 2, 4.0 }, { 0, 1.0 } });
	zones.back ().roomName = L"Электрощитовая";
	zones.back ().guid = L"G-electro";
	zone (L"ТП_Санузел", -1, L"3051", L"00 Покраска", L"00 Покраска", 2.0, {});
	zones.back ().roomName = L"С/У";
	zone (L"ВПП_Административное помещение", 1, L"4099", L"00 Покраска", L"00 Покраска", 20.0, {});
	zones.back ().roomName = L"Административное\\nпомещение";
	zone (L"Венткамера на кровле", 16, L"8010", L"00 Покраска", L"00 Покраска", 30.0, { { 1, 5.0 } });

	CHECK (CleanZoneName (L"Административное\\nпомещение") == L"Административное помещение");
	CHECK (CleanZoneName (L" Холл\n 2 ") == L"Холл 2");
	CHECK (JoinNames ({ L"Электрощитовая", L"Лестничная клетка", L"С/У", L"КУИ" }) == L"Электрощитовая, лестничная клетка, С/У, КУИ");

	const Sheet sh = BuildSheet (zones, r, L"1");
	CHECK (sh.title == L"1 секция. Ведомость отделки помещений жилого дома");
	CHECK (sh.rows.size () == 5);
	for (const auto& row : sh.rows)
		std::printf ("  %-24s | %-48s | %-34s %6.2f | %6.2f %6.2f %6.2f [%5.2f]\n", U (row.group).c_str (), U (row.title).c_str (),
					 U (row.ceilFinish).c_str (), row.ceilArea, row.wallArea[1], row.wallArea[2], row.wallArea[3], row.wallArea[0]);
	if (sh.rows.size () == 5) {
		CHECK (sh.rows[0].group == L"Тех.помещения (подвал)");
		CHECK (sh.rows[0].title == L"Электрощитовая, С/У");
		NEAR (sh.rows[0].wallArea[0], 2.5);
		CHECK (sh.rows[0].floors.count (-1) == 1);
		CHECK (sh.rows[1].group == L"Квартиры");
		CHECK (sh.rows[1].title == L"Жилая комната, кабинет, кухня-столовая");
		NEAR (sh.rows[1].ceilArea, 51.0);
		NEAR (sh.rows[1].wallArea[1], 6 * 2.5);
		NEAR (sh.rows[1].wallArea[3], 6 * 2.5);
		CHECK (sh.rows[1].wallFinish == L"Оклейка обоями светлых тонов");
		CHECK (sh.rows[2].title == L"Лифтовый холл (2 эт.)");
		CHECK (sh.rows[3].title == L"Лифтовый холл (15 эт.)");
		CHECK (sh.rows[4].group == L"1 этаж" && sh.rows[4].title == L"Административное помещение");
		NEAR (sh.unknownArea, 2.5);
	}

	// --- правила пользователя: свои коды, разделы, ошибки
	{
		std::vector<std::wstring> w2;
		const Rules r2 = ParseRules (L"[Материалы]\nУ = 1\nКБ = 2\nплохо\n[Неизвестно]\nа = б\n[Столбцы]\n5 = x\n", &w2);
		CHECK (w2.size () == 4);
		CHECK (ClassifyWall (L"Ст_В| Ш 10_У 100_Кл 10_КБ 190", r2) == 1);
		CHECK (r2.columnTitle[2] == L"Стены, столбец 2");
	}

	// --- книга Excel
	std::vector<xlsx::Sheet> book = { MakeVedomostSheet (sh), MakeDetailsSheet (zones, r) };
	const std::string bytes = xlsx::BuildXlsx (book);
	CHECK (bytes.size () > 1000);
	CHECK (bytes.compare (0, 4, "PK\x03\x04") == 0);
	CHECK (xlsx::Crc32 ("123456789") == 0xCBF43926UL);
	CHECK (xlsx::ColName (0) == "A" && xlsx::ColName (25) == "Z" && xlsx::ColName (26) == "AA");
	CHECK (FromUtf8 (ToUtf8 (L"Ёлка ж/б м²")) == L"Ёлка ж/б м²");
	if (argc > 1) {
		std::ofstream out (argv[1], std::ios::binary);
		out.write (bytes.data (), (std::streamsize) bytes.size ());
	}

	// --- таблица для рабочего листа
	{
		auto ln = WrapText (L"Водомерный узел, электрощитовая, лестничная клетка, приточная вент.камера", 37.6, 2.5, 0.62);
		CHECK (ln.size () >= 3);
		for (const auto& l : ln) CHECK (l.size () <= 24);
		CHECK (WrapText (L"", 30, 2.5, 0.62).size () == 1);
		const TableLayout t = LayoutSheet (sh);
		CHECK (t.width > 200 && t.height > 40);
		CHECK (t.segs.size () > 15);
		bool hasTitle = false, hasVert = false;
		for (const auto& tx : t.texts) {
			if (tx.text == sh.title) hasTitle = true;
			if (tx.vertical) hasVert = true;
		}
		CHECK (hasTitle && hasVert);

		const std::vector<CheckRow> cr = CheckRows (zones, r);
		CHECK (cr.size () == zones.size ());
		CHECK (cr.front ().floor == -1 && cr.back ().floor == 16 && cr.back ().excluded);
		TableLayout both = t;
		const double w0 = both.width;
		const TableLayout ct = LayoutCheck (zones, r, L"Проверка: зоны по подкатегориям");
		CHECK (ct.width > 100 && ct.segs.size () > 20);
		AppendLayout (both, ct, 20);
		CHECK (std::fabs (both.width - (w0 + 20 + ct.width)) < 1e-9);
		bool hasSum = false, hasOut = false;
		for (const auto& tx : ct.texts) {
			if (tx.text.find (L"Всего в ведомости") == 0) hasSum = true;
			if (tx.text.find (L"не входит: 1") != std::wstring::npos) hasOut = true;
		}
		CHECK (hasSum && hasOut);
		size_t tagged = 0;
		for (const auto& tx : ct.texts) if (tx.tag == L"G-electro") ++tagged;
		CHECK (tagged >= 6);		// все тексты строки зоны помечены её GUID
		if (argc > 3) {
			std::ofstream svg (argv[3], std::ios::binary);
			const std::string s = LayoutSvg (both);
			svg.write (s.data (), (std::streamsize) s.size ());
		}
		if (argc > 2) {
			std::ofstream svg (argv[2], std::ios::binary);
			const std::string s = LayoutSvg (t);
			svg.write (s.data (), (std::streamsize) s.size ());
		}
	}

	std::printf ("%d проверок, ошибок: %d\n", checks, fails);
	return fails == 0 ? 0 : 1;
}
