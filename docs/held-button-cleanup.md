# Zakończenie trzymania przycisku przez dłoń

Analiza statyczna z 2026-10-29 dotyczy wyłącznie profilu EXE SHA-256
`f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`.
Nie uruchamiano Stormworks i nie zmieniano pamięci procesu gry. Wszystkie adresy
poniżej są RVA; `C` oznacza aktualny kontroler, `S` aktualny stan symulacji,
`C=S+0x38`. `C+0x558` i `C+0x560` to zapamiętane cele prawej i lewej interakcji.

## Wdrożony backend i weryfikacja offline — 2026-11-01

Aktualizacja 2026-11-02: hooki `7ACA40` i `5F2920` wykonują teraz pre-cleanup
w rozpoznanych żywych zakresach przed zmianą kontekstu. Deaktywacja zachowuje
cztery argumenty; callery `84C671/84DF34` przekazują owner+358 w R9. Caller
wyjścia uruchamia ją przed resetem D+280. Zmiana postaci zachowuje sześć
argumentów i RAX, wymaga callera `852C86`, lokalnego ID i bieżącej pętli logiki.
Moduł nie wysyła spóźnionego release z zapamiętanych obiektów po utracie zakresu.
Reset/destrukcja Q mają osobny retirement payloadu po native delete requestu.
Pełny Release build i 23/23 CTest przeszły:
[raport](../artifacts/offline-transitions-tests-20261102.xml),
[dowody ABI/kolejności](../artifacts/native-transitions-evidence-20261102.json).
Poniższy opis z 2026-11-01 zachowuje wcześniejszy zakres; blokada nadal dotyczy
przejść nierozpoznanych lub wykrytych dopiero po utracie kontekstu.

Eksperymentalny `native_hand_buttons` realizuje już dedykowany release type10
i sprzątanie własnego C+558/C+4BF w żywym zakresie logiki. Otwarcie dłoni, utrata
śledzenia/fokusu, reset XR oraz znane bramki pauzy/UI anulują własne trzymanie.
Cleanup nie korzysta z phase0 ani z nowego hover. Backend porównuje również
bieżący token świata/postaci i odnawia go po natywnych callbackach.

Zmiana świata/postaci z nierozliczonym hold, utrata celu lub niepewny handoff
blokują dalsze operacje i zachowują nierozliczoną własność. `last_fault` podaje
pierwszą przyczynę, zamiast deklarować zwolnienie starego celu. Te ścieżki nadal
nie dowodzą sieciowego release po zniknięciu sceny ani pełnego rozpoznania
nowego czasu życia obiektów odtworzonych pod tymi samymi adresami.

18 nowych izolowanych scenariuszy obejmuje zmiany świata/postaci, utratę celu
z odebraniem dostępu do jego pamięci, pauzę/UI, zmianę hover, fizyczne przejęcie
wejścia i shutdown XR z pending/hold. Łączony test sprawdza rzeczywiste moduły
press/capture/builder/push/retirement oraz zachowanie requestu aż do rozpoznanego
końca WRITE/localhost. Pełny Release build i 23/23 CTest przeszły bez gry:
[raport](../artifacts/offline-lifecycle-tests-20261101.xml).
Szczegóły stanu i ograniczeń: [status.md](status.md).

Dalsze sekcje zachowują analizę natywnych ścieżek z 2026-10-29 i jej ówczesne
zalecenia implementacyjne. Aktualny zakres backendu opisano powyżej.

**Zwykłe `key_action(20,0)` nie stanowi udowodnionej, bezwarunkowej operacji
anulowania trzymania.** Może odpaść przy braku żywej postaci, a przed samym
zwolnieniem przycisku wykonuje też logikę krótkiego kliknięcia na bieżącym celu.
Samo zrównanie OpenXR `cancelled` z takim zdarzeniem nie zamyka poprawnie cyklu.

## Co faktycznie robi oryginał

| Ścieżka | Dowód instrukcjami | Zakres gwarancji |
| --- | --- | --- |
| Rozpoczęcie | `78D28A` zapisuje do `C+558`, `78D311` do `C+560`. | Pamięć celu po przejściu przez oryginalny branch przycisku. |
| Zwykła faza 0 | `7902F2` i `790545` zerują odpowiednio oba pola po budowie żądania `use_vehicle_button`. | Wymaga dojścia do tej gałęzi. Nie jest czystym helperem release. |
| Zbyt duża odległość / brak transformu pojazdu | Okresowy `742570`, gałęzie `74794A..747BE9` i `747BEC..747E7F`, buduje żądanie `B0FFF0` z bajtem `+24=0`, dodatkowym bajtem `+34=1`, potem zeruje pole. | Wymaga istniejącej aktywnej postaci; porównanie odległości w kwadracie ze stałą `9.100000381469727`. Nie jest cleanup śmierci ani utraty fokusu. Znaczenia dodatkowego bajtu nie należy nazywać bez dalszego dowodu. |
| Obsługa komunikatu `787F80` | `788038..78806F`: dla niezerowego celu wirtualna metoda `+450`, a potem wyzerowanie obu pól. | Obejmuje lokalny stan wskazanego komponentu. Nie dowodzi wysłania sieciowego release. Cała funkcja ma dodatkowe operacje na świecie i nie nadaje się do wywołania jako helper anulowania. |
| Wyjście/deaktywacja stanu symulacji | Vtable `B1A790` slot `+20` wskazuje `7ACA40`; pola `S+590/S+598` odpowiadają `C+558/C+560`. | Ten sam lokalny reset `virtual+450` i wyzerowanie obu pól; odrębny od destruktora. |
| Konstruktor kontrolera | `741870`, zapisy `74199D/7419A4`. | Inicjalizuje pola zerem; nie kończy trzymania poprzedniej instancji. |
| Destruktor | `7B93E0 → 7B9420 → 7B95F0(C)`. | Przejrzany kod nie wysyła żądania zwolnienia ani nie wywołuje `virtual+450` dla tych pól. Nie zastępuje osobnej deaktywacji. |

Dla dokładnie rozpoznanej klasy przycisku (`B75678`) slot `+450` wskazuje
`35D4E0`. Ta dziesięciobajtowa funkcja wykonuje wyłącznie
`mov word ptr [rcx+278h],0; ret`. **Wyzerowanie lokalnych flag `button+278/+279`
nie jest dowodem, że serwer lub host przestał trzymać przycisk.** Nie nazywać tej
metody ogólnym sieciowym release i nie wywoływać jej dla niezweryfikowanej klasy.

Setter śmierci `76A340` zmienia `C+4BC`, wysyła zmianę UI i wywołuje ścieżkę
łóżka `76A1E0`. Żadna z tych dwóch funkcji nie zeruje `C+558/C+560` ani nie
emituje żądania `use_vehicle_button`. Okresowy `742570` pobiera aktywną postać
przez `world+BB9F8`; gdy jej brak, pomija również opisane sprawdzanie odległości.
Nie wykazano więc automatycznej, bezwarunkowej kompensacji dla nagłej zmiany
postaci lub śmierci podczas własnego wciśnięcia dłonią.

## Konsekwencja dla adaptera dłoni

1. Rejestrować własność naciśnięcia dopiero po faktycznym natywnym przyjęciu:
   aktualne `C+558` musi odpowiadać dopuszczonemu celowi z tej samej generacji
   świata/postaci i próbki promienia. Fizyczne wejście użytkownika nie staje się
   przez to własnością moda. Bezpośrednio przed zakończeniem ponownie porównać
   aktualny, żywy kontekst; nie dereferencjonować zapamiętanego wskaźnika celu.
2. Utrata fokusu, śledzenia, kalibracji i otwarcie UI blokują nowe press, ale
   pozostawiają osobny obowiązek cleanup wcześniej przyjętego trzymania.
   Zamknięta bramka nowego naciśnięcia nie uprawnia do porzucenia tego obowiązku.
3. Nie kierować opóźnionego release do nowej postaci lub nowego świata. Stare
   własne eventy wymagają oznaczenia aż do odbioru/zniszczenia, aby nie zamieniły
   się po resecie w zwykłe wejście. Zmiana generacji unieważnia ich press.
4. Nie stosować `phase0` jako czystego cancel na podstawie samego `held!=0`.
   W `78FCA5..7900D3` możliwe są inne działania względem bieżącego `C+520/C+538`
   oraz zwolnienie `C+550`; dopiero później jest branch held-button.
5. Dalsza implementacja musi osobno udowodnić bezpieczny, ograniczony do
   posiadanego przycisku sposób zakończenia trzymania. Dostępne są dwa konkretne
   wzorce natywnego żądania (normalny release i oddalenie), lecz skopiowanie ich
   bez potwierdzenia dodatkowych flag, żywotności celu i kolejki nie jest jeszcze
   poprawnym backendem. Nie omijać arbitralnie ochron zdrowia w `78C770`.

Te ustalenia zmieniają projekt adaptera: najpierw cykl przyjęcie → trzymanie →
potwierdzone zwolnienie, potem aktywacja prawdziwych press. Testy z atrapą muszą
obejmować utratę fokusu podczas trzymania, menu, śmierć, zmianę postaci/świata,
fizyczną akcję20 między własnym press i release oraz zniszczenie zakolejkowanego
eventu bez odbioru. Test zatrzymujący się na samej liczbie emitted-release byłby
zbyt słaby — wymagany jest wynik właściwego natywnego held/request.

Materiały: `artifacts/held-button-cleanup-01.json` i `-02.json` zawierają
instrukcje i dekompilację powyższych funkcji; `-03.json` zawiera destruktory.
Dekompilacja ogromnego `855A30` w `-03` przekroczyła limit 45 s; jej niepełny
wynik nie stanowi dowodu roli komunikatu `787F80`. Surowe kandydaty przesunięć
są w `held-button-cleanup-displacements.json` i wymagają potwierdzenia granic
instrukcji (część dotyczy stosu lub innych klas). Dodatkowo użyto wcześniejszych
`cockpit-input-functions-02.json`, `hand-input-gates-14076a340.c` i
`hand-input-gates-14076a1e0.c`. To dowód statyczny, bez pomiaru nowej emisji dłoni.

Zwięzły, powtarzalny wyciąg instrukcji i odczytów vtable zapisano w
`artifacts/held-button-cleanup-evidence.json`. Dodatkowy `-04.json` obejmuje
`76E1E0` (osobna tabela interakcji `world+BB208`, nie helper pól `C+558/C+560`)
i slice `857470..857510`. Ten drugi nie jest początkiem funkcji: jego wyjściowa
dekompilacja ma odziedziczone rejestry i nie stanowi deklaracji ABI. Instrukcja
`8574F9` potwierdza wywołanie `787F80`; odtworzony switch wskazuje case `0x1C`.
Nie rozpoznano jeszcze nazwy tego komunikatu ani nie udowodniono, że dotyczy
każdej zmiany postaci lub śmierci. Nie używać tej niepełnej klasyfikacji do
ominięcia wymogu zweryfikowanego zakończenia własnego trzymania.
