# Natywna interakcja dłonią z kontrolkami kokpitu

Analiza statyczna lokalnego Stormworks, SHA-256 `f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`, Steam build 26275574. Wszystkie adresy są RVA. Nie uruchamiano gry, nie wysyłano wejścia systemowego i nie zmieniano kodu moda podczas tej analizy. Poniższe punkty wymagają pomiaru w rzeczywistym kokpicie przed zapisem do struktur lub wywoływaniem akcji.

## Znaleziona ścieżka

Istnieje konkretna droga do ponownego użycia oryginalnej logiki przycisków: wyznaczenie celu w `74F230`, następnie obsługa akcji w `78C770`. Ta druga funkcja tworzy także zdarzenia przycisków pojazdu, zachowuje trafioną kontrolkę na czas trzymania i wysyła jej puszczenie. Nie jest to obsługa kliknięć menu.

| Etap | RVA / dowód |
| --- | --- |
| Rejestracja akcji | `213E30–21887A`: `action_interact_left` przy `214D14`, ID `0x13`; `action_interact_right` przy `214DC9`, ID `0x14`. |
| Producenci akcji interakcji | `92BB20–92C262` i `92C270–92C9B2` mapują bieżące wiązania na `0x13/0x14` i przekazują fazę bez zmiany do zdarzenia wejściowego. |
| Aktualizacja trafienia | `74F230–751CB2`, wywołanie z `742570–7495C3` przy `7459EF`. |
| Początek promienia | `4E33A0–4E36E1`, wywołany przez aktualizację trafienia przy `74F358`, powrót `74F35D`. |
| Dyspozytor akcji | `78C770–790813`; akceptuje ID akcji i fazę: `1` naciśnięcie, `0` puszczenie. |
| Dyspozytor zdarzeń wejścia | `790820–79453C`, gałąź typu `0x17` wywołuje `78C770` przy `79337D`. |
| Kolejka zdarzeń | `236ED0–236FF1`, dodaje wskaźnik do synchronizowanego kontenera. Dyspozytor akcji przekazuje mu `arg3+340`. |

Identyfikatory `0x13/0x14` oznaczają dwa rodzaje interakcji gry, **nie lewą i prawą dłoń XR**. Przypisanie gestów obu dłoni do tych ID jest osobną decyzją; automatyczne użycie jednego ID na rękę mogłoby zmienić znaczenie sterowania.

## Promień wskazania

`74F230` bierze kontroler w `RCX`, zachowuje go w `R13` i znajduje aktywną postać przez `*controller`, pole `+BB9F8`, następnie `+90` i pierwszy wskaźnik. Sprawdza rodzaj aktywnego obiektu równy 4. Flagi kontrolera `+4BC/+4BD` wyłączają wybór i zerują wskazanie; ich pełna semantyka nie została jeszcze ustalona.

Przy `74F358` wywołuje `4E33A0(character, output, character[468C], 1)`. Pomocnik zapisuje dokładnie trzy wartości double do `output+0/+8/+10`. Uwzględnia transformację postaci, wysokość/punkt głowy i alternatywną gałąź pozy. **Nie należy zastępować go globalnie**: ma innych odbiorców poza wyborem kontrolki.

Następnie kod wylicza:

```text
ray_start = returned_double3
ray_end   = ray_start + 3.0 * float3(controller+AA0, +AA4, +AA8)
```

Stała `BB3890` zawiera float `3.0`. Są to trzy jednostki współrzędnych świata gry. Odczyty pozostałych składowych macierzy `A88..AA8` są mnożone przez zero; kolumna `AA0/AA4/AA8` jest rzeczywiście użytym kierunkiem. Wynik powinien zostać porównany z kamerą i skalą świata podczas pomiaru, zanim przyjmiemy orientację osi XR.

Wybór pojazdów korzysta m.in. z `4C2E60` przy `74F58F`. Przekazuje parę punktów double, zbiera potencjalne trafienia, przelicza je do lokalnych współrzędnych części i porównuje odległości. Funkcja selekcji ma też osobną ścieżkę obiektów świata. Samo podmienienie wyłącznie jednego niższego raycastu nie gwarantuje spójności obu rodzajów celów.

Najmniejszy kandydat do przyszłego nadpisania promienia to zakres jednego wywołania `74F230`: tymczasowy kierunek `AA0..AA8` oraz wynik `4E33A0` **tylko przy powrocie `74F35D` i aktywnym kontekście tego wywołania**. Oryginał nadal wyznacza trafienia, blokady, punkt lokalny i opis kontrolki. Nie ma potrzeby zmieniać globalnej pozycji postaci ani kamery renderera. Najpierw wymagany jest obserwacyjny pomiar obu punktów.

## Wynik wskazania i trzymanie

Pola względem tego samego kontrolera:

| Pole | Znaczenie poparte przepływem |
| --- | --- |
| `+520` | Bieżący trafiony komponent pojazdu; dyspozytor odpytuje jego metody typu przycisk/monitor/inwentarz itd. |
| `+528/+52C/+530` | Trafienie float3 w lokalnych współrzędnych komponentu, używane w żądaniu przycisku. |
| `+538` | Bieżący obiekt interaktywny świata; odrębny od komponentu pojazdu. |
| `+54C` | Dodatkowy indeks/rodzaj wskazania; `-1` dla braku. Pełna semantyka nieustalona. |
| `+558` | Zapamiętany przycisk rozpoczętej akcji `0x14`. |
| `+560` | Zapamiętany przycisk rozpoczętej akcji `0x13`. |
| `+568/+56C/+570` | Zapamiętany lokalny punkt dla pierwszego z powyższych celów. |
| `+574/+578/+57C` | Zapamiętany lokalny punkt dla drugiego celu. |

Nie kopiować całego kontrolera dla dłoni i nie podmieniać wyłącznie `+520` na zgadnięty obiekt. Znaczenie wyniku obejmuje lokalny punkt, odwołania do pojazdu, metody wirtualne, kontekst postaci i stan trzymania. Aktualizacja selekcji także emituje tooltip; wielokrotne wywołanie jej na każdą dłoń nie jest automatycznie wolne od efektów ubocznych.

## Akcje i rzeczywisty przycisk pojazdu

Prolog `78C770` potwierdza siedem argumentów ABI Windows x64: cztery pierwsze sloty w rejestrach, piąty slot na stosie, potem `int action_id` i `int phase`. Po prologu action ID czytane jest z `RBP+D8`, phase z `RBP+E0`, czyli z `entry_RSP+30/+38`. Pierwszy argument to kontroler, trzeci jest właścicielem kolejki `+340`; dokładne typy pozostałych kontekstów pozostają do pomiaru.

Nie zaleca się jeszcze wywoływać tej funkcji z ręcznie odtworzonym zestawem argumentów. Pierwszy hook powinien zapisać rzeczywiste argumenty, ID/fazę, wątek, wskazanie przed i po oraz zapamiętany cel trzymania podczas zwykłego użycia kontrolki przez użytkownika.

RTTI jednoznacznie rozdziela:

- vtable `B0FFF0`: `c_client_to_server_request_use_vehicle_button`;
- vtable `B49C40`: `c_client_to_server_request_use_interactable` — ogólne obiekty świata;
- vtable `BB1ED8`: `c_event_game_to_logic_key_action` — zdarzenie wejścia do logiki, nie pakiet przycisku.

Dyspozytor tworzy żądanie przycisku o rozmiarze `0x48`, typie `10` pod `+8`. Pole `+24` jest ustawiane na `1` w ścieżce naciśnięcia (`78D5F1`), na `0` w ścieżkach puszczenia (`790277`, `7904C7`). Zawiera także identyfikację pojazdu/części, punkt float3 `+28/+2C/+30` i tekst/etykietę. Nie wszystkie pola ID są jeszcze nazwane. Kod puszczenia używa zapamiętanego celu `+558/+560`, a następnie zeruje go; nie bierze dowolnego nowego obiektu pod kursorem.

Zdarzenie wejściowe `c_event_game_to_logic_key_action` ma rozmiar `0x10`, action ID pod `+8` i fazę pod `+C`. `790820` obsługuje je w gałęzi `0x17`. Producent `7B2440` potwierdza format na akcjach kółka `0x18/0x19`; nie jest samodzielnym konstruktorem przycisku kokpitu. Dwa dalsze eksporty potwierdzają właściwe interakcje: `92BB20` porównuje kod wejścia z konfiguracją `+6B0/+708`, a `92C270` z `+6B8/+710`, po czym oba tworzą `0x13/0x14` i zapisują swój argument 7 jako fazę. Nie ustalono jeszcze nazw źródeł wejścia tych dwóch funkcji; nie są po prostu osobnymi funkcjami press i release. Obie fazy przechodzą przez ten sam argument.

Producenci alokują zdarzenie natywnym `95C638(0x10)` i przekazują je do `236ED0(queue,event)`. Jest to możliwa droga przekazania gestu na właściwy wątek logiki, lecz przejęcie własności zdarzenia, alokator i typ wirtualny muszą pozostać zgodne z grą. Bezpieczniejszy pierwszy etap pozostaje obserwacją istniejących zdarzeń, nie samodzielnym konstruowaniem pakietów sieciowych przycisku.

## Najbliższy test integracyjny

1. Dodać tylko obserwacyjne hooki `74F230`, `4E33A0` z filtrem caller `74F35D` i `78C770`, chronione dokładnym SHA oraz zapisanymi prefiksami. Logować wątek i wskaźnik kontrolera, promień, cel, lokalny punkt i pary `action/phase`. Nie inicjować akcji.
2. W potwierdzonym kokpicie wskazać momentary button, toggle button i dźwignię; nacisnąć i puścić zwykłe akcje gry. Sprawdzić zgodność pól, moment zmiany celu oraz kolejność aktualizacji selekcji i wejścia. Oddzielić wynik dla przycisku od monitora i inwentarza.
3. Przekazać najnowszą, ważną próbkę dłoni z wątku XR do wątku gry przez własny synchronizowany bufor. Nie wywoływać logiki kontrolki z hooka renderera. Przy utracie fokusu, nieaktywnych stawach lub starym czasie nie wolno rozpoczynać interakcji.
4. Włączyć wyłącznie wizualizację i podmianę promienia w opisanym zakresie jednej selekcji; porównać trafiony przycisk i punkt lokalny z rzeczywistym palcem. Utrzymać normalne ograniczenia odległości i blokady gry. Osi oraz transformacji dużych współrzędnych nie zgadywać.
5. Dopiero potem mapować `PinchEvent::began` na naciśnięcie i `released` na puszczenie tego samego zapamiętanego celu. Anulowanie wymaga osobnej polityki natywnego backendu: należy zakończyć stan trzymania bez nowego naciśnięcia, ale zdarzenie toggle już wykonane na press nie musi dać się cofnąć. Nie traktować `cancelled` jako ogólnego nowego kliknięcia.

Pierwsza wersja może arbitrować jedną aktywną dłoń wskazującą, zachowując śledzenie obu. Równoczesne trzymanie dwiema dłońmi nie jest dowiedzione przez dwa ID akcji i wymaga osobnego modelu własności celu. Powyższa analiza nie potwierdza jeszcze działającej interakcji optyczną dłonią.

## Materiał dowodowy i ograniczenia

- `artifacts/cockpit-input-strings.json`, `cockpit-input-string-refs.json`: nazwy akcji i referencje.
- `cockpit-input-rtti.json`, `cockpit-input-event-types.json`, `cockpit-input-key-action-rtti.json`: nazwy typów i tablice metod; nie są to symbole od autora gry.
- `cockpit-input-functions-01.json` do `-07.json` oraz odpowiadające pliki `.c`: ograniczone eksporty Ghidry i instrukcje wskazanych funkcji.
- `cockpit-input-target-writes.json`, `cockpit-input-selection-callers.json`: kandydaci odnalezieni przez wzorce kodu; następnie weryfikowani dekompilacją.
- `cockpit-input-button-events.txt`: kontekst naciśnięcia/puszczenia przycisku pojazdu.
- `cockpit-input-signatures.json`: prefiksy funkcji i SHA badanego pliku.

Analiza bez pełnej autoanalizy może mieć niepoprawne nazwy i typy dekompilatora. Eksporter zapisuje listę instrukcji **przed** dekompilacją; przy funkcji ze switch/jump table (np. `790820`) lista ta jest niepełna, mimo że późniejszy dekompilator odtworzył dalsze gałęzie. Nie używać jej do twierdzenia, że pokazuje cały graf. Nie potwierdzono jeszcze kolejności i wątków w wykonaniu, osi promienia w rzeczywistym kokpicie ani zachowania przy utracie dłoni.

## Zaimplementowana sonda obserwacyjna

`include/swvr/interaction_probe.hpp` i `src/interaction_probe.cpp` implementują wyłącznie pomiar. Rodzic wywołuje `install_interaction_probe(exeBase, shaMatched, tracePath)` po inicjalizacji MinHook, przed włączeniem hooków i poza `DllMain`. Funkcja wymaga `SWVR_INTERACTION_PROBE=1`, zgodności SHA zgłoszonej przez wywołującego i dokładnych 24-bajtowych prefiksów wszystkich trzech funkcji. Tworzy tylko wyłączone hooki; po częściowym niepowodzeniu usuwa wyłącznie własne utworzone hooki. Nie wywołuje globalnego initialize/enable/uninitialize MinHook.

ABI ponownie sprawdzono z instrukcjami. `74F230` ma sześć slotów całkowitoliczbowych/wskaźnikowych, `78C770` siedem, z dwoma końcowymi int32. Nie ma wejściowych argumentów XMM dla tych dwóch punktów. Dla nieustalonych kontekstów użyto uint64 zamiast wymyślać typy klas. Ważna korekta deklaracji dekompilatora: `4E33A0` wykonuje `MOV RAX,RDI` przy `4E36C4`, zwracając wskaźnik wyniku. Wrapper ma zwrot `void*` i zachowuje rzeczywistą wartość oryginału, a nie deklarowane wcześniej przez dekompilator `void`.

Sonda zapisuje osobny `tracePath.interaction.tsv`, utworzony przez `CREATE_NEW`, bez nadpisywania istniejącego pliku. Limit wynosi 10000 wierszy danych, niezależnie od limitu śladu GL. Każdy wiersz zawiera wątek, zegar QPC, kontroler, nieinterpretowane konteksty, wynik wskazania i stan trzymania przed/po. Dla selekcji zapisuje pozycję double oraz kierunek tylko wtedy, gdy `4E33A0` wraca do `74F35D` wewnątrz aktywnego, lokalnego dla wątku wywołania selekcji. Pozostałe wywołania pomocnika pozycji są przekazywane bez logowania. Dla akcji zapisuje tylko ID `0x13/0x14`. Kod nie odczytuje wskazanych obiektów, nie zmienia bajtów kontrolera i nie generuje żadnych akcji. Nie wykonuje wywołań GL.

`tests/interaction_probe_tests.cpp` przeszedł samodzielną kompilację MSVC x64 C++20 z `/W4 /WX /permissive-`. Używa atrap MinHook i funkcji natywnych; nie uruchamia Stormworks. Własny thunk w pamięci procesu testowego odwzorowuje dokładny adres powrotu `74F35D`. Test sprawdza pełne argumenty ABI, zwracany wskaźnik, filtr caller/TLS, zdjęcia pól przed/po, brak modyfikacji kontrolera, odmowę niezgodnego SHA/prefiksu i istniejącego pliku, rollback oraz dalsze wykonywanie oryginału po osiągnięciu limitu logu. Pozostaje integracja CMake i rzeczywisty pomiar kokpitu.
