// =============================================================================
// Ведомость отделки помещений по зонам (Archicad 27)
//
//   1. Окно: секция (параметр зоны HimeraZone «Секция»), этажи, толщина пола и потолка.
//   2. Для каждой зоны HimeraZone выбранной секции и этажей:
//      - стены по границе зоны (связи зоны): длина участка, высота стены, название
//        многослойной конструкции / строительного материала -> столбец ведомости;
//      - высота отделки = высота стены − толщина пола − толщина потолка;
//        толщина пола — перекрытие в слое «Полы» в зоне, иначе из окна;
//      - вычитаются окна и двери этого участка стены (часть проёма в полосе отделки);
//      - колонны и пилоны — по рёбрам контура зоны, лежащим на колонне;
//      - потолок = площадь зоны; виды отделки — параметры зоны «Потолок» и «Стены».
//   3. Ведомость (лист «Ведомость» в форме листа 63) и проверочный лист «Расчёт» —
//      файл Excel в папке «Документы». Правила (столбцы, строки, названия отделки) —
//      текстовый файл там же, открывается кнопкой в окне.
// =============================================================================

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "ResourceIds.hpp"
#include "DGModule.hpp"
#include "UniString.hpp"
#include "File.hpp"
#include "FileSystem.hpp"
#include "Location.hpp"

#include "Otdelka.hpp"
#include "FinishCore.hpp"
#include "FinishReport.hpp"
#include "XlsxWriter.hpp"
#include "FinishTable.hpp"
#include "ApiUtils.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

#if defined (WINDOWS)
#include <shellapi.h>
#endif

namespace {

const char*		kSettingsKey	= "OtdSettings";
const wchar_t*	kRulesFile		= L"Ведомость отделки - правила.txt";

// ----------------------------------------------------------------------------- настройки

struct Settings {
	std::wstring	section = L"1";
	std::wstring	floors = L"1";
	int				floorMm = 80;			// пол зон прочих категорий
	int				floorCat[6] = { 80, 80, 80, 0, 80, 80 };	// по первой цифре кода категории: 1 квартиры, 2 МОП, 3 подвал, 4 ТП, 5 ВПП
	int				ceilMm = 0;
	bool			floorFromSlab = true;
	std::wstring	floorLayer = L"Полы";
	bool			subtractOpenings = true;
	bool			toExcel = true;		// также сохранить в Excel (с листом «Расчёт»)
	std::wstring	zoneLib = L"HimeraZone";

	std::wstring Serialize () const
	{
		std::wostringstream o;
		o << L"section\t" << section << L"\n" << L"floors\t" << floors << L"\n"
		  << L"floorMm\t" << floorMm << L"\n" << L"ceilMm\t" << ceilMm << L"\n"
		  << L"floorCat\t" << floorCat[1] << L";" << floorCat[2] << L";" << floorCat[3] << L";" << floorCat[4] << L";" << floorCat[5] << L"\n"
		  << L"floorFromSlab\t" << (floorFromSlab ? 1 : 0) << L"\n" << L"floorLayer\t" << floorLayer << L"\n"
		  << L"subtractOpenings\t" << (subtractOpenings ? 1 : 0) << L"\n" << L"toExcel\t" << (toExcel ? 1 : 0) << L"\n"
		  << L"zoneLib\t" << zoneLib << L"\n";
		return o.str ();
	}

	// Толщина пола по коду категории зоны (первая цифра), мм
	int FloorFor (const std::wstring& catCode) const
	{
		const std::wstring c = otd::Trim (catCode);
		if (!c.empty () && c[0] >= L'1' && c[0] <= L'5')
			return floorCat[c[0] - L'0'];
		return floorMm;
	}

	void Parse (const std::wstring& text)
	{
		std::wistringstream in (text);
		std::wstring line;
		while (std::getline (in, line)) {
			const size_t t = line.find (L'\t');
			if (t == std::wstring::npos) continue;
			const std::wstring k = line.substr (0, t), v = line.substr (t + 1);
			auto toInt = [&v] (int def) { try { return std::stoi (v); } catch (...) { return def; } };
			if (k == L"section") section = v;
			else if (k == L"floors") floors = v;
			else if (k == L"floorMm") floorMm = toInt (floorMm);
			else if (k == L"floorCat") {
				std::wistringstream fs (v);
				std::wstring item;
				for (int i = 1; i <= 5 && std::getline (fs, item, L';'); ++i) {
					try { floorCat[i] = std::stoi (item); } catch (...) {}
				}
			}
			else if (k == L"ceilMm") ceilMm = toInt (ceilMm);
			else if (k == L"floorFromSlab") floorFromSlab = toInt (1) != 0;
			else if (k == L"floorLayer") floorLayer = v;
			else if (k == L"subtractOpenings") subtractOpenings = toInt (1) != 0;
			else if (k == L"toExcel") toExcel = toInt (1) != 0;
			else if (k == L"zoneLib" && !otd::Trim (v).empty ()) zoneLib = v;
		}
	}
};


std::wstring LoadModulText (const char* key)
{
	std::wstring res;
	API_ModulData md = {};
	if (ACAPI_ModulData_Get (&md, key) == NoError && md.dataHdl != nullptr) {
		const GSSize size = BMGetHandleSize (md.dataHdl);
		res = otd::FromUtf8 (std::string (*md.dataHdl, (size_t) size));
		BMKillHandle (&md.dataHdl);
	}
	return res;
}


void StoreModulText (const char* key, const std::wstring& textW)
{
	const std::string utf8 = otd::ToUtf8 (textW);
	API_ModulData md = {};
	md.dataVersion = 1;
	md.platformSign = GS::Act_Platform_Sign;
	md.dataHdl = BMAllocateHandle ((GSSize) std::max<size_t> (utf8.size (), 1), ALLOCATE_CLEAR, 0);
	if (md.dataHdl == nullptr)
		return;
	if (!utf8.empty ())
		memcpy (*md.dataHdl, utf8.data (), utf8.size ());
	ACAPI_ModulData_Store (&md, key);
	BMKillHandle (&md.dataHdl);
}

// ----------------------------------------------------------------------------- файлы в «Документах»

bool DocumentsFile (const std::wstring& name, IO::Location& loc)
{
	API_SpecFolderID sf = API_UserDocumentsFolderID;
	if (ACAPI_ProjectSettings_GetSpecFolder (&sf, &loc) != NoError)
		return false;
	loc.AppendToLocal (IO::Name (au::FromW (name)));
	return true;
}


std::wstring PathOf (const IO::Location& loc)
{
	GS::UniString path;
	loc.ToPath (&path);
	return au::ToW (path);
}


GSErrCode WriteBytes (const IO::Location& loc, const std::string& bytes)
{
	IO::File f (loc, IO::File::Create);
	GSErrCode err = f.GetStatus ();
	if (err == NoError) err = f.Open (IO::File::WriteEmptyMode);
	if (err == NoError && !bytes.empty ()) err = f.WriteBin (bytes.data (), (USize) bytes.size ());
	f.Close ();
	return err;
}


bool ReadBytes (const IO::Location& loc, std::string& bytes)
{
	bool exists = false;
	if (IO::fileSystem.Contains (loc, &exists) != NoError || !exists)
		return false;
	IO::File f (loc);
	if (f.GetStatus () != NoError || f.Open (IO::File::ReadMode) != NoError)
		return false;
	UInt64 len = 0;
	bool ok = f.GetDataLength (&len) == NoError;
	if (ok) {
		bytes.assign ((size_t) len, '\0');
		ok = len == 0 || f.ReadBin (&bytes[0], (USize) len) == NoError;
	}
	f.Close ();
	return ok;
}


// Правила: файл в «Документах»; если его нет — создаётся с правилами по умолчанию
otd::Rules LoadRules (std::vector<std::wstring>& warnings, std::wstring& path)
{
	IO::Location loc;
	if (!DocumentsFile (kRulesFile, loc))
		return otd::ParseRules (otd::DefaultRulesText (), &warnings);
	path = PathOf (loc);
	std::string bytes;
	if (!ReadBytes (loc, bytes)) {
		std::wstring text = otd::DefaultRulesText ();
		std::wstring crlf;
		for (wchar_t c : text) { if (c == L'\n') crlf += L'\r'; crlf += c; }
		WriteBytes (loc, "\xEF\xBB\xBF" + otd::ToUtf8 (crlf));
		return otd::ParseRules (text, &warnings);
	}
	return otd::ParseRules (otd::FromUtf8 (bytes), &warnings);
}


void OpenInShell (const std::wstring& file, bool inNotepad)
{
#if defined (WINDOWS)
	if (inNotepad)
		ShellExecuteW (nullptr, L"open", L"notepad.exe", (L"\"" + file + L"\"").c_str (), nullptr, SW_SHOWNORMAL);
	else
		ShellExecuteW (nullptr, L"open", file.c_str (), nullptr, nullptr, SW_SHOWNORMAL);
#else
	(void) file; (void) inNotepad;
#endif
}


void ShowReport (const std::vector<std::wstring>& lines)
{
	std::wstring text;
	for (const auto& l : lines) {
		text += l;
		text += L"\n";
	}
	GS::UniString u = au::FromW (text);
	u.ReplaceAll ("%", "%%");
	ACAPI_WriteReport (u, true);
}

// ----------------------------------------------------------------------------- окно

class OtdDialog : public DG::ModalDialog,
				  public DG::PanelObserver,
				  public DG::ButtonItemObserver,
				  public DG::CheckItemObserver,
				  public DG::CompoundItemObserver
{
public:
	enum {
		OkId = 1, CancelId = 2,
		LblSection = 3, EdSection = 4, LblFloors = 5, EdFloors = 6,
		LblFloor = 7, EdFloor = 8, ChkSlab = 9, EdLayer = 10,
		LblCeil = 11, EdCeil = 12, ChkOpenings = 13, ChkExcel = 14, BtnRules = 15,
		LblHint = 16, LblLib = 17, EdLib = 18, Sep = 19,
		LblCat1 = 20, EdCat1 = 21, LblCat2 = 22, EdCat2 = 23, LblCat3 = 24, EdCat3 = 25,
		LblCat4 = 26, EdCat4 = 27, LblCat5 = 28, EdCat5 = 29, LblOther = 30
	};

	OtdDialog (Settings& s) :
		DG::ModalDialog (ACAPI_GetOwnResModule (), ID_OTD_DLG, ACAPI_GetOwnResModule ()),
		st (s),
		okButton (GetReference (), OkId),
		cancelButton (GetReference (), CancelId),
		rulesButton (GetReference (), BtnRules),
		edSection (GetReference (), EdSection),
		edFloors (GetReference (), EdFloors),
		edLayer (GetReference (), EdLayer),
		edLib (GetReference (), EdLib),
		edFloor (GetReference (), EdFloor),
		edCat1 (GetReference (), EdCat1), edCat2 (GetReference (), EdCat2), edCat3 (GetReference (), EdCat3),
		edCat4 (GetReference (), EdCat4), edCat5 (GetReference (), EdCat5),
		edCeil (GetReference (), EdCeil),
		chkSlab (GetReference (), ChkSlab),
		chkOpenings (GetReference (), ChkOpenings),
		chkExcel (GetReference (), ChkExcel)
	{
		edSection.SetText (au::FromW (st.section));
		edFloors.SetText (au::FromW (st.floors));
		edLayer.SetText (au::FromW (st.floorLayer));
		edLib.SetText (au::FromW (st.zoneLib));
		edFloor.SetValue (st.floorMm);
		DG::IntEdit* cats[6] = { nullptr, &edCat1, &edCat2, &edCat3, &edCat4, &edCat5 };
		for (int i = 1; i <= 5; ++i) cats[i]->SetValue (st.floorCat[i]);
		edCeil.SetValue (st.ceilMm);
		chkSlab.SetState (st.floorFromSlab);
		chkOpenings.SetState (st.subtractOpenings);
		chkExcel.SetState (st.toExcel);
		UpdateItems ();
		AttachToAllItems (*this);
		Attach (*this);
	}

	~OtdDialog ()
	{
		Detach (*this);
		DetachFromAllItems (*this);
	}

private:
	virtual void ButtonClicked (const DG::ButtonClickEvent& ev) override
	{
		if (ev.GetSource () == &okButton) {
			std::set<int> f;
			if (!otd::ParseFloors (au::ToW (edFloors.GetText ()), f)) {
				ACAPI_WriteReport (au::FromW (L"Не удалось разобрать номера этажей.\nПишите так: 1   или   2-14   или   1; 3; 5-7"), true);
				return;
			}
			st.section = otd::Trim (au::ToW (edSection.GetText ()));
			st.floors = otd::Trim (au::ToW (edFloors.GetText ()));
			st.floorLayer = otd::Trim (au::ToW (edLayer.GetText ()));
			st.zoneLib = otd::Trim (au::ToW (edLib.GetText ()));
			if (st.zoneLib.empty ()) st.zoneLib = L"HimeraZone";
			st.floorMm = (int) edFloor.GetValue ();
			DG::IntEdit* cats[6] = { nullptr, &edCat1, &edCat2, &edCat3, &edCat4, &edCat5 };
			for (int i = 1; i <= 5; ++i) st.floorCat[i] = (int) cats[i]->GetValue ();
			st.ceilMm = (int) edCeil.GetValue ();
			st.floorFromSlab = chkSlab.IsChecked ();
			st.subtractOpenings = chkOpenings.IsChecked ();
			st.toExcel = chkExcel.IsChecked ();
			PostCloseRequest (DG::ModalDialog::Accept);
		} else if (ev.GetSource () == &cancelButton) {
			PostCloseRequest (DG::ModalDialog::Cancel);
		} else if (ev.GetSource () == &rulesButton) {
			std::vector<std::wstring> warn;
			std::wstring path;
			LoadRules (warn, path);		// создаст файл, если его ещё нет
			if (!path.empty ())
				OpenInShell (path, true);
		}
	}

	virtual void CheckItemChanged (const DG::CheckItemChangeEvent& ev) override
	{
		if (ev.GetSource () == &chkSlab)
			UpdateItems ();
	}

	void UpdateItems ()
	{
		if (chkSlab.IsChecked ()) edLayer.Enable (); else edLayer.Disable ();
	}

	Settings&		st;
	DG::Button		okButton, cancelButton, rulesButton;
	DG::TextEdit	edSection, edFloors, edLayer, edLib;
	DG::IntEdit		edFloor, edCat1, edCat2, edCat3, edCat4, edCat5, edCeil;
	DG::CheckBox	chkSlab, chkOpenings, chkExcel;
};

// ----------------------------------------------------------------------------- чтение модели

std::wstring AttrName (API_AttrTypeID type, const API_AttributeIndex& idx)
{
	API_Attribute a = {};
	a.header.typeID = type;
	a.header.index = idx;
	GS::UniString name;
	a.header.uniStringNamePtr = &name;
	if (ACAPI_Attribute_Get (&a) != NoError)
		return std::wstring ();
	return otd::Trim (au::ToW (name));
}


std::wstring ElemId (const API_Guid& g)
{
	GS::UniString s;
	if (ACAPI_Element_GetElementInfoString (&g, &s) != NoError)
		return std::wstring ();
	return otd::Trim (au::ToW (s));
}


struct WallInfo {
	bool			ok = false;
	std::wstring	id, construction;
	int				column = 0;
	double			height = 0;
};


WallInfo ReadWall (const API_Guid& g, const otd::Rules& rules, std::map<API_Guid, WallInfo>& cache)
{
	auto it = cache.find (g);
	if (it != cache.end ())
		return it->second;
	WallInfo w;
	API_Element el = {};
	el.header.guid = g;
	if (ACAPI_Element_Get (&el) == NoError && el.header.type.typeID == API_WallID) {
		w.ok = true;
		w.id = ElemId (g);
		w.height = el.wall.height;
		switch (el.wall.modelElemStructureType) {
			case API_CompositeStructure:	w.construction = AttrName (API_CompWallID, el.wall.composite); break;
			case API_ProfileStructure:		w.construction = AttrName (API_ProfileID, el.wall.profileAttr); break;
			default:						w.construction = AttrName (API_BuildingMaterialID, el.wall.buildingMaterial); break;
		}
		w.column = otd::ClassifyWall (w.construction, rules);
	}
	cache[g] = w;
	return w;
}


struct OpeningInfo {
	API_Guid	owner = APINULLGuid;
	double		loc = 0, width = 0, lower = 0, height = 0;
};


std::vector<OpeningInfo> ReadOpenings (const API_RoomRelation& rel)
{
	std::vector<OpeningInfo> res;
	for (API_ElemTypeID t : { API_WindowID, API_DoorID }) {
		const API_ElemType et (t);
		if (!rel.elementsGroupedByType.ContainsKey (et))
			continue;
		for (const API_Guid& g : rel.elementsGroupedByType[et]) {
			API_Element el = {};
			el.header.guid = g;
			if (ACAPI_Element_Get (&el) != NoError)
				continue;
			const API_WindowType& w = (t == API_WindowID) ? el.window : el.door;
			OpeningInfo o;
			o.owner = w.owner;
			o.loc = w.objLoc;
			o.width = w.openingBase.width;
			o.height = w.openingBase.height;
			o.lower = w.lower;
			res.push_back (o);
		}
	}
	return res;
}


// Толщина пола: самое толстое перекрытие в слое «Полы» среди связанных с зоной
bool FloorFromSlabs (const API_RoomRelation& rel, const std::wstring& layer, double& thick, std::wstring& source)
{
	const API_ElemType et (API_SlabID);
	if (layer.empty () || !rel.elementsGroupedByType.ContainsKey (et))
		return false;
	bool found = false;
	for (const API_Guid& g : rel.elementsGroupedByType[et]) {
		API_Element el = {};
		el.header.guid = g;
		if (ACAPI_Element_Get (&el) != NoError)
			continue;
		if (!otd::SameText (AttrName (API_LayerID, el.header.layer), layer))
			continue;
		if (!found || el.slab.thickness > thick) {
			thick = el.slab.thickness;
			source = L"перекрытие " + ElemId (g) + L" в слое «" + layer + L"»";
		}
		found = true;
	}
	return found;
}


struct ColumnBox { std::wstring id; double x1, y1, x2, y2, height; };


std::vector<ColumnBox> ReadColumns (const API_RoomRelation& rel)
{
	std::vector<ColumnBox> res;
	const API_ElemType et (API_ColumnID);
	if (!rel.elementsGroupedByType.ContainsKey (et))
		return res;
	for (const API_Guid& g : rel.elementsGroupedByType[et]) {
		API_Element el = {};
		el.header.guid = g;
		if (ACAPI_Element_Get (&el) != NoError)
			continue;
		API_Box3D box = {};
		if (ACAPI_Element_CalcBounds (&el.header, &box) != NoError)
			continue;
		res.push_back ({ ElemId (g), box.xMin, box.yMin, box.xMax, box.yMax, el.column.height });
	}
	return res;
}


struct Edge { double x1, y1, x2, y2; };

std::vector<Edge> ZoneEdges (const API_ElementMemo& memo)
{
	std::vector<Edge> res;
	if (memo.coords == nullptr || memo.pends == nullptr)
		return res;
	const Int32 nSub = (Int32) (BMGetHandleSize ((GSConstHandle) memo.pends) / sizeof (Int32)) - 1;
	for (Int32 k = 1; k <= nSub; ++k) {
		const Int32 i1 = (*memo.pends)[k - 1] + 1, i2 = (*memo.pends)[k];
		for (Int32 i = i1; i < i2; ++i) {
			const API_Coord& a = (*memo.coords)[i];
			const API_Coord& b = (*memo.coords)[i + 1];
			res.push_back ({ a.x, a.y, b.x, b.y });
		}
	}
	return res;
}


struct RunStats {
	int zonesAll = 0, zonesLib = 0, zonesUsed = 0, floorFromSlab = 0, floorFromZone = 0;
	std::map<std::wstring, int> sections;		// какие секции встречаются в зонах выбранных этажей
	std::map<std::wstring, double> unknown;		// конструкции без столбца -> м²
};


std::vector<otd::ZoneData> ReadZones (const Settings& st, const std::set<int>& floors, const otd::Rules& rules, RunStats& rs)
{
	std::vector<otd::ZoneData> res;
	GS::Array<API_Guid> list;
	if (ACAPI_Element_GetElemList (API_ElemType (API_ZoneID), &list) != NoError)
		return res;
	std::map<API_Guid, WallInfo> wallCache;
	const double ceil = st.ceilMm / 1000.0;
	const std::wstring libPrefix = otd::Lower (st.zoneLib);

	for (const API_Guid& g : list) {
		++rs.zonesAll;
		API_Element el = {};
		el.header.guid = g;
		if (ACAPI_Element_Get (&el) != NoError)
			continue;
		const std::wstring lib = otd::Lower (au::ToW (au::LibPartName (el.zone.libInd)));
		if (lib.compare (0, libPrefix.size (), libPrefix) != 0)
			continue;
		++rs.zonesLib;
		if (floors.count (el.header.floorInd) == 0)
			continue;

		API_ElementMemo memo = {};
		if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars | APIMemoMask_Polygon) != NoError)
			continue;
		au::Params P (memo.params);
		otd::ZoneData z;
		z.section = otd::Trim (au::ToW (P.Str ("asAddOn_stStaircaseCode")));
		rs.sections[z.section] += 1;
		if (!st.section.empty () && !otd::SameText (z.section, st.section)) {
			ACAPI_DisposeElemMemoHdls (&memo);
			continue;
		}
		++rs.zonesUsed;
		z.number = otd::Trim (au::ToW (GS::UniString (el.zone.roomNoStr)));
		z.roomName = otd::Trim (au::ToW (GS::UniString (el.zone.roomName)));
		z.name = AttrName (API_ZoneCatID, el.zone.catInd);		// наименование — из категории зоны
		if (z.name.empty ())
			z.name = z.roomName;
		z.floor = el.header.floorInd;
		{
			API_Attribute a = {};
			a.header.typeID = API_ZoneCatID;
			a.header.index = el.zone.catInd;
			if (ACAPI_Attribute_Get (&a) == NoError)
				z.catCode = otd::Trim (au::ToW (GS::UniString (a.zoneCat.catCode)));
		}
		z.ceilFinishRaw = au::ToW (P.Str ("stUpMat"));
		z.wallFinishRaw = au::ToW (P.Str ("stWallMat"));
		const std::vector<Edge> edges = ZoneEdges (memo);
		ACAPI_DisposeElemMemoHdls (&memo);

		// площадь потолка = чистая площадь зоны
		{
			API_QuantityPar qp = {};
			qp.minOpeningSize = 0.0001;
			API_ElementQuantity eq = {};
			API_Quantities q;
			q.elements = &eq;
			API_QuantitiesMask mask;
			ACAPI_ELEMENT_QUANTITY_MASK_CLEAR (mask);
			ACAPI_ELEMENT_QUANTITY_MASK_SET (mask, zone, area);
			ACAPI_ELEMENT_QUANTITY_MASK_SET (mask, zone, netarea);
			if (ACAPI_Element_GetQuantities (g, &qp, &q, &mask) == NoError)
				z.ceilArea = eq.zone.netarea > 0 ? eq.zone.netarea : eq.zone.area;
		}

		API_RoomRelation rel;
		const bool hasRel = ACAPI_Element_GetRelations (g, API_ElemType (API_ZombieElemID), &rel) == NoError;

		// толщина пола: перекрытие в слое «Полы» -> «Толщина пола» в параметрах зоны -> по категории из окна
		if (hasRel && st.floorFromSlab && FloorFromSlabs (rel, st.floorLayer, z.floorThick, z.floorSource)) {
			++rs.floorFromSlab;
		} else if (el.zone.roomFlThick > 0.0005) {
			z.floorThick = el.zone.roomFlThick;
			z.floorSource = L"толщина пола в параметрах зоны";
			++rs.floorFromZone;
		} else {
			z.floorThick = st.FloorFor (z.catCode) / 1000.0;
			z.floorSource = L"из окна (категория " + z.catCode + L")";
		}

		if (hasRel) {
			const std::vector<OpeningInfo> openings = st.subtractOpenings ? ReadOpenings (rel) : std::vector<OpeningInfo> ();
			for (const API_WallPart& part : rel.wallPart) {
				const WallInfo w = ReadWall (part.guid, rules, wallCache);
				if (!w.ok)
					continue;
				otd::WallPiece p;
				p.elemId = w.id;
				p.construction = w.construction;
				p.column = w.column;
				const double t1 = std::min (part.tBeg, part.tEnd), t2 = std::max (part.tBeg, part.tEnd);
				p.length = t2 - t1;
				p.elemHeight = w.height;
				const double lo = z.floorThick, hi = w.height - ceil;
				p.finishHeight = std::max (0.0, hi - lo);
				for (const OpeningInfo& o : openings) {
					if (o.owner != part.guid)
						continue;
					const double ow = std::min (o.loc + o.width / 2, t2) - std::max (o.loc - o.width / 2, t1);
					if (ow <= 1e-4)
						continue;
					const double oh = otd::OpeningOverlap (o.lower, o.height, lo, hi);
					if (oh <= 0)
						continue;
					p.openings += ow * oh;
					p.nOpenings += 1;
				}
				if (p.length > 1e-4)
					z.pieces.push_back (p);
				if (p.column == 0)
					rs.unknown[w.construction.empty () ? std::wstring (L"(без названия)") : w.construction] += p.Area ();
			}

			// колонны и пилоны: рёбра контура зоны, лежащие на колонне
			const std::vector<ColumnBox> cols = ReadColumns (rel);
			const double tol = 0.01;
			for (const Edge& e : edges) {
				for (const ColumnBox& c : cols) {
					auto inside = [&] (double x, double y) {
						return x >= c.x1 - tol && x <= c.x2 + tol && y >= c.y1 - tol && y <= c.y2 + tol;
					};
					if (!inside (e.x1, e.y1) || !inside (e.x2, e.y2))
						continue;
					otd::WallPiece p;
					p.elemId = c.id;
					p.construction = L"колонна / пилон";
					p.isColumn = true;
					p.column = rules.columnsColumn;
					p.length = std::hypot (e.x2 - e.x1, e.y2 - e.y1);
					p.elemHeight = c.height;
					p.finishHeight = std::max (0.0, c.height - ceil - z.floorThick);
					if (p.length > 1e-4)
						z.pieces.push_back (p);
					break;
				}
			}
			ACAPI_DisposeRoomRelationHdls (&rel);
		}
		res.push_back (z);
	}
	std::stable_sort (res.begin (), res.end (), [] (const otd::ZoneData& a, const otd::ZoneData& b) {
		if (a.floor != b.floor) return a.floor < b.floor;
		return a.number < b.number;
	});
	return res;
}

// ----------------------------------------------------------------------------- рабочий лист

// Рабочий лист с таким именем (ищется среди существующих) или новый
GSErrCode FindOrCreateWorksheet (const std::wstring& name, const std::wstring& ref, API_DatabaseInfo& out, bool& existed)
{
	existed = false;
	API_DatabaseUnId* ids = nullptr;
	GS::Array<API_DatabaseUnId> arr;
	if (ACAPI_Database_GetWorksheetDatabases (&ids, &arr) == NoError) {
		for (const API_DatabaseUnId& id : arr) {
			API_DatabaseInfo info = {};
			info.typeID = APIWind_WorksheetID;
			info.databaseUnId = id;
			if (ACAPI_Window_GetDatabaseInfo (&info) != NoError)
				continue;
			if (otd::SameText (au::ToW (GS::UniString (info.name)), name)) {
				out = info;
				existed = true;
				break;
			}
		}
	}
	if (ids != nullptr)
		BMpFree (reinterpret_cast<GSPtr> (ids));
	if (existed)
		return NoError;
	out = {};
	out.typeID = APIWind_WorksheetID;
	const GS::UniString wn = au::FromW (name), wr = au::FromW (ref);
	GS::ucsncpy (out.name, wn.ToUStr (), API_UniLongNameLen - 1);
	GS::ucsncpy (out.ref, wr.ToUStr (), API_UniLongNameLen - 1);
	GSErrCode err = ACAPI_Database_NewDatabase (&out);
	if (err == NoError) {
		API_DatabaseInfo info = {};
		info.typeID = APIWind_WorksheetID;
		info.databaseUnId = out.databaseUnId;
		if (ACAPI_Window_GetDatabaseInfo (&info) == NoError)
			out = info;
	}
	return err;
}


// Рисует таблицу в текущей базе (рабочем листе): прежние линии и тексты листа удаляются
GSErrCode DrawTable (const otd::TableLayout& t, int& nLines, int& nTexts)
{
	nLines = nTexts = 0;
	double scale = 100;
	if (ACAPI_Drawing_GetDrawingScale (&scale) != NoError || scale <= 0)
		scale = 100;
	const double k = scale / 1000.0;		// мм листа -> м модели

	GS::Array<API_Guid> old, part;
	if (ACAPI_Element_GetElemList (API_ElemType (API_LineID), &part) == NoError) old.Append (part);
	part.Clear ();
	if (ACAPI_Element_GetElemList (API_ElemType (API_TextID), &part) == NoError) old.Append (part);
	if (!old.IsEmpty ())
		ACAPI_Element_Delete (old);

	API_Element lineDef = {};
	lineDef.header.type = API_LineID;
	GSErrCode err = ACAPI_Element_GetDefaults (&lineDef, nullptr);
	if (err != NoError)
		return err;
	for (const otd::TSeg& g : t.segs) {
		API_Element el = lineDef;
		el.line.begC.x = g.x1 * k; el.line.begC.y = g.y1 * k;
		el.line.endC.x = g.x2 * k; el.line.endC.y = g.y2 * k;
		if (ACAPI_Element_Create (&el, nullptr) == NoError)
			++nLines;
	}

	API_Element textDef = {};
	textDef.header.type = API_TextID;
	err = ACAPI_Element_GetDefaults (&textDef, nullptr);
	if (err != NoError)
		return err;
	for (const otd::TText& tx : t.texts) {
		API_Element el = textDef;
		el.text.loc.x = tx.x * k;
		el.text.loc.y = tx.y * k;
		el.text.size = tx.size;
		el.text.anchor = APIAnc_MM;
		el.text.just = APIJust_Center;
		el.text.angle = tx.vertical ? 1.5707963267948966 : 0.0;
		el.text.faceBits = tx.bold ? APIFace_Bold : APIFace_Plain;
		el.text.nonBreaking = true;
		el.text.multiStyle = false;
		el.text.useEolPos = false;
		el.text.nLine = 1;
		el.text.charCode = CC_UniCode;
		const GS::UniString u = au::FromW (tx.text);
		API_ElementMemo memo = {};
		memo.textContent = BMhAllClear ((GSSize) ((u.GetLength () + 1) * sizeof (GS::uchar_t)));
		if (memo.textContent == nullptr)
			continue;
		GS::ucscpy (reinterpret_cast<GS::uchar_t*> (*memo.textContent), u.ToUStr ());
		if (ACAPI_Element_Create (&el, &memo) == NoError)
			++nTexts;
		ACAPI_DisposeElemMemoHdls (&memo);
	}
	return NoError;
}

}	// namespace

// ============================================================================= команда меню

void DoOtdelka ()
{
	Settings st;
	st.Parse (LoadModulText (kSettingsKey));
	{
		OtdDialog dlg (st);
		if (!dlg.Invoke ())
			return;
	}
	ACAPI_CallUndoableCommand (au::FromW (L"Ведомость отделки: настройки"), [&] () -> GSErrCode {
		StoreModulText (kSettingsKey, st.Serialize ());
		return NoError;
	});

	{	// чтение модели — с плана этажа
		API_DatabaseInfo fp = {};
		fp.typeID = APIWind_FloorPlanID;
		ACAPI_Database_ChangeCurrentDatabase (&fp);
	}
	std::set<int> floors;
	otd::ParseFloors (st.floors, floors);
	std::vector<std::wstring> lines, warn;
	std::wstring rulesPath;
	const otd::Rules rules = LoadRules (warn, rulesPath);

	RunStats rs;
	const std::vector<otd::ZoneData> zones = ReadZones (st, floors, rules, rs);

	if (zones.empty ()) {
		lines.push_back (L"Зоны для ведомости не найдены.");
		lines.push_back (L"Зон в проекте: " + std::to_wstring (rs.zonesAll) + L", из них «" + st.zoneLib + L"…»: " + std::to_wstring (rs.zonesLib) + L".");
		if (!rs.sections.empty ()) {
			std::wstring s;
			for (const auto& kv : rs.sections)
				s += (s.empty () ? L"" : L", ") + (kv.first.empty () ? std::wstring (L"(пусто)") : kv.first) + L" — " + std::to_wstring (kv.second);
			lines.push_back (L"На этажах " + st.floors + L" в параметре зоны «Секция» записано: " + s + L".");
		} else {
			lines.push_back (L"На этажах " + st.floors + L" зон нет. Номер этажа — как в навигаторе (1. Этаж 1 -> 1).");
		}
		ShowReport (lines);
		return;
	}

	const std::wstring secName = st.section.empty () ? std::wstring (L"все") : st.section;
	const otd::Sheet sheet = otd::BuildSheet (zones, rules, secName);

	// ---- таблица на рабочем листе «Ведомость отделки - секция N»
	const std::wstring wsName = L"Ведомость отделки - секция " + secName;
	API_DatabaseInfo ws = {};
	bool existed = false;
	std::wstring wsStep = L"создание листа";
	int nLines = 0, nTexts = 0;
	GSErrCode wsErr = FindOrCreateWorksheet (wsName, L"ВО-" + secName, ws, existed);
	if (wsErr == NoError) {
		// открыть лист: дальше элементы создаются в текущей базе — на этом листе
		wsStep = L"открытие листа";
		API_WindowInfo wi = ws;
		wsErr = ACAPI_Window_ChangeWindow (&wi);
		API_DatabaseInfo cur = {};
		if (wsErr == NoError && (ACAPI_Database_GetCurrentDatabase (&cur) != NoError || !(cur.databaseUnId == ws.databaseUnId))) {
			wsStep = L"переход на лист";
			API_DatabaseInfo db = ws;
			wsErr = ACAPI_Database_ChangeCurrentDatabase (&db);
		}
	}
	if (wsErr == NoError) {
		wsStep = L"построение таблицы";
		const otd::TableLayout layout = otd::LayoutSheet (sheet);
		wsErr = ACAPI_CallUndoableCommand (au::FromW (L"Ведомость отделки: таблица"), [&] () -> GSErrCode {
			return DrawTable (layout, nLines, nTexts);
		});
	}

	// ---- Excel (по желанию): ведомость и лист «Расчёт»
	std::wstring outPath, fname;
	GSErrCode xlErr = NoError;
	if (st.toExcel) {
		std::vector<xlsx::Sheet> book = { otd::MakeVedomostSheet (sheet), otd::MakeDetailsSheet (zones, rules) };
		fname = L"Ведомость отделки - секция " + secName + L" - эт. " + st.floors;
		for (wchar_t& c : fname)
			if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == L';')
				c = L'_';
		fname += L".xlsx";
		IO::Location loc;
		xlErr = DocumentsFile (fname, loc) ? WriteBytes (loc, xlsx::BuildXlsx (book)) : APIERR_GENERAL;
		if (xlErr == NoError)
			outPath = PathOf (loc);
	}

	double ceilSum = 0, wall[otd::kWallColumns + 1] = { 0, 0, 0, 0 };
	for (const auto& row : sheet.rows) {
		ceilSum += row.ceilArea;
		for (int c = 0; c <= otd::kWallColumns; ++c) wall[c] += row.wallArea[c];
	}
	lines.push_back (L"Ведомость отделки: секция " + secName + L", этажи " + st.floors + L".");
	if (wsErr == NoError)
		lines.push_back (L"Таблица построена на рабочем листе «" + wsName + L"»" + (existed ? L" (лист перерисован заново)." : L"."));
	else
		lines.push_back (L"Таблицу на рабочем листе построить не удалось: шаг «" + wsStep + L"», код " + std::to_wstring (wsErr) + L".");
	lines.push_back (L"Зон в расчёте: " + std::to_wstring (rs.zonesUsed) + L", строк ведомости: " + std::to_wstring (sheet.rows.size ()) + L".");
	lines.push_back (L"Потолки: " + otd::FormatNum (ceilSum, 1) + L" м². Стены: монолит " + otd::FormatNum (wall[1], 1) +
					 L", кладка/КБ " + otd::FormatNum (wall[2], 1) + L", ГКЛ " + otd::FormatNum (wall[3], 1) + L" м².");
	lines.push_back (L"Толщина пола: из перекрытий слоя «" + st.floorLayer + L"» — " + std::to_wstring (rs.floorFromSlab) +
					 L" зон, из параметров зоны — " + std::to_wstring (rs.floorFromZone) + L", у остальных — по категории из окна. Потолок — " +
					 std::to_wstring (st.ceilMm) + L" мм.");
	if (!rs.unknown.empty ()) {
		lines.push_back (L"");
		lines.push_back (L"Не удалось определить столбец (" + otd::FormatNum (wall[0], 1) + L" м²) — добавьте код в правила, раздел [Материалы]:");
		for (const auto& kv : rs.unknown)
			lines.push_back (L"   " + kv.first + L" — " + otd::FormatNum (kv.second, 1) + L" м²");
	}
	for (const auto& w : warn)
		lines.push_back (w);
	lines.push_back (L"");
	if (st.toExcel) {
		if (xlErr == NoError)
			lines.push_back (L"Excel: " + outPath + L" (лист «Расчёт» — каждая стена каждой зоны, для проверки).");
		else
			lines.push_back (L"Не удалось записать «" + fname + L"» в папку «Документы». Если файл открыт в Excel — закройте и запустите расчёт ещё раз.");
	}
	if (!rulesPath.empty ())
		lines.push_back (L"Правила: " + rulesPath);
	ShowReport (lines);
}
