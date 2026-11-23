// Перевод зон чужих категорий на категории HimeraZone_АСП (без API, тестируется).
// В чужом проекте одна категория может покрывать все помещения квартиры («1 HNF 1 Квартира»),
// поэтому наша категория подбирается по ИМЕНИ зоны, а старая категория подсказывает группу
// (квартира, общедомовое, офис, подвал…). Что не подобралось — правила в файле «Перевод категорий зон.txt».
#ifndef CATMAP_HPP
#define CATMAP_HPP

#include <string>
#include <vector>

namespace cm {

struct TargetCat {
	std::wstring	code, name;			// наша категория (маркер HimeraZone_АСП)
};

struct Rule {
	std::wstring	oldCat;				// пусто — любая старая категория
	std::wstring	zoneName;			// «*» — любое имя
	std::wstring	code;				// пусто — не переводить
};

enum How { None = 0, ByRule = 1, ByName = 2, Ambiguous = 3, SkipByRule = 4 };

struct Result {
	How							how = None;
	std::wstring				code;
	std::vector<std::wstring>	variants;		// при Ambiguous — коды-кандидаты
};

std::wstring			NormName (const std::wstring& s);			// регистр, ё, точки, пробелы, «№…» в конце
int						GroupHint (const std::wstring& oldCatName);	// 1…8 по словам в имени старой категории, 0 — нет
// Имена зон, которые попадают в нашу категорию (по коду и имени категории)
std::vector<std::wstring> NamesOf (const TargetCat& c);
// story — индекс этажа (0 — первый: там тамбур / лифтовой холл — входной группы «ВхГ»)
Result					Match (const std::vector<TargetCat>& targets, const std::vector<Rule>& rules,
							   const std::wstring& oldCat, const std::wstring& zoneName, int story);

std::vector<Rule>		ParseRules (const std::wstring& text);
std::wstring			DefaultRulesText ();
struct Unresolved {
	std::wstring				oldCat, zoneName;
	std::vector<std::wstring>	variants;
	int							count = 0;
};
// Дописать в файл правил строки «старая категория | имя зоны = » для неподобранных (без повторов)
std::wstring			AppendUnresolved (const std::wstring& text, const std::vector<Unresolved>& un, const std::vector<TargetCat>& targets);

}	// namespace cm

#endif
