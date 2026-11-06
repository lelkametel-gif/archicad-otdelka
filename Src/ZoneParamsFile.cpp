// Файл параметров зон
#include "ZoneParamsFile.hpp"

#include <cstdio>
#include <cwchar>
#include <sstream>

namespace zp {

static std::wstring Esc (const std::wstring& s)
{
	std::wstring r;
	for (wchar_t c : s) {
		if (c == L'\\') r += L"\\\\";
		else if (c == L'\t') r += L"\\t";
		else if (c == L'\n') r += L"\\n";
		else if (c == L'\r') r += L"\\r";
		else r += c;
	}
	return r;
}

static std::wstring Unesc (const std::wstring& s)
{
	std::wstring r;
	for (size_t i = 0; i < s.size (); ++i) {
		if (s[i] == L'\\' && i + 1 < s.size ()) {
			const wchar_t n = s[++i];
			r += n == L't' ? L'\t' : n == L'n' ? L'\n' : n == L'r' ? L'\r' : n;
		} else
			r += s[i];
	}
	return r;
}

static std::wstring Num (double v)
{
	wchar_t b[64];
	std::swprintf (b, 64, L"%.12g", v);
	return b;
}

static double ToD (const std::wstring& s)
{
	try { return std::stod (s); } catch (...) { return 0; }
}

static std::vector<std::wstring> Split (const std::wstring& s, wchar_t sep)
{
	std::vector<std::wstring> r;
	std::wstring cur;
	for (wchar_t c : s) {
		if (c == sep) { r.push_back (cur); cur.clear (); }
		else cur += c;
	}
	r.push_back (cur);
	return r;
}

std::wstring Write (const std::vector<ZoneRec>& zones, const std::wstring& header)
{
	std::wstring out = L"# Параметры зон — надстройка «Ведомость отделки». " + Esc (header) + L"\n"
					   L"# Z: GUID, индекс этажа, номер, имя; затем параметры: S — строка, N — число, A — массив чисел (размеры, значения через ;)\n";
	for (const auto& z : zones) {
		out += L"Z\t" + z.guid + L"\t" + std::to_wstring (z.story) + L"\t" + Esc (z.number) + L"\t" + Esc (z.name) + L"\n";
		for (const auto& p : z.pars) {
			if (p.kind == 's') out += L"S\t" + p.name + L"\t" + Esc (p.str) + L"\n";
			else if (p.kind == 'n') out += L"N\t" + p.name + L"\t" + Num (p.num) + L"\n";
			else {
				std::wstring v;
				for (size_t i = 0; i < p.arr.size (); ++i) v += (i ? L";" : L"") + Num (p.arr[i]);
				out += L"A\t" + p.name + L"\t" + std::to_wstring (p.dim1) + L"\t" + std::to_wstring (p.dim2) + L"\t" + v + L"\n";
			}
		}
	}
	return out;
}

std::vector<ZoneRec> Read (const std::wstring& text, std::vector<std::wstring>* warnings)
{
	std::vector<ZoneRec> res;
	std::wistringstream in (text);
	std::wstring line;
	int lineNo = 0;
	while (std::getline (in, line)) {
		++lineNo;
		if (!line.empty () && line.back () == L'\r') line.pop_back ();
		if (!line.empty () && line[0] == 0xFEFF) line.erase (0, 1);
		if (line.empty () || line[0] == L'#') continue;
		const auto f = Split (line, L'\t');
		if (f[0] == L"Z" && f.size () >= 5) {
			ZoneRec z;
			z.guid = f[1];
			z.story = (int) ToD (f[2]);
			z.number = Unesc (f[3]);
			z.name = Unesc (f[4]);
			res.push_back (z);
		} else if (!res.empty () && f[0] == L"S" && f.size () >= 3) {
			Par p; p.kind = 's'; p.name = f[1]; p.str = Unesc (f[2]);
			res.back ().pars.push_back (p);
		} else if (!res.empty () && f[0] == L"N" && f.size () >= 3) {
			Par p; p.kind = 'n'; p.name = f[1]; p.num = ToD (f[2]);
			res.back ().pars.push_back (p);
		} else if (!res.empty () && f[0] == L"A" && f.size () >= 5) {
			Par p; p.kind = 'a'; p.name = f[1]; p.dim1 = (int) ToD (f[2]); p.dim2 = (int) ToD (f[3]);
			if (!f[4].empty ()) for (const auto& v : Split (f[4], L';')) p.arr.push_back (ToD (v));
			res.back ().pars.push_back (p);
		} else if (warnings)
			warnings->push_back (L"Файл параметров зон, строка " + std::to_wstring (lineNo) + L" не понята — пропущена.");
	}
	return res;
}

bool Skip (const std::wstring& n)
{
	std::wstring l;
	for (wchar_t c : n) l += (c >= L'A' && c <= L'Z') ? (wchar_t) (c - L'A' + L'a') : c;
	return l == L"a" || l == L"b" || l == L"asaddon_calcarea" || l == L"asaddon_floorarea" || l == L"asaddon_fillarea" ||
		   l.compare (0, 5, L"room_") == 0 || l.compare (0, 5, L"glob_") == 0;
}

int FindRec (const std::vector<ZoneRec>& recs, const std::wstring& guid, int story, const std::wstring& number,
			 const std::wstring& name, bool* byGuid)
{
	if (byGuid) *byGuid = false;
	for (size_t i = 0; i < recs.size (); ++i)
		if (recs[i].guid == guid) { if (byGuid) *byGuid = true; return (int) i; }
	int found = -1;
	for (size_t i = 0; i < recs.size (); ++i) {
		const auto& r = recs[i];
		if (r.story == story && r.number == number && r.name == name && !number.empty ()) {
			if (found >= 0) return -1;		// несколько одинаковых — не угадываем
			found = (int) i;
		}
	}
	return found;
}

}	// namespace zp
