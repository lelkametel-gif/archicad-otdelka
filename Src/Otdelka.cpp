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
#include "TepCore.hpp"
#include "ZoneParamsFile.hpp"
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


// Контуры зоны (внешний и отверстия) из memo
otd::Polys ZonePolys (const API_ElementMemo& memo)
{
	otd::Polys res;
	if (memo.coords == nullptr || memo.pends == nullptr)
		return res;
	const Int32 nSub = (Int32) (BMGetHandleSize ((GSConstHandle) memo.pends) / sizeof (Int32)) - 1;
	for (Int32 k = 1; k <= nSub; ++k) {
		const Int32 i1 = (*memo.pends)[k - 1] + 1, i2 = (*memo.pends)[k];
		std::vector<otd::Pt> poly;
		for (Int32 i = i1; i < i2; ++i)			// последняя точка = первой, её не берём
			poly.push_back ({ (*memo.coords)[i].x, (*memo.coords)[i].y });
		if (poly.size () >= 3)
			res.push_back (poly);
	}
	return res;
}


struct RunStats {
	int zonesAll = 0, zonesLib = 0, zonesUsed = 0, floorFromSlab = 0, floorFromZone = 0;
	int onFloors = 0;							// зон любого объекта на выбранных этажах
	std::map<std::wstring, int> sections;		// какие секции встречаются в зонах выбранных этажей
	std::map<std::wstring, double> unknown;		// конструкции без столбца -> м²
	std::map<std::wstring, int> otherLib;		// зоны выбранных этажей другого объекта: имя объекта -> шт
	std::vector<std::pair<API_Guid, std::wstring>> otherLibZones;	// они же: GUID и имя объекта (для свойства «проверка»)
	std::map<std::wstring, int> hiddenLayers;	// пропущены (скрытый слой): слой -> шт
	std::map<std::wstring, int> usedLayers;		// взяты в расчёт: слой -> шт
	std::map<int, int> usedFloors;				// взяты в расчёт: этаж -> шт
	std::map<std::wstring, int> usedCats;		// взяты в расчёт: «код категория» -> шт
	int fromHotlink = 0;						// взяты в расчёт из модулей (hotlink)
	int secFromProp = 0;						// секция взята из свойства
	int col3Moved = 0;							// ГКЛ вне квартир -> другой столбец
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


// ----------------------------------------------------------------------------- стены и колонны в плане
// Все прямые ортогональные стены и колонны проекта — прямоугольники в плане с отметками низа и верха.
// Отделка зоны — грани этих прямоугольников, обращённые в зону (otd::ExposedFaces), независимо от того,
// связал ли Archicad элемент с зоной (стена другого этажа, «не влияет на зоны», пилон внутри зоны).
struct ElemGeo {
	API_Guid	guid = APINULLGuid;
	bool		isColumn = false;
	int			floor = 0;							// собственный этаж (индекс)
	bool		ortho = false;						// прямоугольник в плане точный
	double		x1 = 0, y1 = 0, x2 = 0, y2 = 0;		// габарит в плане
	double		bot = 0, top = 0;					// абсолютные отметки, м
	API_Coord	begC = {}, endC = {};				// ось стены
	double		height = 0;
	bool		hidden = false;						// скрытый слой: не считается, но закрывает грани за собой
	bool		polyWall = false;					// многоугольная стена (контур из memo)
	otd::Shape	shape;								// контур в плане: прямоугольник или многоугольник стены
};

std::vector<ElemGeo> LoadElemGeo (const std::vector<StoryRec>& stories, bool skipHidden, LayerCache& layers)
{
	std::vector<ElemGeo> res;
	std::map<int, double> lev;
	for (const auto& r : stories) lev[r.index] = r.level;
	for (API_ElemTypeID type : { API_WallID, API_ColumnID }) {
		GS::Array<API_Guid> list;
		if (ACAPI_Element_GetElemList (API_ElemType (type), &list) != NoError)
			continue;
		for (const API_Guid& g : list) {
			API_Element el = {};
			el.header.guid = g;
			if (ACAPI_Element_Get (&el) != NoError)
				continue;
			ElemGeo w;
			w.hidden = skipHidden && ReadLayer (el.header.layer, layers).hidden;
			w.guid = g;
			w.isColumn = type == API_ColumnID;
			w.floor = el.header.floorInd;
			const double base = lev.count (el.header.floorInd) ? lev[el.header.floorInd] : 0.0;
			if (w.isColumn) {
				const double a = std::fmod (std::fabs (el.column.axisRotationAngle), 1.5707963267948966);
				w.ortho = !el.column.isSlanted && (a < 1e-4 || 1.5707963267948966 - a < 1e-4);
				w.bot = base + el.column.bottomOffset;
				w.height = el.column.height;
			} else {
				w.begC = el.wall.begC;
				w.endC = el.wall.endC;
				w.ortho = el.wall.type == APIWtyp_Normal && std::fabs (el.wall.angle) < 1e-6 &&
						  (std::fabs (w.begC.x - w.endC.x) < 1e-3 || std::fabs (w.begC.y - w.endC.y) < 1e-3);
				w.bot = base + el.wall.bottomOffset;
				w.height = el.wall.height;
			}
			w.top = w.bot + w.height;
			API_Box3D box = {};
			if (ACAPI_Element_CalcBounds (&el.header, &box) != NoError)
				continue;
			w.x1 = box.xMin; w.y1 = box.yMin; w.x2 = box.xMax; w.y2 = box.yMax;
			w.shape = otd::BoxShape ({ w.x1, w.y1, w.x2, w.y2 });
			if (!w.isColumn && el.wall.type == APIWtyp_Poly) {		// многоугольная стена — по её контуру
				API_ElementMemo memo = {};
				if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_Polygon) == NoError && memo.coords != nullptr && memo.pends != nullptr) {
					const Int32 i2 = (*memo.pends)[1];
					otd::Shape sh;
					for (Int32 k = 1; k < i2; ++k)
						sh.push_back ({ (*memo.coords)[k].x, (*memo.coords)[k].y });
					if (sh.size () >= 3) { w.shape = sh; w.ortho = true; w.polyWall = true; }
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			}
			res.push_back (w);
		}
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
	const std::vector<ElemGeo> allElems = LoadElemGeo (stories, st.skipHidden, layerCache);
	std::map<API_Guid, std::vector<OpeningInfo>> openCache;
	std::map<API_Guid, const ElemGeo*> geoByGuid;
	for (const ElemGeo& e : allElems) geoByGuid[e.guid] = &e;
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
		if (lib.find (libPrefix) == std::wstring::npos) {		// имя объекта содержит «HimeraZone» (АСП_HimeraZone, HimeraZone22)
			if (onFloors) {
				rs.otherLib[libName.empty () ? std::wstring (L"(без объекта)") : libName] += 1;
				if (el.header.hotlinkGuid == APINULLGuid)
					rs.otherLibZones.push_back ({ g, libName });
			}
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
		const otd::Polys polys = ZonePolys (memo);
		for (const auto& poly : polys)
			for (size_t pi = 0; pi < poly.size (); ++pi) {
				const auto& a = poly[pi];
				const auto& b = poly[(pi + 1) % poly.size ()];
				z.perimeter += std::hypot (b.first - a.first, b.second - a.second);
			}
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

		// высота отделки = высота зоны (пол в ней уже учтён). Если полы ещё не замоделированы (нет перекрытия
		// в слое «Полы» в зоне) — вычитается толщина пола: «Толщина пола» в параметрах зоны, иначе по категории из окна
		double slabThick = 0;
		std::wstring slabSrc;
		if (hasRel && st.floorFromSlab && FloorFromSlabs (rel, st.floorLayer, slabThick, slabSrc)) {
			z.floorThick = 0;
			z.floorSource = L"полы замоделированы (" + slabSrc + L") — учтено в высоте зоны";
			++rs.floorFromSlab;
		} else if (el.zone.roomFlThick > 0.0005) {
			z.floorThick = el.zone.roomFlThick;
			z.floorSource = L"толщина пола в параметрах зоны";
			++rs.floorFromZone;
		} else {
			z.floorThick = st.FloorFor (z.catCode) / 1000.0;
			z.floorSource = L"из окна (категория " + z.catCode + L")";
		}

		// ---- стены и колонны: грани, обращённые в зону (по плану, без учёта связей зоны)
		const double zBot = (storyLevel.count (el.header.floorInd) ? storyLevel[el.header.floorInd] : 0.0) + el.zone.roomBaseLev;
		const double zTop = zBot + (el.zone.roomHeight > 0.1 ? el.zone.roomHeight : 3.0);
		z.finishH = std::max (0.0, (zTop - ceil) - (zBot + z.floorThick));		// строго по высоте зоны
		double zx1 = 1e30, zy1 = 1e30, zx2 = -1e30, zy2 = -1e30;
		for (const auto& poly : polys)
			for (const auto& pt : poly) {
				zx1 = std::min (zx1, pt.first); zy1 = std::min (zy1, pt.second);
				zx2 = std::max (zx2, pt.first); zy2 = std::max (zy2, pt.second);
			}
		std::vector<const ElemGeo*> nearElems;			// элементы у зоны, проходящие по её высоте
		std::vector<otd::Shape> boxes;
		std::vector<int> prio;						// колонна вырезает стену
		const bool excludedZone = otd::IsExcluded (z, rules);		// 8NNN/9NNN — в ведомость не входит, по плану не считаем
		for (const ElemGeo& e : allElems) {
			if (excludedZone) break;
			if (e.x2 < zx1 - 0.1 || e.x1 > zx2 + 0.1 || e.y2 < zy1 - 0.1 || e.y1 > zy2 + 0.1)
				continue;
			if (std::min (e.top, zTop) - std::max (e.bot, zBot) < 0.3)
				continue;
			nearElems.push_back (&e);
			boxes.push_back (e.shape);
			// приоритет на общей грани, как при пересечении в Archicad: колонна > ж/б стена > кладка, ГКЛ, штукатурка
			int pr = 0;
			if (e.isColumn) pr = 2;
			else if (otd::IsReinforcedConcrete (ReadWall (e.guid, rules, wallCache).construction)) pr = 1;
			prio.push_back (pr);
		}
		// Основное — связи зоны: стены зоны считаются ниже по связям. По плану добавляются только
		// элементы, которые ограничивают зону, но не связаны с ней (о них — заметка в таблице проверки),
		// и колонны зоны (их открытые грани).
		std::set<API_Guid> relWalls, relCols;
		if (hasRel) {
			for (const API_WallPart& part : rel.wallPart) relWalls.insert (part.guid);
			const API_ElemType ct (API_ColumnID);
			if (rel.elementsGroupedByType.ContainsKey (ct))
				for (const API_Guid& cg : rel.elementsGroupedByType[ct]) relCols.insert (cg);
		}
		std::vector<otd::Shape> noHidden = boxes;	// для диагностики: без элементов скрытых слоёв
		for (size_t i = 0; i < nearElems.size (); ++i)
			if (nearElems[i]->hidden) noHidden[i].clear ();
		auto elemLabel = [&] (const ElemGeo& e) {
			const std::wstring id = ElemId (e.guid);
			return (e.isColumn ? std::wstring (L"колонна ") : std::wstring (L"стена ")) + (id.empty () ? std::wstring (L"без ID") : id) +
				   L" (центр X " + otd::FormatNum ((e.x1 + e.x2) / 2, 2) + L" Y " + otd::FormatNum ((e.y1 + e.y2) / 2, 2) + L")";
		};
		auto runsLen = [] (const std::vector<otd::FaceRun>& rr) { double s = 0; for (const auto& r : rr) s += r.Length (); return s; };
		std::map<API_Guid, double> geoRel;			// стены из связей зоны: открытая в зону длина по плану
		for (size_t i = 0; i < nearElems.size (); ++i) {
			const ElemGeo& e = *nearElems[i];
			if (!e.isColumn && e.ortho && relWalls.count (e.guid) != 0)
				geoRel[e.guid] = runsLen (otd::ExposedFaces (polys, boxes, i, 0.01, prio));
		}
		for (size_t i = 0; i < nearElems.size (); ++i) {
			const ElemGeo& e = *nearElems[i];
			const bool related = e.isColumn ? relCols.count (e.guid) != 0 : relWalls.count (e.guid) != 0;
			if (!e.isColumn && related)
				continue;						// стена зоны — по связям ниже
			if (!e.ortho)
				continue;
			const std::vector<otd::FaceRun> runs = otd::ExposedFaces (polys, boxes, i, 0.01, prio);
			const double len = runsLen (runs);
			// не берём: скрытый слой; не связан с зоной и стоит на другом этаже (на плане зоны его не видно)
			std::wstring why;
			if (e.hidden) why = L"скрытый слой";
			else if (!related && e.floor != el.header.floorInd) why = L"другой этаж (на плане этажа зоны не виден)";
			if (!why.empty ()) {
				if (len >= 0.05)
					z.skipped.push_back (L"не взят по плану: " + elemLabel (e) + L", " + otd::FormatNum (len, 2) + L" м — " + why);
				continue;
			}
			if (len < 0.02) {
				const double len2 = runsLen (otd::ExposedFaces (polys, noHidden, i, 0.01, prio));
				if (len2 >= 0.05)
					z.skipped.push_back (L"не взят по плану: " + elemLabel (e) + L", " + otd::FormatNum (len2, 2) +
										 L" м — закрыт элементом скрытого слоя");
				else {		// сам по себе выходит в зону, но грань отдана соседнему элементу
					const std::vector<otd::Shape> alone = { e.shape };
					const double len3 = runsLen (otd::ExposedFaces (polys, alone, 0, 0.01));
					if (len3 >= 0.3)
						z.skipped.push_back (L"не взят по плану: " + elemLabel (e) + L", " + otd::FormatNum (len3, 2) +
											 L" м у контура — грань отдана соседнему элементу (облицовка, стена с бо́льшим приоритетом)");
				}
				continue;
			}
			otd::WallPiece p;
			// высота отделки = высота зоны − пол − потолок; элемент ниже зоны — до своего верха
			const double lo = zBot + z.floorThick, hi = zTop - ceil;		// строго по высоте зоны
			p.length = len;
			p.elemHeight = e.top - e.bot;
			p.finishHeight = std::max (0.0, hi - lo);
			p.lowBy = std::max (0.0, (zTop - ceil) - e.top);		// заканчивается ниже зоны — проверить
			if (e.isColumn) {
				p.elemId = ElemId (e.guid);
				// размеры в плане и центр — чтобы найти колонну на плане по листу «Расчёт»
				p.construction = L"колонна / пилон " + otd::FormatNum (e.x2 - e.x1, 2) + L"×" + otd::FormatNum (e.y2 - e.y1, 2) +
								 L" м, центр X " + otd::FormatNum ((e.x1 + e.x2) / 2, 2) + L" Y " + otd::FormatNum ((e.y1 + e.y2) / 2, 2);
				p.isColumn = true;
				p.column = rules.columnsColumn;
				p.byContour = relCols.count (e.guid) == 0;
			} else {
				const WallInfo wi = ReadWall (e.guid, rules, wallCache);
				p.elemId = wi.id;
				p.construction = wi.construction + L" (центр X " + otd::FormatNum ((e.x1 + e.x2) / 2, 2) + L" Y " +
								 otd::FormatNum ((e.y1 + e.y2) / 2, 2) + L")";
				p.column = wi.column;
				p.byContour = true;
				if (st.subtractOpenings) {
					auto it = openCache.find (e.guid);
					if (it == openCache.end ()) it = openCache.emplace (e.guid, WallOpenings (e.guid)).first;
					const double wl = std::hypot (e.endC.x - e.begC.x, e.endC.y - e.begC.y);
					if (wl > 1e-6 && !e.polyWall && !it->second.empty ()) {
						const double ux = (e.endC.x - e.begC.x) / wl, uy = (e.endC.y - e.begC.y) / wl;
						for (const auto& r : runs) {
							const double ta = (r.ax - e.begC.x) * ux + (r.ay - e.begC.y) * uy;
							const double tb = (r.bx - e.begC.x) * ux + (r.by - e.begC.y) * uy;
							const double t1 = std::min (ta, tb), t2 = std::max (ta, tb);
							if (t2 - t1 < 0.01) continue;		// торец стены
							for (const OpeningInfo& o : it->second) {
								const double ow = std::min (o.loc + o.width / 2, t2) - std::max (o.loc - o.width / 2, t1);
								if (ow <= 1e-4) continue;
								const double oh = otd::OpeningOverlap (e.bot + o.lower, o.height, lo, hi);
								if (oh <= 0) continue;
								p.openings += ow * oh;
								p.nOpenings += 1;
							}
						}
					}
				}
			}
			if (p.byContour) {
				++rs.byContour;
				z.notes.push_back ((p.isColumn ? L"кол. " : L"") + (p.elemId.empty () ? std::wstring (L"без ID") : p.elemId) +
								   L" " + otd::FormatNum (p.length, 2));
			}
			z.pieces.push_back (p);
			if (p.column == 0)
				rs.unknown[p.construction.empty () ? std::wstring (L"(без названия)") : p.construction] += p.Area ();
		}

		// ---- стены из связей зоны (основной расчёт)
		if (hasRel) {
			const std::vector<OpeningInfo> openings = st.subtractOpenings ? ReadOpenings (rel) : std::vector<OpeningInfo> ();
			// стена из связей частично закрыта колоннами / ж/б (пилоны на стене КБ): длина участков уменьшается
			// пропорционально открытой по плану длине, чтобы закрытые части не считались дважды
			std::map<API_Guid, double> relSum;
			for (const API_WallPart& part : rel.wallPart) relSum[part.guid] += std::fabs (part.tEnd - part.tBeg);
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
				{
					const auto gi2 = geoRel.find (part.guid);
					const double rsum = relSum[part.guid];
					if (gi2 != geoRel.end () && gi2->second > 0.05 && rsum > 1e-6 && gi2->second < rsum - 0.05) {
						const double cut = p.length * (1.0 - gi2->second / rsum);
						p.length -= cut;
						p.construction += L" (минус " + otd::FormatNum (cut, 2) + L" м под колоннами / ж/б)";
					}
				}
				// высота отделки = высота зоны − пол − потолок; стена ниже зоны — на свою высоту
				double wBot = zBot, wTop = zBot + w.height;
				const auto gi = geoByGuid.find (part.guid);
				if (gi != geoByGuid.end ()) { wBot = gi->second->bot; wTop = gi->second->top; }
				p.elemHeight = wTop - wBot;
				const double lo = zBot + z.floorThick, hi = zTop - ceil;		// строго по высоте зоны
				p.finishHeight = std::max (0.0, hi - lo);
				p.lowBy = std::max (0.0, (zTop - ceil) - wTop);
				for (const OpeningInfo& o : openings) {
					if (o.owner != part.guid)
						continue;
					const double ow = std::min (o.loc + o.width / 2, t2) - std::max (o.loc - o.width / 2, t1);
					if (ow <= 1e-4)
						continue;
					const double oh = otd::OpeningOverlap (wBot + o.lower, o.height, lo, hi);
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
			ACAPI_DisposeRoomRelationHdls (&rel);
		}

		// ---- участки контура без стен и колонн (проходы)
		if (!excludedZone)
			z.gaps = otd::ContourGaps (polys, boxes);
		rs.col3Moved += otd::ApplyColumnRules (z, rules);
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

// Шрифт таблиц: Arial Narrow Кириллический (иначе Arial Narrow; нет — шрифт текста по умолчанию)
short TableFont ()
{
	for (const wchar_t* name : { L"Arial Narrow Кириллический", L"Arial Narrow" }) {
		API_FontType f = {};
		GS::UniString n = au::FromW (name);
		f.head.index = 0;
		f.head.uniStringNamePtr = &n;
		if (ACAPI_Font_SearchFont (f) == NoError && f.head.index > 0)
			return (short) f.head.index;
	}
	return 0;
}

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
	{
		const short fnt = TableFont ();
		if (fnt > 0)
			textDef.text.font = fnt;
	}
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
		el.text.anchor = tx.left ? APIAnc_LM : APIAnc_MM;
		el.text.just = tx.left ? APIJust_Left : APIJust_Center;
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


// ============================================================================= паспорта квартир и ТЭП

const wchar_t*	kPassportLib = L"Паспорт_АСП";

std::wstring SafeFileName (std::wstring s)
{
	for (wchar_t& c : s)
		if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == L';')
			c = L'_';
	return s;
}

// «ТЭП - <имя проекта>» (у безымянного проекта — «ТЭП»)
std::wstring TepFileBase ()
{
	API_ProjectInfo pi;
	if (ACAPI_ProjectOperation_Project (&pi) == NoError && !pi.untitled && pi.projectName != nullptr) {
		std::wstring n = otd::Trim (au::ToW (*pi.projectName));
		const size_t dot = n.rfind (L'.');
		if (dot != std::wstring::npos && dot + 5 >= n.size ()) n.erase (dot);	// без расширения .pln
		if (!n.empty ()) return L"ТЭП - " + SafeFileName (n);
	}
	return L"ТЭП";
}

// Данные ТЭП и паспортов: файл в «Документах»; если его нет — создаётся по умолчанию
tep::Config LoadTepConfig (std::vector<std::wstring>& warnings, std::wstring& path, bool& created)
{
	created = false;
	IO::Location loc;
	if (!DocumentsFile (TepFileBase () + L".txt", loc))
		return tep::ParseConfig (tep::DefaultConfigText (), &warnings);
	path = PathOf (loc);
	std::string bytes;
	if (!ReadBytes (loc, bytes)) {
		std::wstring crlf;
		for (wchar_t c : tep::DefaultConfigText ()) { if (c == L'\n') crlf += L'\r'; crlf += c; }
		WriteBytes (loc, "\xEF\xBB\xBF" + otd::ToUtf8 (crlf));
		created = true;
		return tep::ParseConfig (tep::DefaultConfigText (), &warnings);
	}
	std::wstring text = otd::FromUtf8 (bytes);
	if (!text.empty () && text[0] == 0xFEFF) text.erase (0, 1);
	if (tep::MigrateConfigText (text)) {
		WriteBytes (loc, "\xEF\xBB\xBF" + otd::ToUtf8 (text));
		warnings.push_back (L"Файл данных ТЭП обновлён до версии " + std::to_wstring (tep::kConfigVersion) +
							L" (подвал — 3xxx; кладовые — 4xxx, нежилые — 6xxx, общедомовые — 2xxx; в паспортах — запятая), если эти строки не меняли вручную.");
	}
	return tep::ParseConfig (text, &warnings);
}


struct RoomStats {
	int all = 0, lib = 0, fromStamp = 0, fromArchicad = 0, fromHotlink = 0;
	std::map<std::wstring, int> hiddenLayers;
};

// Все зоны HimeraZone всех этажей и секций (зоны на скрытых слоях — по настройке окна ведомости)
std::vector<tep::Room> ReadRooms (const Settings& st, const std::vector<StoryRec>& stories, const API_Guid& secProp, RoomStats& rs)
{
	std::vector<tep::Room> res;
	GS::Array<API_Guid> list;
	if (ACAPI_Element_GetElemList (API_ElemType (API_ZoneID), &list) != NoError)
		return res;
	const std::wstring libPrefix = otd::Lower (st.zoneLib);
	LayerCache layerCache;
	std::map<int, std::wstring> storyName;
	std::map<int, bool> storyUnder;
	for (const auto& sr : stories) {
		storyName[sr.index] = sr.name;
		storyUnder[sr.index] = tep::IsUnderground (sr.index, sr.level, sr.name);
	}
	for (const API_Guid& g : list) {
		++rs.all;
		API_Element el = {};
		el.header.guid = g;
		if (ACAPI_Element_Get (&el) != NoError)
			continue;
		const std::wstring lib = otd::Lower (otd::Trim (au::ToW (au::LibPartName (el.zone.libInd))));
		if (lib.find (libPrefix) == std::wstring::npos)
			continue;
		++rs.lib;
		const LayerInfo layer = ReadLayer (el.header.layer, layerCache);
		if (st.skipHidden && layer.hidden) {
			rs.hiddenLayers[layer.name] += 1;
			continue;
		}
		API_ElementMemo memo = {};
		if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars | APIMemoMask_Polygon) != NoError)
			continue;
		tep::Room r;
		{
			au::Params P (memo.params);
			std::wstring sec;
			if (secProp != APINULLGuid && ReadPropertyText (g, secProp, sec))
				r.section = sec;
			else
				r.section = otd::Trim (au::ToW (P.Str ("asAddOn_stStaircaseCode")));
			r.apartment = otd::Trim (au::ToW (P.Str ("asAddOn_stApartmentCode")));
			r.area = P.Real ("asAddOn_CalcArea", 0);		// площадь, как в штампе зоны
			const otd::Polys polys = ZonePolys (memo);
			r.x1 = r.y1 = 1e30; r.x2 = r.y2 = -1e30;
			for (const auto& poly : polys)
				for (const auto& pt : poly) {
					r.x1 = std::min (r.x1, pt.first); r.y1 = std::min (r.y1, pt.second);
					r.x2 = std::max (r.x2, pt.first); r.y2 = std::max (r.y2, pt.second);
				}
			if (polys.empty ()) { r.x1 = r.y1 = r.x2 = r.y2 = 0; }
			r.polys = polys;
		}
		ACAPI_DisposeElemMemoHdls (&memo);
		r.guid = au::ToW (APIGuidToString (g));
		r.number = otd::Trim (au::ToW (GS::UniString (el.zone.roomNoStr)));
		const std::wstring catName = AttrName (API_ZoneCatID, el.zone.catInd);
		std::wstring catCode;
		{
			API_Attribute a = {};
			a.header.typeID = API_ZoneCatID;
			a.header.index = el.zone.catInd;
			if (ACAPI_Attribute_Get (&a) == NoError)
				catCode = otd::Trim (au::ToW (GS::UniString (a.zoneCat.catCode)));
		}
		r.catCode = tep::CodeOf (catCode, catName);
		r.name = otd::Trim (au::ToW (GS::UniString (el.zone.roomName)));
		if (r.name.empty ()) r.name = catName;
		r.story = el.header.floorInd;
		r.storyName = storyName.count (r.story) ? storyName[r.story] : std::to_wstring (r.story);
		r.underground = storyUnder.count (r.story) ? storyUnder[r.story] : r.story < 0;
		r.height = el.zone.roomHeight;
		if (el.header.hotlinkGuid != APINULLGuid) ++rs.fromHotlink;
		{
			API_QuantityPar qp = {};
			qp.minOpeningSize = 0.0001;
			API_ElementQuantity eq = {};
			API_Quantities q;
			q.elements = &eq;
			API_QuantitiesMask mask;
			ACAPI_ELEMENT_QUANTITY_MASK_CLEAR (mask);
			ACAPI_ELEMENT_QUANTITY_MASK_SET (mask, zone, area);
			ACAPI_ELEMENT_QUANTITY_MASK_SET (mask, zone, calcArea);
			ACAPI_ELEMENT_QUANTITY_MASK_SET (mask, zone, volume);
			if (ACAPI_Element_GetQuantities (g, &qp, &q, &mask) == NoError) {
				r.volume = eq.zone.volume;
				if (r.area <= 1e-4) {
					r.area = eq.zone.calcArea > 1e-4 ? eq.zone.calcArea : eq.zone.area;
					++rs.fromArchicad;
				} else
					++rs.fromStamp;
			}
		}
		res.push_back (r);
	}
	return res;
}


// Общая часть команд: зоны -> квартиры
struct AptData {
	Settings					st;
	std::vector<StoryRec>		stories;
	tep::Config					cfg;
	std::wstring				cfgPath;
	bool						cfgCreated = false;
	std::vector<tep::Room>		rooms;
	std::vector<tep::Apartment>	apts;
	std::vector<tep::Apartment>	premises;		// встроенные помещения с номером (АП 1005)
	std::vector<std::wstring>	warn;
	RoomStats					rs;
	std::wstring				secInfo;
};

void LoadAptData (AptData& d)
{
	d.st.Parse (LoadModulText (kSettingsKey));
	{
		API_DatabaseInfo fp = {};
		fp.typeID = APIWind_FloorPlanID;
		ACAPI_Database_ChangeCurrentDatabase (&fp);
	}
	d.stories = LoadStories ();
	std::wstring secPropName;
	const API_Guid secProp = FindPropertyDef ({ L"Секция №", L"Секция", L"Номер секции" }, secPropName);
	d.secInfo = secProp != APINULLGuid ? L"Секция — из свойства «" + secPropName + L"», иначе из параметра зоны «Секция»."
									   : L"Секция — из параметра зоны «Секция».";
	d.cfg = LoadTepConfig (d.warn, d.cfgPath, d.cfgCreated);
	d.rooms = ReadRooms (d.st, d.stories, secProp, d.rs);
	d.apts = tep::BuildApartments (d.rooms, d.cfg, &d.warn);
	d.premises = tep::BuildPremises (d.rooms, d.cfg);
}

void AptSummary (const AptData& d, std::vector<std::wstring>& lines)
{
	lines.push_back (L"Зон «" + d.st.zoneLib + L"…» в проекте: " + std::to_wstring (d.rs.lib) + L" (всего зон " + std::to_wstring (d.rs.all) +
					 L"), квартир найдено: " + std::to_wstring (d.apts.size ()) + L", встроенных помещений с номером: " +
					 std::to_wstring (d.premises.size ()) + L". " + d.secInfo);
	std::map<std::wstring, int> bySec;
	for (const auto& a : d.apts) bySec[L"секция " + (a.section.empty () ? std::wstring (L"(пусто)") : a.section)] += 1;
	if (!bySec.empty ())
		lines.push_back (L"   по секциям: " + JoinCounts (bySec, 20) + L".");
	if (d.rs.fromHotlink > 0)
		lines.push_back (L"   Зон из модулей (hotlink): " + std::to_wstring (d.rs.fromHotlink) + L".");
	if (!d.rs.hiddenLayers.empty ())
		lines.push_back (L"Пропущены зоны на скрытых слоях: " + JoinCounts (d.rs.hiddenLayers) + L".");
}

void AptFooter (const AptData& d, std::vector<std::wstring>& lines)
{
	if (!d.warn.empty ()) {
		lines.push_back (L"");
		for (const auto& w : d.warn) lines.push_back (w);
	}
	lines.push_back (L"");
	lines.push_back (L"Квартира = зоны помещений квартир (категории " + tep::CodesText (d.cfg.apartment) +
					 L") с одинаковым параметром зоны «Квартира» на одном этаже одной секции.");
	if (!d.cfgPath.empty ())
		lines.push_back (L"Данные ТЭП (категории строк, округление, значения вручную): " + d.cfgPath +
						 (d.cfgCreated ? L" — создан по умолчанию, проверьте категории." : L""));
}


// ---- объект паспорта
bool PassportLibInd (Int32& libInd)
{
	API_LibPart lp = {};
	const GS::UniString n = au::FromW (kPassportLib);
	GS::ucsncpy (lp.docu_UName, n.ToUStr (), API_UniLongNameLen - 1);
	const GSErrCode err = ACAPI_LibraryPart_Search (&lp, false, true);
	if (lp.location != nullptr) { delete lp.location; lp.location = nullptr; }
	libInd = lp.index;
	return err == NoError && libInd > 0;
}

// true — что-то изменилось
bool FillPassport (au::Params& P, const tep::Apartment& a, const tep::Config& c)
{
	bool changed = false;
	auto setS = [&] (const char* name, const std::wstring& v) {
		if (au::ToW (P.Str (name)) != v) { P.SetStr (name, au::FromW (v)); changed = true; }
	};
	auto setR = [&] (const char* name, double v) {
		if (std::fabs (P.Real (name, -1) - v) > 1e-6) { P.SetReal (name, v); changed = true; }
	};
	setS ("stType", a.label);
	setR ("iKind", a.office ? 1 : 0);
	if (a.office) {
		setS ("stLiving", L"");
		setS ("stArea", tep::OfficeArea (a.area, c));
		setS ("stTotal", L"");
		setS ("stReduced", L"");
		setS ("stHeight", tep::OfficeHeight (a.height, c));
	} else {
		setS ("stLiving", tep::PassportNum (a.living, c));
		setS ("stArea", tep::PassportNum (a.area, c));
		setS ("stTotal", tep::PassportNum (a.total, c));
		setS ("stReduced", tep::PassportNum (a.reduced, c));
		setS ("stHeight", L"");
	}
	setR ("rLiving", a.living);
	setR ("rArea", a.area);
	setR ("rTotal", a.total);
	setR ("rReduced", a.reduced);
	setR ("iRooms", a.livingRooms);
	setS ("stApt", a.code);
	setS ("stSection", a.section);
	setS ("stKey", a.Key ());
	setS ("sFont", L"Arial Narrow Кириллический");
	setR ("tSizeA", 2.5);		// как у паспортов ZoneSum
	setR ("tSizeT", 2.5);
	return changed;
}

void SetPlaced (au::Params& P, double x, double y)
{
	P.SetReal ("rPlaceX", x);
	P.SetReal ("rPlaceY", y);
}

struct PassportObj {
	API_Guid	guid = APINULLGuid;
	short		floor = 0;
	std::wstring key;
	bool		used = false;
	bool		movable = false;		// стоит там, где поставила надстройка (не двигали) — можно переставить
	double		x = 0, y = 0, wMm = 18, hMm = 14;
};

std::vector<PassportObj> FindPassports (Int32 libInd, int& inHotlinks)
{
	std::vector<PassportObj> res;
	inHotlinks = 0;
	GS::Array<API_Guid> list;
	if (ACAPI_Element_GetElemList (API_ElemType (API_ObjectID), &list) != NoError)
		return res;
	for (const API_Guid& g : list) {
		API_Element el = {};
		el.header.guid = g;
		if (ACAPI_Element_Get (&el) != NoError || el.object.libInd != libInd)
			continue;
		if (el.header.hotlinkGuid != APINULLGuid) { ++inHotlinks; continue; }
		API_ElementMemo memo = {};
		if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars) != NoError)
			continue;
		PassportObj p;
		p.guid = g;
		p.floor = el.header.floorInd;
		{
			au::Params P (memo.params);
			p.key = otd::Trim (au::ToW (P.Str ("stKey")));
			p.x = el.object.pos.x;
			p.y = el.object.pos.y;
			p.wMm = P.Real ("wCol1", 7.5) + P.Real ("wCol2", 9.5);
			p.hMm = (P.Int ("iKind", 0) == 1 ? 3 : 4) * P.Real ("hRow2", 4.4);
			const double px = P.Real ("rPlaceX", 0), py = P.Real ("rPlaceY", 0);
			p.movable = (std::fabs (px) < 1e-9 && std::fabs (py) < 1e-9) ||
						(std::fabs (p.x - px) < 1e-3 && std::fabs (p.y - py) < 1e-3);
		}
		ACAPI_DisposeElemMemoHdls (&memo);
		res.push_back (p);
	}
	return res;
}

GSErrCode CreatePassport (Int32 libInd, short floor, double x, double y, const tep::Apartment& a, const tep::Config& c)
{
	API_Element el = {};
	el.header.type = API_ObjectID;
	API_ElementMemo memo = {};
	GSErrCode err = ACAPI_Element_GetDefaults (&el, &memo);
	if (err != NoError) { ACAPI_DisposeElemMemoHdls (&memo); return err; }
	double pa = 0, pb = 0;
	Int32 n = 0;
	API_AddParType** pars = nullptr;
	err = ACAPI_LibraryPart_GetParams (libInd, &pa, &pb, &n, &pars);
	if (err != NoError) { ACAPI_DisposeElemMemoHdls (&memo); return err; }
	el.header.guid = APINULLGuid;
	el.header.floorInd = floor;
	el.object.libInd = libInd;
	el.object.xRatio = pa;
	el.object.yRatio = pb;
	el.object.pos.x = x;
	el.object.pos.y = y;
	el.object.angle = 0;
	el.object.level = 0;
	el.object.reflected = false;
	el.object.linkToSettings.newCreationMode = false;
	if (memo.params != nullptr) ACAPI_DisposeAddParHdl (&memo.params);
	memo.params = pars;
	{
		au::Params P (memo.params);
		FillPassport (P, a, c);
		SetPlaced (P, x, y);
	}
	err = ACAPI_Element_Create (&el, &memo);
	ACAPI_DisposeElemMemoHdls (&memo);
	return err;
}

// Цифры обновляются всегда. Место — только если паспорт не двигали после того, как его поставила надстройка
// (или место ещё не записано — паспорта прежней версии): тогда он переносится в (x, y).
GSErrCode UpdatePassport (const API_Guid& g, const tep::Apartment& a, const tep::Config& c, double x, double y,
						  bool& changed, bool& moved)
{
	changed = moved = false;
	API_Element el = {};
	el.header.guid = g;
	GSErrCode err = ACAPI_Element_Get (&el);
	if (err != NoError) return err;
	API_ElementMemo memo = {};
	err = ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars);
	if (err != NoError) return err;
	API_Element mask;
	ACAPI_ELEMENT_MASK_CLEAR (mask);
	{
		au::Params P (memo.params);
		changed = FillPassport (P, a, c);
		const double px = P.Real ("rPlaceX", 0), py = P.Real ("rPlaceY", 0);
		const bool notRecorded = std::fabs (px) < 1e-9 && std::fabs (py) < 1e-9;
		const bool untouched = std::fabs (el.object.pos.x - px) < 1e-3 && std::fabs (el.object.pos.y - py) < 1e-3;
		if ((notRecorded || untouched) && (std::fabs (el.object.pos.x - x) > 1e-3 || std::fabs (el.object.pos.y - y) > 1e-3)) {
			el.object.pos.x = x;
			el.object.pos.y = y;
			ACAPI_ELEMENT_MASK_SET (mask, API_ObjectType, pos);
			moved = true;
		}
		if (notRecorded || untouched) {
			if (std::fabs (px - x) > 1e-6 || std::fabs (py - y) > 1e-6) changed = true;
			SetPlaced (P, x, y);
		}
	}
	if (changed || moved)
		err = ACAPI_Element_Change (&el, &mask, &memo, APIMemoMask_AddPars, true);
	ACAPI_DisposeElemMemoHdls (&memo);
	return err;
}

// Таблица на рабочем листе name (создаётся при первом запуске, затем перерисовывается целиком)
GSErrCode DrawOnWorksheet (const std::wstring& name, const std::wstring& ref, const otd::TableLayout& layout, const std::wstring& undoName,
						   bool& existed, DrawResult& dr, std::wstring& step)
{
	API_DatabaseInfo ws = {};
	step = L"создание листа";
	GSErrCode err = FindOrCreateWorksheet (name, ref, ws, existed);
	if (err == NoError) {
		step = L"открытие листа";
		API_WindowInfo wi = ws;
		err = ACAPI_Window_ChangeWindow (&wi);
		API_DatabaseInfo cur = {};
		if (err == NoError && (ACAPI_Database_GetCurrentDatabase (&cur) != NoError || !(cur.databaseUnId == ws.databaseUnId))) {
			step = L"переход на лист";
			API_DatabaseInfo db = ws;
			err = ACAPI_Database_ChangeCurrentDatabase (&db);
		}
	}
	if (err == NoError) {
		step = L"построение таблицы";
		err = ACAPI_CallUndoableCommand (au::FromW (undoName), [&] () -> GSErrCode { return DrawTable (layout, dr); });
	}
	return err;
}

void DrawReport (const std::wstring& name, GSErrCode err, bool existed, const DrawResult& dr, const std::wstring& step, std::vector<std::wstring>& lines)
{
	if (err == NoError) {
		lines.push_back (L"Таблица построена на рабочем листе «" + name + L"»" + (existed ? L" (лист перерисован заново)" : L"") + L".");
		if (dr.lineErrs > 0 || dr.textErrs > 0)
			lines.push_back (L"   Не создано линий: " + std::to_wstring (dr.lineErrs) + L", текстов: " + std::to_wstring (dr.textErrs) + L".");
		if (dr.layerHidden)
			lines.push_back (L"   Слой «" + dr.layer + L"» скрыт — включите его, чтобы увидеть таблицу.");
	} else
		lines.push_back (L"Таблицу на рабочем листе построить не удалось: шаг «" + step + L"», код " + std::to_wstring (err) + L".");
}

}	// namespace

// ============================================================================= проверка зон на плане
// При каждом расчёте зонам пишутся свойства группы «ПСО и отделка»: «Отделка - подкатегория», «Отделка - проверка»,
// «Отделка - замечание». По ним создаются правила графической замены (группа правил «ПСО и отделка») и две
// комбинации: «Отделка - подкатегории» и «Отделка - проверка зон». Уже существующие правила не меняются
// (цвета, поправленные вручную, сохраняются); удалённые — создаются заново.

namespace {

struct MarkResult {
	int written = 0, failed = 0, noAvail = 0;
	int rulesNew = 0, rulesFailed = 0, combosNew = 0, combosFixed = 0;
	GSErrCode propErr = NoError, ruleErr = NoError, comboErr = NoError;
	std::wstring propStep;
	std::map<std::wstring, int> byCheck;		// значение «проверка» -> зон
};


GSErrCode EnsurePropGroup (API_Guid& out)
{
	GS::Array<API_PropertyGroup> groups;
	if (ACAPI_Property_GetPropertyGroups (groups) == NoError)
		for (const API_PropertyGroup& g : groups)
			if (g.groupType == API_PropertyCustomGroupType && au::ToW (g.name) == otd::kPropGroup) { out = g.guid; return NoError; }
	API_PropertyGroup g;
	g.groupType = API_PropertyCustomGroupType;
	g.name = au::FromW (otd::kPropGroup);
	g.description = au::FromW (L"Пишет надстройка «ПСО и отделка» при расчёте ведомости отделки");
	const GSErrCode err = ACAPI_Property_CreatePropertyGroup (g);
	out = g.guid;
	return err;
}


GSErrCode EnsureStringProp (const API_Guid& group, const std::wstring& name, const std::wstring& descr,
							const std::set<API_Guid>& avail, API_PropertyDefinition& out)
{
	GS::Array<API_PropertyDefinition> defs;
	if (ACAPI_Property_GetPropertyDefinitions (group, defs) == NoError)
		for (const API_PropertyDefinition& d : defs)
			if (au::ToW (d.name) == name) {
				out = d;
				bool changed = false;
				for (const API_Guid& a : avail)
					if (!out.availability.Contains (a)) { out.availability.Push (a); changed = true; }
				return changed ? ACAPI_Property_ChangePropertyDefinition (out) : NoError;
			}
	out = API_PropertyDefinition ();
	out.definitionType = API_PropertyCustomDefinitionType;
	out.groupGuid = group;
	out.name = au::FromW (name);
	out.description = au::FromW (descr);
	out.collectionType = API_PropertySingleCollectionType;
	out.valueType = API_PropertyStringValueType;
	out.measureType = API_PropertyDefaultMeasureType;
	out.defaultValue.basicValue.singleVariant.variant.type = API_PropertyStringValueType;
	out.canValueBeEditable = true;
	for (const API_Guid& a : avail)
		out.availability.Push (a);
	return ACAPI_Property_CreatePropertyDefinition (out);
}


API_Property StringProp (const API_PropertyDefinition& d, const std::wstring& v)
{
	API_Property p;
	p.definition = d;
	p.isDefault = false;
	p.status = API_Property_HasValue;
	p.value.variantStatus = API_VariantStatusNormal;
	p.value.singleVariant.variant.type = API_PropertyStringValueType;
	p.value.singleVariant.variant.uniStringValue = au::FromW (v);
	return p;
}


void EnsureOverrides (const otd::Rules& rules, MarkResult& mr)
{
	API_OverrideRuleGroup grp = { APINULLGuid, au::FromW (otd::kPropGroup) };
	if (ACAPI_GraphicalOverride_GetOverrideRuleGroup (grp) != NoError) {
		grp.guid = APINULLGuid;
		mr.ruleErr = ACAPI_GraphicalOverride_CreateOverrideRuleGroup (grp);
		if (mr.ruleErr != NoError)
			return;
	}
	std::map<std::wstring, API_Guid> ruleGuid;
	for (const otd::OverrideRuleDef& d : otd::OverrideRules (rules)) {
		API_OverrideRule rule;
		rule.guid = APINULLGuid;
		rule.name = au::FromW (d.name);
		if (ACAPI_GraphicalOverride_GetOverrideRuleByName (rule, grp.guid) == NoError && rule.guid != APINULLGuid) {
			ruleGuid[d.name] = rule.guid;		// уже есть — не трогаем
			continue;
		}
		rule = API_OverrideRule ();
		rule.guid = APINULLGuid;
		rule.name = au::FromW (d.name);
		rule.style = API_OverrideRuleStyle ();
		// цвет фона штриховки зоны (как в графзамене ZoneSum: перо фона покрывающей штриховки)
		rule.style.fillBackgroundPenOverride = API_RGBColor { d.rgb[0], d.rgb[1], d.rgb[2] };
		rule.style.fillTypeBackgroundPen.overrideCoverFill = true;
		rule.style.fillTypeBackgroundPen.overrideCutFill = false;
		rule.style.fillTypeBackgroundPen.overrideDraftingFill = false;
		rule.criterionXML = au::FromW (otd::CriterionXml (d.prop, d.value));
		const GSErrCode err = ACAPI_GraphicalOverride_CreateOverrideRule (rule, grp.guid);
		if (err == NoError && rule.guid != APINULLGuid) {
			ruleGuid[d.name] = rule.guid;
			++mr.rulesNew;
		} else {
			++mr.rulesFailed;
			mr.ruleErr = err;
		}
	}
	for (const otd::OverrideComboDef& c : otd::OverrideCombos (rules)) {
		GS::Array<API_Guid> list;
		for (const auto& n : c.rules)
			if (ruleGuid.count (n)) list.Push (ruleGuid[n]);
		API_OverrideCombination cmb = { APINULLGuid, au::FromW (c.name) };
		GS::Array<API_Guid> have;
		if (ACAPI_GraphicalOverride_GetOverrideCombination (cmb, &have) == NoError && cmb.guid != APINULLGuid) {
			bool missing = false;
			for (const API_Guid& g : list)
				if (!have.Contains (g)) { missing = true; break; }
			if (!missing)
				continue;
			// наших правил не хватает (удалены и созданы заново) — наши по порядку, затем добавленные вручную
			GS::Array<API_Guid> merged = list;
			for (const API_Guid& g : have)
				if (!merged.Contains (g)) merged.Push (g);
			const GSErrCode err = ACAPI_GraphicalOverride_ChangeOverrideCombination (cmb, &merged);
			if (err == NoError) ++mr.combosFixed; else mr.comboErr = err;
			continue;
		}
		cmb.guid = APINULLGuid;
		const GSErrCode err = ACAPI_GraphicalOverride_CreateOverrideCombination (cmb, list);
		if (err == NoError) ++mr.combosNew; else mr.comboErr = err;
	}
}


// Свойства зонам расчёта и зонам других объектов на выбранных этажах; затем правила графической замены
MarkResult MarkZones (const std::vector<otd::ZoneData>& zones, const RunStats& rs, const otd::Rules& rules)
{
	MarkResult mr;
	std::vector<std::pair<API_Guid, otd::ZoneCheck>> marks;
	for (const otd::ZoneData& z : zones) {
		const API_Guid g = APIGuidFromString (otd::ToUtf8 (z.guid).c_str ());
		if (g != APINULLGuid)
			marks.push_back ({ g, otd::CheckZone (z, rules) });
	}
	for (const auto& o : rs.otherLibZones)
		marks.push_back ({ o.first, otd::OtherLibCheck (o.second) });

	// свойства доступны по классификации: добавляем классификации этих зон
	std::set<API_Guid> avail;
	for (const auto& m : marks) {
		GS::Array<GS::Pair<API_Guid, API_Guid>> items;
		if (ACAPI_Element_GetClassificationItems (m.first, items) == NoError)
			for (const auto& it : items) avail.insert (it.second);
	}
	API_Guid group = APINULLGuid;
	API_PropertyDefinition dSub, dChk, dNote;
	mr.propStep = L"группа свойств";
	mr.propErr = EnsurePropGroup (group);
	if (mr.propErr == NoError) {
		mr.propStep = L"свойство «" + std::wstring (otd::kPropSubcat) + L"»";
		mr.propErr = EnsureStringProp (group, otd::kPropSubcat, L"Подкатегория ведомости отделки; «Не входит» — не входит в ведомость", avail, dSub);
	}
	if (mr.propErr == NoError) {
		mr.propStep = L"свойство «" + std::wstring (otd::kPropCheck) + L"»";
		mr.propErr = EnsureStringProp (group, otd::kPropCheck, L"Верно / Замечания / Ошибка: нет отделки / Ошибка: нет подкатегории / Не входит / Другой объект зоны", avail, dChk);
	}
	if (mr.propErr == NoError) {
		mr.propStep = L"свойство «" + std::wstring (otd::kPropNote) + L"»";
		mr.propErr = EnsureStringProp (group, otd::kPropNote, L"Что проверить в зоне (как в таблице проверки)", avail, dNote);
	}
	if (mr.propErr != NoError)
		return mr;
	for (const auto& m : marks) {
		if (!ACAPI_Element_IsPropertyDefinitionAvailable (m.first, dChk.guid)) { ++mr.noAvail; continue; }
		GS::Array<API_Property> props;
		props.Push (StringProp (dSub, m.second.subcat));
		props.Push (StringProp (dChk, m.second.check));
		props.Push (StringProp (dNote, m.second.note));
		if (ACAPI_Element_SetProperties (m.first, props) == NoError) {
			++mr.written;
			mr.byCheck[m.second.check] += 1;
		} else
			++mr.failed;
	}
	EnsureOverrides (rules, mr);
	return mr;
}


void MarkReport (const MarkResult& mr, std::vector<std::wstring>& lines)
{
	if (mr.propErr != NoError) {
		lines.push_back (L"Проверка зон на плане: не удалось создать " + mr.propStep + L" (код " + std::to_wstring (mr.propErr) + L").");
		return;
	}
	std::wstring cnt;
	for (const wchar_t* v : { otd::kChkOk, otd::kChkNotes, otd::kChkHeader, otd::kChkNoFinish, otd::kChkNoGroup, otd::kChkExcluded, otd::kChkOtherLib }) {
		const auto it = mr.byCheck.find (v);
		if (it != mr.byCheck.end ())
			cnt += (cnt.empty () ? L"" : L", ") + std::wstring (v) + L" — " + std::to_wstring (it->second);
	}
	lines.push_back (L"Проверка зон на плане: свойства «Отделка - …» (группа «" + std::wstring (otd::kPropGroup) + L"») записаны у " +
					 std::to_wstring (mr.written) + L" зон" + (cnt.empty () ? L"" : L": " + cnt) + L".");
	if (mr.noAvail > 0)
		lines.push_back (L"   У " + std::to_wstring (mr.noAvail) + L" зон нет классификации — свойства не записаны (присвойте зонам классификацию).");
	if (mr.failed > 0)
		lines.push_back (L"   Не записано у " + std::to_wstring (mr.failed) + L" зон (зарезервированы другим пользователем или из модуля).");
	if (mr.rulesNew > 0 || mr.combosNew > 0)
		lines.push_back (L"   Созданы правила графической замены: " + std::to_wstring (mr.rulesNew) + L", комбинации: " + std::to_wstring (mr.combosNew) + L".");
	if (mr.combosFixed > 0)
		lines.push_back (L"   В комбинации добавлены созданные заново правила: " + std::to_wstring (mr.combosFixed) + L".");
	if (mr.rulesFailed > 0 || mr.ruleErr != NoError)
		lines.push_back (L"   Не удалось создать правил графической замены: " + std::to_wstring (mr.rulesFailed) + L" (код " + std::to_wstring (mr.ruleErr) + L").");
	if (mr.comboErr != NoError)
		lines.push_back (L"   Не удалось создать комбинацию графической замены (код " + std::to_wstring (mr.comboErr) + L").");
	lines.push_back (L"   На плане: Графическая замена -> «Отделка - проверка зон» (зелёные — верно, жёлтые — замечания, красные — ошибки, "
					 L"серые — не входят) или «Отделка - подкатегории» (свой цвет у каждой подкатегории).");
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

	// ---- свойства зон и графическая замена «Отделка - проверка зон» / «Отделка - подкатегории»
	MarkResult mark;
	ACAPI_CallUndoableCommand (au::FromW (L"Ведомость отделки: проверка зон на плане"), [&] () -> GSErrCode {
		mark = MarkZones (zones, rs, rules);
		return NoError;
	});

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
		lines.push_back (L"Добавлено по плану (элементы ограничивают зону, но не связаны с ней): " + std::to_wstring (rs.byContour) +
						 L" — проверьте: в таблице проверки — столбец «Примечание», на листе «Расчёт» — «по контуру».");
	if (rs.col3Moved > 0)
		lines.push_back (L"Межкомнатные перегородки (столбец 3) вне квартир и в санузлах/лоджиях перенесены в столбец " +
						 std::to_wstring (rules.col3Else) + L": " + std::to_wstring (rs.col3Moved) + L" участков (раздел [Столбец 3] правил).");
	if (rs.zonesWithGaps > 0)
		lines.push_back (L"Зон с участками контура без стен: " + std::to_wstring (rs.zonesWithGaps) + L" — см. лист «Расчёт» (проходы, проёмы без стены или элементы не-стены).");
	MarkReport (mark, lines);
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
	lines.push_back (L"Высота отделки — по высоте зоны. Полы замоделированы (слой «" + st.floorLayer + L"», пол не вычитается) — " +
					 std::to_wstring (rs.floorFromSlab) + L" зон; пол вычтен из параметров зоны — " + std::to_wstring (rs.floorFromZone) +
					 L", у остальных — по категории из окна. Потолок — " + std::to_wstring (st.ceilMm) + L" мм.");
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

// ============================================================================= паспорта квартир на планах этажей
// Для каждой квартиры (зоны с одинаковым параметром «Квартира») на её этаже — объект «Паспорт_АСП».
// Уже стоящие паспорта (по ключу «секция|квартира» на том же этаже) только обновляются — место сохраняется.

void DoPassports ()
{
	AptData d;
	LoadAptData (d);
	std::vector<std::wstring> lines;
	lines.push_back (L"Паспорта квартир и встроенных помещений на планах этажей.");
	AptSummary (d, lines);

	Int32 libInd = 0;
	if (!PassportLibInd (libInd)) {
		lines.push_back (L"");
		lines.push_back (L"Объект «Паспорт_АСП» не найден в загруженных библиотеках. Соберите Паспорт_АСП.gsm (файл «Собрать Паспорт_АСП.bat» рядом с Паспорт_АСП.xml),");
		lines.push_back (L"добавьте его в библиотеку проекта (Файл → Библиотеки и объекты → Менеджер библиотек) и запустите команду снова.");
		AptFooter (d, lines);
		ShowReport (lines);
		return;
	}

	double scale = 100;
	if (ACAPI_Drawing_GetDrawingScale (&scale) != NoError || scale <= 0)
		scale = 100;
	double wMm = 17, hMm = 17.6;		// размер паспорта по умолчанию — из объекта
	{
		double pa = 0, pb = 0;
		Int32 n = 0;
		API_AddParType** pars = nullptr;
		if (ACAPI_LibraryPart_GetParams (libInd, &pa, &pb, &n, &pars) == NoError && pars != nullptr) {
			au::Params P (pars);
			wMm = P.Real ("wCol1", 7.5) + P.Real ("wCol2", 9.5);
			hMm = 4 * P.Real ("hRow2", 4.4);
			ACAPI_DisposeAddParHdl (&pars);
		}
	}
	const double k = scale / 1000.0, w = wMm * k, gap = 12 * k;

	// габарит этажа (все секции) — по нему выбирается наружная сторона квартиры; паспорт — у стены квартиры
	std::map<std::wstring, std::vector<double>> box;
	for (const auto& r : d.rooms) {
		if (d.cfg.excluded.Has (r.catCode)) continue;
		auto& b = box[std::to_wstring (r.story)];
		if (b.empty ()) b = { 1e30, 1e30, -1e30, -1e30 };
		b[0] = std::min (b[0], r.x1); b[1] = std::min (b[1], r.y1);
		b[2] = std::max (b[2], r.x2); b[3] = std::max (b[3], r.y2);
	}

	int inHot = 0;
	std::vector<PassportObj> objs = FindPassports (libInd, inHot);
	int created = 0, updated = 0, same = 0, deleted = 0, errs = 0, noKey = 0, movedN = 0, noFree = 0;
	GSErrCode firstErr = NoError;
	std::map<std::wstring, int> createdBySt;

	// свободное место по этажам: занято зонами (все секции), стенами, колоннами и паспортами, которые двигали вручную
	std::map<int, tep::FreeSpace> space;
	{
		std::set<int> aptStories;
		for (const auto& a : d.apts) aptStories.insert (a.story);
		for (const auto& a : d.premises) aptStories.insert (a.story);
		for (const auto& r : d.rooms) {
			if (d.cfg.excluded.Has (r.catCode) || aptStories.count (r.story) == 0) continue;
			for (const auto& poly : r.polys) space[r.story].AddShape (poly);
		}
		std::map<int, double> lev;
		for (const auto& sr : d.stories) lev[sr.index] = sr.level;
		LayerCache lc;
		for (const ElemGeo& e : LoadElemGeo (d.stories, false, lc)) {
			for (int s : aptStories) {
				const double z = (lev.count (s) ? lev[s] : 0) + 1.0;		// на высоте 1 м над этажом
				if (e.floor == s || (e.bot < z && e.top > z)) space[s].AddShape (e.shape);
			}
		}
		for (const auto& o : objs)
			if (!o.movable)
				space[o.floor].AddRect ({ o.x, o.y, o.x + o.wMm * k, o.y + o.hMm * k });
	}
	const double minGap = 2 * k, maxGap = 80 * k, step = 1.5 * k, margin = 0.8 * k;

	std::vector<tep::Apartment> all = d.apts;
	all.insert (all.end (), d.premises.begin (), d.premises.end ());
	const double hRow1 = hMm / 4 * k;		// высота строки паспорта, м
	ACAPI_CallUndoableCommand (au::FromW (L"Паспорта квартир"), [&] () -> GSErrCode {
		for (const auto& a : all) {
			const double h = (a.office ? 3 : 4) * hRow1;
			const auto bi = box.find (std::to_wstring (a.story));
			double x = a.x1, y = a.y2;
			if (bi != box.end ()) {
				if (!tep::PlaceFree (a, bi->second[0], bi->second[1], bi->second[2], bi->second[3], w, h, minGap, maxGap, step, margin,
									 space[a.story], x, y)) {
					++noFree;
					tep::PassportPlace (a, bi->second[0], bi->second[1], bi->second[2], bi->second[3], w, h, gap, x, y);
				}
			}
			bool found = false;
			for (auto& o : objs) {
				if (o.used || o.floor != a.story || o.key != a.Key ()) continue;
				o.used = true;
				found = true;
				bool ch = false, mv = false;
				const GSErrCode e = UpdatePassport (o.guid, a, d.cfg, x, y, ch, mv);
				if (e != NoError) { if (errs++ == 0) firstErr = e; }
				else if (ch || mv) ++updated;
				else ++same;
				if (e == NoError && mv) ++movedN;
				if (o.movable) space[a.story].AddRect ({ x, y, x + w, y + h });
				break;
			}
			if (found) continue;
			const GSErrCode e = CreatePassport (libInd, (short) a.story, x, y, a, d.cfg);
			if (e != NoError) { if (errs++ == 0) firstErr = e; }
			else { ++created; createdBySt[a.storyName] += 1; space[a.story].AddRect ({ x, y, x + w, y + h }); }
		}
		// паспорта квартир, которых больше нет (номер квартиры изменён / зоны удалены), и повторы
		GS::Array<API_Guid> del;
		for (const auto& o : objs) {
			if (o.used) continue;
			if (o.key.empty ()) { ++noKey; continue; }		// поставлен вручную — не трогаем
			del.Push (o.guid);
		}
		if (!del.IsEmpty () && ACAPI_Element_Delete (del) == NoError)
			deleted = (int) del.GetSize ();
		return NoError;
	});

	lines.push_back (L"");
	lines.push_back (L"Поставлено новых паспортов: " + std::to_wstring (created) + (createdBySt.empty () ? std::wstring () : L" (" + JoinCounts (createdBySt, 30) + L")") + L".");
	lines.push_back (L"Обновлено: " + std::to_wstring (updated) + L", без изменений: " + std::to_wstring (same) +
					 L", удалено (квартиры больше нет или повтор): " + std::to_wstring (deleted) + L".");
	if (movedN > 0)
		lines.push_back (L"Перенесено к своим квартирам (их не двигали вручную): " + std::to_wstring (movedN) + L".");
	if (created > 0 || movedN > 0)
		lines.push_back (L"Паспорт ставится в ближайшее к квартире свободное место (не на зоны любых секций, стены, колонны и другие паспорта), сначала — со стороны, ближайшей к краю этажа. Передвинутые вручную паспорта не переносятся — обновляются только цифры.");
	if (noFree > 0)
		lines.push_back (L"Свободного места рядом не нашлось (до 80 мм листа от квартиры): " + std::to_wstring (noFree) + L" — поставлены у стены квартиры, передвиньте вручную.");
	if (noKey > 0)
		lines.push_back (L"Паспортов без ключа (поставлены вручную, не тронуты): " + std::to_wstring (noKey) + L".");
	if (inHot > 0)
		lines.push_back (L"Паспортов в модулях (hotlink) — не тронуты: " + std::to_wstring (inHot) + L".");
	if (errs > 0)
		lines.push_back (L"Ошибок при создании / изменении: " + std::to_wstring (errs) + L" (первая — код " + std::to_wstring (firstErr) +
						 L"). Проверьте, не заблокирован ли слой объектов по умолчанию.");
	lines.push_back (L"Площадь помещения — как в штампе зоны (" + std::to_wstring (d.rs.fromStamp) + L" зон), у остальных " +
					 std::to_wstring (d.rs.fromArchicad) + L" — расчётная площадь Archicad; округляется до " + std::to_wstring (d.cfg.dec) +
					 L" знака и складывается.");
	lines.push_back (L"Встроенные помещения (категории " + tep::CodesText (d.cfg.builtIn) + L") с номером в параметре «Квартира» (1005…) — паспорт «" +
					 d.cfg.officeLabel + L"»: номер, площадь (сумма зон), высота самой большой зоны.");
	AptFooter (d, lines);
	ShowReport (lines);
}

// ============================================================================= ТЭП по всему дому

void DoTep ()
{
	AptData d;
	LoadAptData (d);
	const tep::Tep t = tep::BuildTep (d.rooms, d.apts, d.cfg);
	const std::wstring title = L"Технико-экономические показатели";
	std::vector<std::wstring> lines;
	lines.push_back (L"ТЭП по дому: секции " + [&t] () { std::wstring s; for (const auto& x : t.sections) s += (s.empty () ? L"" : L", ") + (x.empty () ? std::wstring (L"(пусто)") : x); return s; } () + L".");
	AptSummary (d, lines);

	bool existed = false;
	DrawResult dr;
	std::wstring step;
	const std::wstring wsName = L"ТЭП";
	otd::TableLayout layout = tep::LayoutTep (t, title);
	otd::AppendLayout (layout, tep::LayoutTepCheck (t, L"Проверка ТЭП: какие зоны учтены"), 20);
	const GSErrCode wsErr = DrawOnWorksheet (wsName, L"ТЭП", layout, L"ТЭП: таблица", existed, dr, step);
	DrawReport (wsName, wsErr, existed, dr, step, lines);

	IO::Location loc;
	const std::wstring fname = TepFileBase () + L".xlsx";
	const GSErrCode xlErr = DocumentsFile (fname, loc) ?
		WriteBytes (loc, xlsx::BuildXlsx ({ tep::TepSheet (t, title), tep::TepCheckSheet (t), tep::ApartmentsSheet (d.rooms, [&d] () { auto v = d.apts; v.insert (v.end (), d.premises.begin (), d.premises.end ()); return v; } (), d.cfg) })) : APIERR_GENERAL;
	if (xlErr == NoError)
		lines.push_back (L"Excel: " + PathOf (loc) + L" (листы «Проверка ТЭП» — что учтено в каждой строке по секциям, «Квартиры» — каждая квартира с помещениями).");
	else
		lines.push_back (L"Не удалось записать «" + fname + L"» в папку «Документы». Если файл открыт в Excel — закройте и запустите ещё раз.");
	lines.push_back (L"Справа от ТЭП на листе — таблица проверки: по каждой строке и секции какие зоны (код, имя, количество, сумма), этажи и квартиры учтены.");

	for (const auto& fi : t.floorsInfo)
		lines.push_back (L"   " + fi);
	for (const auto& r : t.rows)
		if (r.num == L"9" || r.num == L"10" || r.num == L"16")
			lines.push_back (L"   " + r.num + L". " + r.name.substr (0, 40) + (r.name.size () > 40 ? L"…" : L"") + L" — " + r.total + L" " + r.unit);
	lines.push_back (L"Этажность — надземные этажи с первого до последнего этажа с квартирами; количество этажей = этажность + подземные (подвал: отметка этажа ниже −0,5 м, отрицательный индекс или «подвал / подполье / подзем» в названии); этаж выше последнего жилого (только ЛК) не считается. Строку 18 и любые другие можно задать вручную в файле данных, раздел [Вручную].");
	AptFooter (d, lines);
	ShowReport (lines);
}

// Открыть файл данных ТЭП (создаётся по умолчанию, если его нет)
void DoTepData ()
{
	std::vector<std::wstring> warn;
	std::wstring path;
	bool created = false;
	LoadTepConfig (warn, path, created);
	if (path.empty ()) {
		ShowReport ({ L"Не удалось найти папку «Документы»." });
		return;
	}
	OpenInShell (path, true);
	if (!warn.empty ())
		ShowReport (warn);
}

// ============================================================================= параметры зон: сохранить / восстановить
// Перед сменой штампа категорий (Менеджер реквизитов) — сохранить; после — восстановить: Archicad при смене штампа
// сбрасывает параметры зон на значения категории («Квартира», «Секция» и т. п. пропадают).

namespace {

const wchar_t*	kZoneParamsFile = L"Параметры зон.txt";

std::vector<zp::Par> ReadPars (API_AddParType** h)
{
	std::vector<zp::Par> res;
	if (h == nullptr) return res;
	const Int32 n = (Int32) ((size_t) BMGetHandleSize ((GSConstHandle) h) / sizeof (API_AddParType));
	for (Int32 i = 0; i < n; ++i) {
		const API_AddParType& p = (*h)[i];
		if (p.typeID == APIParT_Separator || p.typeID == APIParT_Title || p.typeID == APIParT_Dictionary || p.typeID == API_ZombieParT)
			continue;
		zp::Par q;
		q.name = au::ToW (GS::UniString (p.name));
		if (zp::Skip (q.name)) continue;
		if (p.typeMod == API_ParArray) {
			if (p.typeID == APIParT_CString || p.value.array == nullptr) continue;		// массивы строк не сохраняем
			q.kind = 'a';
			q.dim1 = p.dim1;
			q.dim2 = p.dim2;
			const Int32 cnt = p.dim1 * (p.dim2 > 0 ? p.dim2 : 1);
			const double* arr = *reinterpret_cast<double**> (p.value.array);
			for (Int32 k = 0; k < cnt; ++k) q.arr.push_back (arr[k]);
		} else if (p.typeID == APIParT_CString) {
			q.kind = 's';
			q.str = au::ToW (GS::UniString (p.value.uStr));
		} else {
			q.kind = 'n';
			q.num = p.value.real;
		}
		res.push_back (q);
	}
	return res;
}

// Записать значения из rec в параметры; возвращает число изменённых параметров
int ApplyPars (API_AddParType** h, const zp::ZoneRec& rec)
{
	if (h == nullptr) return 0;
	std::map<std::wstring, const zp::Par*> byName;
	for (const auto& p : rec.pars) byName[au::Lower (p.name)] = &p;
	int changed = 0;
	const Int32 n = (Int32) ((size_t) BMGetHandleSize ((GSConstHandle) h) / sizeof (API_AddParType));
	for (Int32 i = 0; i < n; ++i) {
		API_AddParType& p = (*h)[i];
		const std::wstring name = au::ToW (GS::UniString (p.name));
		if (zp::Skip (name)) continue;
		const auto it = byName.find (au::Lower (name));
		if (it == byName.end ()) continue;
		const zp::Par& q = *it->second;
		if (p.typeMod == API_ParArray) {
			if (q.kind != 'a' || p.typeID == APIParT_CString) continue;
			const Int32 cnt = q.dim1 * (q.dim2 > 0 ? q.dim2 : 1);
			if (cnt <= 0 || (Int32) q.arr.size () < cnt) continue;
			bool same = p.value.array != nullptr && p.dim1 == q.dim1 && p.dim2 == q.dim2;
			if (same) {
				const double* arr = *reinterpret_cast<double**> (p.value.array);
				for (Int32 k = 0; k < cnt && same; ++k) same = std::fabs (arr[k] - q.arr[(size_t) k]) < 1e-12;
			}
			if (same) continue;
			if (p.value.array != nullptr) BMKillHandle (&p.value.array);
			p.dim1 = q.dim1;
			p.dim2 = q.dim2;
			p.value.array = BMAllocateHandle ((GSSize) ((size_t) cnt * sizeof (double)), ALLOCATE_CLEAR, 0);
			double* arr = *reinterpret_cast<double**> (p.value.array);
			for (Int32 k = 0; k < cnt; ++k) arr[k] = q.arr[(size_t) k];
			++changed;
		} else if (p.typeID == APIParT_CString) {
			if (q.kind != 's') continue;
			if (au::ToW (GS::UniString (p.value.uStr)) == q.str) continue;
			GS::ucsncpy (p.value.uStr, au::FromW (q.str).ToUStr (), API_UAddParStrLen - 1);
			p.value.uStr[API_UAddParStrLen - 1] = 0;
			++changed;
		} else {
			if (q.kind != 'n') continue;
			if (std::fabs (p.value.real - q.num) < 1e-12) continue;
			p.value.real = q.num;
			++changed;
		}
	}
	return changed;
}

std::wstring ZoneCatCode (const API_AttributeIndex& idx)
{
	API_Attribute a = {};
	a.header.typeID = API_ZoneCatID;
	a.header.index = idx;
	if (ACAPI_Attribute_Get (&a) != NoError)
		return std::wstring ();
	return otd::Trim (au::ToW (GS::UniString (a.zoneCat.catCode)));
}

// Категории зон проекта: код, имя и индекс
void LoadZoneCats (std::vector<zp::Cat>& cats, std::vector<API_AttributeIndex>& idx)
{
	ACAPI_Attribute_EnumerateAttributesByType (API_ZoneCatID, [&] (API_Attribute& a) {
		zp::Cat c;
		c.code = otd::Trim (au::ToW (GS::UniString (a.zoneCat.catCode)));
		c.name = AttrName (API_ZoneCatID, a.header.index);
		cats.push_back (c);
		idx.push_back (a.header.index);
	});
}

// Строковый параметр зоны по имени: записать, если отличается (true — изменён)
bool SetStrPar (API_AddParType** h, const wchar_t* parName, const std::wstring& value)
{
	if (h == nullptr) return false;
	const Int32 n = (Int32) ((size_t) BMGetHandleSize ((GSConstHandle) h) / sizeof (API_AddParType));
	for (Int32 i = 0; i < n; ++i) {
		API_AddParType& p = (*h)[i];
		if (p.typeID != APIParT_CString || p.typeMod == API_ParArray) continue;
		if (!otd::SameText (au::ToW (GS::UniString (p.name)), parName)) continue;
		if (otd::Trim (au::ToW (GS::UniString (p.value.uStr))) == value) return false;
		GS::ucsncpy (p.value.uStr, au::FromW (value).ToUStr (), API_UAddParStrLen - 1);
		p.value.uStr[API_UAddParStrLen - 1] = 0;
		return true;
	}
	return false;
}

}	// namespace

void DoSaveZoneParams ()
{
	{
		API_DatabaseInfo fp = {};
		fp.typeID = APIWind_FloorPlanID;
		ACAPI_Database_ChangeCurrentDatabase (&fp);
	}
	GS::Array<API_Guid> list;
	ACAPI_Element_GetElemList (API_ElemType (API_ZoneID), &list);
	std::vector<zp::ZoneRec> recs;
	int withApt = 0, withSec = 0;
	for (const API_Guid& g : list) {
		API_Element el = {};
		el.header.guid = g;
		if (ACAPI_Element_Get (&el) != NoError) continue;
		API_ElementMemo memo = {};
		if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars) != NoError) continue;
		zp::ZoneRec z;
		z.guid = au::ToW (APIGuidToString (g));
		z.story = el.header.floorInd;
		z.number = otd::Trim (au::ToW (GS::UniString (el.zone.roomNoStr)));
		z.name = otd::Trim (au::ToW (GS::UniString (el.zone.roomName)));
		z.catCode = ZoneCatCode (el.zone.catInd);
		z.catName = AttrName (API_ZoneCatID, el.zone.catInd);
		z.pars = ReadPars (memo.params);
		ACAPI_DisposeElemMemoHdls (&memo);
		for (const auto& p : z.pars) {
			if (p.kind != 's' || otd::Trim (p.str).empty ()) continue;
			if (otd::SameText (p.name, L"asAddOn_stApartmentCode")) ++withApt;
			if (otd::SameText (p.name, L"asAddOn_stStaircaseCode")) ++withSec;
		}
		recs.push_back (z);
	}
	std::vector<std::wstring> lines;
	IO::Location loc, prev;
	if (!DocumentsFile (kZoneParamsFile, loc)) {
		ShowReport ({ L"Не удалось найти папку «Документы»." });
		return;
	}
	std::string old;
	if (ReadBytes (loc, old) && DocumentsFile (L"Параметры зон (предыдущие).txt", prev))
		WriteBytes (prev, old);
	const std::wstring header = L"Проект: " + TepFileBase () + L", зон: " + std::to_wstring (recs.size ());
	if (WriteBytes (loc, "\xEF\xBB\xBF" + otd::ToUtf8 (zp::Write (recs, header))) != NoError) {
		ShowReport ({ L"Не удалось записать «" + std::wstring (kZoneParamsFile) + L"» в «Документы»." });
		return;
	}
	lines.push_back (L"Параметры зон сохранены: " + std::to_wstring (recs.size ()) + L" зон, " + PathOf (loc) + L".");
	lines.push_back (L"   с заполненным параметром «Квартира»: " + std::to_wstring (withApt) + L", «Секция»: " + std::to_wstring (withSec) + L".");
	if (!old.empty ())
		lines.push_back (L"Прежний файл сохранён как «Параметры зон (предыдущие).txt».");
	lines.push_back (L"");
	lines.push_back (L"Сохранены и категории зон (код и имя).");
	lines.push_back (L"Теперь можно перезаписать категории зон — в Менеджере реквизитов кнопкой «<< По Имени» (не «По Индексу»!); затем — «Восстановить параметры зон из файла».");
	ShowReport (lines);
}

void DoRestoreZoneParams ()
{
	IO::Location loc;
	std::string bytes;
	if (!DocumentsFile (kZoneParamsFile, loc) || !ReadBytes (loc, bytes)) {
		ShowReport ({ L"Нет файла «" + std::wstring (kZoneParamsFile) + L"» в «Документах». Сначала — «Сохранить параметры зон в файл»." });
		return;
	}
	std::vector<std::wstring> warn;
	const std::vector<zp::ZoneRec> recs = zp::Read (otd::FromUtf8 (bytes), &warn);
	{
		API_DatabaseInfo fp = {};
		fp.typeID = APIWind_FloorPlanID;
		ACAPI_Database_ChangeCurrentDatabase (&fp);
	}
	GS::Array<API_Guid> list;
	ACAPI_Element_GetElemList (API_ElemType (API_ZoneID), &list);
	std::wstring secPropName;
	const API_Guid secProp = FindPropertyDef ({ L"Секция №", L"Секция", L"Номер секции" }, secPropName);
	int secSet = 0, secFromProp = 0;
	std::vector<zp::Cat> cats;
	std::vector<API_AttributeIndex> catIdx;
	LoadZoneCats (cats, catIdx);
	int catSet = 0, catMissing = 0, catErrs = 0, recsWithCat = 0;
	std::map<std::wstring, int> catLost;		// категории из файла, которых нет в проекте
	for (const auto& r : recs) if (!r.catName.empty () || !r.catCode.empty ()) ++recsWithCat;
	int zones = 0, byGuid = 0, byNum = 0, notFound = 0, changedZones = 0, changedPars = 0, hot = 0, errs = 0;
	GSErrCode firstErr = NoError;
	std::vector<std::wstring> missing;
	ACAPI_CallUndoableCommand (au::FromW (L"Восстановить параметры зон"), [&] () -> GSErrCode {
		for (const API_Guid& g : list) {
			API_Element el = {};
			el.header.guid = g;
			if (ACAPI_Element_Get (&el) != NoError) continue;
			++zones;
			if (el.header.hotlinkGuid != APINULLGuid) { ++hot; continue; }
			const std::wstring number = otd::Trim (au::ToW (GS::UniString (el.zone.roomNoStr)));
			const std::wstring name = otd::Trim (au::ToW (GS::UniString (el.zone.roomName)));
			bool g1 = false;
			const int ri = zp::FindRec (recs, au::ToW (APIGuidToString (g)), el.header.floorInd, number, name, &g1);
			// секция — из свойства «Секция №», если оно у зоны заполнено (важнее значения в файле)
			std::wstring sec;
			if (secProp != APINULLGuid && ReadPropertyText (g, secProp, sec) && !otd::Trim (sec).empty ()) {
				sec = otd::Trim (sec);
				++secFromProp;
			} else
				sec.clear ();
			if (ri < 0) {
				++notFound;
				if (missing.size () < 15) missing.push_back (number + L" " + name);
				if (sec.empty ()) continue;
			} else
				(g1 ? byGuid : byNum) += 1;
			// категория — по имени, иначе по коду (до замены категорий «По Индексу» зоны могли «переехать»)
			if (ri >= 0 && (!recs[(size_t) ri].catName.empty () || !recs[(size_t) ri].catCode.empty ())) {
				const zp::ZoneRec& rr = recs[(size_t) ri];
				const int ci = zp::FindCat (cats, rr.catCode, rr.catName);
				if (ci < 0) {
					++catMissing;
					catLost[rr.catCode + L" " + rr.catName] += 1;
				} else if (!(catIdx[(size_t) ci] == el.zone.catInd)) {
					API_Element ce = el;
					API_Element mask;
					ACAPI_ELEMENT_MASK_CLEAR (mask);
					ce.zone.catInd = catIdx[(size_t) ci];
					ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, catInd);
					if (ACAPI_Element_Change (&ce, &mask, nullptr, 0, true) == NoError) {
						++catSet;
						ACAPI_Element_Get (&el);
					} else
						++catErrs;
				}
			}
			API_ElementMemo memo = {};
			if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars) != NoError) continue;
			int ch = ri >= 0 ? ApplyPars (memo.params, recs[(size_t) ri]) : 0;
			if (!sec.empty () && SetStrPar (memo.params, L"asAddOn_stStaircaseCode", sec)) {
				++ch;
				++secSet;
			}
			if (ch > 0) {
				API_Element mask;
				ACAPI_ELEMENT_MASK_CLEAR (mask);
				const GSErrCode e = ACAPI_Element_Change (&el, &mask, &memo, APIMemoMask_AddPars, true);
				if (e != NoError) { if (errs++ == 0) firstErr = e; }
				else { ++changedZones; changedPars += ch; }
			}
			ACAPI_DisposeElemMemoHdls (&memo);
		}
		return NoError;
	});
	std::vector<std::wstring> lines;
	lines.push_back (L"Восстановление параметров зон из " + PathOf (loc) + L".");
	lines.push_back (L"Зон в проекте: " + std::to_wstring (zones) + L", в файле: " + std::to_wstring (recs.size ()) +
					 L"; найдено по GUID: " + std::to_wstring (byGuid) + L", по этажу и номеру: " + std::to_wstring (byNum) + L".");
	lines.push_back (L"Изменено зон: " + std::to_wstring (changedZones) + L" (параметров: " + std::to_wstring (changedPars) + L"), остальные уже совпадали.");
	if (recsWithCat == 0)
		lines.push_back (L"Категорий зон в файле нет (файл сохранён старой версией) — категории не восстанавливались.");
	else {
		lines.push_back (L"Категория зоны восстановлена (по имени, иначе по коду) у " + std::to_wstring (catSet) + L" зон.");
		if (catMissing > 0) {
			std::wstring s;
			for (const auto& kv : catLost) s += (s.empty () ? L"" : L", ") + otd::Trim (kv.first) + L" — " + std::to_wstring (kv.second);
			lines.push_back (L"   Нет в проекте категорий: " + s + L".");
		}
		if (catErrs > 0)
			lines.push_back (L"   Не удалось сменить категорию у " + std::to_wstring (catErrs) + L" зон.");
	}
	if (secProp != APINULLGuid)
		lines.push_back (L"Параметр «Секция» — из свойства «" + secPropName + L"» (заполнено у " + std::to_wstring (secFromProp) +
						 L" зон); изменён у " + std::to_wstring (secSet) + L" зон.");
	else
		lines.push_back (L"Свойство «Секция №» в проекте не найдено — параметр «Секция» взят из файла.");
	if (notFound > 0) {
		std::wstring s;
		for (const auto& m : missing) s += (s.empty () ? L"" : L", ") + m;
		lines.push_back (L"Нет в файле: " + std::to_wstring (notFound) + L" зон (" + s + (notFound > (int) missing.size () ? L"…" : L"") + L").");
	}
	if (hot > 0) lines.push_back (L"Зоны из модулей (hotlink) не меняются: " + std::to_wstring (hot) + L" — восстановите в файлах модулей.");
	if (errs > 0) lines.push_back (L"Не удалось изменить: " + std::to_wstring (errs) + L" зон (код " + std::to_wstring (firstErr) + L").");
	lines.push_back (L"Не восстанавливаются: A, B, ROOM_*, GLOB_*, вычисленные площади (asAddOn_CalcArea / FloorArea / FillArea) и массивы строк.");
	lines.push_back (L"Отменить — Ctrl+Z.");
	for (const auto& w : warn) lines.push_back (w);
	ShowReport (lines);
}
