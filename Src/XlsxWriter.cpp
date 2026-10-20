// Минимальная запись книги Excel (.xlsx)
#include "XlsxWriter.hpp"
#include "FinishCore.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

namespace xlsx {

void Sheet::Set (int r, int c, const std::wstring& t, int style)
{
	Cell cell;
	cell.row = r; cell.col = c; cell.text = t; cell.style = style;
	cells.push_back (cell);
}


void Sheet::SetNum (int r, int c, double v, int style)
{
	Cell cell;
	cell.row = r; cell.col = c; cell.num = v; cell.isNum = true; cell.style = style;
	cells.push_back (cell);
}


void Sheet::Merge (int r1, int c1, int r2, int c2)
{
	merges.push_back ({ r1, c1, r2, c2 });
}


std::string ColName (int c)
{
	std::string s;
	++c;
	while (c > 0) {
		const int m = (c - 1) % 26;
		s.insert (s.begin (), (char) ('A' + m));
		c = (c - 1) / 26;
	}
	return s;
}


static std::string Esc (const std::wstring& w)
{
	const std::string s = otd::ToUtf8 (w);
	std::string r;
	for (char ch : s) {
		switch (ch) {
			case '&': r += "&amp;"; break;
			case '<': r += "&lt;"; break;
			case '>': r += "&gt;"; break;
			case '"': r += "&quot;"; break;
			case '\r': break;
			default:
				if ((unsigned char) ch < 0x20 && ch != '\n' && ch != '\t') break;
				r += ch;
		}
	}
	return r;
}


static std::string Ref (int r, int c)
{
	return ColName (c) + std::to_string (r + 1);
}


static std::string Num (double v)
{
	char buf[64];
	snprintf (buf, sizeof (buf), "%.6f", v);
	std::string s (buf);
	while (!s.empty () && s.back () == '0') s.pop_back ();
	if (!s.empty () && s.back () == '.') s.pop_back ();
	if (s == "-0") s = "0";
	return s;
}


static std::string SheetXml (const Sheet& sh)
{
	// ячейки объединений получают стиль левой верхней (чтобы рамка была по всему контуру)
	std::vector<Cell> cells = sh.cells;
	std::set<std::pair<int, int>> have;
	for (const auto& c : cells) have.insert ({ c.row, c.col });
	for (const auto& m : sh.merges) {
		int st = Default;
		for (const auto& c : cells)
			if (c.row == m.r1 && c.col == m.c1) st = c.style;
		for (int r = m.r1; r <= m.r2; ++r)
			for (int c = m.c1; c <= m.c2; ++c)
				if (have.insert ({ r, c }).second) {
					Cell e; e.row = r; e.col = c; e.style = st;
					cells.push_back (e);
				}
	}
	std::stable_sort (cells.begin (), cells.end (), [] (const Cell& a, const Cell& b) {
		return a.row != b.row ? a.row < b.row : a.col < b.col;
	});

	std::string x;
	x += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
	x += "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
		 "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
	x += "<sheetPr><pageSetUpPr fitToPage=\"1\"/></sheetPr>";
	if (!sh.colWidth.empty ()) {
		x += "<cols>";
		for (const auto& w : sh.colWidth)
			x += "<col min=\"" + std::to_string (w.first + 1) + "\" max=\"" + std::to_string (w.first + 1) +
				 "\" width=\"" + Num (w.second) + "\" customWidth=\"1\"/>";
		x += "</cols>";
	}
	x += "<sheetData>";
	int curRow = -1;
	auto openRow = [&] (int r) {
		x += "<row r=\"" + std::to_string (r + 1) + "\"";
		auto h = sh.rowHeight.find (r);
		if (h != sh.rowHeight.end ())
			x += " ht=\"" + Num (h->second) + "\" customHeight=\"1\"";
		x += ">";
	};
	for (const auto& c : cells) {
		if (c.row != curRow) {
			if (curRow >= 0) x += "</row>";
			curRow = c.row;
			openRow (curRow);
		}
		x += "<c r=\"" + Ref (c.row, c.col) + "\"";
		if (c.style != Default) x += " s=\"" + std::to_string (c.style) + "\"";
		if (c.isNum) {
			x += "><v>" + Num (c.num) + "</v></c>";
		} else if (!c.text.empty ()) {
			x += " t=\"inlineStr\"><is><t xml:space=\"preserve\">" + Esc (c.text) + "</t></is></c>";
		} else {
			x += "/>";
		}
	}
	if (curRow >= 0) x += "</row>";
	x += "</sheetData>";
	if (!sh.merges.empty ()) {
		x += "<mergeCells count=\"" + std::to_string (sh.merges.size ()) + "\">";
		for (const auto& m : sh.merges)
			x += "<mergeCell ref=\"" + Ref (m.r1, m.c1) + ":" + Ref (m.r2, m.c2) + "\"/>";
		x += "</mergeCells>";
	}
	x += "<pageMargins left=\"0.6\" right=\"0.4\" top=\"0.4\" bottom=\"0.4\" header=\"0.2\" footer=\"0.2\"/>";
	x += std::string ("<pageSetup paperSize=\"9\" orientation=\"") + (sh.landscape ? "landscape" : "portrait") +
		 "\" fitToWidth=\"1\" fitToHeight=\"0\"/>";
	x += "</worksheet>";
	return x;
}


static std::string StylesXml ()
{
	return
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
		"<numFmts count=\"2\"><numFmt numFmtId=\"164\" formatCode=\"#,##0.0\"/><numFmt numFmtId=\"165\" formatCode=\"#,##0.00\"/></numFmts>"
		"<fonts count=\"3\">"
		"<font><sz val=\"10\"/><name val=\"Arial\"/><family val=\"2\"/><charset val=\"204\"/></font>"
		"<font><b/><sz val=\"14\"/><name val=\"Arial\"/><family val=\"2\"/><charset val=\"204\"/></font>"
		"<font><b/><sz val=\"10\"/><name val=\"Arial\"/><family val=\"2\"/><charset val=\"204\"/></font>"
		"</fonts>"
		"<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
		"<borders count=\"2\">"
		"<border><left/><right/><top/><bottom/><diagonal/></border>"
		"<border><left style=\"thin\"><color auto=\"1\"/></left><right style=\"thin\"><color auto=\"1\"/></right>"
		"<top style=\"thin\"><color auto=\"1\"/></top><bottom style=\"thin\"><color auto=\"1\"/></bottom><diagonal/></border>"
		"</borders>"
		"<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
		"<cellXfs count=\"10\">"
		// 0 Default
		"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
		// 1 Title
		"<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
		// 2 Head
		"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf>"
		// 3 Text
		"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf>"
		// 4 Num1
		"<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
		// 5 Vert
		"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" textRotation=\"90\" wrapText=\"1\"/></xf>"
		// 6 Plain
		"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\"><alignment horizontal=\"left\" vertical=\"top\"/></xf>"
		// 7 Num2
		"<xf numFmtId=\"165\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
		// 8 Bold
		"<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
		// 9 TextLeft
		"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"left\" vertical=\"center\" wrapText=\"1\"/></xf>"
		"</cellXfs>"
		"<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
		"</styleSheet>";
}


std::string BuildXlsx (const std::vector<Sheet>& sheets)
{
	std::vector<ZipEntry> e;
	std::string ct =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
		"<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
		"<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
		"<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
		"<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
	for (size_t i = 0; i < sheets.size (); ++i)
		ct += "<Override PartName=\"/xl/worksheets/sheet" + std::to_string (i + 1) +
			  ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
	ct += "</Types>";
	e.push_back ({ "[Content_Types].xml", ct });

	e.push_back ({ "_rels/.rels",
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
		"</Relationships>" });

	std::string wb =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
		"xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";
	std::string rels =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
	for (size_t i = 0; i < sheets.size (); ++i) {
		const std::string id = std::to_string (i + 1);
		std::wstring nm = sheets[i].name.empty () ? L"Лист" + std::to_wstring (i + 1) : sheets[i].name;
		if (nm.size () > 31) nm = nm.substr (0, 31);
		wb += "<sheet name=\"" + Esc (nm) + "\" sheetId=\"" + id + "\" r:id=\"rId" + id + "\"/>";
		rels += "<Relationship Id=\"rId" + id + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet" + id + ".xml\"/>";
	}
	const std::string sid = std::to_string (sheets.size () + 1);
	rels += "<Relationship Id=\"rId" + sid + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>";
	rels += "</Relationships>";
	wb += "</sheets></workbook>";
	e.push_back ({ "xl/workbook.xml", wb });
	e.push_back ({ "xl/_rels/workbook.xml.rels", rels });
	e.push_back ({ "xl/styles.xml", StylesXml () });
	for (size_t i = 0; i < sheets.size (); ++i)
		e.push_back ({ "xl/worksheets/sheet" + std::to_string (i + 1) + ".xml", SheetXml (sheets[i]) });
	return BuildZip (e);
}

// ============================================================================= ZIP

unsigned long Crc32 (const std::string& data)
{
	static unsigned long table[256];
	static bool init = false;
	if (!init) {
		for (unsigned long i = 0; i < 256; ++i) {
			unsigned long c = i;
			for (int k = 0; k < 8; ++k)
				c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		init = true;
	}
	unsigned long crc = 0xFFFFFFFFUL;
	for (unsigned char ch : data)
		crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
	return (crc ^ 0xFFFFFFFFUL) & 0xFFFFFFFFUL;
}


static void Put16 (std::string& s, unsigned long v) { s += (char) (v & 0xFF); s += (char) ((v >> 8) & 0xFF); }
static void Put32 (std::string& s, unsigned long v) { Put16 (s, v & 0xFFFF); Put16 (s, (v >> 16) & 0xFFFF); }


std::string BuildZip (const std::vector<ZipEntry>& entries)
{
	const unsigned long dosTime = 0, dosDate = (unsigned long) (((2024 - 1980) << 9) | (1 << 5) | 1);
	std::string out, central;
	for (const auto& en : entries) {
		const unsigned long crc = Crc32 (en.data);
		const unsigned long off = (unsigned long) out.size ();
		Put32 (out, 0x04034b50UL);
		Put16 (out, 20); Put16 (out, 0x0800); Put16 (out, 0);		// версия, флаги (UTF-8 имена), без сжатия
		Put16 (out, dosTime); Put16 (out, dosDate);
		Put32 (out, crc); Put32 (out, (unsigned long) en.data.size ()); Put32 (out, (unsigned long) en.data.size ());
		Put16 (out, (unsigned long) en.name.size ()); Put16 (out, 0);
		out += en.name;
		out += en.data;

		Put32 (central, 0x02014b50UL);
		Put16 (central, 20); Put16 (central, 20); Put16 (central, 0x0800); Put16 (central, 0);
		Put16 (central, dosTime); Put16 (central, dosDate);
		Put32 (central, crc); Put32 (central, (unsigned long) en.data.size ()); Put32 (central, (unsigned long) en.data.size ());
		Put16 (central, (unsigned long) en.name.size ()); Put16 (central, 0); Put16 (central, 0);
		Put16 (central, 0); Put16 (central, 0); Put32 (central, 0);
		Put32 (central, off);
		central += en.name;
	}
	const unsigned long cdOff = (unsigned long) out.size ();
	out += central;
	Put32 (out, 0x06054b50UL);
	Put16 (out, 0); Put16 (out, 0);
	Put16 (out, (unsigned long) entries.size ()); Put16 (out, (unsigned long) entries.size ());
	Put32 (out, (unsigned long) central.size ()); Put32 (out, cdOff);
	Put16 (out, 0);
	return out;
}

}	// namespace xlsx
