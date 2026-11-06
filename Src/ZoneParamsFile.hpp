// Файл параметров зон: сохранить параметры всех зон (например, из резервной копии проекта .bpn)
// и восстановить их в текущем проекте — по GUID зоны, иначе по этажу + номеру + имени. Без API Archicad.
#ifndef ZONEPARAMSFILE_HPP
#define ZONEPARAMSFILE_HPP

#include <string>
#include <vector>

namespace zp {

struct Par {
	std::wstring		name;
	char				kind = 'n';		// 's' строка, 'n' число, 'a' массив чисел
	std::wstring		str;
	double				num = 0;
	int					dim1 = 0, dim2 = 0;
	std::vector<double>	arr;
};

struct ZoneRec {
	std::wstring		guid, number, name;
	int					story = 0;
	std::vector<Par>	pars;
};

std::wstring			Write (const std::vector<ZoneRec>& zones, const std::wstring& header);
std::vector<ZoneRec>	Read (const std::wstring& text, std::vector<std::wstring>* warnings = nullptr);

// Параметры, которые не восстанавливаются (считаются объектом / Archicad)
bool					Skip (const std::wstring& parName);

// Поиск записи для зоны: по GUID, иначе единственная запись с тем же этажом, номером и именем (-1 — нет)
int						FindRec (const std::vector<ZoneRec>& recs, const std::wstring& guid, int story,
								 const std::wstring& number, const std::wstring& name, bool* byGuid = nullptr);

}	// namespace zp

#endif
