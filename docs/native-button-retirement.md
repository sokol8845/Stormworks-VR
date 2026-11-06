# Własność i retirement natywnego requestu przycisku

Aktualizacja 2026-11-02: adresy uzywane przez implementacje przeniesiono na
Steam build 26294447. [Zakres portu i dowody](game-profile.md). Powiazane
starsze zrzuty analizy zachowuja adresy i hash swojego oryginalnego EXE.

Wdrożono `native_button_retirement` dla profilu SHA-256
`c38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d`.
Moduł ma prawdziwe wrappery ABI i hooki MinHook, sprawdzone na wykonywalnej
fixture bez uruchamiania ani dołączania do Stormworks. Nie jest to wynik testu
żywej gry ani gogli. Zakres statycznego dowodu opisuje `button-request-last-reader.md`.

## Instalacja i API

`install_native_button_retirement(base,full_sha_matched,absolute_trace_path)`
wymaga `SWVR_HAND_BUTTONS=1`, zgodności pełnego SHA i poprawnych prefiksów/CALL
oraz vtable. Wywołuje się ją po `MH_Initialize`, przed globalnym `MH_EnableHook`.
Tworzy pięć **wyłączonych** hooków i nowy plik `.button-retirement.tsv`.
Nie włącza/wyłącza hooków i nie wywołuje helperów gry. Częściowa instalacja
jest wycofywana przez usunięcie wszystkich utworzonych hooków.

`wrap_native_button_request_api(source)` można utworzyć po udanej instalacji,
jeszcze przed globalnym włączeniem hooków. Dopiero użycie callbacków wymaga
aktywności wszystkich hooków. Context wrappera jest stabilną metadaną procesu;
source.context musi nadal żyć. Moduł nie ma uninstall/disable API, które mogłoby
pozostawić własne żywe alokacje bez obserwacji.

Wrapper zachowuje źródłowe callbacki i ich context. Rezerwuje jeden z 128 slotów
**przed** source.allocate, więc pełny ledger odmawia zanim powstanie nowy natywny
obiekt. Alokacja dostaje monotoniczne allocation ID. Source.assign/discard są
wywoływane bez mutexu ledgeru. Pomyślne pre-handoff discard zwalnia rezerwację;
nieokreślony błąd alokacji/discard zachowuje quarantine. Wrapper może być
współdzielony przez niezależne builders; ten sam request ma jednego właściciela
serializującego operacje.

Bezpośrednio przed source.enqueue wrapper kopiuje własny request pointer,
payload pointer, allocation ID oraz stan requestu. Dopiero wtedy dodaje adres
do lookup obiektów przekazanych grze. Source.enqueue dostaje ten sam queue i
request. Po wywołaniu, także przy wyjątku, wrapper nie dereferencjonuje requestu.
Nie ponawia enqueue i nie zgaduje jego wyniku. Synchroniczny pełny retirement
wewnątrz source.enqueue jest poprawny; późniejszy wyjątek source nie odtwarza
już rozliczonego starego ID.

## Rzeczywiste ABI i guardy

| RVA hooka | ABI i dopuszczony zakres obserwacji |
| --- | --- |
| `23CA90` | `void(message*,buffer*)`; return caller `23CD40`, buffer byte+14==1 (WRITE) |
| `855AB0` | `void(owner*,graphics*,live_context*,message*,game_data*)`; message jest argumentem **4/R9**, piąty argument jest zachowywany; return `86A7FF` |
| `8D3BB0` | `void(deque*)`; końcowe WRITE return `240898` albo `240A21`; inner localhost return `89D0A8` wymaga dodatkowego TLS typed scope |
| `89D030` | `void(deque*)`; localhost typed clear return `86A817` |
| `95C2EC` | `void(pointer,size_t)`; oryginalne oba argumenty są zachowywane; własny localhost delete wymaga return `8D3BEE` i zgodnego TLS/ID |

Instalacja porównuje 24 B serializer/dispatch/raw/typed oraz helpera
`9B6750` (`void aligned_free(void*)`) i 5 B delete thunk `95C2EC`.
Wymaga `B89760[0]==base+23CA90`. Sprawdza virtual CALL `FF 10` pod `23CD3E`
oraz rel32 CALL do odpowiednich funkcji kończące się w `86A7FF`, `240898`,
`240A21`, `89D0A8`, `86A817`, `8D3BEE` i `89D0CF`. Każdy używany adres musi być
w czytelnym, wykonywalnym regionie; false SHA jest odrzucane przed odczytem bazy.

ABI potwierdzają instrukcje w `button-request-lifecycle-evidence.json`,
`button-request-last-reader-evidence.json` oraz `button-request-teardown-evidence.json`.
Szczególnie `855AB0` ma piąty argument stosowy — wrapper czteroargumentowy
byłby niepoprawny. `B0FEC0[0]` nadal jest serializerem requestu, nigdy destruktorem.

## Obserwacja konkretnego parenta

Rozpoznany parent to message0xD0 o vtable B89760, type-u32+8==3. Jego ring dzieci
zaczyna się pod +18: pointer tablicy, capacity-u32+8, begin-u32+C, count-u32+10.
Odczyt jest wykonywany wyłącznie podczas bieżącego native wywołania. Dziecko
jest własne tylko gdy jego adres jest w aktywnym lookup i allocation ID nadal
pasuje. Do rekordu trafia parent address i odrębne monotoniczne parent ID.

Pomyślny powrót serializera oznacza completed WRITE observation, a powrót
dispatchera completed localhost observation. **Żadna z tych obserwacji sama
nie uruchamia free.** READ, nieznany caller, obca vtable lub niepasujące
członkostwo nie tworzą tego dowodu. Zmiana parenta istniejącego własnego requestu
lub zniknięcie wcześniej zaobserwowanego dziecka nie przenoszą starego dowodu
na nową alokację: zapis jest quarantined.

Przed dokładnie rozpoznanym końcowym clear powstaje ograniczony snapshot
allocation ID, request address, payload address, parent address/ID i rodzaju
końca. Po clear nie są odczytywane pola zwolnionego parenta/requestu. Brak
obserwacji last reader lub niezgodność snapshotu daje quarantine, nie domyślny free.

## Dwa różne końce

**WRITE:** oryginalny raw clear usuwa outermessage. Dopiero po poprawnym
powrocie z tego clear mod wywołuje aligned-free dla własnego payloadu i
delete-trampoline(request,72). Nie usuwa outermessage ponownie. Podczas native
free payloadu request pozostaje rozpoznawalny; dodatkowy unknown delete w tym
oknie quarantines jego ID. Metadata recheck po free zapobiega późniejszemu
drugiemu delete albo pomyleniu ponownie użytego adresu.

**Localhost:** oryginalny typed clear woła raw clear parent+18. TLS scope musi
równocześnie zawierać dopasowany parent/ID i inner queue; sam return adres nie
wystarcza. Hook delete potwierdza rzeczywiste native usunięcie własnego requestu.
Po poprawnym powrocie całego typed clear mod zwalnia **tylko** skopiowany pointer
własnego payloadu. Nie wykonuje drugiego delete requestu.

Powtórny callback tego samego własnego native delete w tym samym dopasowanym
localhost zakresie jest tłumiony po ID. Po potwierdzonym native delete stary
request address może być użyty przez nową alokację, nawet zanim typed clear
wróci; stare ownership payloadu pozostaje po starym ID. Nie wolno utożsamiać
tych dwóch alokacji przez sam pointer.

Nieznany kontekst delete własnego przekazanego requestu usuwa jego adres z
lookup **przed** oryginalnym delete, ale zachowuje quarantined rekord po ID.
Oryginalny delete nadal wykonuje swoją operację; mod nie zgaduje free stringu.
Nowa alokacja pod tym samym adresem dostaje nowe ID. Obce delete i obce requesty
zawsze przechodzą oryginalną drogą z niezmienionymi argumentami.

Wyjątek last reader unieważnia jego completed proof. Wyjątek końcowego clear
zachowuje quarantine wszystkich objętych własnych rekordów. Wyjątek własnego
free/delete po udanym clear również daje quarantine bez ponowienia; nie jest
zamieniany na arbitralne „rozliczone”. Żaden native callback nie jest wywoływany
pod mutexem ledgeru ani logu.

## Źródłowa kolejka — wdrożenie 2026-11-02

Wrapper zapisuje rzeczywistą Q przed enqueue. Hook raw-clear rozpoznaje reset
return `24005B` i destrukcję return `23EB68`, z dodatkowymi guardami prefiksów
`23FF80/23EA20` i obu CALL. Snapshot obejmuje wyłącznie własne ID nadal obecne
w dokładnie tej Q, w stanie submitted, bez parenta. Sam logical reset nie jest
takim dowodem. Brak membership nie uruchamia żadnego cleanup.

W obrębie tego konkretnego clear hook delete potwierdza native usunięcie
właściwej alokacji przy return `8D3BEE`. Po zakończonym clear moduł zwalnia
wyłącznie skopiowany payload, bez drugiego delete requestu i bez odczytu jego
zwolnionych pól. Duplicate własnego delete jest tłumiony po ID. Zagnieżdżony
nieznany clear nie może pożyczyć dowodu z zewnętrznego source scope.

Inna kolejka, parented child, return `23EB0F/242057`, nieznany delete lub
niepotwierdzony powrót zachowują quarantine. Liczniki `source_reset_retired`
i `source_destroy_retired` oraz odpowiadające eventy logu oznaczają disposal
pending requestu, nie transmisję ani potwierdzenie release przez serwer.
Podstawa statyczna: [button-request-teardown.md](button-request-teardown.md).

## Reset, idle i ograniczenia

`note_native_button_retirement_reset()` unieważnia logiczny zamiar, podnosi
generację i **zachowuje** pending/uncertain ownership. Zmiana XR focus nie
zwalnia pamięci requestu i nie usuwa go z lookup. Własność może zostać rozliczona
później przez zaobserwowaną właściwą trasę.

`retained_records==0` oznacza idle ledgeru. `live_records==0` **nie wystarcza**,
ponieważ wyklucza quarantines. Normalnie zakończone retirement records są
recyklowane; retained obejmuje rezerwacje pre-handoff, queued/uncertain,
retirement w toku i trwałe quarantine. Pełna pojemność nigdy nie usuwa istniejącego
rekordu. Limit 10000 wierszy diagnostyki jest odrębny: po jego osiągnięciu
mechanizm własności nadal działa.

Moduł obsługuje dwa końce parentmessage oraz dwa dokładnie rozpoznane końce
source-Q. Pozostałe nieznane trasy zachowują unknown-delete quarantine;
nie dostają automatycznie zasad WRITE ani source-Q.
Hooków nie wolno wyłączać, gdy ledger przechowuje własną nierozliczoną pamięć.

## Wykonane testy

Po wdrożeniu source-Q pełny Release build i **23/23 CTest przeszły**:
[raport 2026-11-02](../artifacts/offline-transitions-tests-20261102.xml).
Nowe próby obejmują oba source końce, duplicate delete, dokładną Q, nieznane
callery, child przeniesiony do parenta, nieznany delete, wyjątek clear i nested
unknown clear. Rzeczywiste moduły HandButtons/capture/builder/retirement przechodzą
też łączony test deaktywacji i zmiany lokalnej postaci. Pamięć requestów fixture
jest decommitowana przy native delete, więc odczyt po usunięciu nie jest legalny.

Wcześniejsze samodzielne testy:

Standalone MSVC `/std:c++20 /EHsc /W4 /WX /permissive-` i
`build/button-retirement-standalone/swvr_native_button_retirement_tests.exe`
przeszły. Fixture używa fake MinHook i prawdziwie wykonywanych caller thunks
pod sprawdzanymi return RVA. Thunki mają poprawny shadow space, kopiują piąty
argument i rejestrują `RtlAddFunctionTable` z unwind kodem alokacji stosu.
Rzeczywiste wyjątki C++ przechodzą przez te thunki w próbach błędów.

Sprawdzono oba WRITE końce, localhost typed→inner raw→delete, dodatkowy delete
callback, synchroniczny retirement w enqueue, niepewny enqueue rozstrzygnięty
późniejszą obserwacją, source partial-assign/discard, foreign passthrough,
unknown/READ serializer, parent i request address reuse oraz reuse requestu
jeszcze wewnątrz typed clear. Zwolnienia decommitują całe strony fixture, więc
odczyt po free nie byłby legalny. Osobne przypadki obejmują wyjątki serializer,
dispatch, raw/typed clear i free, unknown delete podczas free, unknown teardown,
log cap, pełną pojemność oraz reset zachowujący ledger. Procesy guard tests
sprawdzają każdy prefix/CALL/vtable, opt-in/SHA, ścieżkę logu i rollback każdego
z pięciu możliwych etapów tworzenia hooków.

To dowód implementacji wrapperów i bookkeeping na fixture. Nie zastępuje
przyszłego testu faktycznej transmisji, callbacków i zwolnień w Stormworks;
podczas tej pracy gra pozostała wyłączona.
