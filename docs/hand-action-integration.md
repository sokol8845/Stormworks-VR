# Plan natywnego kliknięcia dłonią

Aktualizacja 2026-11-04: dodano wybór lewej/prawej ręki oraz natywne Q/E.
Certyfikat zawiera akcję; pola zapisanego hitu, trzymania i zwolnienia są
wybierane dla danej ręki. [Aktualny opis i dowody](left-hand-q.md).
Poniższy opis zachowuje również historyczny etap prawej dłoni.


Analiza statyczna, 2026-10-29. Profil SHA-256
`f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`.
Nie uruchamiano Stormworks. Ten dokument opisuje następny moduł i jego brakujące
dowody; nie oznacza włączenia emisji wejścia.

## Decyzja o punkcie integracji

Wstępny adaptor powinien dopisać własny `key_action` przez natywny `236ED0`
**przed rozpoznanym pierwszym `694200`**, a następnie pozwolić zwykłej pętli
obsłużyć wynik. Dotyczy to callerów `86A6A8` i `86CC6F`, wyłącznie gdy początkowy
slot eventu jest pusty i potwierdzono bieżącego ownera. Nie dopisywać przy
kontynuacjach `86A6FC`/`86CCCE`: można w ten sposób samemu podtrzymywać pętlę
opróżniania kolejki.

Podmiana wyniku `pop` po oryginale nie jest prostszą alternatywą. Gdy oryginał
zwrócił event użytkownika, trzeba go zachować bez zmiany kolejności, a slot może
już zawierać poprzedni event, który zniszczył oryginalny pop. Wynik pusty zachowuje
ostatni event w slocie. Ręczne zastąpienie wymagałoby własnego buforowania,
odtwarzania kolejności i przejęcia destrukcji. Nie usuwa też konieczności końcowej
kontroli pauzy ani unieważnienia gestu. Native push zachowuje te istniejące reguły.

Nie wywoływać `78C770` z renderera ani z zapamiętanych argumentów pętli. Końcowy
gate własnego eventu mieści się w hooku `790820`, który dostaje żywe konteksty
i wskaźnik eventu. Oryginalny dispatcher korzysta dalej z natywnych warunków
odległości, postaci i żądań sieciowych.

## Cztery odpowiedzialności modułu

1. **Certyfikat selekcji.** Po `74F230` zapisać wartości identyfikujące użyty
   promień, próbkę, gest i aktualny cel. Przy nowym press sprawdzać dokładną
   vtable `B75678`, definicję `component+58`, `button_type` pod `definition+DA4`
   oraz kategorię 0 (Push Button; ewentualne 1 wymaga osobnej próby). Same
   wartości wskaźników są tokenami równości, nie zezwoleniem na późniejszą
   dereferencję. W każdym następnym hooku ponownie odczytać cel z żywego C.
2. **Plan przejścia gestu.** Jeden logiczny konsument mostu, mimo migracji
   wątków. Istniejący `consume_selected(expected_sequence)` odbiera dodatnie
   began/held tylko dla bieżącej próbki naprawdę użytej przez selekcję; mismatch
   nie konsumuje nowego naciśnięcia. Release/cancel może przejść niezależnie od
   numeru selekcji, również bez nowego poprawnego promienia. Najwyżej jeden
   pending press i jedno własne trzymanie. Ten seam nie emituje natywnej akcji.
3. **Natywna własność eventu.** Alokacja 16 B przez `95C638`, vtable `BB1ED8`,
   action 20, phase 1/0; własność przekazana dokładnie raz przez `236ED0`.
   Dopisanie do kolejki nie oznacza wykonania ani zwolnienia kontrolki.
4. **Kontrola wykonania i potwierdzenie.** W `790820` rozpoznać własny event,
   odnowić gate i dopiero wtedy wywołać oryginał. Po oryginale odczytać wynik
   `C+558` w tym samym żywym zakresie. Press jest przejęty dopiero gdy gra
   faktycznie ustawiła własny, oczekiwany cel. Release jest zakończony dopiero
   gdy własny cel zniknął z tego slotu albo zaobserwowano właściwy cleanup.

## Próbka, gest i aktualny promień

Nie wymagać równości z `renderer.latest.sequence` podczas dispatchera: renderer
publikuje częściej niż logika i spowodowałoby to odrzucanie niemal każdego press.
Certyfikat powinien zawierać `selected_sequence`, `xr_frame_id`,
`space_revision`, `cancellation_generation`, czas hosta oraz osobny
`pinch_gesture_id`. Most udostępnia już monotoniczne id na każde source-began
oraz read-only `pinch_down` (false dla nieważnej/starej próbki). Id zachowuje
wartość przy release/reset; jego licznik nie jest zerowany. Gdy konsument
przegapi release i zobaczy kolejny gest podczas trzymania starego, najpierw
otrzymuje cancelled i wymaga nowego otwarcia przed nowym press.

Końcowy gate dopuszcza nowszą poprawną pozę tego samego gestu, ale wymaga:
ważnego i świeżego certyfikatu, aktualnej ważnej dłoni, `pinch_down`, identycznych
gesture id/cancellation generation/space revision i zgodnego natywnego celu.
Normalny release, a potem kolejny pinch nie może reaktywować starego pending
press. Sama cancellation generation tego nie wykryje: normalne puszczenie nie
jest anulowaniem śledzenia.

Po zablokowanym began nie przenosić go na następny wskazany przycisk, gdy dłoń
pozostaje zaciśnięta. Wymagać otwarcia przed kolejnym nowym press. Po rozpoczęciu
trzymania ruch promienia na inną kontrolkę nie zmienia właściciela trzymania.

## Rejestr własnych eventów

Wdrożono już niezależny `HandEventRegistry` opisany w `hand-event-registry.md`,
z testami FIFO, reset, pointer reuse i równoczesnego claim. Poniższe powiązanie
z natywną alokacją, push, dispatcherem i destruktorem nadal nie jest aktywne.

16-bajtowy natywny event nie ma wolnego pola na metadane. Używać bocznego
rejestru `native_event_pointer -> immutable token + state`, z limitem pojemności.
Token obejmuje własny event id, generację sesji/świata, certyfikat selekcji,
gesture id, phase i etap: allocated, submitted, dispatched/suppressed, destroyed.
Wskaźnik służy wyłącznie do rozpoznania tożsamości obiektu należącego już do gry.

Rejestracja następuje przed push. Dtor `695E40` usuwa zapis **przed** oryginalnym
zwolnieniem pamięci, także gdy event został zniszczony z kolejką ownera bez
dispatchera. Nie trzymać mutexu rejestru podczas native push/dispatch/dtor.
Zapobiega to zakleszczeniom oraz pomyleniu nowej alokacji pod tym samym adresem.

Reset/focus loss oznacza oczekujące eventy jako odrzucone; **nie usuwa ich z
rejestru**. W przeciwnym razie stary queued press stanie się zwykłym wejściem
fizycznym. Rekord pozostaje aż do destruktora. Własny event dostarczony z
nieznanego callera/niezgodnego ownera jest tłumiony; nie wolno go puścić dalej
tylko dlatego, że nie rozpoznano ścieżki wykonania. Eventy nieobecne w rejestrze
zawsze przechodzą oryginalną drogą bez zmian.

Przy odmowie przed push natywny pełny obiekt może zniszczyć `695E40(event,1)`.
Nieznany błąd push nie uprawnia do ponowienia ani do samodzielnego free: przejście
własności może już nastąpić. Rekord pozostaje oznaczony jako niepewny i wymaga
obserwacji konsumpcji/destrukcji. Backend nie może być odłączony, pozostawiając
własne nierozpoznawalne eventy w żywej kolejce.

## Końcowe bramki i fizyczne wejście

W rozpoznanym dispatcherze owner można powiązać z bieżącymi argumentami:
`arg5 == owner+CE5C8`, `arg6 == owner+CE5F8`,
`controller == pointer_at(owner+CE190)+38`, active-state==simulation i zgodne
vtable owner/simulation. Nie należy wyprowadzać ownera z historycznego C.
`arg2` w tej pętli odpowiada bieżącemu c_application_data D; wymaga potwierdzenia
vtable `B17BF0` przed odczytem `D+233` (benchmark). Dowód przepływu D:
`84CF90 -> 84C890(owner,context,D,D+280) -> 86CBB0/86A0D0 -> 790820(C,D,...)`.

Nowy press wymaga całego gate z `hand-input-gates.md`: pięć flag C zerowych,
zamknięta pauza UI, brak aktywnego celu/przechwycenia UI, właściwy aktywny stan,
benchmark=0, whitelist przycisku, żywa postać, zdrowie dodatnie i oryginalne
native distance checks. Gate należy powtórzyć tu, po poprzedzających eventach
pauzy. Odczyty UI między wątkami oraz warianty chat/inventory nadal wymagają
potwierdzenia; suma znanych pól nie jest dowodem kompletności.

Własny press odrzucić również gdy right-action jest już aktywne fizycznie
(`C+4BF`, `C+558`) lub istnieją inne współdzielone interakcje. Fizyczne action20
oraz gest nie mają osobnych natywnych slotów. Zaobserwowanie fizycznego wejścia
podczas własnego trzymania wymaga rozstrzygnięcia właściciela i kolejności release;
nie filtrować fizycznego eventu. Należy sprawdzić tę kolizję w fixture i przyszłej
próbie zamiast deklarować niezależność na podstawie oddzielnych źródeł wejścia.

## Release i cancellation: czego obecne dowody nie gwarantują

Przy utracie fokusu, wieku próbki, zmianie kalibracji lub otwarciu UI planuje się
release wcześniej przejętego celu. Gate nowych press **nie blokuje** cleanup.
Trzeba jednak potwierdzić zgodność bieżącego C, świata/postaci, nadal własnego
held-right i jego kategorii, bez użycia nowego wskazania jako celu release.
Jeśli te warunki zniknęły, nie wywoływać starego C ani komponentu.

Istnieją dwie istotne przeszkody w utożsamianiu action20/phase0 z czystym cleanup:

- `78C770` już na wejściu odrzuca brak aktywnej postaci typu 4. Faza 0 pod
  `78FB2D..78FB3F` odrzuca health<=0 **przed** dojściem do `C+558`.
  Dopiero `7902DA` wysyła request release przycisku, a `7902F2` zeruje held-right.
  Enqueue phase0 po zgonie nie dowodzi zwolnienia.
- Przed tym release są ścieżki short-click zależne od bieżącego `C+538/C+520`
  (`78FCA5..7900D3`) oraz cleanup `C+550`. Gdy promień przesunie się z przycisku
  na inny obiekt, natywna faza0 może mieć dodatkowe skutki. Whitelist samego press
  tego nie wyklucza. Nie rozwiązywać tego podmianą C+520 na zachowany wskaźnik
  bez dowodu żywotności ani arbitralnym zapisem liczników czasu przycisku.

Nowa analiza `hand-release-cleanup-01.json` wykazuje, że `742570` emituje własny
release i zeruje C+558 przy brakującym transformie held-target albo odległości
od głowy >9.100000381469727 m². Przy bliskim celu `747A6E:JBE 747BE9` pomija tę
ścieżkę. Nie jest to ogólny cleanup śmierci/focus loss. `787F80` na początku
wywołuje held.vtable+450 i zeruje oba held-sloty, ale jest większą funkcją
obsługi zdarzenia świata, z dodatkowymi argumentami i skutkami; nie wywoływać jej
jako uniwersalnego release API. Dla konkretnej klasy Button slot +450 prowadzi
do `35D4E0`, które jedynie zeruje word pod button+278/+279, bez sieciowego release
(potwierdzenie w równoległej analizie cleanup; dalszy dowód tego slotu pozostaje
w jej artefaktach).

Dlatego stan `release_pending` trwa aż do potwierdzenia efektu oryginału lub
zaobserwowanego natywnego wyczyszczenia. Potrzebny jest jeszcze dowód i test
cleanup przy śmierci, zmianie postaci/świata oraz braku kolejnych przebiegów
logiki. Bez niego nie należy deklarować pełnej gwarancji niepozostawienia
przycisku wciśniętego ani włączać produkcyjnej aktywacji.

## Próby możliwe teraz i brakujące pomiary

Pasywny `native_input_probe` jest już podłączony jako opcjonalne `-InputProbe`
w Gameplay. Zapisuje rozpoznane pop/dispatch oraz żywe relacje i pola UI,
bez emisji lub tłumienia wejścia. Przeszedł fixture z atrapą MinHook; w grze
nie był uruchamiany. Może zebrać potrzebne obserwacje po zmianie instrukcji
użytkownika o zakazie uruchamiania gry. Szczegóły: `native-input-probe.md`.

Fixture poza grą może sprawdzić rzeczywiste wrappery ABI, FIFO, pusty pop,
zniszczenie poprzedniego/ostatniego eventu, queue teardown, cancel-before-receipt,
nieznany caller własnego eventu, pointer reuse, kolejność pause→press,
gesture release→new-began, migrację wątków i kolizję z fizycznym action20.
Nie dowiedzie rzeczywistej kategorii celu, kompletności UI ani zniknięcia
problemu kamery zgłoszonego przez użytkownika.

Po odwołaniu zakazu uruchamiania wymagane są pomiary zwykłego kokpitu, focus loss,
pauzy, tekstu/czatu, ekwipunku, śmierci i zmiany świata oraz pointer-away release.
Do tego czasu kod planera i fixture można rozwijać, zachowując brak produkcyjnej
emisji. Docelowo mod nadal ma obsługiwać faktyczne kliknięcia i puszczenia,
nie tylko wizualizację promienia.
