# ZoneSum — паспорта квартир, секций, здания (справка для будущей «Ведомости помещений»)

Чужие файлы (Alexey Krikunov, Svetlana Kravchenko; в объектах указана лицензия CC0 1.0), сохранены как образец.

| Файл | Что это |
|---|---|
| ZoneSum.apx | Надстройка AC27 (доработанный пример Graphisoft ZoneSum). Ищет зоны объекта «HimeraZone», собирает данные в объекты паспортов |
| ZoneSumPassport25.gsm | Паспорт квартиры (общий макрос; Section/House вызывают его с typePassport = 2 / 3) |
| ZoneSumSection25.gsm, ZoneSumHouse25.gsm | Паспорт секции / здания |
| ZoneSumHouseTable25.gsm | Таблица по зданию (экспликация): до 14 столбцов, сортировка, группировка, итоги |
| ZoneSumFinishTable25.gsm | Ведомость отделки/полов по данным ZoneSum (берёт отделку из XML-файла) |
| ZoneSumGlobals.gsm | Глобальные параметры отображения паспортов (v25.109) |

## Что ZoneSum читает из зоны HimeraZone(_АСП)

asAddOn_SubArea (тип пола), asAddOn_SubRegul (тип помещения: 0 эскиз, 1 комнаты, 2 нежилые, 3 летние, 4 общедом.,
5 обществ., 6 парковка), asAddOn_SubFinish (коэф. уменьшения), asAddOn_CalcArea, asAddOn_FloorArea, asAddOn_FillArea,
asAddOn_stApartmentCode, asAddOn_stStaircaseCode, asAddOn_stHouseCode, asAddOn_stPositionCode, stRoomCode.

## Что ZoneSum записывает в объект паспорта/таблицы

`ac_valueZoneAreas_number[i][1..8]` — тип пола, тип помещения, коэф. уменьшения, площадь зоны, площадь пола (в HouseTable —
«тип отделки?»), площадь штриховки, этаж − 1, внутренний apartID.

`ac_stZoneIds_name[i][1..11]` — имя зоны, номер зоны, ID зоны, X-строка / польз. свойство 0, примечание / свойство 1,
свойство 2, номер (код) квартиры, номер секции (в FinishTable — отделка), расположение (этаж), ID штампа квартиры, тип квартиры.

Плюс `ac_stApartmentCode`, `ac_stStaircaseCode`, `ac_stHouseCode`, `ac_stPositionCode`.
Своя «Ведомость помещений» может читать те же параметры зон и рисовать таблицу на рабочем листе, как ведомость отделки.
