// Проверки логики ведомости отделки:
//   g++ -std=c++17 -ISrc -o /tmp/t tests/test_otd.cpp Src/FinishCore.cpp Src/FinishReport.cpp Src/XlsxWriter.cpp && /tmp/t [out.xlsx]
#include "FinishCore.hpp"
#include "FinishReport.hpp"
#include "XlsxWriter.hpp"

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
	CHECK (r.basementGroup == L"Техпомещения (подвал)");
	CHECK (r.groupByType.at (1) == L"Квартиры");
	CHECK (r.groupOrder.front () == L"Техпомещения (подвал)");
	CHECK (r.groupOrder.back () == L"Прочие помещения");

	// --- коды и столбцы по названиям конструкций со скриншота
	{
		auto c = MaterialCodes (L"Ст_В| Ш 15_КБ 190_Ш 15");
		CHECK (c.size () == 3 && c[0] == L"Ш" && c[1] == L"КБ" && c[2] == L"Ш");
		c = MaterialCodes (L"Ст_П_О | ГКЛв С623_ПС50 | <10м");
		CHECK (c.size () == 2 && c[0] == L"ГКЛв" && c[1] == L"ПС");
		c = MaterialCodes (L"Ст_П| ПГП-80 | Плитка/- | <3,6м");
		CHECK (c.size () == 1 && c[0] == L"ПГП");
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
	auto zone = [&] (const wchar_t* name, int floor, int type, const wchar_t* ceil, const wchar_t* wall, double area,
					 std::vector<std::pair<int, double>> walls) {
		ZoneData z;
		z.name = name; z.floor = floor; z.type = type; z.ceilFinishRaw = ceil; z.wallFinishRaw = wall; z.ceilArea = area;
		z.number = std::to_wstring (zones.size () + 1);
		for (auto& w : walls) {
			WallPiece p;
			p.column = w.first; p.length = w.second; p.finishHeight = 2.5; p.elemHeight = 2.8;
			p.construction = L"тест"; p.elemId = L"W" + std::to_wstring (w.first);
			z.pieces.push_back (p);
		}
		zones.push_back (z);
	};
	zone (L"Комната", 2, 1, L"Натяжной потолок", L"02 Обои", 15.0, { { 1, 3.0 }, { 2, 4.0 }, { 3, 2.0 } });
	zone (L"Спальня", 3, 1, L"Натяжной потолок", L"02 Обои", 12.0, { { 1, 2.0 }, { 3, 4.0 } });
	zone (L"Кухня", 2, 1, L"Натяжной потолок", L"02 Обои", 10.0, { { 2, 3.0 } });
	zone (L"Лифтовой холл", 2, 4, L"00 Покраска", L"00 Покраска", 6.0, { { 1, 2.0 } });
	zone (L"Лифтовой холл", 15, 4, L"Подвесной потолок \"Грильято\"", L"00 Покраска", 5.0, { { 1, 2.0 } });
	zone (L"Электрощитовая", -1, 4, L"00 Покраска", L"00 Покраска", 8.0, { { 2, 4.0 }, { 0, 1.0 } });
	zone (L"Мастерская", 2, 5, L"00 Покраска", L"00 Покраска", 20.0, {});

	const Sheet sh = BuildSheet (zones, r, L"1");
	CHECK (sh.title == L"1 секция. Ведомость отделки помещений жилого дома");
	CHECK (sh.rows.size () == 6);
	for (const auto& row : sh.rows)
		std::printf ("  %-24s | %-48s | %-34s %6.2f | %6.2f %6.2f %6.2f [%5.2f]\n", U (row.group).c_str (), U (row.title).c_str (),
					 U (row.ceilFinish).c_str (), row.ceilArea, row.wallArea[1], row.wallArea[2], row.wallArea[3], row.wallArea[0]);
	if (sh.rows.size () == 6) {
		CHECK (sh.rows[0].group == L"Техпомещения (подвал)");
		CHECK (sh.rows[0].title.find (L"Водомерный узел") == 0);
		NEAR (sh.rows[0].wallArea[0], 2.5);
		CHECK (sh.rows[1].group == L"Встроенные помещения" && sh.rows[1].title == L"Мастерская");
		CHECK (sh.rows[2].title == L"Жилая комната");
		NEAR (sh.rows[2].ceilArea, 27.0);
		NEAR (sh.rows[2].wallArea[1], 5 * 2.5);
		NEAR (sh.rows[2].wallArea[3], 6 * 2.5);
		CHECK (sh.rows[2].wallFinish == L"Оклейка обоями светлых тонов");
		CHECK (sh.rows[3].title == L"Кухня-столовая");
		CHECK (sh.rows[4].title == L"Лифтовой холл (тамбур-шлюз) (2 эт.)");
		CHECK (sh.rows[5].title == L"Лифтовой холл (тамбур-шлюз) (15 эт.)");
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

	std::printf ("%d проверок, ошибок: %d\n", checks, fails);
	return fails == 0 ? 0 : 1;
}
