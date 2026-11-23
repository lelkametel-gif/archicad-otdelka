// Перевод зон чужих категорий на категории HimeraZone_АСП
#include "CatMap.hpp"
#include "FinishCore.hpp"

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <sstream>

namespace cm {

namespace {

// Имена зон по коду категории — как в мастер-скрипте HimeraZone_АСП (docs/Категории зон.md)
const std::map<std::wstring, std::vector<std::wstring>>& BuiltinNames ()
{
	static const std::map<std::wstring, std::vector<std::wstring>> m = {
		{ L"1010", { L"Жилая комната", L"Комната", L"Гостиная", L"Спальня" } },
		{ L"1011", { L"Кабинет" } },
		{ L"1012", { L"Холл" } },
		{ L"1020", { L"Кухня" } },
		{ L"1021", { L"Зона кухни", L"Кухня-ниша" } },
		{ L"1022", { L"Кухня-столовая" } },
		{ L"1030", { L"Прихожая" } },
		{ L"1031", { L"Тамбур" } },
		{ L"1040", { L"Коридор" } },
		{ L"1050", { L"Совмещенный санузел", L"Совм. с/у" } },
		{ L"1051", { L"Санузел", L"С/у", L"Туалет" } },
		{ L"1052", { L"Ванная" } },
		{ L"1053", { L"Душевая" } },
		{ L"1060", { L"Балкон" } },
		{ L"1061", { L"Терраса" } },
		{ L"1070", { L"Лоджия" } },
		{ L"1080", { L"Гардеробная", L"Зона гардероба" } },
		{ L"1081", { L"Кладовая" } },
		{ L"1082", { L"Постирочная" } },
		{ L"2010", { L"Колясочная" } },
		{ L"2011", { L"Вестибюль" } },
		{ L"2012", { L"Тамбур" } },
		{ L"2013", { L"Лифтовой холл" } },
		{ L"2020", { L"Коридор" } },
		{ L"2030", { L"Лестничная клетка задымляемая" } },
		{ L"2031", { L"Лестничная клетка незадымляемая" } },
		{ L"2040", { L"Пожаробезопасная зона" } },
		{ L"2050", { L"Лифтовой холл", L"Лифтовый холл" } },
		{ L"2060", { L"Тамбур" } },
		{ L"2061", { L"Тамбур-шлюз" } },
		{ L"2070", { L"Переходной балкон" } },
		{ L"2110", { L"Помещение консьержа" } },
		{ L"2111", { L"Диспетчерская" } },
		{ L"2112", { L"Помещение охраны" } },
		{ L"2113", { L"Рабочее помещение" } },
		{ L"2114", { L"Подсобное помещение" } },
		{ L"2120", { L"ПУИ", L"ПУИ с с/у" } },
		{ L"2130", { L"Мусоросборная камера" } },
		{ L"2140", { L"УК" } },
		{ L"2150", { L"С/у" } },
		{ L"3010", { L"Техническое подполье" } },
		{ L"3020", { L"Тамбур" } },
		{ L"3030", { L"Коридор" } },
		{ L"3040", { L"Машинное помещение лифтов" } },
		{ L"3050", { L"Пожаробезопасная зона" } },
		{ L"3060", { L"Кладовая" } },
		{ L"3061", { L"Блок кладовых" } },
		{ L"3070", { L"Подвальное помещение" } },
		{ L"3110", { L"Венткамера" } },
		{ L"3111", { L"Венткамера приточная" } },
		{ L"3112", { L"Венткамера вытяжная" } },
		{ L"3120", { L"Водомерный узел" } },
		{ L"3121", { L"Насосная" } },
		{ L"3122", { L"АУПТ-насосная пожаротушения" } },
		{ L"3123", { L"Помещение водоочистки" } },
		{ L"3130", { L"ГРЩ" } },
		{ L"3140", { L"ИТП" } },
		{ L"3150", { L"Электрощитовая" } },
		{ L"3160", { L"Пространство для прокладки коммуникаций" } },
		{ L"4010", { L"Индивидуальная кладовая", L"Кладовая" } },
		{ L"5010", { L"Административное помещение", L"Торговый зал", L"Торгово-выставочный зал", L"Подсобное помещение", L"Обеденный зал" } },
		{ L"5020", { L"Кладовая" } },
		{ L"5030", { L"С/у" } },
		{ L"5040", { L"ПУИ с с/у" } },
		{ L"5050", { L"ПУИ" } },
		{ L"6010", { L"Нежилое помещение" } },
		{ L"7010", { L"Технический чердак" } },
		{ L"7020", { L"Машинное помещение лифтов" } },
		{ L"7030", { L"Котельная" } },
		{ L"7040", { L"Венткамера" } },
		{ L"8010", { L"Строительный объем" } },
		{ L"8020", { L"Площадь застройки" } },
		{ L"8030", { L"Площадь жилого здания" } },
		{ L"8040", { L"Площадь лестницы" } },
	};
	return m;
}

// Другие написания -> наши имена (нормализованные с обеих сторон при сравнении)
const std::map<std::wstring, std::vector<std::wstring>>& Synonyms ()
{
	static const std::map<std::wstring, std::vector<std::wstring>> m = {
		{ L"совм с/у",				{ L"Совмещенный санузел" } },
		{ L"совмещ с/у",			{ L"Совмещенный санузел" } },
		{ L"совмещенный с/у",		{ L"Совмещенный санузел" } },
		{ L"с/у совмещенный",		{ L"Совмещенный санузел" } },
		{ L"ванная комната",		{ L"Ванная" } },
		{ L"уборная",				{ L"Санузел" } },
		{ L"жилая",					{ L"Жилая комната" } },
		{ L"кухня ниша",			{ L"Кухня-ниша" } },
		{ L"гардероб",				{ L"Гардеробная" } },
		{ L"куи",					{ L"ПУИ" } },
		{ L"куи с/у",				{ L"ПУИ с с/у" } },
		{ L"куи с с/у",				{ L"ПУИ с с/у" } },
		{ L"пуи с/у",				{ L"ПУИ с с/у" } },
		{ L"офисное помещение",		{ L"Административное помещение" } },
		{ L"офис",					{ L"Административное помещение" } },
		{ L"помещение офиса",		{ L"Административное помещение" } },
		{ L"лифт холл",				{ L"Лифтовой холл" } },
		{ L"тех подполье",			{ L"Техническое подполье" } },
		{ L"техподполье",			{ L"Техническое подполье" } },
		{ L"мусорокамера",			{ L"Мусоросборная камера" } },
		{ L"мусоросборная",			{ L"Мусоросборная камера" } },
		{ L"щитовая",				{ L"Электрощитовая" } },
		{ L"л/кл",					{ L"Лестничная клетка задымляемая", L"Лестничная клетка незадымляемая" } },
		{ L"лк",					{ L"Лестничная клетка задымляемая", L"Лестничная клетка незадымляемая" } },
		{ L"лестничная клетка",		{ L"Лестничная клетка задымляемая", L"Лестничная клетка незадымляемая" } },
		{ L"лестница",				{ L"Лестничная клетка задымляемая", L"Лестничная клетка незадымляемая" } },
	};
	return m;
}

bool Has (const std::wstring& s, const wchar_t* part) { return s.find (part) != std::wstring::npos; }

bool IsHeaderCode (const std::wstring& code)
{
	if (code.size () < 4) return true;										// «1», «2» — заголовки групп
	return code.size () == 4 && code[2] == L'0' && code[3] == L'0';			// 2100, 3100
}

std::wstring LowCat (const std::wstring& s)
{
	std::wstring r = otd::Lower (otd::Trim (s));
	for (auto& c : r) if (c == L'ё') c = L'е';
	return r;
}

}	// namespace

std::wstring NormName (const std::wstring& s)
{
	std::wstring r = LowCat (s);
	const size_t no = r.find (L'№');
	if (no != std::wstring::npos) r.erase (no);
	for (auto& c : r)
		if (c == L'\n' || c == L'\r' || c == L'\t' || c == L'(' || c == L')' || c == L',' || c == L'_') c = L' ';
	r.erase (std::remove (r.begin (), r.end (), L'.'), r.end ());
	// пробелы: схлопнуть, убрать вокруг «-» и «/»
	std::wstring o;
	for (wchar_t c : r) {
		if (c == L' ' && (o.empty () || o.back () == L' ' || o.back () == L'-' || o.back () == L'/')) continue;
		if ((c == L'-' || c == L'/') && !o.empty () && o.back () == L' ') o.pop_back ();
		o += c;
	}
	while (!o.empty () && o.back () == L' ') o.pop_back ();
	// номер в конце («Офисное помещение 2», «Кладовая 1005»)
	size_t e = o.size ();
	while (e > 0 && iswdigit (o[e - 1])) --e;
	if (e < o.size () && e > 0 && o[e - 1] == L' ') o.erase (e - 1);
	return o;
}

int GroupHint (const std::wstring& oldCatName)
{
	const std::wstring s = LowCat (oldCatName);
	if (Has (s, L"общедом") || Has (s, L"моп") || Has (s, L"общего польз")) return 2;
	if (Has (s, L"офис") || Has (s, L"встроен") || Has (s, L"коммерч") || Has (s, L"аренд")) return 5;
	if (Has (s, L"подвал") || Has (s, L"подпол") || Has (s, L"техн")) return 3;
	if (Has (s, L"чердак")) return 7;
	if (Has (s, L"кладов")) return 4;
	if (Has (s, L"квартир") || Has (s, L"продо") || Has (s, L"прода") || Has (s, L"жил") ||
		Has (s, L"лодж") || Has (s, L"балкон")) return 1;
	if (Has (s, L"тэп") || Has (s, L"застрой") || Has (s, L"объем")) return 8;
	return 0;
}

std::vector<std::wstring> NamesOf (const TargetCat& c)
{
	std::vector<std::wstring> res;
	const std::wstring code = otd::Trim (c.code);
	if (IsHeaderCode (code)) return res;
	auto add = [&res] (const std::wstring& n) {
		const std::wstring k = NormName (n);
		if (!k.empty () && std::find (res.begin (), res.end (), k) == res.end ()) res.push_back (k);
	};
	const auto it = BuiltinNames ().find (code);
	if (it != BuiltinNames ().end ())
		for (const auto& n : it->second) add (n);
	// имя категории без приставки «МОП_ВхГ_», «ТП_»…; перечисление через запятую
	std::wstring name = otd::Trim (c.name);
	const size_t us = name.rfind (L'_');
	if (us != std::wstring::npos) name = name.substr (us + 1);
	if (name.find (L"---") != std::wstring::npos) return res;
	std::wstringstream in (name);
	std::wstring part;
	while (std::getline (in, part, L',')) add (part);
	return res;
}

static bool IsSkipCode (const std::wstring& code)
{
	const std::wstring c = LowCat (code);
	return c == L"-" || c == L"нет" || c == L"не переводить";
}

Result Match (const std::vector<TargetCat>& targets, const std::vector<Rule>& rules,
			  const std::wstring& oldCat, const std::wstring& zoneName, int story)
{
	Result res;
	const std::wstring n = NormName (zoneName);
	const std::wstring oc = LowCat (oldCat);
	auto hasTarget = [&targets] (const std::wstring& code) {
		for (const auto& t : targets) if (otd::Trim (t.code) == code) return true;
		return false;
	};
	// 1. правила файла: точнее — главнее (старая категория + имя > категория + «*» > имя > «*»)
	int best = -1;
	const Rule* br = nullptr;
	for (const Rule& r : rules) {
		if (otd::Trim (r.code).empty ()) continue;
		if (!r.oldCat.empty () && LowCat (r.oldCat) != oc) continue;
		const bool any = otd::Trim (r.zoneName) == L"*";
		if (!any && NormName (r.zoneName) != n) continue;
		const int score = (r.oldCat.empty () ? 0 : 2) + (any ? 0 : 1);
		if (score > best) { best = score; br = &r; }
	}
	if (br != nullptr) {
		const std::wstring code = otd::Trim (br->code);
		if (IsSkipCode (code)) { res.how = SkipByRule; return res; }
		res.code = code;
		res.how = hasTarget (code) ? ByRule : None;
		if (res.how == None) res.variants.push_back (code);		// код из правила, которого нет среди наших категорий
		return res;
	}
	if (n.empty ()) return res;
	// 2. по имени зоны
	std::set<std::wstring> keys = { n };
	const auto sy = Synonyms ().find (n);
	if (sy != Synonyms ().end ())
		for (const auto& s : sy->second) keys.insert (NormName (s));
	std::vector<const TargetCat*> cands;
	for (const auto& t : targets) {
		for (const auto& nm : NamesOf (t))
			if (keys.count (nm)) { cands.push_back (&t); break; }
	}
	if (cands.empty ()) return res;
	const int g = GroupHint (oldCat);
	if (g > 0 && cands.size () > 1) {
		std::vector<const TargetCat*> in;
		for (const auto* t : cands) if (!t->code.empty () && t->code[0] == (wchar_t) (L'0' + g)) in.push_back (t);
		if (!in.empty ()) cands = in;
	}
	bool oneGroup = true;
	for (const auto* t : cands) if (t->code.empty () || t->code[0] != cands[0]->code[0]) oneGroup = false;
	if (cands.size () > 1 && oneGroup) {
		// тамбур / лифтовой холл: входной группы («ВхГ») — на первом этаже, остальные — выше
		std::vector<const TargetCat*> vh, other;
		for (const auto* t : cands) (Has (LowCat (t->name), L"вхг") || Has (LowCat (t->name), L"вх_") ? vh : other).push_back (t);
		if (!vh.empty () && !other.empty ()) cands = story == 0 ? vh : other;
	}
	if (cands.size () == 1) {
		res.how = ByName;
		res.code = otd::Trim (cands[0]->code);
		return res;
	}
	res.how = Ambiguous;
	for (const auto* t : cands) res.variants.push_back (otd::Trim (t->code));
	return res;
}

std::vector<Rule> ParseRules (const std::wstring& text)
{
	std::vector<Rule> res;
	std::wstringstream in (text);
	std::wstring line;
	while (std::getline (in, line)) {
		if (!line.empty () && line.back () == L'\r') line.pop_back ();
		const std::wstring t = otd::Trim (line);
		if (t.empty () || t[0] == L'#' || t[0] == L'[' || t[0] == 0xFEFF) continue;
		const size_t eq = t.rfind (L'=');
		if (eq == std::wstring::npos) continue;
		std::wstring left = t.substr (0, eq), right = t.substr (eq + 1);
		const size_t hash = right.find (L'#');
		if (hash != std::wstring::npos) right.erase (hash);
		Rule r;
		r.code = otd::Trim (right);
		const size_t bar = left.find (L'|');
		if (bar != std::wstring::npos) {
			r.oldCat = otd::Trim (left.substr (0, bar));
			r.zoneName = otd::Trim (left.substr (bar + 1));
		} else
			r.zoneName = otd::Trim (left);
		if (r.zoneName.empty ()) continue;
		res.push_back (r);
	}
	return res;
}

std::wstring DefaultRulesText ()
{
	return
		L"# Перевод зон на категории HimeraZone_АСП (меню «Перевести зоны на категории HimeraZone_АСП»)\n"
		L"#\n"
		L"# Надстройка подбирает категорию по ИМЕНИ зоны; старая категория подсказывает группу\n"
		L"# (квартира, общедомовые, офисы, подвал). Здесь — правила для того, что не подобралось или подобралось не так.\n"
		L"#\n"
		L"# Строка правила:   имя зоны = код\n"
		L"#            или:   старая категория | имя зоны = код\n"
		L"# Имя «*» — все зоны этой старой категории. Код «-» — такие зоны не переводить.\n"
		L"# Пустой код (ничего после «=») — правило не действует.\n"
		L"# Имя сравнивается без регистра, точек и номера в конце («Кладовая №12» = «Кладовая»).\n"
		L"# Правило главнее подбора по имени; правило со старой категорией главнее правила без неё.\n"
		L"#\n"
		L"# Примеры:\n"
		L"#   Л/Кл = 2031\n"
		L"#   2 HNF 2 Помещения под офисы | Коридор = 5010\n"
		L"#   ГОСТ Полы | * = -\n"
		L"\n";
}

std::wstring AppendUnresolved (const std::wstring& text, const std::vector<Unresolved>& un, const std::vector<TargetCat>& targets)
{
	const std::vector<Rule> rules = ParseRules (text);
	std::set<std::wstring> present;
	for (const auto& r : rules) present.insert (LowCat (r.oldCat) + L"|" + NormName (r.zoneName));
	// уже вписанные пустые строки тоже не повторять
	{
		std::wstringstream in (text);
		std::wstring line;
		while (std::getline (in, line)) {
			const std::wstring t = otd::Trim (line);
			if (t.empty () || t[0] == L'#') continue;
			const size_t eq = t.rfind (L'=');
			if (eq == std::wstring::npos) continue;
			const std::wstring left = t.substr (0, eq);
			const size_t bar = left.find (L'|');
			if (bar != std::wstring::npos)
				present.insert (LowCat (otd::Trim (left.substr (0, bar))) + L"|" + NormName (otd::Trim (left.substr (bar + 1))));
			else
				present.insert (L"|" + NormName (otd::Trim (left)));
		}
	}
	std::map<std::wstring, std::wstring> nameOf;
	for (const auto& t : targets) nameOf[otd::Trim (t.code)] = otd::Trim (t.name);
	std::wstring add;
	for (const auto& u : un) {
		const std::wstring key = LowCat (u.oldCat) + L"|" + NormName (u.zoneName);
		if (present.count (key) || present.count (L"|" + NormName (u.zoneName))) continue;
		present.insert (key);
		std::wstring line = otd::Trim (u.oldCat) + L" | " + otd::Trim (u.zoneName) + L" = ";
		std::wstring note = L"зон: " + std::to_wstring (u.count);
		if (!u.variants.empty ()) {
			note += L"; варианты: ";
			for (size_t i = 0; i < u.variants.size (); ++i) {
				if (i > 0) note += L", ";
				note += u.variants[i];
				const auto it = nameOf.find (u.variants[i]);
				if (it != nameOf.end ()) note += L" " + it->second;
			}
		}
		add += line + L"   # " + note + L"\n";
	}
	if (add.empty ()) return text;
	std::wstring res = text;
	if (!res.empty () && res.back () != L'\n') res += L"\n";
	res += L"\n# --- не подобрано: впишите код после «=» (или «-», чтобы не переводить) ---\n" + add;
	return res;
}

}	// namespace cm
