# Kolejka wejścia dla zdarzeń dłoni

Wynik analizy statycznej: **kolejką wejścia `c_event_game_to_logic_key_action` jest `c_application_state_game + 0xCE6B0`**. To nie kolejka `action_arg3+0x340`, do której dyspozytor wysyła żądania przycisków pojazdu. Najwęższy punkt przyszłej emisji bez przechowywania kontekstów stosowych to pierwsze pobranie z tej kolejki w istniejącej pętli logiki, opisane poniżej.

Profil: SHA-256 `f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`, Steam build 26275574. Wszystkie adresy są RVA tej wersji. Podczas zadania nie uruchamiano gry, nie odczytywano żywego procesu i nie dodano żadnego natywnego zapisu lub emisji wejścia.

## Właściciel i rozdzielenie kolejek

RTTI przypisuje vtable `B0F4F0` do `c_application_state_game`. Nie należy utożsamiać go z kontrolerem interakcji. Wskaźnik do `c_game_state_simulation` znajduje się w właścicielu pod `+CE190`. Kontroler przekazywany do `790820` wylicza się jako `pointer_at(owner+CE190) + 0x38` — jeden odczyt wskaźnika, potem przesunięcie o 56 bajtów. Vtable stanu symulacji to `B1A790`.

| Adres względem właściciela | Rola potwierdzona przepływem |
| --- | --- |
| `+CE6B0` | Synchronizowana kolejka **game → logic**, pobierana przez `694200`, z której zdarzenia trafiają do `790820`. |
| `+CE5F8` | Osobna kolejka **logic → game**, przekazywana do `790820` jako argument 6, a następnie do akcji jako argument 5. Dyspozytor kieruje tu np. `c_event_logic_to_game_toggle_map`. |
| `+CE5C8` | Oddzielny pomocniczy kontener/kontekst, argument 5 `790820`, argument 4 akcji. Nie jest kolejką wejścia. |
| `action_arg3+340` | Kolejka żądań klient → serwer w innym kontekście; otrzymuje `c_client_to_server_request_use_vehicle_button`. **Nie przekazywać tu key_action.** |

Zachowany pomiar `cockpit-input-live-01` daje zgodne przesunięcia: `action_arg4=237FEB2E5E8`, `action_arg5=237FEB2E618`, zatem historyczny właściciel to `237FEA60020`, a jego wejściowa kolejka byłaby pod `237FEB2E6D0`. `selection_arg6=237FEA60378` równa się temu właścicielowi `+358`. To kontrola analizy, **nie adresy do ponownego użycia**.

Argumenty akcji 2 i 3 były `5C4CDC4F80` i `5C4CDC65D8`; pochodzą z kontekstów przekazywanych przez aktualną pętlę. Żadna z tych wartości nie jest potrzebna do proponowanego punktu emisji. Nie wolno zapamiętać ich w hooku renderera i później ponownie wywoływać dyspozytora.

## Producent: dowód właściwej kolejki

Łańcuch natywnego wejścia jest następujący:

1. `84E300–84E523` pobiera aktywny stan pod `owner+350`. Przekazuje `owner+CE6B0` jako argument 5 wywołania jego metody wirtualnej `+48`.
2. Dla vtable `B1A790` ta metoda to `7B20E0–7B2406` (wskaźnik metody w `B1A7D8`). Przekazuje swój argument 5 jako argument 4 `92BB20`.
3. `92BB20` rozpoznaje wiązanie interakcji, alokuje zdarzenie 16-bajtowe, ustawia `action=0x13/0x14`, `phase=arg7` i wywołuje `236ED0(arg4,event)`.

Drugi łańcuch jest równoważny: `84E6D0–84E79C` przekazuje `owner+CE6B0` do metody `+60`; w stanie symulacji jest to `7B2870–7B2972` (slot `B1A7F0`), która przekazuje kolejkę do `92C270`. To potwierdzenie pochodzenia kolejki, niezależne od wspólnej funkcji `push`.

Producenci wykonują dodatkowe filtry UI. `84E300` uwzględnia modalny obiekt `owner+CDE80`, stan `*(owner+CE150)+58`, aktywny stan pod `+350`; `7B20E0` sprawdza też własne elementy UI i flagę `arg3+233`. Nie wszystkie flagi są jeszcze nazwane. Późniejsza emisja bezpośrednio do kolejki ominęłaby te frontowe filtry, więc samo `XR_FOCUSED` nie wystarcza do dopuszczenia nowych naciśnięć przy otwartym menu, czacie lub edytorze. Wymagane są bramki trybu gry i ich rzeczywista weryfikacja.

## Konsument i najwęższy punkt emisji

`86A0D0–86CBAA` oraz `86CBB0–86D270` wywołują `694200(owner+CE6B0,&current_event)` i dla każdego wyniku uruchamiają:

```text
790820(controller, live_context3, live_context2, live_context4,
       owner+CE5C8, owner+CE5F8, current_event)
```

Dyspozytor `790820` odczytuje typ przez `event.vtable[1]`. Dla typu `0x17` przekazuje action/fazę do `78C770`, korzystając z nadal żywych kontekstów pętli. Sam `790820` nie zwalnia eventu.

| Ścieżka | Pierwszy pop — adres powrotu | Kolejne pop — adres powrotu | Wywołanie dyspozytora |
| --- | --- | --- | --- |
| `86A0D0` | `86A6A8` | `86A6FC` | `86A6E7` |
| `86CBB0` | `86CC6F` | `86CCCE` | `86CCB8` |

Oba pierwsze pop są wykonywane bezwarunkowo dla danego przebiegu i poprzedzone zapisaniem zera do lokalnego slotu `current_event`. Zatem istnieje regularny punkt również wtedy, gdy użytkownik nie naciska żadnego fizycznego przycisku. Pętla `84C890` wybiera `86CBB0`, gdy globalna flaga `CEE65F` jest zero; w drugiej gałęzi czasuje `86A0D0`. Nie utożsamiać ich na podstawie samej tej flagi z konkretnym trybem pauzy lub multiplayer. Obsłużyć oba rozpoznane pierwsze wywołania i nie wymagać stałego ID wątku — rzeczywisty pomiar już pokazał migrację wątków.

Rekomendowany następny **pomiar tylko odczytowy**: hook `694200` z filtrem tych dwóch adresów powrotu; zapisać przekazane `queue`, `event_slot`, fakt `*event_slot==nullptr`, wątek i wartości po oryginale. Pozostałe wywołania muszą przejść bez zmiany. Zgodność z kontrolerem można potwierdzić w tym samym natywnym przebiegu, nie przez późniejsze dereferencje zapamiętanych obiektów.

Po takim pomiarze możliwa jest kontrolowana emisja **przed oryginalnym pierwszym pop**, używając wyłącznie aktualnego argumentu `queue`: pobrać jeden przejściowy gest z mostu, alokować i przekazać event przez oryginalny `236ED0`, następnie pozwolić oryginalnej pętli go pobrać, obsłużyć i zniszczyć. Nie zachowuje się `queue`, `event_slot` ani kontekstów poza bieżącym wywołaniem. Filtr pierwszego pop jest ważny: emisja przy każdej kontynuacji mogłaby dopisywać zdarzenia podczas opróżniania kolejki i tworzyć niekończący się przebieg.

Ta propozycja zachowuje kolejność istniejących zdarzeń (push dopisuje na końcu), ale jeszcze **nie dowodzi zgodności gestu z aktualnym promieniem**. Należy powiązać generację próbki dłoni z generacją ostatniego wyboru komponentu. Pierwsze pop może poprzedzać aktualizację wskazania w danym takcie; nie wolno aktywować nowego celu tylko dlatego, że obraz renderera pokazuje inną, nowszą próbkę. To osobny warunek przed natywnym naciśnięciem.

## Format eventu i własność pamięci

`c_event_game_to_logic_key_action` ma dokładnie 16 bajtów:

| Przesunięcie | Typ | Wartość |
| --- | --- | --- |
| `+0` | wskaźnik vtable | `exe_base+BB1ED8` |
| `+8` | int32 | action ID (`0x13` lub `0x14`) |
| `+C` | int32 | phase (`1` press, `0` release) |

Vtable zawiera `695E40` jako destruktor oraz `37C4D0` jako funkcję typu. Ta druga ma instrukcje `mov eax,17h; ret`. Nie ma dodatkowego pola typu w obiekcie. Dla porównania żądania sieciowe przycisku używają innego formatu i vtable; włożenie key_action do kolejki żądań nie jest równoważne wejściu.

Natywny alokator `95C638–95C674` bierze rozmiar w `RCX` i zwraca przydzielony adres w `RAX` (dekompilator błędnie pokazał `void`). Oryginalni producenci żądają `0x10` i inicjalizują powyższe pola inline. Po pomyślnym przekazaniu do `236ED0` producent już nie zwalnia eventu.

Własność przy odbiorze jest dokładnie następująca:

1. `694200` wyjmuje następny wskaźnik z kolejki.
2. Jeżeli `*event_slot` zawiera poprzednio pobrany event, wywołuje `event.vtable[0](event,1)` przy `6942F5`, a potem wpisuje nowy wskaźnik.
3. Jeśli kolejka jest pusta, `694200` zwraca 0 **bez zniszczenia ostatniego eventu i bez wyzerowania slotu**.
4. Konsument niszczy ten ostatni event przy wyjściu: `86CB50` w pierwszej ścieżce i `86D246` w drugiej. Oba przekazują flagę usunięcia równą 1.

Destruktor key_action `695E40–695E6F` przywraca bazową vtable `BA2150` i dla ustawionego bitu 0 flagi wywołuje natywne sized delete `95C22C(event,0x10)`. Nie używać `delete`/`free` z modułu do obiektu już przekazanego grze. Przed enqueue świeżo utworzony, pełny obiekt może zostać zniszczony przez ten sam natywny destruktor, jeżeli adaptor zrezygnuje z emisji. `236ED0` nie zwraca osobnego potwierdzenia przyjęcia; nie wolno po nieustalonym błędzie ponawiać enqueue lub zgadywać, czy własność już przeszła.

## Czas życia i synchronizacja kolejki

Konstruktor właściciela `899260–899703` inicjalizuje kolejkę pod `+CE6B0`; obejmuje ona `0xB8` bajtów, do następnego pola przy `+CE768`. Rozpoznane pola kolejki: tablica wskaźników `+0`, capacity `+8`, begin `+C`, count `+10`, obiekt blokady `+18`, condition variable `+68` i liczniki kolejności `+B0/+B4`. `push` i `pop` używają synchronizacji natywnej. Nie wykonywać ręcznych zapisów do tablicy/liczników i nie traktować niesynchronizowanego odczytu count/capacity jako spójnej walidacji w obecności producenta z innego wątku.

Destruktor właściciela `89AEA0–89B254` niszczy synchronizację oraz wywołuje `89CCB0–89CD4A` dla kontenera. Ten ostatni przechodzi po oczekujących eventach, wywołuje każdy wirtualny destruktor z flagą 1 (`89CCEE`), a potem zwalnia tablicę. **Żaden zapisany wskaźnik kolejki nie jest uprawniony po tym etapie.** Sam właściciel lub kolejka mogą też przetrwać zmianę świata; nie są tokenem ważności wskazanej kontrolki.

Najprostszy model adaptera nie przechowuje natywnych wskaźników: poza hookiem pozostają tylko wartości próbki dłoni i własny stan przejść. Zmiana sesji/kalibracji/trybu gry unieważnia próbkę. Nowe press dopuszcza się wyłącznie po potwierdzeniu aktualnego wyboru i bramek UI. Cancel nie jest nowym press; zakończenie już aktywnego przycisku wymaga uzgodnionej polityki natywnego backendu i żywego bieżącego kontekstu.

## ABI, granice i dowody

`694200` ma dwa argumenty wskaźnikowe i zwraca 0/1 przez `MOVZX EAX,DIL` przy `69433A`; odpowiedni wrapper powinien zachować to wyjście. `236ED0` ma dwa argumenty wskaźnikowe, bez znaczącego zwrotu. `95C638` zwraca wskaźnik, a scalar-deleting destructor `695E40` zwraca adres obiektu zgodnie z natywnym kodem. Nie przepisywać deklaracji `void` z samego dekompilatora, gdy instrukcje dowodzą zwrotu w RAX.

`artifacts/native-input-queue-signatures.json` zawiera pełny SHA, granice i pierwsze 24 bajty pop/push/alokatora/destruktora/konsumentów/właściciela, 6 bajtów funkcji typu, vtable oraz 5-bajtowe wywołania dla wszystkich czterech punktów pop. Kontrole prefiksu muszą być dodatkiem do zgodności całego SHA.

Materiały: `native-input-queue-01.json` do `-04.json` i odpowiadające eksporty `.c`, `native-input-queue-callers.json`, `native-input-queue-owner-refs.json`, wcześniejsze `cockpit-input-functions-02/03/07.json` oraz rzeczywisty `cockpit-input-live-01-summary.json`. Instrukcje potwierdzają adresy i własność; rozpoznanie kolejki nie jest jeszcze testem emisji gestu, poprawności osi ani działania przycisku przez dłoń.
