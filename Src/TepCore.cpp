// Паспорта квартир и ТЭП жилого дома по зонам HimeraZone
#include "TepCore.hpp"
#include "FinishCore.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace tep {

using otd::Trim;
using otd::SameText;
using otd::FormatNum;

// ----------------------------------------------------------------------------- коды категорий
static std::vector<std::wstring> SplitList (const std::wstring& s, wchar_t sep)
{
	std::vector<std::wstring> res;
	std::wstring cur;
	for (wchar_t ch : s) {
		if (ch == sep) { res.push_back (Trim (cur)); cur.clear (); }
		else cur += ch;
	}
	res.push_back (Trim (cur));
	return res;
}

static bool StartsWith (const std::wstring& s, const std::wstring& p)
{
	return !p.empty () && s.size () >= p.size () && s.compare (0, p.size (), p) == 0;
}

bool CodeSet::Has (const std::wstring& code) const
{
	const std::wstring c = Trim (code);
	if (c.empty ()) return false;
	bool in = false;
	for (const auto& p : inc) if (StartsWith (c, p)) { in = true; break; }
	if (!in) return false;
	for (const auto& p : exc) if (StartsWith (c, p)) return false;
	return true;
}

CodeSet ParseCodes (const std::wstring& text)
{
	CodeSet s;
	std::wstring t = text;
	for (wchar_t& ch : t) if (ch == L';' || ch == L' ') ch = L',';
	for (const auto& item : SplitList (t, L',')) {
		if (item.empty ()) continue;
		if (item[0] == L'-' || item[0] == 0x2212) {
			const std::wstring v = Trim (item.substr (1));
			if (!v.empty ()) s.exc.push_back (v);
		} else
			s.inc.push_back (item);
	}
	return s;
}

std::wstring CodesText (const CodeSet& s)
{
	std::wstring r;
	for (const auto& p : s.inc) r += (r.empty () ? L"" : L", ") + p;
	for (const auto& p : s.exc) r += (r.empty () ? L"-" : L", -") + p;
	return r;
}

// ----------------------------------------------------------------------------- файл данных
std::wstring DefaultConfigText ()
{
	return
		L"# ТЭП и паспорта квартир — данные проекта (надстройка «Ведомость отделки»)\n"
		L"# Версия: 1\n"
		L"# Коды категорий зон: «1010, 1011» — точно; «5» — все категории 5xxx; «-5020» — кроме 5020.\n"
		L"# Подземные этажи — этажи с отрицательным индексом (подвал = -1).\n"
		L"\n"
		L"[Площади]\n"
		L"Знаков после запятой = 1\n"
		L"# площадь каждого помещения округляется, затем складывается (как в паспортах на плане)\n"
		L"Разделитель в паспортах = .\n"
		L"Коэффициент балконов и террас = 0,3\n"
		L"Коэффициент лоджий = 0,5\n"
		L"Площадь на жителя = 30\n"
		L"Обозначение студии = С\n"
		L"\n"
		L"[Категории]\n"
		L"Помещения квартир = 1\n"
		L"Жилые комнаты = 1010, 1011\n"
		L"Кухня-столовая = 1022\n"
		L"Кухня = 1020\n"
		L"Балконы и террасы = 1060, 1061\n"
		L"Лоджии = 1070\n"
		L"Площадь застройки = 8020\n"
		L"Строительный объём = 8010\n"
		L"Площадь жилого здания = 8030\n"
		L"Техпомещения подвала = 4\n"
		L"Встроенные помещения = 5, -5020\n"
		L"Индивидуальные кладовые = 5020\n"
		L"Нежилые помещения = 3060, 3061\n"
		L"Общедомовые помещения = 2, 6\n"
		L"Не входят в площади = 8, 9\n"
		L"\n"
		L"[Вручную]\n"
		L"# Номер строки ТЭП = секция: значение; секция: значение; всего: значение\n"
		L"# Заменяет посчитанное. Пустая строка «18 =» ничего не меняет. Пример:\n"
		L"#   18 = 1: 22; 2: 22; 3: 13; 4: 22; 5: 28\n"
		L"#   2 = 1: 15; 2: 10; всего: перем.\n"
		L"18 =\n";
}

static double ToNum (const std::wstring& s, double def)
{
	std::wstring t = Trim (s);
	for (wchar_t& ch : t) if (ch == L',') ch = L'.';
	try { size_t n = 0; const double v = std::stod (t, &n); return n > 0 ? v : def; } catch (...) { return def; }
}

Config ParseConfig (const std::wstring& text, std::vector<std::wstring>* warnings)
{
	Config c;
	const Config def0 = Config ();
	// значения по умолчанию — из текста по умолчанию (рекурсия один раз)
	static bool inDefault = false;
	if (!inDefault) {
		inDefault = true;
		c = ParseConfig (DefaultConfigText (), nullptr);
		inDefault = false;
	}
	std::wistringstream in (text);
	std::wstring line, section;
	int lineNo = 0;
	auto warn = [&] (const std::wstring& w) { if (warnings) warnings->push_back (L"ТЭП, строка " + std::to_wstring (lineNo) + L": " + w); };
	while (std::getline (in, line)) {
		++lineNo;
		if (!line.empty () && line.back () == L'\r') line.pop_back ();
		if (!line.empty () && line[0] == 0xFEFF) line.erase (0, 1);
		const std::wstring t = Trim (line);
		if (t.empty () || t[0] == L'#') continue;
		if (t.front () == L'[' && t.back () == L']') { section = Trim (t.substr (1, t.size () - 2)); continue; }
		const size_t eq = t.find (L'=');
		if (eq == std::wstring::npos) { warn (L"нет «=» — пропущено"); continue; }
		const std::wstring k = Trim (t.substr (0, eq)), v = Trim (t.substr (eq + 1));
		if (SameText (section, L"Площади")) {
			if (SameText (k, L"Знаков после запятой")) c.dec = std::max (0, std::min (3, (int) std::lround (ToNum (v, 1))));
			else if (SameText (k, L"Разделитель в паспортах")) c.passportSep = v.empty () ? std::wstring (L".") : v.substr (0, 1);
			else if (SameText (k, L"Коэффициент балконов и террас")) c.coefBalcony = ToNum (v, def0.coefBalcony);
			else if (SameText (k, L"Коэффициент лоджий")) c.coefLoggia = ToNum (v, def0.coefLoggia);
			else if (SameText (k, L"Площадь на жителя")) c.perResident = std::max (1.0, ToNum (v, def0.perResident));
			else if (SameText (k, L"Обозначение студии")) c.studioLabel = v;
			else warn (L"неизвестный параметр «" + k + L"»");
		} else if (SameText (section, L"Категории")) {
			const CodeSet s = ParseCodes (v);
			struct { const wchar_t* key; CodeSet* dst; } map[] = {
				{ L"Помещения квартир", &c.apartment }, { L"Жилые комнаты", &c.living }, { L"Кухня-столовая", &c.kitchenDining },
				{ L"Кухня", &c.kitchen }, { L"Балконы и террасы", &c.balcony }, { L"Лоджии", &c.loggia },
				{ L"Площадь застройки", &c.building }, { L"Строительный объём", &c.volume }, { L"Строительный объем", &c.volume },
				{ L"Площадь жилого здания", &c.houseArea }, { L"Техпомещения подвала", &c.tech }, { L"Встроенные помещения", &c.builtIn },
				{ L"Индивидуальные кладовые", &c.storage }, { L"Нежилые помещения", &c.nonResidential },
				{ L"Общедомовые помещения", &c.common }, { L"Не входят в площади", &c.excluded } };
			bool found = false;
			for (auto& m : map)
				if (SameText (k, m.key)) { *m.dst = s; found = true; break; }
			if (!found) warn (L"неизвестная категория «" + k + L"»");
		} else if (SameText (section, L"Вручную")) {
			if (v.empty ()) continue;
			for (const auto& part : SplitList (v, L';')) {
				if (part.empty ()) continue;
				const size_t col = part.find (L':');
				if (col == std::wstring::npos) { warn (L"«" + part + L"» — нужно «секция: значение»"); continue; }
				std::wstring sec = Trim (part.substr (0, col));
				if (SameText (sec, L"всего")) sec = L"всего";
				c.manual[k][sec] = Trim (part.substr (col + 1));
			}
		} else
			warn (L"строка вне раздела [Площади] / [Категории] / [Вручную]");
	}
	return c;
}

// ----------------------------------------------------------------------------- квартиры
double RoundTo (double v, int dec)
{
	const double k = std::pow (10.0, dec);
	return std::floor (v * k + 0.5 + 1e-9) / k;
}

std::wstring CodeOf (const std::wstring& catCode, const std::wstring& catName)
{
	const std::wstring c = Trim (catCode);
	if (!c.empty ()) return c;
	std::wstring d;
	for (wchar_t ch : Trim (catName)) {
		if (ch >= L'0' && ch <= L'9') d += ch;
		else if (!d.empty ()) break;
		else if (ch != L' ') break;
	}
	return d;
}

std::wstring StoryKey (int story) { return std::to_wstring (story); }

static std::wstring TypeName (int n, bool kd, bool studio)
{
	if (studio) return L"1-комнатных (студий)";
	std::wstring s = n == 1 ? std::wstring (L"1-комнатных") : std::to_wstring (n) + L"-х комнатных";
	if (n <= 0) s = L"без жилых комнат";
	if (kd) s += L" с кухней-столовой";
	return s;
}

std::vector<Apartment> BuildApartments (const std::vector<Room>& rooms, const Config& c, std::vector<std::wstring>* warnings)
{
	std::map<std::wstring, Apartment> byKey;
	std::vector<std::wstring> order;
	std::map<std::wstring, std::vector<std::wstring>> noCode;	// этаж -> зоны квартир без номера
	std::map<std::wstring, std::vector<std::wstring>> notApt;
	std::map<std::wstring, double> bestLiving;
	for (size_t i = 0; i < rooms.size (); ++i) {
		const Room& r = rooms[i];
		if (c.excluded.Has (r.catCode)) continue;
		const bool aptCat = c.apartment.Has (r.catCode);
		const std::wstring code = Trim (r.apartment);
		if (!aptCat) {
			if (!code.empty ()) notApt[r.storyName].push_back (r.number + L" " + r.name + L" (" + r.catCode + L")");
			continue;
		}
		if (code.empty ()) {
			noCode[r.storyName + L", секция " + r.section].push_back ((r.number.empty () ? std::wstring () : r.number + L" ") + r.name);
			continue;
		}
		const std::wstring key = r.section + L"|" + StoryKey (r.story) + L"|" + code;
		auto it = byKey.find (key);
		if (it == byKey.end ()) {
			Apartment a;
			a.section = r.section; a.code = code; a.story = r.story; a.storyName = r.storyName;
			it = byKey.emplace (key, a).first;
			order.push_back (key);
		}
		Apartment& a = it->second;
		a.rooms.push_back (i);
		a.x1 = std::min (a.x1, r.x1); a.y1 = std::min (a.y1, r.y1);
		a.x2 = std::max (a.x2, r.x2); a.y2 = std::max (a.y2, r.y2);
		const double ar = RoundTo (r.area, c.dec);
		double k = 1;
		if (c.balcony.Has (r.catCode)) k = c.coefBalcony;
		else if (c.loggia.Has (r.catCode)) k = c.coefLoggia;
		if (k < 1) {
			a.total += ar;
			a.reduced += RoundTo (ar * k, c.dec);
		} else {
			a.area += ar;
			a.total += ar;
			a.reduced += ar;
		}
		if (c.living.Has (r.catCode)) {
			a.living += ar;
			++a.livingRooms;
			if (ar > bestLiving[key]) {		// самая большая жилая комната — у неё паспорт
				bestLiving[key] = ar;
				a.ax1 = r.x1; a.ay1 = r.y1; a.ax2 = r.x2; a.ay2 = r.y2;
			}
		}
		if (c.kitchenDining.Has (r.catCode)) a.hasKitchenDining = true;
		if (c.kitchen.Has (r.catCode)) a.hasKitchen = true;
	}
	std::vector<Apartment> res;
	for (const auto& key : order) {
		Apartment a = byKey[key];
		if (bestLiving.count (key) == 0) a.AnchorAll ();
		const bool studio = a.livingRooms == 1 && !a.hasKitchenDining && !a.hasKitchen;
		a.label = studio ? c.studioLabel : (a.livingRooms > 0 ? std::to_wstring (a.livingRooms) + L"К" : std::wstring (L"?"));
		a.typeName = TypeName (a.livingRooms, a.hasKitchenDining, studio);
		a.typeOrder = studio ? 0 : (a.livingRooms <= 0 ? 999 : a.livingRooms * 10 + (a.hasKitchenDining ? 2 : 1));
		// суммы — с тем же округлением (убрать хвосты сложения)
		a.living = RoundTo (a.living, c.dec); a.area = RoundTo (a.area, c.dec);
		a.total = RoundTo (a.total, c.dec); a.reduced = RoundTo (a.reduced, c.dec);
		if (a.livingRooms == 0 && warnings)
			warnings->push_back (L"Квартира «" + a.code + L"» (" + a.storyName + L", секция " + a.section + L") — нет жилых комнат (категории " +
								 CodesText (c.living) + L").");
		res.push_back (a);
	}
	std::stable_sort (res.begin (), res.end (), [] (const Apartment& p, const Apartment& q) {
		if (p.section != q.section) return SortSections ({ p.section, q.section }).front () == p.section;
		if (p.story != q.story) return p.story < q.story;
		return p.code < q.code;
	});
	if (warnings) {
		for (const auto& kv : noCode) {
			std::wstring s;
			for (size_t i = 0; i < kv.second.size () && i < 12; ++i) s += (i ? L", " : L"") + kv.second[i];
			if (kv.second.size () > 12) s += L" и ещё " + std::to_wstring (kv.second.size () - 12);
			warnings->push_back (L"Зоны квартир без параметра «Квартира» (не вошли в паспорта и ТЭП), " + kv.first + L": " + s + L".");
		}
		for (const auto& kv : notApt) {
			std::wstring s;
			for (size_t i = 0; i < kv.second.size () && i < 8; ++i) s += (i ? L", " : L"") + kv.second[i];
			warnings->push_back (L"Заполнен номер квартиры, но категория не квартирная (не вошли в квартиры), " + kv.first + L": " + s + L".");
		}
	}
	return res;
}

std::wstring PassportNum (double v, const Config& c)
{
	std::wstring s = FormatNum (RoundTo (v, c.dec), c.dec);
	std::wstring r;
	for (wchar_t ch : s) {
		if (ch == L' ' || ch == 0xA0) continue;
		r += ch == L',' ? c.passportSep[0] : ch;
	}
	return r;
}

void PassportPlace (const Apartment& a, double bx1, double by1, double bx2, double by2, double w, double h, double gap, double& x, double& y)
{
	const double dTop = by2 - a.ay2, dBot = a.ay1 - by1, dLeft = a.ax1 - bx1, dRight = bx2 - a.ax2;
	const double cx = (a.ax1 + a.ax2) / 2, cy = (a.ay1 + a.ay2) / 2;
	const double vMin = std::min (dTop, dBot), hMin = std::min (dLeft, dRight);
	if (vMin <= hMin + 2.0) {		// сверху / снизу (продольный фасад; лоджии отодвигают край этажа)
		x = cx - w / 2;
		y = dTop <= dBot ? a.ay2 + gap : a.ay1 - gap - h;
	} else {
		y = cy - h / 2;
		x = dLeft <= dRight ? a.ax1 - gap - w : a.ax2 + gap;
	}
}

// ----------------------------------------------------------------------------- свободное место
static bool Overlap (const Rect& a, const Rect& b)
{
	return a.x1 < b.x2 && b.x1 < a.x2 && a.y1 < b.y2 && b.y1 < a.y2;
}

static bool InPoly (const std::vector<otd::Pt>& p, double x, double y)
{
	bool in = false;
	for (size_t i = 0, j = p.size () - 1; i < p.size (); j = i++) {
		if (((p[i].second > y) != (p[j].second > y)) &&
			x < (p[j].first - p[i].first) * (y - p[i].second) / (p[j].second - p[i].second) + p[i].first)
			in = !in;
	}
	return in;
}

static bool SegHitsRect (double ax, double ay, double bx, double by, const Rect& r)
{
	// отсечение отрезка прямоугольником (Лян — Барски)
	double t0 = 0, t1 = 1;
	const double dx = bx - ax, dy = by - ay;
	const double p[4] = { -dx, dx, -dy, dy };
	const double q[4] = { ax - r.x1, r.x2 - ax, ay - r.y1, r.y2 - ay };
	for (int i = 0; i < 4; ++i) {
		if (std::fabs (p[i]) < 1e-12) { if (q[i] < 0) return false; continue; }
		const double t = q[i] / p[i];
		if (p[i] < 0) { if (t > t1) return false; if (t > t0) t0 = t; }
		else { if (t < t0) return false; if (t < t1) t1 = t; }
	}
	return t1 - t0 > 1e-9;
}

void FreeSpace::AddShape (const std::vector<otd::Pt>& poly)
{
	if (poly.size () < 3) return;
	Rect b { 1e30, 1e30, -1e30, -1e30 };
	for (const auto& p : poly) {
		b.x1 = std::min (b.x1, p.first); b.y1 = std::min (b.y1, p.second);
		b.x2 = std::max (b.x2, p.first); b.y2 = std::max (b.y2, p.second);
	}
	shapes.push_back (poly);
	shapeBox.push_back (b);
}

void FreeSpace::AddRect (const Rect& r) { rects.push_back (r); }

bool FreeSpace::IsFree (const Rect& r) const
{
	for (const Rect& q : rects)
		if (Overlap (r, q)) return false;
	for (size_t i = 0; i < shapes.size (); ++i) {
		if (!Overlap (r, shapeBox[i])) continue;
		const auto& p = shapes[i];
		if (InPoly (p, (r.x1 + r.x2) / 2, (r.y1 + r.y2) / 2)) return false;		// паспорт внутри зоны / стены
		for (size_t k = 0; k < p.size (); ++k) {
			const auto& a = p[k];
			const auto& b = p[(k + 1) % p.size ()];
			if (SegHitsRect (a.first, a.second, b.first, b.second, r)) return false;
		}
	}
	return true;
}

bool PlaceFree (const Apartment& a, double bx1, double by1, double bx2, double by2, double w, double h,
				double minGap, double maxGap, double step, double margin, const FreeSpace& fs, double& x, double& y)
{
	PassportPlace (a, bx1, by1, bx2, by2, w, h, minGap, x, y);
	step = std::max (step, 0.01);
	// стороны по близости к краю этажа: 0 верх, 1 низ, 2 лево, 3 право
	// продольные фасады (верх / низ) предпочтительнее торцов: лоджии и балконы отодвигают край этажа до ~2 м
	const double kLongBias = 2.0;
	std::vector<std::pair<double, int>> sides = { { by2 - a.ay2 - kLongBias, 0 }, { a.ay1 - by1 - kLongBias, 1 },
												  { a.ax1 - bx1, 2 }, { bx2 - a.ax2, 3 } };
	std::stable_sort (sides.begin (), sides.end ());
	const double cx = (a.ax1 + a.ax2) / 2, cy = (a.ay1 + a.ay2) / 2;
	const double sidePenalty = 4 * step + (w + h) / 2;		// другая сторона «дороже» небольшого отступа
	bool found = false;
	double best = 1e30;
	for (size_t rank = 0; rank < sides.size (); ++rank) {
		const int side = sides[rank].second;
		const bool horiz = side <= 1;
		const double span = horiz ? a.ax2 - a.ax1 : a.ay2 - a.ay1;
		const double size = horiz ? w : h;
		const double maxOff = std::max (0.0, span / 2 - size / 2) + size / 2;		// паспорт — в пределах стороны квартиры
		for (double d = minGap; d <= maxGap + 1e-9; d += step) {
			const double base = d + rank * sidePenalty;
			if (base >= best) break;
			bool hit = false;
			for (int k = 0; ; ++k) {
				const double off = ((k + 1) / 2) * step * (k % 2 == 1 ? 1 : -1);
				if (std::fabs (off) > maxOff + 1e-9) break;
				const double score = base + 0.5 * std::fabs (off);
				if (score >= best) break;
				double px, py;
				if (side == 0) { px = cx - w / 2 + off; py = a.ay2 + d; }
				else if (side == 1) { px = cx - w / 2 + off; py = a.ay1 - d - h; }
				else if (side == 2) { px = a.ax1 - d - w; py = cy - h / 2 + off; }
				else { px = a.ax2 + d; py = cy - h / 2 + off; }
				const Rect r { px - margin, py - margin, px + w + margin, py + h + margin };
				if (fs.IsFree (r)) {
					best = score; x = px; y = py; found = true; hit = true;
					break;
				}
			}
			if (hit) break;
		}
	}
	return found;
}

// ----------------------------------------------------------------------------- ТЭП
std::vector<std::wstring> SortSections (std::vector<std::wstring> s)
{
	std::sort (s.begin (), s.end (), [] (const std::wstring& a, const std::wstring& b) {
		const double na = ToNum (a, 1e18), nb = ToNum (b, 1e18);
		if (na != nb) return na < nb;
		return a < b;
	});
	s.erase (std::unique (s.begin (), s.end ()), s.end ());
	return s;
}

namespace {

struct Acc {
	std::map<std::wstring, double> v;
	double Total () const { double t = 0; for (const auto& kv : v) t += kv.second; return t; }
};

std::wstring Area (double v, int dec) { return std::fabs (v) < 0.5 * std::pow (10.0, -dec) ? std::wstring (L"-") : FormatNum (v, dec); }
std::wstring Count (double v) { return std::lround (v) == 0 ? std::wstring (L"-") : std::to_wstring (std::lround (v)); }

}	// namespace

Tep BuildTep (const std::vector<Room>& rooms, const std::vector<Apartment>& apts, const Config& c)
{
	Tep t;
	std::vector<std::wstring> secs;
	for (const Room& r : rooms) if (!c.excluded.Has (r.catCode) || c.building.Has (r.catCode) || c.volume.Has (r.catCode) || c.houseArea.Has (r.catCode)) secs.push_back (r.section);
	t.sections = SortSections (secs);

	std::map<std::wstring, std::set<int>> storiesAll, storiesUnder;
	Acc build, vol, volUnder, house, houseUnder, base, baseTech, builtIn, living, aptArea, aptTotal, aptReduced, storage, nonRes, nonResN, common;
	for (const Room& r : rooms) {
		const std::wstring& s = r.section;
		const std::wstring& k = r.catCode;
		if (c.building.Has (k)) build.v[s] += r.area;
		if (c.volume.Has (k)) { vol.v[s] += r.volume; if (r.underground) volUnder.v[s] += r.volume; }
		if (c.houseArea.Has (k)) { house.v[s] += r.area; if (r.underground) houseUnder.v[s] += r.area; }
		if (c.excluded.Has (k)) continue;
		storiesAll[s].insert (r.story);
		if (r.underground) storiesUnder[s].insert (r.story);
		const double a = RoundTo (r.area, c.dec);
		if (r.underground) {
			base.v[s] += a;
			if (c.tech.Has (k)) baseTech.v[s] += a;
		}
		if (c.builtIn.Has (k)) builtIn.v[s] += a;
		if (c.storage.Has (k)) storage.v[s] += a;
		if (c.nonResidential.Has (k)) { nonRes.v[s] += a; nonResN.v[s] += 1; }
		if (c.common.Has (k) && !r.underground) common.v[s] += a;
	}
	std::map<std::wstring, std::map<int, int>> types;		// секция -> порядок типа -> количество
	std::map<int, std::wstring> typeNames;
	Acc aptN;
	for (const Apartment& a : apts) {
		living.v[a.section] += a.living;
		aptArea.v[a.section] += a.area;
		aptTotal.v[a.section] += a.total;
		aptReduced.v[a.section] += a.reduced;
		aptN.v[a.section] += 1;
		types[a.section][a.typeOrder] += 1;
		typeNames[a.typeOrder] = a.typeName;
	}

	const int d = c.dec;
	auto row = [&] (const std::wstring& num, const std::wstring& name, const std::wstring& unit, bool sub = false, bool bold = false) -> TepRow& {
		TepRow r; r.num = num; r.name = name; r.unit = unit; r.sub = sub; r.bold = bold;
		t.rows.push_back (r);
		return t.rows.back ();
	};
	auto areaRow = [&] (const std::wstring& num, const std::wstring& name, const Acc& acc, bool sub = false, bool bold = false, const wchar_t* unit = L"м²") {
		TepRow& r = row (num, name, unit, sub, bold);
		for (const auto& s : t.sections) r.cells.push_back (Area (acc.v.count (s) ? acc.v.at (s) : 0, d));
		r.total = Area (acc.Total (), d);
	};
	auto floorsRow = [&] (TepRow& r, const std::map<std::wstring, std::set<int>>& under, bool aboveOnly, bool onlyUnder) {
		std::set<int> vals;
		for (const auto& s : t.sections) {
			const int all = storiesAll.count (s) ? (int) storiesAll.at (s).size () : 0;
			const int un = under.count (s) ? (int) under.at (s).size () : 0;
			const int v = onlyUnder ? un : (aboveOnly ? all - un : all);
			r.cells.push_back (v == 0 ? std::wstring (L"-") : std::to_wstring (v));
			vals.insert (v);
		}
		r.total = vals.size () == 1 ? (*vals.begin () == 0 ? std::wstring (L"-") : std::to_wstring (*vals.begin ())) : std::wstring (L"перем.");
	};

	areaRow (L"1", L"Площадь застройки", build);
	floorsRow (row (L"2", L"Этажность здания", L"эт."), storiesUnder, true, false);
	floorsRow (row (L"2.1", L"Количество этажей,", L"эт."), storiesUnder, false, false);
	floorsRow (row (L"", L"в т.ч. подземных:", L"эт.", true), storiesUnder, false, true);
	{
		Acc above;
		for (const auto& kv : vol.v) above.v[kv.first] = kv.second - (volUnder.v.count (kv.first) ? volUnder.v[kv.first] : 0);
		areaRow (L"3", L"Строительный объем,", vol, false, false, L"м³");
		areaRow (L"", L"в т.ч. подземная часть:", volUnder, true, false, L"м³");
		areaRow (L"", L"в т.ч. надземная часть:", above, true, false, L"м³");
	}
	areaRow (L"4", L"Площадь жилого здания", house);
	areaRow (L"", L"в т.ч. подземная часть:", houseUnder, true, false, L"");
	{
		Acc rest;
		for (const auto& kv : base.v) rest.v[kv.first] = kv.second - (baseTech.v.count (kv.first) ? baseTech.v[kv.first] : 0);
		areaRow (L"5", L"Площадь подвала,", base);
		areaRow (L"", L"в т.ч. площадь тех. помещений:", baseTech, true);
		areaRow (L"", L"в т.ч. площадь подвального помещения и ЛК:", rest, true);
	}
	areaRow (L"6", L"Общая площадь встроенных помещений (админ. помещ.)", builtIn);
	areaRow (L"7", L"Жилая площадь квартир", living);
	areaRow (L"8", L"Площадь квартир (без балконов и лоджий)", aptArea);
	areaRow (L"9", L"Общая площадь квартир (с балконами и лоджиями без понижающего коэффициента)", aptTotal, false, true);
	areaRow (L"10", L"Общая приведенная площадь квартир (с балконами с коэфф. " + FormatNum (c.coefBalcony, 1) + L" и лоджиями с коэфф. " +
				   FormatNum (c.coefLoggia, 1) + L")", aptReduced, false, true);
	areaRow (L"11", L"Площадь индивидуальных кладовых (1 этаж)", storage);
	areaRow (L"12", L"Площадь нежилых помещений (продаваемая)", nonRes);
	{
		TepRow& r = row (L"13", L"Количество нежилых помещений", L"шт.");
		for (const auto& s : t.sections) r.cells.push_back (Count (nonResN.v.count (s) ? nonResN.v.at (s) : 0));
		r.total = Count (nonResN.Total ());
	}
	areaRow (L"14", L"Площадь общедомовых помещений (без подвала)", common);
	{
		Acc sum;
		for (const auto& s : t.sections) {
			auto g = [&s] (const Acc& a) { return a.v.count (s) ? a.v.at (s) : 0.0; };
			sum.v[s] = g (aptReduced) + g (builtIn) + g (nonRes) + g (storage) + g (common);
		}
		areaRow (L"15", L"Общая площадь помещений жилого дома: общая площадь квартир (приведенная) + общая площадь офисов + "
						L"площадь нежилых помещений + площадь индивидуальных кладовых + площадь общедомовых помещений", sum);
	}
	{
		const double all = aptN.Total ();
		TepRow& r = row (L"16", L"Количество квартир, в т.ч.:", L"шт.");
		for (const auto& s : t.sections) r.cells.push_back (Count (aptN.v.count (s) ? aptN.v.at (s) : 0));
		r.total = all > 0 ? std::to_wstring (std::lround (all)) + L" (100%)" : std::wstring (L"-");
		for (const auto& tn : typeNames) {
			TepRow& q = row (L"", tn.second, L"шт.", true);
			int sum = 0;
			for (const auto& s : t.sections) {
				const int n = types.count (s) && types.at (s).count (tn.first) ? types.at (s).at (tn.first) : 0;
				sum += n;
				q.cells.push_back (n == 0 ? std::wstring (L"-") : std::to_wstring (n));
			}
			q.total = std::to_wstring (sum) + L" (" + std::to_wstring (all > 0 ? std::lround (sum * 100.0 / all) : 0) + L"%)";
		}
	}
	{
		TepRow& r = row (L"17", L"Количество жителей (" + FormatNum (c.perResident, 0) + L" м²/чел.), по СП 42.13330.2016 (табл. 5.1)", L"чел.");
		long sum = 0;
		for (const auto& s : t.sections) {
			const long n = std::lround (std::floor ((aptReduced.v.count (s) ? aptReduced.v.at (s) : 0) / c.perResident + 0.5));
			sum += n;
			r.cells.push_back (Count ((double) n));
		}
		r.total = Count ((double) sum);
	}
	{
		TepRow& r = row (L"18", L"Количество работающих (офисы)", L"чел.");
		for (size_t i = 0; i < t.sections.size (); ++i) r.cells.push_back (L"-");
		r.total = L"-";
	}

	// значения вручную
	for (auto& r : t.rows) {
		if (r.num.empty ()) continue;
		const auto it = c.manual.find (r.num);
		if (it == c.manual.end ()) continue;
		bool anySec = false;
		double sum = 0;
		bool numeric = true;
		for (size_t i = 0; i < t.sections.size (); ++i) {
			const auto jt = it->second.find (t.sections[i]);
			if (jt == it->second.end ()) continue;
			r.cells[i] = jt->second;
			anySec = true;
		}
		for (size_t i = 0; i < t.sections.size (); ++i) {
			if (r.cells[i] == L"-") continue;
			const double v = ToNum (r.cells[i], std::nan (""));
			if (std::isnan (v)) { numeric = false; break; }
			sum += v;
		}
		const auto tt = it->second.find (L"всего");
		if (tt != it->second.end ()) r.total = tt->second;
		else if (anySec && numeric)
			r.total = r.unit == L"чел." || r.unit == L"шт." || r.unit == L"эт." ? Count (sum) : Area (sum, d);
	}
	return t;
}

otd::TableLayout LayoutTep (const Tep& t, const std::wstring& title)
{
	std::vector<double> colW = { 14, 110, 16, 26 };
	for (size_t i = 0; i < t.sections.size (); ++i) colW.push_back (22);
	std::vector<otd::GridCell> cells;
	auto add = [&cells] (int r, int c, const std::wstring& s, bool bold = false, bool left = false, double indent = 0) {
		otd::GridCell g; g.r1 = g.r2 = r; g.c1 = g.c2 = c; g.text = s; g.bold = bold; g.left = left; g.indent = indent;
		cells.push_back (g);
	};
	add (0, 0, L"№ п/п", true); add (0, 1, L"Наименование показателя", true); add (0, 2, L"Ед. изм.", true); add (0, 3, L"ВСЕГО", true);
	for (size_t i = 0; i < t.sections.size (); ++i) add (0, 4 + (int) i, L"Секция " + t.sections[i], true);
	int r = 1;
	for (const auto& row : t.rows) {
		add (r, 0, row.num);
		add (r, 1, row.name, row.bold, true, row.sub ? 8.0 : 0.0);
		add (r, 2, row.unit);
		add (r, 3, row.total, row.bold);
		for (size_t i = 0; i < row.cells.size (); ++i) add (r, 4 + (int) i, row.cells[i]);
		++r;
	}
	otd::TableStyle st;
	st.minRow = 6;
	return otd::RenderGrid (cells, r, 1, colW, st, title);
}

xlsx::Sheet TepSheet (const Tep& t, const std::wstring& title)
{
	xlsx::Sheet sh;
	sh.name = L"ТЭП";
	sh.landscape = false;
	const int nc = 4 + (int) t.sections.size ();
	sh.Set (0, 0, title, xlsx::Title);
	sh.Merge (0, 0, 0, nc - 1);
	sh.Set (2, 0, L"№ п/п", xlsx::Head); sh.Set (2, 1, L"Наименование показателя", xlsx::Head);
	sh.Set (2, 2, L"Ед. изм.", xlsx::Head); sh.Set (2, 3, L"ВСЕГО", xlsx::Head);
	for (size_t i = 0; i < t.sections.size (); ++i) sh.Set (2, 4 + (int) i, L"Секция " + t.sections[i], xlsx::Head);
	sh.colWidth[0] = 7; sh.colWidth[1] = 55; sh.colWidth[2] = 8; sh.colWidth[3] = 13;
	for (int c = 4; c < nc; ++c) sh.colWidth[c] = 11;
	int r = 3;
	for (const auto& row : t.rows) {
		sh.Set (r, 0, row.num, xlsx::Text);
		sh.Set (r, 1, (row.sub ? L"      " : L"") + row.name, xlsx::TextLeft);
		sh.Set (r, 2, row.unit, xlsx::Text);
		sh.Set (r, 3, row.total, xlsx::Text);
		for (size_t i = 0; i < row.cells.size (); ++i) sh.Set (r, 4 + (int) i, row.cells[i], xlsx::Text);
		++r;
	}
	return sh;
}

xlsx::Sheet ApartmentsSheet (const std::vector<Room>& rooms, const std::vector<Apartment>& apts, const Config& c)
{
	xlsx::Sheet sh;
	sh.name = L"Квартиры";
	const wchar_t* head[] = { L"Секция", L"Этаж", L"Квартира", L"Тип", L"Вид квартиры", L"Жилая, м²", L"Площадь квартиры, м²",
							  L"Общая (без коэф.), м²", L"Приведённая, м²", L"Помещения (площадь округлённая; летние — × коэф.)" };
	for (int i = 0; i < 10; ++i) sh.Set (0, i, head[i], xlsx::Head);
	const double w[] = { 8, 14, 10, 6, 30, 10, 12, 12, 12, 90 };
	for (int i = 0; i < 10; ++i) sh.colWidth[i] = w[i];
	int r = 1;
	for (const auto& a : apts) {
		sh.Set (r, 0, a.section, xlsx::Text);
		sh.Set (r, 1, a.storyName, xlsx::Text);
		sh.Set (r, 2, a.code, xlsx::Text);
		sh.Set (r, 3, a.label, xlsx::Text);
		sh.Set (r, 4, a.typeName, xlsx::TextLeft);
		sh.SetNum (r, 5, a.living); sh.SetNum (r, 6, a.area); sh.SetNum (r, 7, a.total); sh.SetNum (r, 8, a.reduced);
		std::wstring list;
		for (size_t i : a.rooms) {
			const Room& q = rooms[i];
			std::wstring s = (q.number.empty () ? std::wstring () : q.number + L" ") + q.name + L" " + FormatNum (RoundTo (q.area, c.dec), c.dec);
			if (c.balcony.Has (q.catCode)) s += L" × " + FormatNum (c.coefBalcony, 1);
			else if (c.loggia.Has (q.catCode)) s += L" × " + FormatNum (c.coefLoggia, 1);
			list += (list.empty () ? L"" : L"; ") + s;
		}
		sh.Set (r, 9, list, xlsx::TextLeft);
		++r;
	}
	return sh;
}

}	// namespace tep
