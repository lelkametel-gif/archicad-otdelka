// Ведомость помещений (Excel): квартиры с площадями, общедомовые помещения по секциям, нежилые помещения
#include "PremisesReport.hpp"
#include "FinishCore.hpp"

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>

namespace prem {

using tep::Room;
using tep::Apartment;
using tep::Config;

int FloorNo (const std::wstring& storyName, int story)
{
	for (size_t i = 0; i < storyName.size (); ++i) {
		if (!std::iswdigit (storyName[i])) continue;
		const bool neg = i > 0 && (storyName[i - 1] == L'-' || storyName[i - 1] == L'−');
		int v = 0;
		while (i < storyName.size () && std::iswdigit (storyName[i])) v = v * 10 + (storyName[i++] - L'0');
		return neg ? -v : v;
	}
	return story;
}


bool NumLess (const std::wstring& a, const std::wstring& b)
{
	auto lead = [] (const std::wstring& s, long& n) {
		size_t i = 0;
		while (i < s.size () && std::iswspace (s[i])) ++i;
		if (i >= s.size () || !std::iswdigit (s[i])) return false;
		n = 0;
		while (i < s.size () && std::iswdigit (s[i])) n = n * 10 + (s[i++] - L'0');
		return true;
	};
	long na = 0, nb = 0;
	const bool ha = lead (a, na), hb = lead (b, nb);
	if (ha && hb && na != nb) return na < nb;
	if (ha != hb) return ha;
	return a < b;
}


namespace {

const std::wstring kKitchenZone = L"1021";		// «Зона кухни» — столбец 11 (в том числе зона кухни)

std::wstring OneLine (std::wstring s)
{
	for (auto& ch : s) if (ch == L'\n' || ch == L'\r') ch = L' ';
	return otd::Trim (s);
}

std::string Ref (int r, int c) { return xlsx::ColName (c) + std::to_string (r + 1); }
std::string Range (int r1, int c, int r2) { return Ref (r1, c) + ":" + Ref (r2, c); }

bool IsHeaderCode (const std::wstring& code)
{
	return code.size () <= 1 || (code.size () == 4 && code.substr (2) == L"00");
}

void SetCodeCell (xlsx::Sheet& sh, int r, int c, const std::wstring& code)
{
	bool digits = !code.empty () && code.size () < 10;
	for (wchar_t ch : code) if (!std::iswdigit (ch)) digits = false;
	if (digits) sh.SetNum (r, c, (double) std::stol (code), xlsx::Text);
	else sh.Set (r, c, code, xlsx::Text);
}

// пустые ячейки блока с рамкой
void Frame (xlsx::Sheet& sh, int r1, int r2, int c1, int c2, std::set<std::pair<int, int>>& used)
{
	for (int r = r1; r <= r2; ++r)
		for (int c = c1; c <= c2; ++c)
			if (!used.count ({ r, c })) sh.Set (r, c, L"", xlsx::Text);
}

struct Tracker {
	xlsx::Sheet& sh;
	std::set<std::pair<int, int>> used;
	void Text (int r, int c, const std::wstring& t, int st = xlsx::Text) { sh.Set (r, c, t, st); used.insert ({ r, c }); }
	void Num (int r, int c, double v, int st = xlsx::Num1) { sh.SetNum (r, c, v, st); used.insert ({ r, c }); }
	void Code (int r, int c, const std::wstring& code) { SetCodeCell (sh, r, c, code); used.insert ({ r, c }); }
	void F (int r, int c, const std::string& f, double v, int st = xlsx::Num1) { sh.SetFormula (r, c, f, v, st); used.insert ({ r, c }); }
};

double Sum (const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return s; }

// ------------------------------------------------------------------ квартиры
xlsx::Sheet ApartmentSheet (const std::vector<Room>& rooms, std::vector<Apartment> apts, const Config& c, Info& info)
{
	xlsx::Sheet sh;
	sh.name = L"Квартиры с площ.";
	Tracker t { sh, {} };
	const double w[] = { 9, 13, 9, 9, 11, 11, 10, 9, 10, 10, 13, 22, 10, 15, 10, 11, 11, 9, 10, 11 };
	for (int i = 0; i < 20; ++i) sh.colWidth[i] = w[i];
	t.Text (0, 0, L"Квартиры с площадями помещений", xlsx::Title);
	sh.Merge (0, 0, 0, 17);
	const wchar_t* top[] = { L"№ квартиры", L"Назначение", L"Этаж расположения", L"Номер подъезда", L"Общая приведенная площадь, м2",
							 L"Общая площадь-1, м2", L"Жилая площадь, м2", L"Количество комнат", L"Площадь комнат", L"", L"",
							 L"Площадь помещений вспомогательного использования", L"",
							 L"Площадь веранд (с коэф.1), лоджий (с коэф. 0,5), балконов и террас (с коэф. 0,3), м2", L"",
							 L"Площадь веранд, лоджий, балконов и террас без коэффициента", L"Общая площадь-2, м2", L"Высота потолков" };
	for (int i = 0; i < 18; ++i) t.Text (1, i, top[i], xlsx::Head);
	t.Text (2, 8, L"Условный номер комнаты", xlsx::Head);
	t.Text (2, 9, L"Площадь, м2", xlsx::Head);
	t.Text (2, 10, L"Площадь, м2, в том числе зона кухни", xlsx::Head);
	t.Text (2, 11, L"Наименование помещения", xlsx::Head);
	t.Text (2, 12, L"Площадь, м2", xlsx::Head);
	t.Text (2, 13, L"Наименование", xlsx::Head);
	t.Text (2, 14, L"Площадь, м2", xlsx::Head);
	for (int i : { 0, 1, 2, 3, 4, 5, 6, 7, 15, 16, 17 }) sh.Merge (1, i, 2, i);
	sh.Merge (1, 8, 1, 10); sh.Merge (1, 11, 1, 12); sh.Merge (1, 13, 1, 14);
	for (int i = 0; i < 18; ++i) t.Num (3, i, i + 1, xlsx::Head);
	Frame (sh, 1, 3, 0, 17, t.used);
	sh.rowHeight[1] = 75; sh.rowHeight[2] = 62;
	sh.freezeRows = 4;

	std::stable_sort (apts.begin (), apts.end (), [] (const Apartment& p, const Apartment& q) {
		if (p.section != q.section) return tep::SortSections ({ p.section, q.section }).front () == p.section;
		if (p.story != q.story) return p.story < q.story;
		return NumLess (p.code, q.code);
	});

	int r = 4;
	std::wstring curSec = L"\x1";
	int curStory = -100000;
	std::vector<std::pair<std::wstring, std::pair<int, int>>> secRows;		// секция -> строки
	for (const Apartment& a : apts) {
		std::vector<const Room*> living, kz, aux, summer;
		double height = 0, best = -1, bestAny = -1, heightAny = 0;		// высота — самой большой жилой комнаты
		for (size_t i : a.rooms) {
			const Room& rm = rooms[i];
			if (c.balcony.Has (rm.catCode) || c.loggia.Has (rm.catCode)) summer.push_back (&rm);
			else if (c.living.Has (rm.catCode)) living.push_back (&rm);
			else if (rm.catCode == kKitchenZone) kz.push_back (&rm);
			else aux.push_back (&rm);
			if (c.living.Has (rm.catCode) && rm.area > best) { best = rm.area; height = rm.height; }
			if (rm.area > bestAny) { bestAny = rm.area; heightAny = rm.height; }
		}
		if (best < 0) height = heightAny;
		auto byCode = [] (const Room* p, const Room* q) { return p->catCode != q->catCode ? p->catCode < q->catCode : NumLess (p->number, q->number); };
		std::stable_sort (living.begin (), living.end (), byCode);
		std::stable_sort (aux.begin (), aux.end (), byCode);
		std::stable_sort (summer.begin (), summer.end (), byCode);
		const bool studio = a.label == c.studioLabel;
		const int nLiv = studio ? 1 : (int) living.size ();
		const int n = std::max ({ 1, nLiv, (int) aux.size (), (int) summer.size () });
		const int r1 = r, r2 = r + n - 1;
		if (a.section != curSec) {
			if (!secRows.empty ()) secRows.back ().second.second = r1 - 1;
			secRows.push_back ({ a.section, { r1, r1 } });
			t.Text (r1, 19, L"СЕКЦИЯ " + a.section, xlsx::Bold);
			curSec = a.section;
			curStory = -100000;
		}
		if (a.story != curStory) {
			t.Text (r1, 18, std::to_wstring (FloorNo (a.storyName, a.story)) + L" этаж", xlsx::Plain);
			curStory = a.story;
		}
		t.Code (r1, 0, a.code);
		t.Text (r1, 1, studio ? L"Квартира-студия" : L"Квартира");
		t.Num (r1, 2, FloorNo (a.storyName, a.story), xlsx::Text);
		SetCodeCell (sh, r1, 3, a.section); t.used.insert ({ r1, 3 });
		t.Num (r1, 7, std::max (1, (int) living.size ()), xlsx::Text);

		std::vector<double> livA, auxA, sumK, sumP;
		for (const Room* p : living) livA.push_back (tep::RoundTo (p->area, c.dec));
		double kzA = 0;
		for (const Room* p : kz) kzA += tep::RoundTo (p->area, c.dec);
		kzA = tep::RoundTo (kzA, c.dec);
		for (const Room* p : aux) auxA.push_back (tep::RoundTo (p->area, c.dec));
		for (const Room* p : summer) {
			const double k = c.balcony.Has (p->catCode) ? c.coefBalcony : c.coefLoggia;
			sumK.push_back (tep::RoundTo (p->area * k, c.dec));
			sumP.push_back (tep::RoundTo (p->area, c.dec));
		}
		const double G = tep::RoundTo (Sum (livA), c.dec);
		// комнаты
		if (studio) {
			t.Num (r1, 6, G);
			t.Num (r1, 8, 1, xlsx::Text);
			t.F (r1, 9, "SUM(" + Ref (r1, 6) + "," + Ref (r1, 10) + ")", tep::RoundTo (G + kzA, c.dec));
		} else {
			for (size_t i = 0; i < living.size (); ++i) {
				t.Num (r1 + (int) i, 8, (double) (i + 1), xlsx::Text);
				t.Num (r1 + (int) i, 9, livA[i]);
			}
			t.F (r1, 6, "SUM(" + Range (r1, 9, r2) + ")", G);
		}
		if (!kz.empty ()) t.Num (r1, 10, kzA);
		for (size_t i = 0; i < aux.size (); ++i) {
			t.Text (r1 + (int) i, 11, OneLine (aux[i]->name), xlsx::TextLeft);
			t.Num (r1 + (int) i, 12, auxA[i]);
		}
		for (size_t i = 0; i < summer.size (); ++i) {
			t.Text (r1 + (int) i, 13, OneLine (summer[i]->name));
			t.Num (r1 + (int) i, 14, sumK[i]);
			t.Num (r1 + (int) i, 15, sumP[i]);
		}
		const double F = tep::RoundTo (Sum (auxA) + G + kzA, c.dec);
		const std::string fF = "SUM(" + Range (r1, 12, r2) + ")+" + (studio ? Ref (r1, 9) : Ref (r1, 6) + "+" + Ref (r1, 10));
		t.F (r1, 5, fF, F);
		t.F (r1, 4, Ref (r1, 5) + "+SUM(" + Range (r1, 14, r2) + ")", tep::RoundTo (F + Sum (sumK), c.dec));
		t.F (r1, 16, Ref (r1, 5) + "+SUM(" + Range (r1, 15, r2) + ")", tep::RoundTo (F + Sum (sumP), c.dec));
		if (height > 0) t.Num (r1, 17, tep::RoundTo (height, 2), xlsx::Num2B);
		Frame (sh, r1, r2, 0, 17, t.used);
		if (n > 1) {
			for (int col : { 0, 1, 2, 3, 4, 5, 6, 7, 10, 16, 17 }) sh.Merge (r1, col, r2, col);
			if (studio) { sh.Merge (r1, 8, r2, 8); sh.Merge (r1, 9, r2, 9); }
			if (summer.size () <= 1) for (int col : { 13, 14, 15 }) sh.Merge (r1, col, r2, col);
		}
		r = r2 + 1;
		++info.apartments;
	}
	if (!secRows.empty ()) secRows.back ().second.second = r - 1;
	info.apartmentRows = r - 4;
	// итоги по секциям
	if (!secRows.empty ()) {
		r += 1;
		const int t1 = r;
		for (const auto& s : secRows) {
			t.Text (r, 3, s.first + L" секция", xlsx::BoldText);
			for (int col : { 4, 5, 6, 16 }) {
				double v = 0;
				for (const auto& cell : sh.cells)
					if (cell.col == col && cell.row >= s.second.first && cell.row <= s.second.second && cell.isNum) v += cell.num;
				t.F (r, col, "SUM(" + Range (s.second.first, col, s.second.second) + ")", tep::RoundTo (v, c.dec), xlsx::BoldNum1);
			}
			++r;
		}
		t.Text (r, 3, L"Итого", xlsx::BoldText);
		for (int col : { 4, 5, 6, 16 }) {
			double v = 0;
			for (const auto& cell : sh.cells)
				if (cell.col == col && cell.row >= t1 && cell.row < r && cell.isNum) v += cell.num;
			t.F (r, col, "SUM(" + Range (t1, col, r - 1) + ")", tep::RoundTo (v, c.dec), xlsx::BoldNum1);
		}
		t.Text (t1 - 1, 1, L"Итоги по секциям:", xlsx::Bold);
	}
	return sh;
}

// ------------------------------------------------------------------ общедомовые по секциям
std::vector<xlsx::Sheet> CommonSheets (const std::vector<Room>& rooms, const std::vector<Apartment>& apts, const Config& c, Info& info)
{
	std::map<std::wstring, int> topApt;		// секция -> верхний этаж с квартирами
	for (const Apartment& a : apts) {
		auto it = topApt.find (a.section);
		if (it == topApt.end () || a.story > it->second) topApt[a.section] = a.story;
	}
	std::map<std::wstring, std::vector<const Room*>> bySec;
	for (const Room& rm : rooms) {
		if (c.excluded.Has (rm.catCode) || IsHeaderCode (rm.catCode)) continue;
		if (!c.common.Has (rm.catCode) && !c.basement.Has (rm.catCode)) continue;
		bySec[rm.section].push_back (&rm);
	}
	std::vector<std::wstring> secs;
	for (const auto& kv : bySec) secs.push_back (kv.first);
	secs = tep::SortSections (secs);
	std::vector<xlsx::Sheet> res;
	for (const auto& sec : secs) {
		auto list = bySec[sec];
		std::stable_sort (list.begin (), list.end (), [] (const Room* p, const Room* q) {
			if (p->story != q->story) return p->story < q->story;
			if (p->number != q->number) return NumLess (p->number, q->number);
			return p->name < q->name;
		});
		xlsx::Sheet sh;
		sh.name = L"Общедомовые " + (sec.empty () ? std::wstring (L"(без секции)") : sec + L" секция");
		Tracker t { sh, {} };
		const double w[] = { 7, 45, 26, 18, 12 };
		for (int i = 0; i < 5; ++i) sh.colWidth[i] = w[i];
		const wchar_t* head[] = { L"№ п\\п", L"Вид помещения", L"Описание места расположения помещения", L"Назначение помещения", L"Площадь, м2" };
		for (int i = 0; i < 5; ++i) t.Text (0, i, head[i], xlsx::Head);
		t.Text (2, 4, L"Подъезд " + sec, xlsx::Head);
		for (int i = 0; i < 4; ++i) sh.Merge (0, i, 2, i);
		sh.Merge (0, 4, 1, 4);
		for (int i = 0; i < 5; ++i) t.Num (3, i, i + 1, xlsx::Head);
		Frame (sh, 0, 3, 0, 4, t.used);
		sh.freezeRows = 4;
		sh.landscape = false;
		int r = 4, n = 0;
		double total = 0;
		const auto top = topApt.find (sec);
		for (const Room* p : list) {
			std::wstring where;
			if (p->underground) where = L"подвальное помещение";
			else if (top != topApt.end () && p->story > top->second) where = L"Выход на кровлю";
			else where = std::to_wstring (FloorNo (p->storyName, p->story)) + L" этаж";
			const bool tech = c.basement.Has (p->catCode) && p->catCode != L"3030";
			t.Num (r, 0, ++n, xlsx::Text);
			t.Text (r, 1, OneLine (p->name), xlsx::TextLeft);
			t.Text (r, 2, where);
			t.Text (r, 3, tech ? L"Техническое" : L"Общественное");
			const double ar = tep::RoundTo (p->area, c.dec);
			t.Num (r, 4, ar);
			total += ar;
			++r;
		}
		t.Text (r, 3, L"Итого", xlsx::BoldText);
		t.F (r, 4, "SUM(" + Range (4, 4, r - 1) + ")", tep::RoundTo (total, c.dec), xlsx::BoldNum1);
		info.common += n;
		res.push_back (sh);
	}
	return res;
}

// ------------------------------------------------------------------ нежилые
xlsx::Sheet NonResSheet (const std::vector<Room>& rooms, std::vector<Apartment> premises, const Config& c, Info& info)
{
	xlsx::Sheet sh;
	sh.name = L"Нежилые";
	Tracker t { sh, {} };
	const double w[] = { 11, 34, 10, 10, 12, 32, 11, 10 };
	for (int i = 0; i < 8; ++i) sh.colWidth[i] = w[i];
	auto header = [&] (int r) {
		const wchar_t* head[] = { L"Условный номер", L"Назначение", L"Этаж расположения", L"Номер подъезда", L"Общая площадь, м2",
								  L"Площадь частей нежилого помещения", L"", L"Высота потолков" };
		for (int i = 0; i < 8; ++i) t.Text (r, i, head[i], xlsx::Head);
		t.Text (r + 1, 5, L"Наименование помещения", xlsx::Head);
		t.Text (r + 1, 6, L"Площадь, м2", xlsx::Head);
		for (int i : { 0, 1, 2, 3, 4, 7 }) sh.Merge (r, i, r + 1, i);
		sh.Merge (r, 5, r, 6);
		for (int i = 0; i < 8; ++i) t.Num (r + 2, i, i + 1, xlsx::Head);
		Frame (sh, r, r + 2, 0, 7, t.used);
		sh.rowHeight[r] = 30;
		return r + 3;
	};
	std::stable_sort (premises.begin (), premises.end (), [] (const Apartment& p, const Apartment& q) {
		if (p.section != q.section) return tep::SortSections ({ p.section, q.section }).front () == p.section;
		if (p.story != q.story) return p.story < q.story;
		return NumLess (p.code, q.code);
	});
	int r = 0;
	if (!premises.empty ()) {
		t.Text (r, 0, L"Нежилые помещения для коммерческого использования", xlsx::Bold);
		r = header (r + 1);
		const int d1 = r;
		double total = 0;
		for (const Apartment& a : premises) {
			std::vector<const Room*> list;
			for (size_t i : a.rooms) list.push_back (&rooms[i]);
			std::stable_sort (list.begin (), list.end (), [] (const Room* p, const Room* q) {
				return p->area != q->area ? p->area > q->area : NumLess (p->number, q->number);		// основное — первым
			});
			const int r1 = r, r2 = r + std::max (1, (int) list.size ()) - 1;
			t.Code (r1, 0, a.code);
			t.Text (r1, 1, L"Нежилое помещение для коммерческого использования");
			t.Num (r1, 2, FloorNo (a.storyName, a.story), xlsx::Text);
			SetCodeCell (sh, r1, 3, a.section); t.used.insert ({ r1, 3 });
			double sum = 0;
			for (size_t i = 0; i < list.size (); ++i) {
				const double ar = tep::RoundTo (list[i]->area, c.dec);
				t.Text (r1 + (int) i, 5, OneLine (list[i]->name), xlsx::TextLeft);
				t.Num (r1 + (int) i, 6, ar);
				sum += ar;
			}
			t.F (r1, 4, "SUM(" + Range (r1, 6, r2) + ")", tep::RoundTo (sum, c.dec));
			if (a.height > 0) t.Num (r1, 7, tep::RoundTo (a.height, 2), xlsx::Num2B);
			Frame (sh, r1, r2, 0, 7, t.used);
			if (r2 > r1) for (int col : { 0, 1, 2, 3, 4, 7 }) sh.Merge (r1, col, r2, col);
			total += sum;
			r = r2 + 1;
			++info.offices;
		}
		t.Text (r, 3, L"Итого", xlsx::BoldText);
		t.F (r, 4, "SUM(" + Range (d1, 4, r - 1) + ")", tep::RoundTo (total, c.dec), xlsx::BoldNum1);
		r += 3;
	}
	// кладовые, колясочные и др. нежилые (по одной зоне)
	std::vector<const Room*> st;
	for (const Room& rm : rooms)
		if (!c.excluded.Has (rm.catCode) && !IsHeaderCode (rm.catCode) && (c.storage.Has (rm.catCode) || c.nonResidential.Has (rm.catCode)))
			st.push_back (&rm);
	if (!st.empty ()) {
		std::stable_sort (st.begin (), st.end (), [] (const Room* p, const Room* q) {
			if (p->section != q->section) return tep::SortSections ({ p->section, q->section }).front () == p->section;
			const std::wstring a = otd::Trim (p->apartment).empty () ? p->number : p->apartment;
			const std::wstring b = otd::Trim (q->apartment).empty () ? q->number : q->apartment;
			if (a != b) return NumLess (a, b);
			return p->story < q->story;
		});
		t.Text (r, 0, L"Нежилые помещения (кладовые, колясочные)", xlsx::Bold);
		r = header (r + 1);
		const int d1 = r;
		double total = 0;
		for (const Room* p : st) {
			const std::wstring code = otd::Trim (p->apartment).empty () ? p->number : otd::Trim (p->apartment);
			const double ar = tep::RoundTo (p->area, c.dec);
			t.Code (r, 0, code);
			t.Text (r, 1, L"Нежилое помещение");
			t.Num (r, 2, FloorNo (p->storyName, p->story), xlsx::Text);
			SetCodeCell (sh, r, 3, p->section); t.used.insert ({ r, 3 });
			t.Num (r, 4, ar);
			t.Text (r, 5, OneLine (p->name), xlsx::TextLeft);
			t.F (r, 6, Ref (r, 4), ar);
			if (p->height > 0) t.Num (r, 7, tep::RoundTo (p->height, 2), xlsx::Num2B);
			total += ar;
			++r;
			++info.storages;
		}
		t.Text (r, 5, L"Итого", xlsx::BoldText);
		t.F (r, 6, "SUM(" + Range (d1, 6, r - 1) + ")", tep::RoundTo (total, c.dec), xlsx::BoldNum1);
	}
	if (premises.empty () && st.empty ())
		t.Text (0, 0, L"Нежилых помещений нет (категории встроенных помещений с номером «Квартира», кладовые, нежилые).", xlsx::Plain);
	return sh;
}

}	// namespace


std::vector<xlsx::Sheet> PremisesSheets (const std::vector<Room>& rooms, const std::vector<Apartment>& apts,
										 const std::vector<Apartment>& premises, const Config& c, Info* infoOut)
{
	Info info;
	std::vector<xlsx::Sheet> res;
	res.push_back (ApartmentSheet (rooms, apts, c, info));
	for (auto& s : CommonSheets (rooms, apts, c, info)) res.push_back (s);
	res.push_back (NonResSheet (rooms, premises, c, info));
	std::set<std::wstring> secs;
	for (const auto& a : apts) secs.insert (a.section);
	info.sections = tep::SortSections (std::vector<std::wstring> (secs.begin (), secs.end ()));
	if (infoOut) *infoOut = info;
	return res;
}

}	// namespace prem
