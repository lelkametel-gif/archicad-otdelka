// Минимальная запись книги Excel (.xlsx) без сторонних библиотек:
// текст, числа, объединённые ячейки, рамки, ширина столбцов. ZIP без сжатия.
#ifndef XLSXWRITER_HPP
#define XLSXWRITER_HPP

#include <map>
#include <string>
#include <vector>

namespace xlsx {

enum Style {
	Default = 0,
	Title,		// крупный жирный, по центру, без рамки
	Head,		// шапка таблицы: рамка, по центру, перенос
	Text,		// ячейка таблицы: рамка, по центру, перенос
	Num1,		// число с одним знаком, рамка
	Vert,		// вертикальный текст (группа), рамка
	Plain,		// без рамки, слева
	Num2,		// число с двумя знаками, без рамки
	Bold,		// жирный, без рамки
	TextLeft	// ячейка таблицы: рамка, слева, перенос
};

struct Cell {
	int				row = 0, col = 0;	// с 0
	std::wstring	text;
	double			num = 0;
	bool			isNum = false;
	int				style = Default;
};

struct MergeRange { int r1, c1, r2, c2; };

struct Sheet {
	std::wstring				name;
	std::vector<Cell>			cells;
	std::vector<MergeRange>		merges;
	std::map<int, double>		colWidth;	// в символах
	std::map<int, double>		rowHeight;	// в пунктах
	bool						landscape = true;

	void Set (int r, int c, const std::wstring& t, int style = Default);
	void SetNum (int r, int c, double v, int style = Num1);
	void Merge (int r1, int c1, int r2, int c2);
};

std::string		ColName (int c);							// 0 -> A, 26 -> AA
std::string		BuildXlsx (const std::vector<Sheet>& sheets);	// содержимое файла .xlsx

// ZIP-архив без сжатия (используется и в тестах)
struct ZipEntry { std::string name, data; };
std::string		BuildZip (const std::vector<ZipEntry>& entries);
unsigned long	Crc32 (const std::string& data);

}	// namespace xlsx

#endif
