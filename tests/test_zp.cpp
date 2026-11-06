// g++ -std=c++17 -ISrc -o /tmp/tz tests/test_zp.cpp Src/ZoneParamsFile.cpp && /tmp/tz
#include "ZoneParamsFile.hpp"
#include <cstdio>
#include <cmath>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf ("FAIL %d %s\n", __LINE__, #c); } } while (0)
using namespace zp;
int main ()
{
	ZoneRec a; a.guid = L"AAAA-1"; a.story = -1; a.number = L"001"; a.name = L"Коридор\tс табом";
	Par s; s.kind = 's'; s.name = L"asAddOn_stApartmentCode"; s.str = L"115\\x\nй"; a.pars.push_back (s);
	Par n; n.kind = 'n'; n.name = L"TextSize"; n.num = 2.5; a.pars.push_back (n);
	Par ar; ar.kind = 'a'; ar.name = L"compOffX"; ar.dim1 = 2; ar.dim2 = 3; ar.arr = { 0, 1.25, -3e-5, 4, 5, 6 }; a.pars.push_back (ar);
	ZoneRec b; b.guid = L"BBBB-2"; b.story = 2; b.number = L"201"; b.name = L"Комната";
	ZoneRec c = b; c.guid = L"CCCC-3";
	const std::wstring t = Write ({ a, b, c }, L"проект Х");
	std::vector<std::wstring> w;
	const auto r = Read (t + L"мусор\n", &w);
	CHECK (r.size () == 3 && w.size () == 1);
	CHECK (r[0].name == a.name && r[0].story == -1 && r[0].pars.size () == 3);
	CHECK (r[0].pars[0].str == s.str);
	CHECK (std::fabs (r[0].pars[1].num - 2.5) < 1e-12);
	CHECK (r[0].pars[2].dim1 == 2 && r[0].pars[2].dim2 == 3 && r[0].pars[2].arr.size () == 6 && std::fabs (r[0].pars[2].arr[2] + 3e-5) < 1e-15);
	bool g = false;
	CHECK (FindRec (r, L"BBBB-2", 0, L"", L"", &g) == 1 && g);
	CHECK (FindRec (r, L"new", -1, L"001", a.name, &g) == 0 && !g);
	CHECK (FindRec (r, L"new", 2, L"201", L"Комната") == -1);		// два одинаковых — не угадываем
	CHECK (Skip (L"A") && Skip (L"ROOM_NAME") && Skip (L"asAddOn_CalcArea") && !Skip (L"asAddOn_stApartmentCode") && !Skip (L"AC_TextFont_1"));
	std::printf ("%d проверок, ошибок: %d\n", checks, fails);
	return fails ? 1 : 0;
}
