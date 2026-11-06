# Bramki nowego naciśnięcia dłonią

Analiza tylko do odczytu, 2026-10-29. Profil EXE SHA-256 `f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`, build 26275574. Nie uruchamiano Stormworks ani nie dodano natywnych zapisów/zdarzeń.

**Flagi dotychczasowej sondy `controller+494/+4BC/+4BD` oraz `origin_called==1` nie są wystarczającą bramką wejścia.** Oznaczają tryb fotograficzny, ekran śmierci i pobyt w łóżku. Pauza i pokazane menu mają inne pola. Można już zdefiniować konkretne warunki odmowy i ograniczyć kategorię celu; pełna zgodność z frontowym przechwytywaniem wejścia przez UI nadal wymaga obserwacji dodatkowych pól, zanim wolno włączyć nowe press.

## Powiązanie bieżącej kolejki, stanu i kontrolera

W rozpoznanym pierwszym `694200` (return RVA `86A6A8` albo `86CC6F`) argument kolejki jest żywy w oryginalnej pętli. Z tej kolejki można w tym samym zakresie wyznaczyć:

```text
owner      = input_queue - 0xCE6B0
simulation = pointer_at(owner + 0xCE190)
controller = simulation + 0x38
active     = pointer_at(owner + 0x350)
```

Dla tego profilu vtable owner ma RVA `B0F4F0` (`c_application_state_game`), a simulation `B1A790` (`c_game_state_simulation`). Początek kontrolera nie jest tą vtable: zawiera wskaźnik do świata/sceny. Nie wykonywać kontroli typu przez porównanie `*controller` do vtable simulation.

Przyszły adapter powinien w bieżącym wywołaniu wymagać niezerowych, zgodnych obiektów i `active==simulation`, a następnie porównać wyliczony controller z kontrolerem **aktualnej selekcji**, nie dowolnym historycznym wskaźnikiem. W `74F230` bieżący controller jest argumentem 1. Zmieniony owner/simulation/controller musi unieważnić własną kalibrację powiązania i stan interakcji. Sama kolejka może żyć dłużej niż świat lub postać.

Zachowany pomiar `cockpit-input-live-01` potwierdza przesunięcia ownera opisane w `native-input-queue.md`, jeden controller i migrację wątków logiki. Nie daje prawa do późniejszego dereferencjonowania zapisanych kontekstów stosowych. Poza hookiem należy przechowywać wartości pomiarowe i własne identyfikatory generacji, nie natywne wskaźniki do ponownego wywołania.

## Potwierdzone flagi kontrolera

| Pole (byte) | Znaczenie i dowód | Warunek dla nowego press |
| --- | --- | --- |
| `C+494` | Photo mode. Event `c_event_game_to_logic_set_is_photo_mode`, vtable `B86A08`, type 8; `790820` case 8 wywołuje `78C460`, które zapisuje to pole. | zero |
| `C+49C` | Simulation paused. Event vtable `B59D68`, type 1; `790820` case 1 kopiuje byte eventu. | zero |
| `C+49F` | Pause menu shown. Event vtable `B66740`, type 2; `790820` case 2 kopiuje byte eventu. | zero |
| `C+4BC` | Death screen. Setter `76A340` zapisuje pole i emituje `c_event_logic_to_game_set_is_death_screen`, vtable `B778B0`. | zero |
| `C+4BD` | In bed / sleep. Setter `76A1E0` zapisuje pole, emituje `c_event_logic_to_game_set_is_in_bed` (`B85CC0`) oraz żądanie snu. | zero |

`74F230` odrzuca wybór dla `4BC/4BD`, ale nie sprawdza w tej bramce `49C/49F`. Dlatego wywołany pomocnik początku promienia nie dowodzi, że menu pauzy jest zamknięte. Dyspozytor naciśnięcia `78C770` sprawdza `494`; to także nie zastępuje pełnej bramki pauzy/UI.

Interfejs posiada opóźnione kopie części flag: w `7B6030` event type `0x12` ustawia `simulation+3C73` (death screen), `0x11` ustawia `+3C74` (in bed), a `0x1C` ustawia `+3C70` (photo mode). Początkowy return w `92BB20/92C270` dla `simulation+3C73!=0` jest więc bramką ekranu śmierci, **nie ekwipunku**.

Zachowany live log ma 7606 wierszy i we wszystkich trzy stare flagi wynoszą zero. Nie rejestrował `49C/49F` ani stanów UI, zatem nie stanowi testu poprawności blokowania przy pauzie, czacie lub ekwipunku.

## Frontowe przechwycenie wejścia przez UI

Poniższe pola są osiągalne z żywego owner/simulation. Wartości powinny być logowane w tym samym natywnym zakresie; między wątkami przekazywać ich kopie. Nie zakładać, że taki odczyt eliminuje kolejkę opóźnionych zmian stanu.

| Pole | Dowód / możliwa konserwatywna bramka |
| --- | --- |
| `pointer_at(owner+CE150)+58` (int32) | Stan `c_ui_pause_menu_interface`. `7A1E70` ustawia go; zero oznacza zamknięty interfejs, niezero wybiera panel. `84E300/84E6D0` wpuszczają zwykły handler stanu symulacji tylko przy zero. |
| `owner+CDE80` (pointer) | To `owner+CDCF0+190`, docelowy element wejścia/fokusu UI. `84E300` najpierw przekazuje mu klawisz. `7A1E70` jawnie ustawia ten cel dla panelu 9. Niezerowy wskaźnik może nie konsumować każdego klawisza, lecz jego odrzucenie jest konserwatywną blokadą nowego gestu. |
| `simulation+25B0` (pointer) | Przechwycony element UI z obiektu pod `simulation+2328`, pole `+288`. `7B20E0` najpierw wywołuje jego handler klawisza, gdy niezerowy. Konserwatywnie blokować nowe press przy niezero. |
| `simulation+1D00` (pointer) | Aktywny element osadzonego UI `simulation+1B20`, pole `+1E0`. `7B20E0` również daje mu pierwszeństwo. Konserwatywnie blokować niezero. |
| `simulation+25A8` (pointer) | Element pod wskazaniem UI `simulation+2328`, pole `+280`. Obsługa myszy `1D7880` zwraca m.in. niezerowość tego pola. Może być użyte do dodatkowego konserwatywnego blokowania wskazywania, ale nie jest wymaganą bramką zwykłej klawiatury. |
| `simulation+3010` (byte) | `7B2870` dopuszcza `92C270` wyłącznie przy niezero. To także pole `+4F8` obiektu UI pod `simulation+2B18`. Pełna nazwa semantyczna nie została dowiedziona; zachować jako dokładnie rozpoznany warunek tej ścieżki, nie nazywać dowolnie „inventory closed”. |

Te wskaźniki przechwycenia nie oznaczają dowolnych, zawsze istniejących widgetów. `1D7880` kopiuje aktualny element z `+280` do `+288` przy obsłudze naciśnięcia, a `1E5190` obsługuje i potrafi wyzerować `+1E0`. **Nie wolno wywoływać tych funkcji jako czystych predykatów**: zmieniają flagi, wywołują callbacki elementów i obsługują kliknięcia. `6AFA40` również zapisuje stan UI i wykonuje operacje wskazania.

`7B20E0/7B2870` ponadto wymagają `producer_context3+233==0`. Późniejsza analiza
`input-context-233-01..04.json` rozstrzyga to pole: jest to aktywny tryb benchmark
w `c_application_data` D (vtable `B17BF0`), a nie kolejny stan UI. Callbacki
`89C6E0/89C640/89C5E0` przekazują `application+8` jako D do handlera aktywnego
stanu. `83A780` przekształca żądanie pod D+232 w aktywne D+233, wybierając
scenę benchmark; `8462D0` zapisuje swój argument 4 do D+233 (zwykły start daje
0, start benchmark daje 1).

`84CF90` przekazuje D i D+280 do `84C890`; worker `849320` przekazuje tę samą
parę ze swojego żywego job+98. Dalej `86CBB0/86A0D0` przekazuje D jako argument
2 `790820`, a bieżąca selekcja `74F230` ma D w argumencie 4. Zatem pole można
sprawdzić po weryfikacji vtable **w aktualnym znanym wywołaniu**, bez szukania
nieudowodnionego stałego offsetu D w ownerze i bez przechowywania kontekstu ze
stosu. First-pop sam nie dostaje D; ostateczna bramka może je odczytać przy
dispatchu własnego eventu. Mismatch vtable oznacza brak dopuszczenia nowego
press. Ta identyfikacja nie dowodzi kompletności pozostałych stanów UI.

Krótkie funkcje `86D270/86D2E0/86D350` odczytują część stanów simulation i ustawień globalnych, ale nie wykazano, że oznaczają ogólne „input allowed”; nie zastępować nimi powyższych warunków. Brak jeszcze pełnego testu pokazującego, które kombinacje przechwycenia występują dla ekwipunku/czatu.

## Moment sprawdzenia: first-pop nie wystarczy

Flagi kontrolera `49C/49F` są aktualizowane dopiero po konsumpcji odpowiednich eventów. Ponadto `7A1E70` najpierw dodaje `pause_menu_shown` do kolejki, a dopiero potem zapisuje nowy int `UI+58`. W chwili pierwszego pop może więc już istnieć zdarzenie pauzy przed później dopisanym key_action, podczas gdy odczytane flagi nadal przedstawiają poprzedni stan.

Z tego powodu przyszły **końcowy gate nowego press musi być ponownie sprawdzony przy logicznym odbiorze/wykonaniu własnej akcji**, po zdarzeniach zmieniających tryb, a nie tylko przed enqueue. Własne zdarzenie musi być odróżnione od fizycznego wejścia, aby nie odrzucać ani zmieniać zwykłych akcji użytkownika. To nadal projekt do wdrożenia po pomiarach, nie wykonana funkcja.

Bramka nowych naciśnięć jest inna niż sprzątanie trzymania. Zablokowanie press przez menu nie może samo powodować utworzenia nowego celu albo zostawić znanego przycisku wciśniętego. `released/cancelled` należy rozpatrywać wobec wcześniej przejętego, nadal właściwego celu i aktualnego kontekstu. Nie używać bieżącego nowego wskazania do zakończenia starego trzymania.

## Ograniczenie pierwszej próby do przycisków

W `78C770` branch przycisku rozpoczyna się od wywołania metody `component.vtable+38`, a potem `button.vtable+440`. RTTI przypisuje **vtable `B75678` do `c_scene_vehicle_component_button`**. Dla tej konkretnej klasy:

- slot `+38` wskazuje `CB690`, instrukcje `mov rax,rcx; ret` — zwraca sam obiekt;
- slot `+70` ma ten sam getter;
- slot `+440` wskazuje `1413B0`, instrukcje `mov al,1; ret` — zawsze true.

Zatem `+440` nie jest sprawdzeniem blokady/zezwolenia na użycie tego przycisku. Wstępny adapter może po zgodności profilu wymagać dokładnie tej vtable i bez wywoływania getterów odczytać `definition=pointer_at(component+58)`, następnie `int32(definition+DA4)` jako `button_type`. Powiązanie pola jest potwierdzone instrukcją `319CAF: lea r9,[rsi+DA4]` przed `319CBC: call 942080`; `942080` czyta/zapisuje atrybut XML `button_type`.

Lokalne definicje ROM:

| Wartość | Plik / nazwa |
| --- | --- |
| 0 | `button_push.xml`, Push Button |
| 1 | `button_toggle.xml`, Toggle Button |

Pierwszą ograniczoną próbę można zawęzić do 0, ewentualnie 0/1. Natywny kod dla typów 5/6 przechodzi do dodatkowego UI wprowadzania tekstu/keypadu; te typy oraz inne vtable powinny pozostać poza wstępną whitelistą. Samo `component!=nullptr` dopuściłoby również fotele, drzwi, ekwipunek i inne działania.

W historycznym pomiarze wszystkie 89 rzeczywistych rozpoczęć trzymania miały `held_right_after==component_before`, zgodnie z getterem identity, ale log nie przechwytywał vtable ani definicji. To dodatkowa spójność, nie dowód, że wszystkie 23 wskazywane cele miały konkretny `button_type`.

Oryginalny branch nadal musi wykonać własne warunki: aktywny obiekt typu 4 (postać), dodatni float pod `pointer_at(actor+258)+3F4`, zerowy wskaźnik `actor+2188`, oraz odległość głowy do komponentu w kwadracie nie większą niż stała double **9.100000381469727** z `BB3828`. Należy zachować te warunki i oryginalną obsługę żądania; whitelist vtable nie uprawnia do bezpośredniego przełączania wartości przycisku.

Aktywną postać oryginał znajduje przez `world=pointer_at(controller)`, `node=pointer_at(world+BB9F8)`, wymaga `int32(node)==4`, następnie niezerowych `holder=pointer_at(node+90)` i `actor=pointer_at(holder)`. To ścieżka do obserwacji w aktualnym zakresie logiki. Nie zachowywać `actor` ani `component` poza ich natywnym czasem życia.

## Następny krok przy zachowaniu zakazu uruchamiania gry

Można przygotować tylko klasyfikację/logowanie: relacja owner/simulation/controller, pięć flag kontrolera, stan interfejsu pauzy, wskaźniki przechwycenia, kategoria celu oraz zgodność generacji próbki dłoni z aktualną selekcją. Wynik powinien rozróżniać „odrzucone”, „niepełna wiedza o UI” i „warunki obserwacyjne spełnione”; ostatnie nie jest jeszcze zezwoleniem na natywne press.

Przyszły pomiar, dopiero po nowej zgodzie na uruchomienie, powinien objąć zwykły kokpit, pauzę, otwarty czat/tekst, aktywny element ekwipunku, przejście trybu i zmianę postaci. Nie ma dowodu wykonania tych wariantów w bieżącym zadaniu.

Materiały: `hand-input-gates-01.json` do `-05.json` i odpowiadające `.c`, `hand-input-gates-types.json`, `hand-input-gates-event-types.json`, `hand-input-gates-controller-refs.json`, `hand-input-gates-front-flags-refs.json`, `hand-input-gates-button-parser-callers.json`, `hand-input-gates-definition-types.json`, `hand-input-gates-profile.json`. Wyniki dekompilatora wymagają potwierdzania instrukcjami; np. `1D7880` ma wejściowe sloty stosowe, choć automatyczna deklaracja pokazuje jeden parametr.
