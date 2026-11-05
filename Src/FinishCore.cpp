// Ведомость отделки помещений — логика без API Archicad
#include "FinishCore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace otd {

// ============================================================================= строки

std::wstring Trim (const std::wstring& s)
{
	size_t b = 0, e = s.size ();
	while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n' || s[b] == 0xFEFF || s[b] == 0xA0)) ++b;
	while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n' || s[e - 1] == 0xA0)) --e;
	return s.substr (b, e - b);
}


std::wstring Lower (const std::wstring& s)
{
	std::wstring r (s);
	for (wchar_t& c : r) {
		if (c >= L'A' && c <= L'Z') c = (wchar_t) (c + 32);
		else if (c >= 0x0410 && c <= 0x042F) c = (wchar_t) (c + 32);		// А..Я
		else if (c == 0x0401) c = 0x0435;									// Ё -> е
		else if (c == 0x0451) c = 0x0435;									// ё -> е
	}
	return r;
}


bool SameText (const std::wstring& a, const std::wstring& b)
{
	return Lower (Trim (a)) == Lower (Trim (b));
}


std::string ToUtf8 (const std::wstring& s)
{
	std::string r;
	for (size_t i = 0; i < s.size (); ++i) {
		unsigned long c = (unsigned long) s[i];
		if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size ()) {		// суррогатная пара (Windows)
			const unsigned long c2 = (unsigned long) s[i + 1];
			if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
				c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
				++i;
			}
		}
		if (c < 0x80) r += (char) c;
		else if (c < 0x800) { r += (char) (0xC0 | (c >> 6)); r += (char) (0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { r += (char) (0xE0 | (c >> 12)); r += (char) (0x80 | ((c >> 6) & 0x3F)); r += (char) (0x80 | (c & 0x3F)); }
		else { r += (char) (0xF0 | (c >> 18)); r += (char) (0x80 | ((c >> 12) & 0x3F)); r += (char) (0x80 | ((c >> 6) & 0x3F)); r += (char) (0x80 | (c & 0x3F)); }
	}
	return r;
}


std::wstring FromUtf8 (const std::string& s)
{
	std::wstring r;
	size_t i = 0;
	if (s.size () >= 3 && (unsigned char) s[0] == 0xEF && (unsigned char) s[1] == 0xBB && (unsigned char) s[2] == 0xBF)
		i = 3;
	while (i < s.size ()) {
		const unsigned char c = (unsigned char) s[i];
		unsigned long cp = 0;
		int n = 0;
		if (c < 0x80) { cp = c; n = 0; }
		else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
		else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
		else { ++i; continue; }
		++i;
		for (int k = 0; k < n && i < s.size (); ++k, ++i)
			cp = (cp << 6) | ((unsigned char) s[i] & 0x3F);
		if (cp >= 0x10000 && sizeof (wchar_t) == 2) {
			cp -= 0x10000;
			r += (wchar_t) (0xD800 + (cp >> 10));
			r += (wchar_t) (0xDC00 + (cp & 0x3FF));
		} else {
			r += (wchar_t) cp;
		}
	}
	return r;
}


std::wstring FormatNum (double v, int dec)
{
	char buf[64];
	snprintf (buf, sizeof (buf), "%.*f", dec, v);
	std::string s (buf);
	std::string sign;
	if (!s.empty () && s[0] == '-') { sign = "-"; s = s.substr (1); }
	const size_t dot = s.find ('.');
	std::string ip = dot == std::string::npos ? s : s.substr (0, dot);
	std::string fp = dot == std::string::npos ? "" : s.substr (dot + 1);
	std::string grouped;
	for (size_t i = 0; i < ip.size (); ++i) {
		if (i > 0 && (ip.size () - i) % 3 == 0) grouped += ' ';
		grouped += ip[i];
	}
	std::wstring r = FromUtf8 (sign + grouped);
	if (!fp.empty ()) r += L"," + FromUtf8 (fp);
	return r;
}

// ============================================================================= правила

int RulesVersion (const std::wstring& text)
{
	const std::wstring key = L"Версия правил:";
	const size_t p = text.find (key);
	if (p == std::wstring::npos) return 1;
	size_t i = p + key.size ();
	while (i < text.size () && text[i] == L' ') ++i;
	int v = 0;
	while (i < text.size () && text[i] >= L'0' && text[i] <= L'9') v = v * 10 + (text[i++] - L'0');
	return v > 0 ? v : 1;
}


std::wstring DefaultRulesText ()
{
	return
		L"# Правила ведомости отделки помещений (надстройка «Ведомость отделки», Archicad 27).\n"
		L"# Строки, начинающиеся с #, — комментарии. Файл можно править в Блокноте;\n"
		L"# изменения применяются при следующем расчёте. Чтобы вернуть правила по умолчанию — удалите файл.\n"
		L"# Версия правил: 8 (более старый файл заменяется новым, старый сохраняется рядом с пометкой «старые»)\n"
		L"\n"
		L"[Заголовок]\n"
		L"# {секция} заменяется номером секции из окна расчёта\n"
		L"{секция} секция. Ведомость отделки помещений жилого дома\n"
		L"\n"
		L"[Столбцы]\n"
		L"1 = Монолит. ж/б стены; ж/б пилоны\n"
		L"2 = Керамзитобет. перегородки (t=90 мм), керамзитобет. стены (t=190 мм), вентблоки, кирп. шахты\n"
		L"3 = Межкомнатные перегородки из ГКЛ, зашивка стояков из ГКЛВ\n"
		L"\n"
		L"[Материалы]\n"
		L"# Код слоя из названия многослойной конструкции = номер столбца.\n"
		L"# Название «Ст_В| Ш 15_КБ 190_Ш 15» делится по «_» на слои «Ш 15», «КБ 190», «Ш 15»;\n"
		L"# код — буквы в начале слоя (Ш, КБ). У однослойной стены код — первое слово названия\n"
		L"# строительного материала. Правила проверяются сверху вниз: столбец даёт первое правило,\n"
		L"# чей код есть в названии. Стена только из штукатурки (отделочная стенка на ж/б стене) — монолит.\n"
		L"# 0 — слой не определяет столбец (утеплитель, воздух). Колонны = столбец для колонн и пилонов.\n"
		L"# «Ж/Б» в названии читается как «ЖБ». ЖБ стоит первым: есть ж/б слой — стена монолитная.\n"
		L"ЖБ = 1\n"
		L"Железобетон = 1\n"
		L"Бетон = 1\n"
		L"ГКЛ = 3\n"
		L"ГКЛв = 3\n"
		L"ГВЛ = 3\n"
		L"КБ = 2\n"
		L"ПГП = 2\n"
		L"Кирп = 2\n"
		L"Кирпич = 2\n"
		L"Керамзитобетон = 2\n"
		L"Газобетон = 2\n"
		L"Пенобетон = 2\n"
		L"ГБ = 2\n"
		L"ВБ = 2\n"
		L"Ш = 1\n"
		L"Штук = 1\n"
		L"Штукатурка = 1\n"
		L"Гипс = 1\n"
		L"У = 0\n"
		L"Ут = 0\n"
		L"И = 0\n"
		L"ЭППС = 0\n"
		L"Возд = 0\n"
		L"Кл = 0\n"
		L"Колонны = 1\n"
		L"\n"
		L"[Группы]\n"
		L"# Начало кода категории зоны = группа (подраздел) ведомости. Порядок групп в ведомости — как здесь.\n"
		L"# Можно уточнять: 2061 = Общедомовые помещения (самое длинное совпадение главнее).\n"
		L"# Подвал = группа — все зоны на этажах с номером 0 и ниже (строка не обязательна).\n"
		L"# Код категории — код в настройках категории, а если он пустой — первое число в названии категории.\n"
		L"# «-» — зоны с таким кодом в ведомость не входят.\n"
		L"3 = Тех.помещения (подвал)\n"
		L"5 = Встроенные помещения\n"
		L"1 = Квартиры\n"
		L"6 = Типовой этаж\n"
		L"4 = 1 этаж\n"
		L"2 = Общедомовые помещения\n"
		L"7 = Чердак\n"
		L"8 = -\n"
		L"9 = -\n"
		L"\n"
		L"[Столбец 3]\n"
		L"# Межкомнатные перегородки (столбец 3) — только в зонах этой подкатегории, кроме зон, в имени или категории\n"
		L"# которых есть одно из слов «Кроме»; у остальных зон такие стены идут в столбец «Иначе».\n"
		L"Подкатегория = Квартиры\n"
		L"Кроме = санузел; с/у; с/у; совмещ; ванн; душ; туалет; уборн; балкон; лоджи; террас\n"
		L"Иначе = 2\n"
		L"\n"
		L"[Отделка]\n"
		L"# Как записано в зоне (номер «00» в начале отбрасывается) = как писать в ведомости\n"
		L"Покраска = Водно-дисперсионная окраска\n"
		L"Обои = Оклейка обоями светлых тонов\n"
		L"Винил обои = Оклейка виниловыми обоями\n"
		L"ГКЛ = Подвесной потолок из ГКЛ с окраской водно-дисперсионной краской\n";
}


static std::vector<std::wstring> SplitBy (const std::wstring& s, wchar_t sep)
{
	std::vector<std::wstring> res;
	std::wstring cur;
	for (wchar_t c : s) {
		if (c == sep) { res.push_back (cur); cur.clear (); }
		else cur += c;
	}
	res.push_back (cur);
	return res;
}


static bool ToInt (const std::wstring& s, int& v)
{
	const std::wstring t = Trim (s);
	if (t.empty ()) return false;
	size_t i = 0;
	bool neg = false;
	if (t[0] == L'-' || t[0] == L'+' || t[0] == 0x2212) { neg = (t[0] != L'+'); i = 1; }
	if (i >= t.size ()) return false;
	long r = 0;
	for (; i < t.size (); ++i) {
		if (t[i] < L'0' || t[i] > L'9') return false;
		r = r * 10 + (t[i] - L'0');
		if (r > 100000) return false;
	}
	v = (int) (neg ? -r : r);
	return true;
}


Rules ParseRules (const std::wstring& text, std::vector<std::wstring>* warnings)
{
	Rules r;
	enum Sec { None, Title, Cols, Mats, Groups, Rows, Finish, Col3 } sec = None;
	auto warn = [&] (int line, const std::wstring& msg) {
		if (warnings != nullptr)
			warnings->push_back (L"Правила, строка " + std::to_wstring (line) + L": " + msg);
	};
	auto addGroup = [&r] (const std::wstring& g) {
		for (const auto& x : r.groupOrder)
			if (SameText (x, g)) return;
		r.groupOrder.push_back (g);
	};

	const std::vector<std::wstring> lines = SplitBy (text, L'\n');
	for (size_t li = 0; li < lines.size (); ++li) {
		const int lineNo = (int) li + 1;
		const std::wstring line = Trim (lines[li]);
		if (line.empty () || line[0] == L'#')
			continue;
		if (line[0] == L'[') {
			const std::wstring name = Lower (Trim (line.substr (1, line.find (L']') == std::wstring::npos ? std::wstring::npos : line.find (L']') - 1)));
			if (name == L"заголовок") sec = Title;
			else if (name == L"столбцы") sec = Cols;
			else if (name == L"материалы") sec = Mats;
			else if (name == L"группы") sec = Groups;
			else if (name == L"строки") sec = Rows;
			else if (name == L"отделка") sec = Finish;
			else if (name == L"столбец 3") sec = Col3;
			else { sec = None; warn (lineNo, L"неизвестный раздел " + line); }
			continue;
		}
		if (sec == Title) {
			r.title = line;
			continue;
		}
		const size_t eq = line.find (L'=');
		if (eq == std::wstring::npos) {
			warn (lineNo, L"нет знака «=»: " + line);
			continue;
		}
		const std::wstring key = Trim (line.substr (0, eq));
		const std::wstring val = Trim (line.substr (eq + 1));
		int n = 0;
		switch (sec) {
			case Cols:
				if (ToInt (key, n) && n >= 1 && n <= kWallColumns) r.columnTitle[n] = val;
				else warn (lineNo, L"номер столбца должен быть от 1 до 3");
				break;
			case Mats:
				if (!ToInt (val, n) || n < 0 || n > kWallColumns) { warn (lineNo, L"столбец должен быть от 0 до 3"); break; }
				if (SameText (key, L"Колонны")) r.columnsColumn = n;
				else if (!key.empty ()) r.materials.push_back ({ key, n });
				break;
			case Groups:
				if (SameText (key, L"Подвал")) { r.basementGroup = val; addGroup (val); }
				else if (!key.empty ()) {
					const bool excl = val == L"-" || val == L"—" || SameText (val, L"не включать") || SameText (val, L"исключить");
					r.groupByCode.push_back ({ key, excl ? std::wstring (kExcludedGroup) : val });
					if (!excl) addGroup (val);
				}
				else warn (lineNo, L"ожидается начало кода категории зоны");
				break;
			case Rows: {
				RowRule rr;
				const size_t bar = key.find (L'|');
				if (bar != std::wstring::npos) {
					rr.group = Trim (key.substr (0, bar));
					rr.title = Trim (key.substr (bar + 1));
				} else {
					rr.title = key;
				}
				for (const auto& nm : SplitBy (val, L';'))
					if (!Trim (nm).empty ()) rr.names.push_back (Trim (nm));
				if (rr.title.empty ()) warn (lineNo, L"пустое название строки");
				else r.rows.push_back (rr);
				break;
			}
			case Finish:
				if (!key.empty ()) r.finishNames.push_back ({ key, val });
				break;
			case Col3:
				if (SameText (key, L"Подкатегория")) r.col3Group = val;
				else if (SameText (key, L"Кроме")) {
					r.col3Except.clear ();
					for (const auto& w : SplitBy (val, L';')) if (!Trim (w).empty ()) r.col3Except.push_back (Lower (Trim (w)));
				}
				else if (SameText (key, L"Иначе")) {
					if (!ToInt (val, n) || n < 0 || n > kWallColumns) warn (lineNo, L"«Иначе» — номер столбца от 0 до 3");
					else r.col3Else = n;
				}
				else warn (lineNo, L"ожидается Подкатегория, Кроме или Иначе");
				break;
			default:
				warn (lineNo, L"строка вне раздела");
				break;
		}
	}
	for (int c = 1; c <= kWallColumns; ++c)
		if (r.columnTitle[c].empty ())
			r.columnTitle[c] = L"Стены, столбец " + std::to_wstring (c);
	addGroup (r.otherGroup);
	return r;
}

// ============================================================================= материалы

static bool IsLetter (wchar_t c)
{
	return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= 0x0400 && c <= 0x04FF);
}


static std::wstring NormalizeRC (const std::wstring& name)
{
	// «Ж/Б», «ж/б» -> «ЖБ»
	std::wstring s = name;
	for (size_t i = 0; i + 2 < s.size (); ++i)
		if ((s[i] == L'Ж' || s[i] == L'ж') && s[i + 1] == L'/' && (s[i + 2] == L'Б' || s[i + 2] == L'б'))
			s.replace (i, 3, L"ЖБ");
	return s;
}


std::vector<std::wstring> MaterialCodes (const std::wstring& nameIn)
{
	const std::wstring name = NormalizeRC (nameIn);
	// «| Ст_В| Ш 15_КБ 190_Ш 15», «Ст_П_О | ГКЛв С623_ПС50 | <10м»: части между «|»;
	// часть с типом конструкции («Ст_В», «Ст_П_О», «Ст_Ут_Вш») пропускается
	std::vector<std::wstring> res;
	for (const auto& seg : SplitBy (name, L'|')) {
		std::vector<std::wstring> codes;
		for (const auto& part : SplitBy (seg, L'_')) {
			const std::wstring t = Trim (part);
			std::wstring code;
			for (wchar_t c : t) {
				if (!IsLetter (c)) break;
				code += c;
			}
			if (!code.empty ())
				codes.push_back (code);
		}
		if (codes.empty () || SameText (codes[0], L"Ст"))
			continue;
		res.insert (res.end (), codes.begin (), codes.end ());
	}
	return res;
}


bool IsReinforcedConcrete (const std::wstring& name)
{
	for (const auto& c : MaterialCodes (name))
		if (SameText (c, L"ЖБ") || SameText (c, L"Железобетон") || SameText (c, L"Бетон"))
			return true;
	return false;
}


int ClassifyWall (const std::wstring& name, const Rules& r)
{
	const std::vector<std::wstring> codes = MaterialCodes (name);
	for (const MatRule& m : r.materials) {
		if (m.column == 0)		// утеплитель, воздух и т. п. — столбец не определяют
			continue;
		for (const auto& c : codes)
			if (SameText (c, m.code))
				return m.column;
	}
	return 0;
}


std::wstring CleanFinish (const std::wstring& raw, const Rules& r)
{
	std::wstring s = Trim (raw);
	// «00 Покраска» -> «Покраска»
	size_t i = 0;
	while (i < s.size () && s[i] >= L'0' && s[i] <= L'9') ++i;
	if (i > 0 && i < s.size () && s[i] == L' ')
		s = Trim (s.substr (i));
	for (const auto& f : r.finishNames)
		if (SameText (f.first, s))
			return f.second;
	return s;
}

// ============================================================================= этажи

bool ParseFloors (const std::wstring& text, std::set<int>& out)
{
	out.clear ();
	std::wstring t;
	for (wchar_t c : text) t += (c == L',') ? L';' : c;
	for (const auto& tok0 : SplitBy (t, L';')) {
		const std::wstring tok = Trim (tok0);
		if (tok.empty ()) continue;
		// разделитель диапазона — «-» или «..» не в начале и после цифры
		size_t sep = std::wstring::npos;
		size_t sepLen = 1;
		for (size_t i = 1; i < tok.size (); ++i) {
			if ((tok[i] == L'-' || tok[i] == 0x2013 || tok[i] == 0x2014) && tok[i - 1] >= L'0' && tok[i - 1] <= L'9') { sep = i; break; }
			if (tok[i] == L'.' && i + 1 < tok.size () && tok[i + 1] == L'.') { sep = i; sepLen = 2; break; }
		}
		int a = 0, b = 0;
		if (sep == std::wstring::npos) {
			if (!ToInt (tok, a)) return false;
			b = a;
		} else {
			if (!ToInt (tok.substr (0, sep), a) || !ToInt (tok.substr (sep + sepLen), b)) return false;
		}
		if (a > b) std::swap (a, b);
		if (b - a > 1000) return false;
		for (int k = a; k <= b; ++k) out.insert (k);
	}
	return !out.empty ();
}


std::wstring FloorsLabel (const std::set<int>& floors)
{
	if (floors.empty ()) return std::wstring ();
	std::wstring res;
	auto it = floors.begin ();
	while (it != floors.end ()) {
		const int a = *it;
		int b = a;
		auto nx = std::next (it);
		while (nx != floors.end () && *nx == b + 1) { b = *nx; ++nx; }
		if (!res.empty ()) res += L", ";
		res += std::to_wstring (a);
		if (b > a) res += L"-" + std::to_wstring (b);
		it = nx;
	}
	return res + L" эт.";
}

// ============================================================================= площади

double OpeningOverlap (double lower, double h, double lo, double hi)
{
	const double a = std::max (lower, lo);
	const double b = std::min (lower + h, hi);
	return b > a ? b - a : 0.0;
}


double WallPiece::Area () const
{
	const double a = length * finishHeight - openings;
	return a > 0 ? a : 0.0;
}

// ============================================================================= ведомость

const wchar_t* const kExcludedGroup = L"-";


std::wstring CatCodeOf (const ZoneData& z)
{
	const std::wstring c = Trim (z.catCode);
	if (!c.empty ())
		return c;
	const std::wstring& n = z.name;
	size_t i = 0;
	while (i < n.size () && !(n[i] >= L'0' && n[i] <= L'9')) ++i;
	size_t e = i;
	while (e < n.size () && n[e] >= L'0' && n[e] <= L'9') ++e;
	return n.substr (i, e - i);
}


bool IsExcluded (const ZoneData& z, const Rules& r)
{
	return GroupOf (z, r) == kExcludedGroup;
}


std::wstring GroupOf (const ZoneData& z, const Rules& r)
{
	size_t best = 0;
	std::wstring g;
	const std::wstring code = CatCodeOf (z);
	for (const auto& kv : r.groupByCode) {
		const std::wstring pre = Trim (kv.first);
		if (pre.size () > best && code.compare (0, pre.size (), pre) == 0) {
			best = pre.size ();
			g = kv.second;
		}
	}
	if (g == kExcludedGroup)
		return g;
	if (z.floor <= 0 && !r.basementGroup.empty ())
		return r.basementGroup;
	return g.empty () ? r.otherGroup : g;
}


std::wstring ShortName (const std::wstring& category)
{
	const std::wstring t = Trim (category);
	const size_t p = t.rfind (L'_');
	const std::wstring s = (p == std::wstring::npos) ? t : Trim (t.substr (p + 1));
	return s.empty () ? t : s;
}


std::wstring CleanZoneName (const std::wstring& raw)
{
	// «Административное\nпомещение» — «\n» (перенос строки в маркере зоны) -> пробел
	std::wstring s;
	for (size_t i = 0; i < raw.size (); ++i) {
		const wchar_t c = raw[i];
		if (c == L'\\' && i + 1 < raw.size () && (raw[i + 1] == L'n' || raw[i + 1] == L'N' || raw[i + 1] == L'п')) { s += L' '; ++i; continue; }
		if (c == L'\n' || c == L'\r' || c == L'\t' || c == 0xA0) { s += L' '; continue; }
		s += c;
	}
	std::wstring res;
	for (wchar_t c : s)
		if (!(c == L' ' && (res.empty () || res.back () == L' '))) res += c;
	return Trim (res);
}


std::wstring ZoneTitle (const ZoneData& z)
{
	const std::wstring n = CleanZoneName (z.roomName);
	if (!n.empty ()) return n;
	return Trim (z.name).empty () ? std::wstring (L"(без имени)") : ShortName (z.name);
}


std::wstring JoinNames (const std::vector<std::wstring>& names)
{
	std::wstring res;
	for (size_t i = 0; i < names.size (); ++i) {
		std::wstring n = names[i];
		// со строчной буквы, кроме аббревиатур (С/У, КУИ, МОП)
		if (i > 0 && n.size () >= 2 && Lower (n.substr (1, 1)) == n.substr (1, 1) && Lower (n.substr (1, 1)) != L"/" && IsLetter (n[1]))
			n = Lower (n.substr (0, 1)) + n.substr (1);
		res += (i == 0 ? L"" : L", ") + n;
	}
	return res;
}


std::wstring RowTitleOf (const ZoneData& z, const std::wstring&, const Rules&)
{
	return ZoneTitle (z);
}


Sheet BuildSheet (const std::vector<ZoneData>& zones, const Rules& r, const std::wstring& section)
{
	Sheet sh;
	sh.title = r.title;
	const std::wstring key = L"{секция}";
	const size_t p = sh.title.find (key);
	if (p != std::wstring::npos)
		sh.title.replace (p, key.size (), section);
	for (int c = 0; c <= kWallColumns; ++c)
		sh.columnTitle[c] = r.columnTitle[c];

	// строка ведомости = подкатегория + отделка потолка + отделка стен; наименование — перечень имён зон
	struct Key { std::wstring group, ceil, wall; };
	std::vector<SheetRow> rows;
	auto findRow = [&rows] (const Key& k) -> SheetRow* {
		for (auto& row : rows)
			if (row.group == k.group && row.ceilFinish == k.ceil && row.wallFinish == k.wall)
				return &row;
		return nullptr;
	};

	for (const ZoneData& z : zones) {
		Key k;
		k.group = GroupOf (z, r);
		if (k.group == kExcludedGroup)
			continue;
		k.ceil = CleanFinish (z.ceilFinishRaw, r);
		k.wall = CleanFinish (z.wallFinishRaw, r);
		SheetRow* row = findRow (k);
		if (row == nullptr) {
			SheetRow nr;
			nr.group = k.group; nr.ceilFinish = k.ceil; nr.wallFinish = k.wall;
			rows.push_back (nr);
			row = &rows.back ();
		}
		{
			const std::wstring n = ZoneTitle (z);
			bool has = false;
			for (const auto& x : row->names) if (SameText (x, n)) { has = true; break; }
			if (!has) row->names.push_back (n);
		}
		row->ceilArea += z.ceilArea;
		row->floors.insert (z.floor);
		row->zones += 1;
		for (const WallPiece& w : z.pieces) {
			const int c = (w.column >= 1 && w.column <= kWallColumns) ? w.column : 0;
			row->wallArea[c] += w.Area ();
			if (c == 0) sh.unknownArea += w.Area ();
		}
	}

	for (auto& row : rows)
		row.title = JoinNames (row.names);

	// порядок: группы как в правилах; внутри — по первому этажу, потом по наименованию
	auto groupIdx = [&r] (const std::wstring& g) {
		for (size_t i = 0; i < r.groupOrder.size (); ++i)
			if (SameText (r.groupOrder[i], g)) return (int) i;
		return (int) r.groupOrder.size ();
	};
	std::stable_sort (rows.begin (), rows.end (), [&] (const SheetRow& a, const SheetRow& b) {
		const int ga = groupIdx (a.group), gb = groupIdx (b.group);
		if (ga != gb) return ga < gb;
		if (a.group != b.group) return a.group < b.group;
		if (a.wallFinish != b.wallFinish) return Lower (a.wallFinish) < Lower (b.wallFinish);	// одинаковые стены — подряд
		if (*a.floors.begin () != *b.floors.begin ()) return *a.floors.begin () < *b.floors.begin ();
		return Lower (a.title) < Lower (b.title);
	});

	// одна строка с разной отделкой на разных этажах — дописать этажи
	std::vector<int> same (rows.size (), 0);
	for (size_t i = 0; i < rows.size (); ++i)
		for (const auto& o : rows)
			if (o.group == rows[i].group && o.title == rows[i].title) ++same[i];
	for (size_t i = 0; i < rows.size (); ++i)
		if (same[i] > 1)
			rows[i].title += L" (" + FloorsLabel (rows[i].floors) + L")";
	// объединение стен у соседних строк с одинаковой отделкой стен
	for (size_t i = 0; i < rows.size (); ) {
		size_t j = i + 1;
		while (j < rows.size () && rows[j].group == rows[i].group && rows[j].wallFinish == rows[i].wallFinish) ++j;
		rows[i].wallSpan = (int) (j - i);
		for (size_t k = i; k < j; ++k) {
			if (k > i) rows[k].wallSpan = 0;
			for (int c = 0; c <= kWallColumns; ++c) rows[i].wallSum[c] += rows[k].wallArea[c];
		}
		i = j;
	}
	sh.rows = rows;
	return sh;
}

// ============================================================================= контур зоны

double Seg::Length () const { return std::hypot (x2 - x1, y2 - y1); }


bool IsAxisEdge (const Seg& e, double tol)
{
	return std::fabs (e.x2 - e.x1) < tol || std::fabs (e.y2 - e.y1) < tol;
}


bool EdgeOnBox (const Seg& e, double bx1, double by1, double bx2, double by2, double tol, double& a, double& b)
{
	const double len = e.Length ();
	if (len < 1e-6 || !IsAxisEdge (e))
		return false;
	const bool horiz = std::fabs (e.y2 - e.y1) < 1e-3;
	const double c = horiz ? (e.y1 + e.y2) / 2 : (e.x1 + e.x2) / 2;		// линия ребра
	const double f1 = horiz ? by1 : bx1, f2 = horiz ? by2 : bx2;		// грани поперёк
	if (!(std::fabs (c - f1) <= tol || std::fabs (c - f2) <= tol || (c > f1 && c < f2)))
		return false;
	const double s = horiz ? e.x1 : e.y1, t = horiz ? e.x2 : e.y2;
	const double lo = std::max (std::min (s, t), horiz ? bx1 : by1);
	const double hi = std::min (std::max (s, t), horiz ? bx2 : by2);
	if (hi - lo < 0.005)
		return false;
	a = std::fabs (lo - s);
	b = std::fabs (hi - s);
	if (a > b) std::swap (a, b);
	return true;
}


std::vector<std::pair<double, double>> UnionIntervals (std::vector<std::pair<double, double>> v)
{
	std::sort (v.begin (), v.end ());
	std::vector<std::pair<double, double>> res;
	for (const auto& x : v) {
		if (!res.empty () && x.first <= res.back ().second + 1e-6) res.back ().second = std::max (res.back ().second, x.second);
		else res.push_back (x);
	}
	return res;
}


double UncoveredLength (double a, double b, const std::vector<std::pair<double, double>>& covered)
{
	double free = b - a;
	for (const auto& c : UnionIntervals (covered)) {
		const double lo = std::max (a, c.first), hi = std::min (b, c.second);
		if (hi > lo) free -= hi - lo;
	}
	return std::max (0.0, free);
}


std::vector<std::pair<double, double>> Gaps (double len, const std::vector<std::pair<double, double>>& covered, double minLen)
{
	std::vector<std::pair<double, double>> res;
	double pos = 0;
	for (const auto& c : UnionIntervals (covered)) {
		if (c.first - pos >= minLen) res.push_back ({ pos, c.first });
		pos = std::max (pos, c.second);
	}
	if (len - pos >= minLen) res.push_back ({ pos, len });
	return res;
}

// ============================================================================= грани элементов внутри зоны

double FaceRun::Length () const { return std::hypot (bx - ax, by - ay); }


bool PointInPolys (const Polys& p, double x, double y)
{
	bool in = false;
	for (const auto& poly : p) {
		const size_t n = poly.size ();
		for (size_t i = 0, j = n - 1; i < n; j = i++) {
			const double xi = poly[i].first, yi = poly[i].second, xj = poly[j].first, yj = poly[j].second;
			if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
				in = !in;
		}
	}
	return in;
}


Shape BoxShape (const Box& b)
{
	return { { b.x1, b.y1 }, { b.x2, b.y1 }, { b.x2, b.y2 }, { b.x1, b.y2 } };
}


static double SignedArea (const Shape& s)
{
	double a = 0;
	for (size_t i = 0, n = s.size (); i < n; ++i)
		a += s[i].first * s[(i + 1) % n].second - s[(i + 1) % n].first * s[i].second;
	return a / 2;
}


static bool InShape (const Shape& s, double x, double y)
{
	bool in = false;
	const size_t n = s.size ();
	for (size_t i = 0, j = n - 1; i < n; j = i++) {
		const double xi = s[i].first, yi = s[i].second, xj = s[j].first, yj = s[j].second;
		if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
			in = !in;
	}
	return in;
}


static double DistToBoundary (const Shape& s, double x, double y)
{
	double best = 1e30;
	for (size_t i = 0, n = s.size (); i < n; ++i) {
		const double ax = s[i].first, ay = s[i].second, bx = s[(i + 1) % n].first, by = s[(i + 1) % n].second;
		const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
		double t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
		t = std::max (0.0, std::min (1.0, t));
		best = std::min (best, std::hypot (x - (ax + t * dx), y - (ay + t * dy)));
	}
	return best;
}


std::vector<FaceRun> ExposedFaces (const Polys& zone, const std::vector<Shape>& shapes, size_t idx, double step, const std::vector<int>& prio)
{
	static const double kDepth[] = { 0.005, 0.015, 0.03, 0.045 };
	const double e = 1e-4;
	std::vector<FaceRun> res;
	const Shape& sh = shapes[idx];
	const size_t n = sh.size ();
	if (n < 3) return res;
	const bool ccw = SignedArea (sh) > 0;
	auto wins = [&] (size_t o) {
		const int po = o < prio.size () ? prio[o] : 0, pi = idx < prio.size () ? prio[idx] : 0;
		return po > pi || (po == pi && o < idx);
	};
	for (size_t f = 0; f < n; ++f) {
		const double sx = sh[f].first, sy = sh[f].second, ex = sh[(f + 1) % n].first, ey = sh[(f + 1) % n].second;
		const double len = std::hypot (ex - sx, ey - sy);
		if (len < step) continue;
		const double ux = (ex - sx) / len, uy = (ey - sy) / len;
		const double nx = ccw ? uy : -uy, ny = ccw ? -ux : ux;		// наружная нормаль
		const int m = (int) std::floor (len / step);
		const double st = len / m;
		int runStart = -1;
		auto flush = [&] (int endK) {
			if (runStart < 0) return;
			FaceRun r; r.face = (int) f;
			r.ax = sx + ux * runStart * st; r.ay = sy + uy * runStart * st;
			r.bx = sx + ux * endK * st;     r.by = sy + uy * endK * st;
			res.push_back (r);
			runStart = -1;
		};
		for (int k = 0; k < m; ++k) {
			const double px = sx + ux * (k + 0.5) * st, py = sy + uy * (k + 0.5) * st;
			// грань внутри соседнего элемента (стык) не отделывается; совпадающие грани (одна линия, одна сторона)
			// считаются один раз — у элемента с большим приоритетом (колонна перед стеной), при равном — у первого
			bool inside = false;
			for (size_t o = 0; o < shapes.size () && !inside; ++o) {
				if (o == idx) continue;
				const Shape& os = shapes[o];
				if (InShape (os, px, py) && DistToBoundary (os, px, py) > e) {
					// колонна, сквозь которую проходит стена, не прячется за стеной
					if (!(idx < prio.size () && o < prio.size () && prio[idx] > prio[o])) inside = true;
					continue;
				}
				if (wins (o) && DistToBoundary (os, px, py) <= e && InShape (os, px - nx * 0.001, py - ny * 0.001))
					inside = true;
			}
			// идём от грани наружу до 5 см: зона без элементов — отделывается; элемент раньше зоны — грань закрыта;
			// элемент сразу за зоной (щель до 5 см между пилоном и стеной) — щель не отделывается
			bool exposed = false;
			for (double d : kDepth) {
				if (inside) break;
				const double qx = px + nx * d, qy = py + ny * d;
				bool blocked = false;
				for (size_t o = 0; o < shapes.size () && !blocked; ++o) {
					if (o == idx || !InShape (shapes[o], qx, qy) || DistToBoundary (shapes[o], qx, qy) <= e)
						continue;
					// элемент ниже по приоритету, который проходит сквозь наш (пилон внутри стены КБ, чуть более толстой),
					// нашу грань не закрывает — наш элемент его вырезает
					const int po = o < prio.size () ? prio[o] : 0, pi = idx < prio.size () ? prio[idx] : 0;
					if (po < pi && InShape (shapes[o], px - nx * 0.001, py - ny * 0.001))
						continue;
					blocked = true;
				}
				if (blocked) { exposed = false; break; }
				if (PointInPolys (zone, qx, qy)) exposed = true;
			}
			if (exposed) { if (runStart < 0) runStart = k; }
			else flush (k);
		}
		flush (m);
	}
	return res;
}


std::vector<FaceRun> ExposedFaces (const Polys& zone, const std::vector<Box>& boxes, size_t idx, double step, const std::vector<int>& prio)
{
	std::vector<Shape> sh;
	for (const Box& b : boxes) sh.push_back (BoxShape (b));
	return ExposedFaces (zone, sh, idx, step, prio);
}


std::vector<double> ContourGaps (const Polys& zone, const std::vector<Shape>& shapes, double minLen, double step)
{
	std::vector<double> res;
	for (const auto& poly : zone) {
		const size_t n = poly.size ();
		for (size_t i = 0; i < n; ++i) {
			const Pt a = poly[i], b = poly[(i + 1) % n];
			const double len = std::hypot (b.first - a.first, b.second - a.second);
			if (len < minLen) continue;
			const double ux = (b.first - a.first) / len, uy = (b.second - a.second) / len;
			const int m = std::max (1, (int) std::floor (len / step));
			const double st = len / m;
			double gap = 0;
			for (int k = 0; k <= m; ++k) {
				bool covered = true;
				if (k < m) {
					const double px = a.first + ux * (k + 0.5) * st, py = a.second + uy * (k + 0.5) * st;
					covered = false;
					for (int side = -1; side <= 1 && !covered; side += 2)		// элемент с любой стороны ребра в пределах 5 см
						for (double d : { 0.005, 0.02, 0.05 }) {
							const double qx = px - uy * d * side, qy = py + ux * d * side;
							if (PointInPolys (zone, qx, qy)) continue;		// это сторона зоны
							for (const Shape& s : shapes) if (InShape (s, qx, qy)) { covered = true; break; }
							if (covered) break;
						}
				}
				if (!covered) gap += st;
				else { if (gap >= minLen) res.push_back (gap); gap = 0; }
			}
		}
	}
	return res;
}


std::vector<double> ContourGaps (const Polys& zone, const std::vector<Box>& boxes, double minLen, double step)
{
	std::vector<Shape> sh;
	for (const Box& b : boxes) sh.push_back (BoxShape (b));
	return ContourGaps (zone, sh, minLen, step);
}


// ============================================================================= столбец 3 — только квартиры

bool Col3Allowed (const ZoneData& z, const Rules& r)
{
	if (r.col3Group.empty ())
		return true;
	if (!SameText (GroupOf (z, r), r.col3Group))
		return false;
	const std::wstring a = Lower (ZoneTitle (z)), b = Lower (z.name);
	for (const auto& w : r.col3Except)
		if (a.find (w) != std::wstring::npos || b.find (w) != std::wstring::npos)
			return false;
	return true;
}


int ApplyColumnRules (ZoneData& z, const Rules& r)
{
	if (Col3Allowed (z, r))
		return 0;
	int n = 0;
	for (WallPiece& p : z.pieces)
		if (p.column == 3) { p.column = r.col3Else; p.col3Moved = true; ++n; }
	return n;
}

}	// namespace otd
