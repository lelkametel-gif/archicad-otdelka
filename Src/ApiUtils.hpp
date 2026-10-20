// Вспомогательные функции для работы с API: строки и параметры библиотечных элементов
#ifndef APIUTILS_HPP
#define APIUTILS_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"

#include <cmath>
#include <string>
#include <vector>

namespace au {

inline std::wstring ToW (const GS::UniString& u)
{
	std::wstring r;
	const USize n = u.GetLength ();
	r.reserve (n);
	auto us = u.ToUStr ();
	const GS::uchar_t* p = us;
	for (USize i = 0; i < n; ++i)
		r.push_back ((wchar_t) p[i]);
	return r;
}


inline GS::UniString FromW (const std::wstring& w)
{
	std::vector<GS::uchar_t> buf;
	buf.reserve (w.size () + 1);
	for (wchar_t c : w)
		buf.push_back ((GS::uchar_t) c);
	buf.push_back (0);
	return GS::UniString (buf.data ());
}


inline std::wstring Lower (const std::wstring& s)
{
	return ToW (FromW (s).ToLowerCase ());
}


// ------------------------------------------------------------ параметры (API_AddParType**)
class Params {
public:
	explicit Params (API_AddParType** h) : hdl (h)
	{
		n = (h == nullptr) ? 0 : (Int32) ((size_t) BMGetHandleSize ((GSConstHandle) h) / sizeof (API_AddParType));
	}

	Int32 Count () const { return n; }

	API_AddParType* Find (const char* name) const
	{
		for (Int32 i = 0; i < n; ++i) {
			if (CHCompareCStrings ((*hdl)[i].name, name, CS_CaseInsensitive) == 0)
				return &(*hdl)[i];
		}
		return nullptr;
	}

	bool Has (const char* name) const { return Find (name) != nullptr; }

	double Real (const char* name, double def = 0) const
	{
		API_AddParType* p = Find (name);
		if (p == nullptr || p->typeMod == API_ParArray || p->typeID == APIParT_CString)
			return def;
		return p->value.real;
	}

	int Int (const char* name, int def = 0) const
	{
		return (int) std::lround (Real (name, def));
	}

	bool Bool (const char* name, bool def = false) const
	{
		return Real (name, def ? 1.0 : 0.0) > 0.5;
	}

	GS::UniString Str (const char* name) const
	{
		API_AddParType* p = Find (name);
		if (p == nullptr || p->typeMod == API_ParArray || p->typeID != APIParT_CString)
			return GS::UniString ();
		return GS::UniString (p->value.uStr);
	}

	// ---- запись
	bool SetReal (const char* name, double v)
	{
		API_AddParType* p = Find (name);
		if (p == nullptr || p->typeMod == API_ParArray || p->typeID == APIParT_CString)
			return false;
		p->value.real = v;
		return true;
	}

	bool SetStr (const char* name, const GS::UniString& v)
	{
		API_AddParType* p = Find (name);
		if (p == nullptr || p->typeMod == API_ParArray || p->typeID != APIParT_CString)
			return false;
		GS::ucsncpy (p->value.uStr, v.ToUStr (), API_UAddParStrLen - 1);
		p->value.uStr[API_UAddParStrLen - 1] = 0;
		return true;
	}

	// двумерный массив чисел rows x cols
	bool SetRealArray (const char* name, const std::vector<double>& values, Int32 rows, Int32 cols)
	{
		API_AddParType* p = Find (name);
		if (p == nullptr || p->typeMod != API_ParArray)
			return false;
		if (p->value.array != nullptr)
			BMKillHandle (&p->value.array);
		p->dim1 = rows;
		p->dim2 = cols;
		p->value.array = BMAllocateHandle ((GSSize) ((size_t) (rows * cols) * sizeof (double)), ALLOCATE_CLEAR, 0);
		double** arr = reinterpret_cast<double**> (p->value.array);
		for (Int32 i = 0; i < rows * cols && i < (Int32) values.size (); ++i)
			(*arr)[i] = values[(size_t) i];
		return true;
	}

	// числовой массив целиком (построчно)
	std::vector<double> Vector (const char* name) const
	{
		std::vector<double> res;
		API_AddParType* p = Find (name);
		if (p == nullptr || p->typeMod != API_ParArray || p->value.array == nullptr || p->typeID == APIParT_CString)
			return res;
		const Int32 n = p->dim1 * (p->dim2 > 0 ? p->dim2 : 1);
		const double* arr = *reinterpret_cast<double**> (p->value.array);
		for (Int32 i = 0; i < n; ++i)
			res.push_back (arr[i]);
		return res;
	}

	// одномерный массив чисел; размерность второго измерения (0 или 1) — как в объекте
	bool SetVector (const char* name, const std::vector<double>& values)
	{
		API_AddParType* p = Find (name);
		if (p == nullptr || p->typeMod != API_ParArray)
			return false;
		const Int32 rows = values.empty () ? 1 : (Int32) values.size ();
		const Int32 cols = p->dim2 > 1 ? 1 : p->dim2;
		if (p->value.array != nullptr)
			BMKillHandle (&p->value.array);
		p->dim1 = rows;
		p->dim2 = cols;
		p->value.array = BMAllocateHandle ((GSSize) ((size_t) rows * sizeof (double)), ALLOCATE_CLEAR, 0);
		if (p->value.array == nullptr)
			return false;
		double** arr = reinterpret_cast<double**> (p->value.array);
		for (Int32 i = 0; i < (Int32) values.size (); ++i)
			(*arr)[i] = values[(size_t) i];
		return true;
	}

	// краткое описание для диагностики
	GS::UniString ValueText (const API_AddParType& p) const
	{
		if (p.typeMod == API_ParArray)
			return GS::UniString::Printf ("[%dx%d]", (int) p.dim1, (int) p.dim2);
		if (p.typeID == APIParT_CString)
			return GS::UniString (p.value.uStr);
		if (p.typeID == APIParT_Title || p.typeID == APIParT_Separator)
			return GS::UniString ();
		GS::UniString res = GS::UniString::Printf ("%.4f", p.value.real);
		if (p.valueDescription[0] != 0)
			res += GS::UniString (" (") + GS::UniString (p.valueDescription) + ")";
		return res;
	}

	API_AddParType** Handle () const { return hdl; }

private:
	API_AddParType**	hdl;
	Int32				n;
};


inline GS::UniString LibPartName (Int32 libInd)
{
	if (libInd <= 0)
		return GS::UniString ();
	API_LibPart lp = {};
	lp.index = libInd;
	GS::UniString name;
	if (ACAPI_LibraryPart_Get (&lp) == NoError)
		name = GS::UniString (lp.docu_UName);
	if (lp.location != nullptr) {
		delete lp.location;
		lp.location = nullptr;
	}
	return name;
}

}	// namespace au

#endif
