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
#include <cstring>
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
	bool			skipHidden = true;	// пропускать зоны на скрытых слоях
	std::wstring	zoneLib = L"HimeraZone";

	std::wstring Serialize () const
	{
		std::wostringstream o;
		o << L"section\t" << section << L"\n" << L"floors\t" << floors << L"\n"
		  << L"floorMm\t" << floorMm << L"\n" << L"ceilMm\t" << ceilMm << L"\n"
		  << L"floorCat\t" << floorCat[1] << L";" << floorCat[2] << L";" << floorCat[3] << L";" << floorCat[4] << L";" << floorCat[5] << L"\n"
		  << L"floorFromSlab\t" << (floorFromSlab ? 1 : 0) << L"\n" << L"floorLayer\t" << floorLayer << L"\n"
		  << L"subtractOpenings\t" << (subtractOpenings ? 1 : 0) << L"\n" << L"toExcel\t" << (toExcel ? 1 : 0) << L"\n"
		  << L"skipHidden\t" << (skipHidden ? 1 : 0) << L"\n"
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
			else if (k == L"skipHidden") skipHidden = toInt (1) != 0;
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
	const std::wstring text = otd::FromUtf8 (bytes);
	if (otd::RulesVersion (text) < otd::kRulesVersion) {
		IO::Location old;
		if (DocumentsFile (L"Ведомость отделки - правила (старые).txt", old))
			WriteBytes (old, bytes);
		std::wstring def = otd::DefaultRulesText ();
		std::wstring crlf;
		for (wchar_t c : def) { if (c == L'\n') crlf += L'\r'; crlf += c; }
		WriteBytes (loc, "\xEF\xBB\xBF" + otd::ToUtf8 (crlf));
		warnings.push_back (L"Файл правил был устаревшим — заменён правилами по умолчанию. Прежний сохранён как «Ведомость отделки - правила (старые).txt»: свои правки перенесите из него.");
		return otd::ParseRules (def, &warnings);
	}
	return otd::ParseRules (text, &warnings);
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
		LblCat4 = 26, EdCat4 = 27, LblCat5 = 28, EdCat5 = 29, LblOther = 30,
		ChkHidden = 31, BtnCheck = 32
	};

	bool withCheck = false;		// нажата «Рассчитать + проверка зон»

	OtdDialog (Settings& s) :
		DG::ModalDialog (ACAPI_GetOwnResModule (), ID_OTD_DLG, ACAPI_GetOwnResModule ()),
		st (s),
		okButton (GetReference (), OkId),
		cancelButton (GetReference (), CancelId),
		rulesButton (GetReference (), BtnRules),
		checkButton (GetReference (), BtnCheck),
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
		chkExcel (GetReference (), ChkExcel),
		chkHidden (GetReference (), ChkHidden)
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
		chkHidden.SetState (st.skipHidden);
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
		if (ev.GetSource () == &okButton || ev.GetSource () == &checkButton) {
			withCheck = ev.GetSource () == &checkButton;
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
			st.skipHidden = chkHidden.IsChecked ();
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
	DG::Button		okButton, cancelButton, rulesButton, checkButton;
	DG::TextEdit	edSection, edFloors, edLayer, edLib;
	DG::IntEdit		edFloor, edCat1, edCat2, edCat3, edCat4, edCat5, edCeil;
	DG::CheckBox	chkSlab, chkOpenings, chkExcel, chkHidden;
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


struct LayerInfo { std::wstring name; bool hidden = false; };

using LayerCache = std::vector<std::pair<API_AttributeIndex, LayerInfo>>;

LayerInfo ReadLayer (const API_AttributeIndex& idx, LayerCache& cache)
{
	for (const auto& kv : cache)
		if (kv.first == idx)
			return kv.second;
	LayerInfo li;
	API_Attribute a = {};
	a.header.typeID = API_LayerID;
	a.header.index = idx;
	GS::UniString name;
	a.header.uniStringNamePtr = &name;
	if (ACAPI_Attribute_Get (&a) == NoError) {
		li.name = otd::Trim (au::ToW (name));
		li.hidden = (a.layer.head.flags & APILay_Hidden) != 0;
	}
	cache.push_back ({ idx, li });
	return li;
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
	int onFloors = 0;							// зон любого объекта на выбранных этажах
	std::map<std::wstring, int> sections;		// какие секции встречаются в зонах выбранных этажей
	std::map<std::wstring, double> unknown;		// конструкции без столбца -> м²
	std::map<std::wstring, int> otherLib;		// зоны выбранных этажей другого объекта: имя объекта -> шт
	std::map<std::wstring, int> hiddenLayers;	// пропущены (скрытый слой): слой -> шт
	std::map<std::wstring, int> usedLayers;		// взяты в расчёт: слой -> шт
	std::map<int, int> usedFloors;				// взяты в расчёт: этаж -> шт
	std::map<std::wstring, int> usedCats;		// взяты в расчёт: «код категория» -> шт
	int fromHotlink = 0;						// взяты в расчёт из модулей (hotlink)
	int secFromProp = 0;						// секция взята из свойства
	int byContour = 0;							// участков стен найдено по контуру зоны
	int zonesWithGaps = 0;						// зон с участками контура без стен
};


std::wstring JoinCounts (const std::map<std::wstring, int>& m, size_t maxItems = 8)
{
	std::vector<std::pair<std::wstring, int>> v (m.begin (), m.end ());
	std::stable_sort (v.begin (), v.end (), [] (const auto& a, const auto& b) { return a.second > b.second; });
	std::wstring s;
	for (size_t i = 0; i < v.size () && i < maxItems; ++i)
		s += (s.empty () ? L"" : L", ") + (v[i].first.empty () ? std::wstring (L"(пусто)") : v[i].first) + L" — " + std::to_wstring (v[i].second);
	if (v.size () > maxItems)
		s += L" и ещё " + std::to_wstring (v.size () - maxItems);
	return s;
}


// ----------------------------------------------------------------------------- этажи проекта
// Номер этажа в окне — как в названии этажа («Этаж 1» -> 1). Если такого названия нет — индекс этажа.
struct StoryRec {
	int				index = 0;
	std::wstring	name;
	bool			hasNum = false;
	int				num = 0;			// последнее число в названии
	double			level = 0;			// отметка этажа, м
};

bool LastNumber (const std::wstring& s, int& out)
{
	int i = (int) s.size () - 1;
	while (i >= 0 && !(s[(size_t) i] >= L'0' && s[(size_t) i] <= L'9')) --i;
	if (i < 0) return false;
	int e = i;
	while (i >= 0 && s[(size_t) i] >= L'0' && s[(size_t) i] <= L'9') --i;
	try { out = std::stoi (s.substr ((size_t) i + 1, (size_t) (e - i))); } catch (...) { return false; }
	if (i >= 0 && (s[(size_t) i] == L'-' || s[(size_t) i] == 0x2212) && (i == 0 || s[(size_t) i - 1] == L' '))
		out = -out;
	return true;
}

std::vector<StoryRec> LoadStories (int* actStory = nullptr)
{
	std::vector<StoryRec> res;
	API_StoryInfo si = {};
	if (ACAPI_ProjectSetting_GetStorySettings (&si) != NoError || si.data == nullptr)
		return res;
	if (actStory != nullptr) *actStory = si.actStory;
	const Int32 n = si.lastStory - si.firstStory + 1;
	for (Int32 i = 0; i < n; ++i) {
		const API_StoryType& st = (*si.data)[i];
		StoryRec r;
		r.index = st.index;
		r.level = st.level;
		r.name = otd::Trim (au::ToW (GS::UniString (st.uName)));
		r.hasNum = LastNumber (r.name, r.num);
		res.push_back (r);
	}
	BMKillHandle (reinterpret_cast<GSHandle*> (&si.data));
	return res;
}

std::wstring StoryLabel (const std::vector<StoryRec>& stories, int index)
{
	for (const auto& r : stories)
		if (r.index == index)
			return L"«" + r.name + L"» (индекс " + std::to_wstring (index) + L")";
	return L"индекс " + std::to_wstring (index);
}

// Номера этажей из окна -> индексы этажей Archicad (и обратно — номер для ведомости)
std::map<int, int> ResolveFloors (const std::set<int>& wanted, const std::vector<StoryRec>& stories)
{
	std::map<int, int> idxToNum;
	for (int n : wanted) {
		bool byName = false;
		for (const auto& r : stories)
			if (r.hasNum && r.num == n) { idxToNum[r.index] = n; byName = true; }
		if (!byName)
			idxToNum[n] = n;
	}
	return idxToNum;
}

// ----------------------------------------------------------------------------- свойство «Секция №»
API_Guid FindPropertyDef (const std::vector<std::wstring>& names, std::wstring& found)
{
	GS::Array<API_PropertyGroup> groups;
	if (ACAPI_Property_GetPropertyGroups (groups) != NoError)
		return APINULLGuid;
	for (const auto& n : names) {
		for (const API_PropertyGroup& g : groups) {
			GS::Array<API_PropertyDefinition> defs;
			if (ACAPI_Property_GetPropertyDefinitions (g.guid, defs) != NoError)
				continue;
			for (const API_PropertyDefinition& d : defs) {
				if (otd::SameText (au::ToW (d.name), n)) {
					found = au::ToW (g.name) + L" / " + au::ToW (d.name);
					return d.guid;
				}
			}
		}
	}
	return APINULLGuid;
}

std::wstring VariantText (const API_Variant& v)
{
	switch (v.type) {
		case API_PropertyStringValueType:	return otd::Trim (au::ToW (v.uniStringValue));
		case API_PropertyIntegerValueType:	return std::to_wstring (v.intValue);
		case API_PropertyRealValueType:		return otd::FormatNum (v.doubleValue, std::fabs (v.doubleValue - std::round (v.doubleValue)) < 1e-9 ? 0 : 2);
		case API_PropertyBooleanValueType:	return v.boolValue ? L"1" : L"0";
		default:							return std::wstring ();
	}
}

bool ReadPropertyText (const API_Guid& elem, const API_Guid& def, std::wstring& out)
{
	API_Property prop;
	if (ACAPI_Element_GetPropertyValue (elem, def, prop) != NoError)
		return false;
	if (prop.value.variantStatus != API_VariantStatusNormal)
		return false;
	const API_Variant& v = prop.value.singleVariant.variant;
	if (v.type == API_PropertyGuidValueType) {		// перечисление: ключ -> показываемое значение
		for (const auto& e : prop.definition.possibleEnumValues)
			if (e.keyVariant.type == API_PropertyGuidValueType && e.keyVariant.guidValue == v.guidValue) {
				out = VariantText (e.displayVariant);
				return true;
			}
		return false;
	}
	out = VariantText (v);
	return true;
}


// ----------------------------------------------------------------------------- стены по контуру зоны
// Стены, которые Archicad не связал с зоной (другой этаж, «не влияет на зоны» и т. п.), ищутся по геометрии:
// грань прямой ортогональной стены лежит на ребре контура зоны и стена проходит по высоте зоны.
struct WallGeo {
	API_Guid	guid = APINULLGuid;
	double		x1 = 0, y1 = 0, x2 = 0, y2 = 0;		// габарит в плане
	double		bot = 0, top = 0;					// абсолютные отметки, м
	API_Coord	begC = {}, endC = {};
	bool		ortho = false;
};

std::vector<WallGeo> LoadWallGeo (const std::vector<StoryRec>& stories)
{
	std::vector<WallGeo> res;
	GS::Array<API_Guid> list;
	if (ACAPI_Element_GetElemList (API_ElemType (API_WallID), &list) != NoError)
		return res;
	std::map<int, double> lev;
	for (const auto& r : stories) lev[r.index] = r.level;
	for (const API_Guid& g : list) {
		API_Element el = {};
		el.header.guid = g;
		if (ACAPI_Element_Get (&el) != NoError)
			continue;
		WallGeo w;
		w.guid = g;
		w.begC = el.wall.begC;
		w.endC = el.wall.endC;
		w.ortho = el.wall.type == APIWtyp_Normal && std::fabs (el.wall.angle) < 1e-6 &&
				  (std::fabs (w.begC.x - w.endC.x) < 1e-3 || std::fabs (w.begC.y - w.endC.y) < 1e-3);
		API_Box3D box = {};
		if (ACAPI_Element_CalcBounds (&el.header, &box) != NoError)
			continue;
		w.x1 = box.xMin; w.y1 = box.yMin; w.x2 = box.xMax; w.y2 = box.yMax;
		const auto it = lev.find (el.header.floorInd);
		w.bot = (it != lev.end () ? it->second : 0.0) + el.wall.bottomOffset;
		w.top = w.bot + el.wall.height;
		res.push_back (w);
	}
	return res;
}


std::vector<OpeningInfo> WallOpenings (const API_Guid& wall)
{
	std::vector<OpeningInfo> res;
	for (API_ElemTypeID t : { API_WindowID, API_DoorID }) {
		GS::Array<API_Guid> list;
		if (ACAPI_Grouping_GetConnectedElements (wall, API_ElemType (t), &list) != NoError)
			continue;
		for (const API_Guid& g : list) {
			API_Element el = {};
			el.header.guid = g;
			if (ACAPI_Element_Get (&el) != NoError)
				continue;
			const API_WindowType& w = (t == API_WindowID) ? el.window : el.door;
			OpeningInfo o;
			o.owner = wall;
			o.loc = w.objLoc;
			o.width = w.openingBase.width;
			o.height = w.openingBase.height;
			o.lower = w.lower;
			res.push_back (o);
		}
	}
	return res;
}


std::vector<otd::ZoneData> ReadZones (const Settings& st, const std::map<int, int>& floors, const std::vector<StoryRec>& stories,
									 const API_Guid& secProp, const otd::Rules& rules, RunStats& rs)
{
	std::vector<otd::ZoneData> res;
	GS::Array<API_Guid> list;
	if (ACAPI_Element_GetElemList (API_ElemType (API_ZoneID), &list) != NoError)
		return res;
	std::map<API_Guid, WallInfo> wallCache;
	const double ceil = st.ceilMm / 1000.0;
	const std::wstring libPrefix = otd::Lower (st.zoneLib);
	LayerCache layerCache;
	const std::vector<WallGeo> allWalls = LoadWallGeo (stories);
	std::map<int, double> storyLevel;
	for (const auto& sr : stories) storyLevel[sr.index] = sr.level;

	for (const API_Guid& g : list) {
		++rs.zonesAll;
		API_Element el = {};
		el.header.guid = g;
		if (ACAPI_Element_Get (&el) != NoError)
			continue;
		const bool onFloors = floors.count (el.header.floorInd) != 0;
		if (onFloors) ++rs.onFloors;
		const std::wstring libName = otd::Trim (au::ToW (au::LibPartName (el.zone.libInd)));
		const std::wstring lib = otd::Lower (libName);
		if (lib.compare (0, libPrefix.size (), libPrefix) != 0) {
			if (onFloors)
				rs.otherLib[libName.empty () ? std::wstring (L"(без объекта)") : libName] += 1;
			continue;
		}
		++rs.zonesLib;
		if (!onFloors)
			continue;
		const LayerInfo layer = ReadLayer (el.header.layer, layerCache);
		if (st.skipHidden && layer.hidden) {
			rs.hiddenLayers[layer.name] += 1;
			continue;
		}

		API_ElementMemo memo = {};
		if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars | APIMemoMask_Polygon) != NoError)
			continue;
		au::Params P (memo.params);
		otd::ZoneData z;
		std::wstring sec;
		if (secProp != APINULLGuid && ReadPropertyText (g, secProp, sec)) {
			z.section = sec;
			++rs.secFromProp;
		} else {
			z.section = otd::Trim (au::ToW (P.Str ("asAddOn_stStaircaseCode")));
		}
		rs.sections[z.section] += 1;
		if (!st.section.empty () && !otd::SameText (z.section, st.section)) {
			ACAPI_DisposeElemMemoHdls (&memo);
			continue;
		}
		++rs.zonesUsed;
		rs.usedLayers[layer.name] += 1;
		rs.usedFloors[el.header.floorInd] += 1;
		if (el.header.hotlinkGuid != APINULLGuid)
			++rs.fromHotlink;
		z.number = otd::Trim (au::ToW (GS::UniString (el.zone.roomNoStr)));
		z.guid = au::ToW (APIGuidToString (g));
		z.roomName = otd::Trim (au::ToW (GS::UniString (el.zone.roomName)));
		z.name = AttrName (API_ZoneCatID, el.zone.catInd);		// наименование — из категории зоны
		if (z.name.empty ())
			z.name = z.roomName;
		z.floor = floors.at (el.header.floorInd);		// номер этажа как в названии
		for (const auto& sr : stories)
			if (sr.index == el.header.floorInd) { z.floorName = sr.name; break; }
		{
			API_Attribute a = {};
			a.header.typeID = API_ZoneCatID;
			a.header.index = el.zone.catInd;
			if (ACAPI_Attribute_Get (&a) == NoError)
				z.catCode = otd::Trim (au::ToW (GS::UniString (a.zoneCat.catCode)));
		}
		{
			const std::wstring code = otd::CatCodeOf (z);
			rs.usedCats[(code.empty () ? std::wstring () : code + L" ") + z.name] += 1;
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

			// стены по контуру зоны, которых нет в связях зоны; участки контура без стен
			std::vector<API_Guid> relWalls;
			for (const API_WallPart& part : rel.wallPart) relWalls.push_back (part.guid);
			const double zBot = (storyLevel.count (el.header.floorInd) ? storyLevel[el.header.floorInd] : 0.0) + el.zone.roomBaseLev;
			const double zTop = zBot + (el.zone.roomHeight > 0.1 ? el.zone.roomHeight : 3.0);
			const double tolC = 0.03;
			for (const Edge& e : edges) {
				otd::Seg sg; sg.x1 = e.x1; sg.y1 = e.y1; sg.x2 = e.x2; sg.y2 = e.y2;
				const double len = sg.Length ();
				if (len < 0.05 || !otd::IsAxisEdge (sg))
					continue;
				std::vector<std::pair<double, double>> cov;
				for (const ColumnBox& c : cols) {
					double a, b;
					if (otd::EdgeOnBox (sg, c.x1, c.y1, c.x2, c.y2, tolC, a, b)) cov.push_back ({ a, b });
				}
				std::vector<std::pair<const WallGeo*, std::pair<double, double>>> extra;
				for (const WallGeo& w : allWalls) {
					if (!w.ortho || std::min (w.top, zTop) - std::max (w.bot, zBot) < 0.5)
						continue;
					double a, b;
					if (!otd::EdgeOnBox (sg, w.x1, w.y1, w.x2, w.y2, tolC, a, b))
						continue;
					if (std::find (relWalls.begin (), relWalls.end (), w.guid) != relWalls.end ())
						cov.push_back ({ a, b });
					else
						extra.push_back ({ &w, { a, b } });
				}
				for (const auto& ex : extra) {
					const WallGeo& w = *ex.first;
					const double a = ex.second.first, b = ex.second.second;
					const double free = otd::UncoveredLength (a, b, cov);
					cov.push_back ({ a, b });
					if (free < 0.05)
						continue;
					const WallInfo wi = ReadWall (w.guid, rules, wallCache);
					otd::WallPiece p;
					p.elemId = wi.id;
					p.construction = wi.construction;
					p.column = wi.column;
					p.byContour = true;
					p.length = free;
					const double bot = std::max (w.bot, zBot), top = std::min (w.top, zTop);
					p.elemHeight = top - bot;
					const double lo = bot + z.floorThick, hi = top - ceil;
					p.finishHeight = std::max (0.0, hi - lo);
					if (st.subtractOpenings) {
						// отрезок ребра -> координата вдоль стены от begC
						const double wl = std::hypot (w.endC.x - w.begC.x, w.endC.y - w.begC.y);
						if (wl > 1e-6) {
							const double ux = (w.endC.x - w.begC.x) / wl, uy = (w.endC.y - w.begC.y) / wl;
							auto along = [&] (double t) {
								const double px = sg.x1 + (sg.x2 - sg.x1) * t / len, py = sg.y1 + (sg.y2 - sg.y1) * t / len;
								return (px - w.begC.x) * ux + (py - w.begC.y) * uy;
							};
							const double t1 = std::min (along (a), along (b)), t2 = std::max (along (a), along (b));
							for (const OpeningInfo& o : WallOpenings (w.guid)) {
								const double ow = std::min (o.loc + o.width / 2, t2) - std::max (o.loc - o.width / 2, t1);
								if (ow <= 1e-4) continue;
								const double oh = otd::OpeningOverlap (w.bot + o.lower, o.height, lo, hi);
								if (oh <= 0) continue;
								p.openings += ow * oh;
								p.nOpenings += 1;
							}
						}
					}
					z.pieces.push_back (p);
					++rs.byContour;
					if (p.column == 0)
						rs.unknown[wi.construction.empty () ? std::wstring (L"(без названия)") : wi.construction] += p.Area ();
				}
				for (const auto& gp : otd::Gaps (len, cov, 0.1))
					z.gaps.push_back (gp.second - gp.first);
			}
			ACAPI_DisposeRoomRelationHdls (&rel);
		}
		if (!z.gaps.empty ()) ++rs.zonesWithGaps;
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


// ----------------------------------------------------------------------------- метка зоны на тексте таблицы проверки
const char* kZoneTag = "OTDZONE:";

void SetZoneTag (const API_Guid& textGuid, const std::wstring& zoneGuid)
{
	const std::string data = std::string (kZoneTag) + otd::ToUtf8 (zoneGuid);
	API_Elem_Head head = {};
	head.guid = textGuid;
	if (ACAPI_Element_GetHeader (&head) != NoError)
		return;
	API_ElementUserData ud = {};
	ud.dataVersion = 1;
	ud.platformSign = GS::Act_Platform_Sign;
	ud.flags = 0;
	ud.dataHdl = BMAllocateHandle ((GSSize) data.size (), ALLOCATE_CLEAR, 0);
	if (ud.dataHdl == nullptr)
		return;
	memcpy (*ud.dataHdl, data.data (), data.size ());
	ACAPI_Element_SetUserData (&head, &ud);
	BMKillHandle (&ud.dataHdl);
}


bool GetZoneTag (const API_Guid& elemGuid, API_Guid& zone)
{
	API_Elem_Head head = {};
	head.guid = elemGuid;
	if (ACAPI_Element_GetHeader (&head) != NoError)
		return false;
	API_ElementUserData ud = {};
	if (ACAPI_Element_GetUserData (&head, &ud) != NoError || ud.dataHdl == nullptr)
		return false;
	const std::string data (*ud.dataHdl, (size_t) BMGetHandleSize (ud.dataHdl));
	BMKillHandle (&ud.dataHdl);
	const size_t n = strlen (kZoneTag);
	if (data.compare (0, n, kZoneTag) != 0)
		return false;
	const std::string g = data.substr (n);
	zone = APIGuidFromString (g.c_str ());
	return zone != APINULLGuid;
}


// Рисует таблицу в текущей базе (рабочем листе): прежние линии и тексты листа удаляются.
// Линии кладутся в слой текстов (слой линий по умолчанию бывает скрыт на рабочем листе).
struct DrawResult {
	int				lines = 0, texts = 0, lineErrs = 0, textErrs = 0;
	GSErrCode		lineErr = NoError, textErr = NoError;
	std::wstring	layer;
	bool			layerHidden = false;
};

GSErrCode DrawTable (const otd::TableLayout& t, DrawResult& dr)
{
	dr = DrawResult ();
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

	API_Element textDef = {};
	textDef.header.type = API_TextID;
	GSErrCode err = ACAPI_Element_GetDefaults (&textDef, nullptr);
	if (err != NoError)
		return err;
	API_Element lineDef = {};
	lineDef.header.type = API_LineID;
	err = ACAPI_Element_GetDefaults (&lineDef, nullptr);
	if (err != NoError)
		return err;
	lineDef.header.layer = textDef.header.layer;
	{
		LayerCache lc;
		const LayerInfo li = ReadLayer (textDef.header.layer, lc);
		dr.layer = li.name;
		dr.layerHidden = li.hidden;
	}

	for (const otd::TSeg& g : t.segs) {
		API_Element el = lineDef;
		el.header.guid = APINULLGuid;
		el.line.begC.x = g.x1 * k; el.line.begC.y = g.y1 * k;
		el.line.endC.x = g.x2 * k; el.line.endC.y = g.y2 * k;
		const GSErrCode e = ACAPI_Element_Create (&el, nullptr);
		if (e == NoError) ++dr.lines;
		else { if (dr.lineErrs++ == 0) dr.lineErr = e; }
	}

	for (const otd::TText& tx : t.texts) {
		API_Element el = textDef;
		el.header.guid = APINULLGuid;
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
		const GSErrCode e = ACAPI_Element_Create (&el, &memo);
		if (e == NoError) {
			++dr.texts;
			if (!tx.tag.empty ())
				SetZoneTag (el.header.guid, tx.tag);
		}
		else { if (dr.textErrs++ == 0) dr.textErr = e; }
		ACAPI_DisposeElemMemoHdls (&memo);
	}
	return NoError;
}

}	// namespace

// ============================================================================= команда меню

void DoOtdelka (bool checkOnly)
{
	Settings st;
	st.Parse (LoadModulText (kSettingsKey));
	bool withCheck = checkOnly;
	if (!checkOnly) {		// из меню «Ведомость + таблица проверки зон» — без окна, с последними настройками
		OtdDialog dlg (st);
		if (!dlg.Invoke ())
			return;
		withCheck = dlg.withCheck;
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
	std::set<int> wanted;
	otd::ParseFloors (st.floors, wanted);
	int actStory = 0;
	const std::vector<StoryRec> stories = LoadStories (&actStory);
	const std::map<int, int> floors = ResolveFloors (wanted, stories);
	std::wstring secPropName;
	const API_Guid secProp = FindPropertyDef ({ L"Секция №", L"Секция", L"Номер секции" }, secPropName);
	std::vector<std::wstring> lines, warn;
	std::wstring rulesPath;
	const otd::Rules rules = LoadRules (warn, rulesPath);

	RunStats rs;
	const std::vector<otd::ZoneData> zones = ReadZones (st, floors, stories, secProp, rules, rs);

	std::wstring floorsInfo;
	for (const auto& kv : floors)
		floorsInfo += (floorsInfo.empty () ? L"" : L", ") + std::to_wstring (kv.second) + L" -> " + StoryLabel (stories, kv.first);
	const std::wstring secInfo = secProp != APINULLGuid ? L"Секция — из свойства «" + secPropName + L"» (у " + std::to_wstring (rs.secFromProp) +
		L" зон), иначе из параметра зоны." : L"Секция — из параметра зоны «Секция» (свойство «Секция №» в проекте не найдено).";

	if (zones.empty ()) {
		lines.push_back (L"Зоны для ведомости не найдены.");
		lines.push_back (L"Этажи: " + floorsInfo + L". Открытый этаж: " + StoryLabel (stories, actStory) + L".");
		lines.push_back (secInfo);
		lines.push_back (L"Зон в проекте: " + std::to_wstring (rs.zonesAll) + L", из них «" + st.zoneLib + L"…»: " + std::to_wstring (rs.zonesLib) + L".");
		if (!rs.sections.empty ()) {
			std::wstring s;
			for (const auto& kv : rs.sections)
				s += (s.empty () ? L"" : L", ") + (kv.first.empty () ? std::wstring (L"(пусто)") : kv.first) + L" — " + std::to_wstring (kv.second);
			lines.push_back (L"На этажах " + st.floors + L" в параметре зоны «Секция» записано: " + s + L".");
		} else {
			lines.push_back (L"На этажах " + st.floors + L" зон «" + st.zoneLib + L"» нет. Номер этажа — как в навигаторе (1. Этаж 1 -> 1).");
		}
		if (!rs.otherLib.empty ())
			lines.push_back (L"Зоны других объектов на этих этажах: " + JoinCounts (rs.otherLib) + L".");
		if (!rs.hiddenLayers.empty ())
			lines.push_back (L"Пропущены зоны на скрытых слоях: " + JoinCounts (rs.hiddenLayers) + L".");
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
	DrawResult dr;
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
		otd::TableLayout layout = otd::LayoutSheet (sheet);
		if (withCheck)		// таблица проверки — справа от ведомости
			otd::AppendLayout (layout, otd::LayoutCheck (zones, rules,
				L"Проверка: зоны секции " + secName + L" по подкатегориям (этажи " + st.floors + L")"), 20);
		wsErr = ACAPI_CallUndoableCommand (au::FromW (L"Ведомость отделки: таблица"), [&] () -> GSErrCode {
			return DrawTable (layout, dr);
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
	if (wsErr == NoError) {
		lines.push_back (L"Таблица построена на рабочем листе «" + wsName + L"»" + (existed ? L" (лист перерисован заново)" : L"") +
						 L": линий " + std::to_wstring (dr.lines) + L", текстов " + std::to_wstring (dr.texts) + L", слой «" + dr.layer + L"».");
		if (dr.lineErrs > 0)
			lines.push_back (L"   Не создано линий: " + std::to_wstring (dr.lineErrs) + L" (код " + std::to_wstring (dr.lineErr) + L").");
		if (dr.textErrs > 0)
			lines.push_back (L"   Не создано текстов: " + std::to_wstring (dr.textErrs) + L" (код " + std::to_wstring (dr.textErr) + L").");
		if (dr.layerHidden)
			lines.push_back (L"   Слой «" + dr.layer + L"» скрыт — включите его, чтобы увидеть таблицу.");
	} else
		lines.push_back (L"Таблицу на рабочем листе построить не удалось: шаг «" + wsStep + L"», код " + std::to_wstring (wsErr) + L".");
	lines.push_back (L"Этажи: " + floorsInfo + L". " + secInfo);
	lines.push_back (L"Зон в расчёте: " + std::to_wstring (rs.zonesUsed) + L", строк ведомости: " + std::to_wstring (sheet.rows.size ()) + L".");
	{
		std::wstring fl;
		for (const auto& kv : rs.usedFloors)
			fl += (fl.empty () ? L"" : L", ") + StoryLabel (stories, kv.first) + L" — " + std::to_wstring (kv.second);
		lines.push_back (L"   по этажам: " + fl + L";");
		lines.push_back (L"   по слоям: " + JoinCounts (rs.usedLayers) + L";");
		lines.push_back (L"   по категориям: " + JoinCounts (rs.usedCats, 12) + L".");
		if (rs.fromHotlink > 0)
			lines.push_back (L"   Из них из модулей (hotlink): " + std::to_wstring (rs.fromHotlink) + L".");
	}
	{
		std::map<std::wstring, int> excl;
		for (const auto& z : zones)
			if (otd::IsExcluded (z, rules)) excl[otd::CatCodeOf (z) + L" " + z.name] += 1;
		if (!excl.empty ())
			lines.push_back (L"Не входят в ведомость (раздел [Группы], «-»): " + JoinCounts (excl) + L".");
	}
	if (rs.byContour > 0)
		lines.push_back (L"Стены по контуру зоны (Archicad не связал их с зоной, добавлены по геометрии): " + std::to_wstring (rs.byContour) +
						 L" участков — на листе «Расчёт» помечены «по контуру».");
	if (rs.zonesWithGaps > 0)
		lines.push_back (L"Зон с участками контура без стен: " + std::to_wstring (rs.zonesWithGaps) + L" — см. лист «Расчёт» (проходы, проёмы без стены или элементы не-стены).");
	if (!rs.hiddenLayers.empty ())
		lines.push_back (L"Пропущены зоны на скрытых слоях: " + JoinCounts (rs.hiddenLayers) + L".");
	{
		std::map<std::wstring, int> other;
		for (const auto& kv : rs.sections)
			if (!otd::SameText (kv.first, st.section)) other[kv.first] = kv.second;
		if (!st.section.empty () && !other.empty ())
			lines.push_back (L"На этих этажах есть зоны других секций (не вошли): " + JoinCounts (other) + L".");
	}
	if (!rs.otherLib.empty ())
		lines.push_back (L"Зоны других объектов (не «" + st.zoneLib + L"…», не вошли): " + JoinCounts (rs.otherLib) + L".");
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

// ============================================================================= выбор зон из таблицы проверки
// На рабочем листе выделите тексты строк таблицы проверки (имя зоны, номер — любой текст строки)
// и запустите команду: план нужного этажа открывается, зоны выделяются и показываются крупно.

void DoSelectZones ()
{
	API_SelectionInfo selInfo = {};
	GS::Array<API_Neig> sel;
	ACAPI_Selection_Get (&selInfo, &sel, false);
	BMKillHandle ((GSHandle*) &selInfo.marquee.coords);

	std::vector<API_Guid> zones;
	for (const API_Neig& n : sel) {
		API_Guid z;
		if (GetZoneTag (n.guid, z) && std::find (zones.begin (), zones.end (), z) == zones.end ())
			zones.push_back (z);
	}
	if (zones.empty ()) {
		ShowReport ({ L"Выделите на рабочем листе «Ведомость отделки - секция N» текст строки таблицы проверки",
					  L"(имя или номер зоны; можно несколько строк) и снова запустите команду.",
					  L"Таблица проверки строится кнопкой «Рассчитать + проверка зон»." });
		return;
	}

	{	// план этажа
		API_WindowInfo wi = {};
		wi.typeID = APIWind_FloorPlanID;
		ACAPI_Window_ChangeWindow (&wi);
	}
	// этаж — первой найденной зоны; зоны на других этажах — в отчёт
	GS::Array<API_Neig> neigs;
	short story = 0;
	bool hasStory = false;
	std::vector<std::wstring> other;
	int missing = 0;
	for (const API_Guid& g : zones) {
		API_Elem_Head h = {};
		h.guid = g;
		if (ACAPI_Element_GetHeader (&h) != NoError) { ++missing; continue; }
		if (!hasStory) { story = h.floorInd; hasStory = true; }
		if (h.floorInd != story) { other.push_back (std::to_wstring (h.floorInd)); continue; }
		neigs.Push (API_Neig (g));
	}
	if (!hasStory) {
		ShowReport ({ L"Зоны из выделенных строк в проекте не найдены (удалены?). Пересчитайте таблицу проверки." });
		return;
	}
	API_StoryCmdType cmd = {};
	cmd.action = APIStory_GoTo;
	cmd.index = story;
	ACAPI_ProjectSetting_ChangeStorySettings (&cmd);

	ACAPI_Selection_DeselectAll ();
	ACAPI_Selection_Select (neigs, true);
	ACAPI_View_ZoomToSelected ();

	if (!other.empty () || missing > 0) {
		std::vector<std::wstring> lines;
		lines.push_back (L"Выделено зон: " + std::to_wstring (neigs.GetSize ()) + L" (этаж с индексом " + std::to_wstring (story) + L").");
		if (!other.empty ())
			lines.push_back (L"Зоны на других этажах (выделите их строки отдельно): " + std::to_wstring (other.size ()) + L".");
		if (missing > 0)
			lines.push_back (L"Не найдено в проекте: " + std::to_wstring (missing) + L" — пересчитайте таблицу проверки.");
		ShowReport (lines);
	}
}
