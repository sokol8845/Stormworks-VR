# Natywna klawiatura numeryczna — badanie offline 2026-11-04

**Korekta po próbie 2026-11-06:** otwarcie 5/6 zapisuje również natywny
wskaźnik trzymania Q/E i bajt naciśnięcia, jeszcze przed gałęzią Show35.
Clear24 zeruje tylko `controller+518`. Adapter zwalnia teraz własny lokalny
stan dłoni po otwarciu, bez type10; wcześniej brak tego sprzątania blokował
przyciski po OK. [Dowód i regresja](keypad-release-fix-20261106.md).

**Uzupełnienie 2026-11-06:** rozpoznano właściciela frontendowego i podłączono
adapter. `84D010(owner, graphics, D)` zawiera wywołanie `7B6060` z S pobranym
z `owner+CE190`, bieżącym D, D+280 i kolejką `owner+CE6B8`. Dowód:
`artifacts/cockpit-frontend-evidence-20261106.json`. Hook tego ticka daje
bieżący zakres dla walidacji UI i zatwierdzenia, a Show/Hide/Update nadają
lub unieważniają generację sesji. Po requestach type11 oryginalny Cancel4
wykonuje Clear24 i zwalnia fokus. W HandButtons dodano wyłącznie natywne
otwieranie 5/6; nie tworzy trzymania ani zwolnienia type10. Testy obejmują
obie ścieżki. Dalszy dokument zachowuje stan rozpoznania z 2026-11-04.

Zakres: Steam build 26294447, pełny SHA-256
`c38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d`.
Badano lokalną kopię PE oraz jego instrukcje, RTTI i lokalne definicje ROM.
Nie uruchamiano Stormworks ani próby z fałszywym runtime. Dokument opisuje
dowody dla przygotowania adaptera; nie potwierdza działania w grze.

## Źródła pierwotne i odtwarzalność

- `tmp/locomotion-game.exe`: kopia aktualnego EXE; SHA sprawdzony ponownie w tej analizie.
- [Opener i zdarzenia](../artifacts/keypad-open-evidence-20261104.json): instrukcje `694CE0`, tablice RTTI i bajty getterów numerów zdarzeń.
- [Protokół, sesja i submit](../artifacts/keypad-native-protocol-evidence-20261104.json): instrukcje serializatora, inicjalizacji UI, bezpośrednie bloki submit/cancel, fragmenty dispatcherów i znane luki.
- Lokalne pliki ROM: `rom/data/definitions/button_keypad_small.xml` (`name="Small Keypad"`, `button_type="5"`) oraz `button_keypad_large.xml` (`name="Large Keypad"`, `button_type="6"`). Atrybuty odczytano bez wykonywania gry. Pliki mają składnię nie w pełni akceptowaną przez standardowy parser XML.

Ghidra pracowała z `-noanalysis -readOnly -deleteProject`, skryptem
`scripts/ghidra/ExportCameraFunctions.java` oraz zakresami z PE exception table.
Duże switch-dispatchery wymagają osobnego wejścia w bloki: pierwszy listing
przed dekompilacją nie obejmuje automatycznie celów skoku po tablicy.
Dlatego bloki `7B463A..7B46BD` i `7B46BD..7B48E9` zachowano dodatkowo
jako bezpośrednio zdekodowane instrukcje, a nie wyłącznie tekst dekompilatora.

## 1. Otwarcie

W akcji `78C7A0` dla Q/E fazy 1 istnieje specjalna gałąź typów 5/6.
Po oryginalnych bramkach postaci/zdrowia/odległości zapisuje ona bieżący
komponent klawiatury w `controller+0x518`, odczytuje wartości
`component+0x4C8/+0x4CC`, teksty `+0x520/+0x528` i `+0x530/+0x538`
oraz wywołuje `694CE0` z powrotem `78D525`. Opener otrzymuje również typ,
vehicle id, section index i component grid (3×int32). Źródło: aktualna dekompilacja
`tmp/dispatcher.json`, instrukcje `78D3AE..78D525`; [dowód openeru](../artifacts/keypad-open-evidence-20261104.json).

`694CE0` alokuje **0x58 bajtów** i tworzy lokalne zdarzenie
`c_event_logic_to_game_show_keypad_input`, vtable **B5F0F8**.
Slot `+8` wskazuje getter `346120`, którego bajty `B8 23 00 00 00 C3`
zwracają id **0x23 (35)**. To zdarzenie logic→game; na tym etapie nie jest
tworzony request sieciowy type10 ani type11.
[Instrukcje i RTTI](../artifacts/keypad-open-evidence-20261104.json).

| Pole zdarzenia Show | Znaczenie potwierdzone przepływem |
|---|---|
| +0x00 | vtable B5F0F8 |
| +0x08, +0x18, +0x28 | trzy niezależnie kopiowane natywne stringi, po 16 B |
| +0x38, +0x3C | bieżące floaty kanałów 0 i 1, z komponentu +4C8/+4CC |
| +0x40 | button_type, 5 albo 6 |
| +0x44 | vehicle id |
| +0x48 | section index |
| +0x4C..+0x54 | component grid: trzy int32 z component+18/+1C/+20 |

Parametry +38/+3C są **bieżącymi wartościami obu kanałów**, nie granicami
min/max. Potwierdza to ścieżka aktualizacji `77B430`, która po otrzymaniu
wartości zapisuje ją do component+4C8 lub +4CC zależnie od indeksu kanału
0/1, a następnie tworzy Update dla aktywnego komponentu.
[Protokół, funkcja 77B430](../artifacts/keypad-native-protocol-evidence-20261104.json).

## 2. Sesja frontendowego UI

Frontendowy handler `7B6060` pobiera id zdarzenia przez slot vtable+8.
Jego tablica przy `7B9208` kieruje Show35 do `7B7902`, Hide36 do `7B79F5`,
Update37 do `7B7A47`. Show wywołuje `3619C0`, Update `361FF0`.
Obiekt UI jest odczytywany jako `*(S+0x3C38)` z bieżącego obiektu frontendowego
`S`; potwierdzona vtable UI `c_ui_button_keypad_input` to **B60628**.
[Dispatchery, RTTI i inicjalizacja UI](../artifacts/keypad-native-protocol-evidence-20261104.json).

`3619C0` wymaga niezerowego vehicle id przed pełną inicjalizacją i zapisuje:

| Pole UI | Znaczenie |
|---|---|
| +0x1C | widoczność z bazowego UI |
| +0xE0 | vehicle id; zerowany przy zamknięciu |
| +0xE4 | section index |
| +0xE8, +0xEC, +0xF0 | component grid (x,y,z) |
| +0x100 | dostępność drugiego kanału: 0 dla type5, 1 dla type6 |
| +0xA8, +0xB0 | widgety kanałów 0 i 1 |

Show zapisuje oba bieżące floaty do obu pól tekstowych. Dla type5 ukrywa
drugi widget (`byte(*(UI+B0)+6C)=1`), lecz nadal go inicjalizuje.
Hide i kilka innych ścieżek frontendowych chowają UI i ustawiają UI+E0=0.
[Instrukcje 3619C0 i fragmenty 7B6060](../artifacts/keypad-native-protocol-evidence-20261104.json).

Nie uzyskano tu kompletnego dowodu caller→owner→actor dla frontendowego `S`.
Jego pola są zgodne z dotychczasowym układem simulation, lecz sam wskaźnik
UI i zgodne vehicle/grid nie są certyfikatem lokalnego świata/postaci.
To pozostaje warunkiem do rozpoznania przed podłączeniem nowego hooka.

## 3. Zatwierdzenie: dwa requesty type11

Handler frontendowych akcji `7B3100` ma dispatch po `param6`:
case4 (blok `7B463A`) anuluje, case5 (blok `7B46BD`) zatwierdza.
Case5 kopiuje i parsuje tekst z obu łańcuchów:
`UI+A8/B0 → widget+60 → input_box+78 → data+B8,length+C0`.
Następnie **bez warunku button_type wysyła dwa requesty**, dla kanału 0 i 1.
Dotyczy to również Small Keypad/type5, mimo ukrytego drugiego pola.
[Bezpośrednie instrukcje obu kanałów](../artifacts/keypad-native-protocol-evidence-20261104.json).

RTTI requestu to
`c_client_to_server_request_set_vehicle_button_float_input`,
vtable **B11460**, serializer w slocie 0 **231E00**, rozmiar alokacji **0x30**,
type **11**, metadata 0. Case5 kopiuje identyfikatory z bieżącego UI,
a nie z aktualnego hover targetu.
[RTTI i instrukcje tworzenia](../artifacts/keypad-native-protocol-evidence-20261104.json).

| Offset requestu | Typ / wartość |
|---|---|
| +0x00 | uintptr vtable |
| +0x08 | uint32 type=11 |
| +0x0C | uint32 metadata=0 |
| +0x10 | uint32 vehicle id z UI+E0 |
| +0x14 | uint32 section index z UI+E4 |
| +0x18..+0x20 | int32 grid[3] z UI+E8..F0 |
| +0x24 | float32 wartość kanału |
| +0x28 | int32 indeks kanału, 0 albo 1 |
| +0x2C | padding do rozmiaru 0x30; nie jest serializowany |

Serializer `231E00..231F65` odczytuje/zapisuje dokładnie siedem 32-bitowych
pól `+10,+14,+18,+1C,+20,+24,+28`. Indeks kanału **nie jest bajtem**.
Potwierdzają to m.in. instrukcje `231F43 MOV [R8+28],ECX` i
`231F51 MOV EAX,[R8+28]`. Prefiks 24 B:
`80 7A 14 00 4C 8B C1 8B 42 10 75 11 83 F8 04 72 27 48 8B 42 08 8B 08 41`.
[Pełna sekwencja serializatora](../artifacts/keypad-native-protocol-evidence-20261104.json).

Requesty są dodawane przez `236ED0` do aktualnego `D+0x5C0`, co odpowiada
`request_context(D+280)+0x340`. Instrukcja `7B46BD LEA RAX,[R8+5C0]`
wiąże tę kolejkę z bieżącym trzecim argumentem handlera. Po obu submitach
frontend dodaje osobne zdarzenie Clear do bieżącej kolejki game→logic,
chowa UI, usuwa fokus obu input boxów i zeruje UI+E0.
[Bloki submit i wspólny cleanup](../artifacts/keypad-native-protocol-evidence-20261104.json).

## 4. Anulowanie, aktualizacje i liczby

Anulowanie case4 **nie tworzy requestu float-input**. Tworzy 8-bajtowe
`c_event_game_to_logic_clear_keypad_button`, vtable **B69928**, id **0x18 (24)**,
i wykonuje wspólne zamknięcie UI. Hide logic→game ma vtable **B524A0**, id36.
Update logic→game ma vtable **AFA6A8**, id37, niesie oba teksty, floaty i typ.
[RTTI, gettery i ścieżki cancel/update](../artifacts/keypad-open-evidence-20261104.json),
[aktualizacja 77B430 i frontend](../artifacts/keypad-native-protocol-evidence-20261104.json).

Pusty tekst jest przez case5 zamieniany na 0. Niepusty przechodzi przez
`9B869C` (wrapper do `9B8628`, dalej parser `9B3E68` z kontekstem locale),
po czym `CVTSD2SS` zawęża double do float. Sam case5 nie sprawdza końca tekstu,
NaN/Inf ani min/max i nie wykonuje clamp przed zapisaniem requestu.
Nie zbadano całej gramatyki parsera `9B3E68`; **nie należy na tej podstawie
obiecywać akceptowania NaN/Inf, exponentów lub konkretnego separatora locale**.
[Wrapper i instrukcje CVTSD2SS](../artifacts/keypad-native-protocol-evidence-20261104.json).

## 5. Minimalny adapter i testy offline

Zalecany port adaptera otrzymuje **jawnie rozpoznany live-scope** oraz kopię
certyfikatu sesji: owner/world/actor jako tokeny porównania, generation Show,
UI identity, vehicle/section/grid, button_type i początkowe wartości obu kanałów.
Sam nie zgaduje ownera z UI i nie otwiera natywnej sesji przez surowy zapis pól.

Przy OK należy ponownie potwierdzić ten sam świat, postać, sesję, UI vtable,
widoczność, niezerowe UI+E0 i identyfikatory. Utrata fokusu, Hide, Clear,
nowy Show, zmiana świata/postaci albo zbyt stara obserwacja unieważniają sesję.
Dla type5 edytowany jest kanał0, a kanał1 zachowuje aktualną potwierdzoną
wartość; dla type6 potrzebna jest jawna obsługa obu kanałów.

Warto zastosować węższą, jednoznaczną gramatykę ASCII decimal i odrzucać
niekończące się, częściowo sparsowane lub niefinitywne wartości. To świadoma
polityka adaptera, nie twierdzenie o identycznej gramatyce natywnego UI.
Nie ma dowodu na używanie progów min/max z otwierającego eventu.

Port natywny powinien alokować oba stałe requesty przed pierwszym enqueue,
stosować natywny allocator/deallocator i bieżącą kolejkę. Po próbie enqueue
własność może być niepewna; nie wolno ponawiać requestów ani zakładać
atomowego rollbacku pary. Clear/ukrycie sesji jest osobną czynnością.

Testy wymagane przed podłączeniem: układ 0x30 i wszystkie offsety, kanały0/1,
zachowanie ukrytego kanału Small, finite/overflow/niepełny tekst, pusty wynik,
odrzucenie starego tokenu i obcego UI, Hide/nowy Show/zmiana postaci,
anulowanie bez submitu, błąd pierwszej/drugiej alokacji, częściowy lub niepewny
handoff bez retry, dokładnie jednokrotne commit oraz cleanup nieprzekazanych
obiektów. Fixture nie potwierdza dostarczenia do serwera.

Do czasu pełnej certyfikacji callerów i czasu życia sesji można ukończyć taki
adapter z atrapami granic i pozostawić produkcyjny panel jako lokalny.
Nie należy rozszerzać whitelisty HandButtons o typy5/6: dotychczasowy release
type10 oraz jego retirement nie są implementacją protokołu klawiatury type11.
