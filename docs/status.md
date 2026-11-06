# Stan wdrożenia — 2026-11-02

## Próba, pomoc i reakcja regulacji menu — 2026-11-05

Próba `headset-xr-20261105-131205-804` uruchomiła autosave i VR automatycznie.
Użytkownik potwierdził dobre dopasowanie kalibracji. Log potwierdza zapis zmian
menu, ale użytkownik nie widział ich skutku: otwarty panel zachowywał poprzednie
parametry. Poprawiono aktualizację odległości, skali i czasu na żywo, oddzielono
komunikat zapisu od wartości i dodano **? → POMOC**. W pomocy wskazanie i dotyk
wyświetlają opis, nie wykonują działań. Krótkie zejścia promienia pauzują postęp;
po kliknięciu wskaźnik i stopka proszą o odsunięcie dłoni/promienia.

Test czterech zgłoszonych zachowań był czerwony przed poprawką i przechodzi po
niej. **51/51 CTest, 51/51 Python, 68 guardów EXE, tracer PASS.** Replay dostępnego
fragmentu starszej sesji: 8175 kontroli obrotu i 7965 paneli, 0 rozbieżności.
Oryginalne nagranie jest urwane; sesja nie ma końcowego raportu wydajności.
Poprawki wykonano offline; nie uruchamiano kolejnej gry.
[Szczegóły, instrukcja i raporty](menu-help-20261105.md).

## Komfort VR, fokus, interfejs i pomiary GPU — 2026-11-05

Wdrożono pięć uzgodnionych rozszerzeń. Nakładki dłoni/paneli znikają po utracie
fokusu także przy aktywnym śledzeniu. Opcjonalna kamera 1:1 wygasza świat przy
granicy wychylenia, zachowując fizyczny ruch głowy. INTERFEJS reguluje dwell
300–2000 ms, skalę 100–140% i dźwięk kliknięć; akcja ma potwierdzenie 150 ms,
a stopka rozróżnia dotyk i wskazanie. KOMFORT udostępnia skokowy obrót 30/45°,
winietę, profil siedzący i recenter. Dotychczasowe wartości domyślne zachowano.

Raport wydajności w schemacie 2 dodaje nieblokujące znaczniki czasu GPU,
pracę klatki CPU i przekroczenia przewidywanego okresu OpenXR. Automatyczne
podsumowanie pokazuje P95/P99 i wyjaśnia ograniczenia pomiaru. Nagrania paneli
i obrotu używają `SWVR_CONTROLS 2`, odtwarzacz przyjmuje także wersję 1.

Release bez ostrzeżeń, **50/50 CTest**, **51/51 Python**, **68 guardów EXE**,
tracer PASS. Test GPU obejmuje oba oczy, przywracanie stanu GL i stabilność
kolejnych obrazów nowych stron menu. Gry nie uruchamiano. Dźwięk, ergonomia
i koszt całego PCVR pozostają do próby na Queście.
[Pełny zakres, obsługa, obrazy i raporty](vr-comfort-20261105.md).

## Stabilna kalibracja, zapis ustawień i rozszerzona diagnostyka — 2026-11-05

Zrealizowano pięć kolejnych zadań przy zamkniętej grze:

- Kalibracja zbiera okno do 1,8 s i maksymalnie 256 próbek. Pojedyncze skoki
  rozstawu, przesunięcia barków i głowy są odrzucane. Wynik wymaga co najmniej
  10 próbek rozciągniętych na 1,2 s i stabilnego środka rozkładu; średnia obcięta
  usuwa po 20% wartości z obu końców. Automat nadal wymaga 1,5 s T-pose.
  Zmiana aktora/LOCAL/modelu i przerwa ponad 300 ms zerują okno. Pozycja głowy
  przy recenter pochodzi z tego samego stabilnego odcinka.
- OBROT / MENU: 15–120°/s, niezależna strefa 5–100 mm i zakres 20–400 mm,
  zawsze większy od strefy; odległość panelu 0,6–1,2 m. Zmiana odległości
  obowiązuje przy ponownym otwarciu. Dodano przywracanie domyślnych ustawień
  i znak plus w czcionce. [Obraz z testu GPU](../artifacts/vr-controls-menu-20261105.png).
- Zmiany menu po potwierdzonym zwolnieniu wejścia zapisują pełne preferencje
  przez plik tymczasowy i atomową podmianę. Błąd zapisu zachowuje działające
  ustawienia sesji i wyświetla komunikat. Preset domyślnie odczytuje zapisany
  tryb; jawne `-Movement Fists/Joystick` ma pierwszeństwo. Starszy INI nadal działa.
- `SWVR_CONTROLS 1` w pliku `.controls.tsv` zapisuje klatki źródłowe, konfigurację,
  otwarcie/zamknięcie paneli, pierwszeństwo dłoni, dotyk/dwell, akcje i obrót.
  Replay uruchamia te same moduły i porównuje rezultaty; nie wykonuje akcji gry.
  Limit 64 MiB kończy plik znacznikiem `END_LIMIT` i raportem `#prefix`;
  urwany zapis jest błędem. Format `.hands.tsv` v1–v3 pozostaje obsługiwany.
- Raport `.performance.json` po zakończeniu VR zawiera średnie, P95/P99,
  maksima i 10 najwolniejszych próbek z identyfikatorami XR: odstępy klatek,
  obliczenia pozy, overlay, zapis nagrań i renderowanie oka. Zawiera też trafienia
  i chybienia cache oraz klatki powtórzone/puste. Histogram ma stałą pamięć,
  rozdzielczość 0,25 ms; percentyl w kubełku ≥127,75 ms podaje maksimum.
  To czas ścienny CPU, bez zapytań GPU; kanały mogą się nakładać.

Release bez ostrzeżeń kompilatora; **49/49 CTest**, **45/45 Python**,
**68 guardów EXE**, osobny tracer PASS. Testy obejmują szum/skoki i niestabilność
kalibracji, zapis/odczyt preferencji, zachowanie starego pliku po odrzuconym zapisie,
zapisany/wyraźnie wybrany tryb presetu, regulację menu, obrót, dotyk/dwell,
utratę fokusu, rozbieżność i ucięcie replay oraz percentyle.
Obie strony menu sprawdzono w izolowanym teście GL; Stormworks nie uruchamiano.
Raporty: [CTest](../build/capture/artifacts/offline-five-20261105.xml),
[Python](../artifacts/offline-five-python-20261105.xml),
[profil EXE](../artifacts/offline-five-profile-20261105.json).
Weryfikacja komfortu, trafności kalibracji i kosztów w rzeczywistej sesji
pozostaje do następnej próby w goglach.

## Przegląd błędów i kosztu renderowania — 2026-11-05

Przegląd bieżących źródeł (katalog nie ma historii Git) objął wejście dłoni,
menu, obrót, kalibrację, zapis/replay, integrację avatara z GL oraz konfigurację
i preset startowy. Gry nie uruchamiano. Potwierdzone i poprawione przypadki:

- **Menu, P1:** niewyświetlany dwell drugiej dłoni mógł uruchomić inny przycisk
  niż wskazywał widoczny pasek. Regresja: lewa wskazuje A od początku, preferowana
  prawa zaczyna wskazywać B po 600 ms; po 800 ms stare wejście uruchamiało A.
  Teraz tylko dłoń z widocznym postępem nalicza dwell, pozostały promień jest
  szary. Bezpośredni dotyk zachowuje pierwszeństwo.
- **Obrót, P2:** deduplikacja próbki następowała przed sprawdzeniem utraty
  śledzenia dłoni. Powtórzona próbka z nieaktywną dłonią nadal rezerwowała ją
  przed interakcjami. Sprawdzenie ważności i otwarcia chwytu odbywa się teraz
  przed deduplikacją; sama deduplikacja nadal zapobiega podwójnemu obrotowi.
- **Obrót, P2:** bez poprzedniej wartości natywnego yaw krok joysticka poniżej
  1° obracał przestrzeń XR, ale martwa strefa pomijała zmianę pola postaci.
  Krok joysticka jest teraz dodawany niezależnie od martwej strefy ruchu głowy.
  Regresja z krokiem 0,01 rad sprawdza zgodność obu wyników.
- **Wydajność:** cache avatara unieważniała sama zmiana adresu bufora kości,
  nawet przy identycznej zawartości. Klucz matematyczny opiera się teraz na
  macierzach; pochodzenie aktualnych buforów nadal jest sprawdzane przed
  każdym użyciem cache. Test GPU potwierdza odrzucenie obcej kopii i ponowne
  obliczenie po zmianie macierzy. Pomiar 400 operacji, 10 zakresów oka:
  **0 → 390 trafień cache**, **5,5863 → 2,2594 ms** (około 2,47×).
  To syntetyczny koszt CPU ścieżki upload/pose/restore, nie wzrost FPS gry.
  Wpływ na rzeczywistą sesję wymaga kolejnego pomiaru w goglach.

Nowe regresje najpierw odtworzyły błędy, następnie przeszły po poprawkach.
Release: bez ostrzeżeń kompilatora; **48/48 CTest**, **45/45 Python**,
**68 guardów profilu EXE**, osobny test tracera PASS. Python nadal zgłasza
ostrzeżenie zainstalowanego pluginu pytest-asyncio o domyślnym zakresie fixture.
Raporty: [CTest](../build/capture/artifacts/review-20261105.xml),
[Python](../artifacts/review-python-20261105.xml),
[profil EXE](../artifacts/review-profile-20261105.json),
[cache przed](../artifacts/review-cache-before-20261105.txt),
[cache po](../artifacts/review-cache-after-20261105.txt).

## Płynny obrót i poprawki kalibracji/menu po trzeciej próbie — 2026-11-04

Użytkownik doprecyzował, że pomijane kliknięcia dotyczą menu VR, a obrót
drugą dłonią ma być płynny. Wszystkie zmiany wykonano przy zamkniętej grze.

- JOYSTICK: wybrana dłoń nadal steruje chodzeniem, przeciwna po zaciśnięciu
  ustawia niezależny środek obrotu. Wychylenie boczne 3–12 cm daje 0–60°/s,
  otwarcie kończy obrót. Chodzenie i obrót mogą działać równocześnie.
  `HandTurn` integruje czas XR raz na klatkę; przerwa, brak danych, zmiana
  LOCAL lub dłoni wymagają nowego środka. Obrót przechodzi przez istniejący
  zapis yaw i obrót przestrzeni XR, więc headset follow go nie cofa.
  Natywne pauza/UI/fotel, zmiana aktora, zawieszenie i wejście fizyczne są
  ponownie sprawdzane przed obrotem. Ręka obrotu jest rezerwowana przed Q/E.
- Kalibracja mierzy poziomą linię ramion zamiast kierunku początkowego
  originu XR. Obrócenie poprawnej T-pose o 0,55 rad wcześniej dawało
  `hands_uneven_forward`; regresja teraz przechodzi bez zmiany proporcji.
  Rozstaw, wysokość i środek nadal podlegają walidacji. Po przyjęciu
  pozycja originu i punkt karku są odświeżane przed następną klatką,
  z zachowaniem kierunku świata. W ostatnim logu ogranicznik głowy
  korygował pozycję nawet o 1,052 m, a wcześniejsza kalibracja ciała
  w ogóle nie zmieniała tego punktu. Dokładność głowy wymaga oceny w goglach.
  KALIBRUJ resetuje zatrzask poprzedniej udanej próby; menu nie wywołuje
  równocześnie automatycznej kalibracji podczas obsługi przycisków.
- Menu: regresja wskazania z odchyleniem 2 mm poza krawędź wcześniej
  nigdy nie kończyła dwell. Tolerancja utrzymuje poprzedni cel przez
  8 mm poziomo / 6 mm pionowo; rzeczywiste trafienie innego przycisku
  ma pierwszeństwo. Utrata śledzenia i większe odejście nadal zerują czas.
  Zielony promień oznacza aktywny cel; nietrafiony jest szary. Pełny panel
  wyłącza dodatkowy diagnostyczny promień do świata gry.
- Dotyk sprawdza także odcinek ruchu palca przez powierzchnię przycisku.
  Regresja +5 cm → −5 cm wcześniej pomijała kliknięcie, teraz daje jedno.
  Pozostają blokady nieruchomego palca, skoku śledzenia i powtarzania akcji.
- MENU przeniesiono z −30 cm na +18 cm względem oczu i odsunięto do 80 cm.
  Główny panel i launcher mają orientację zgodną z pionem LOCAL. Regresja
  przechylenia głowy o 30° wcześniej przechylała menu, teraz zachowuje pion.

Walidacja: Release bez ostrzeżeń kompilatora, **48/48 CTest**, **45/45 Python**,
**68 guardów EXE**, osobny test tracera PASS. Testy obejmują obie konfiguracje
dłoni, niezależny obrót, anulowanie, regrip, ten sam frame XR oraz 100 kolejnych
obrotów dla obu znaków/offsetów natywnego yaw. Obrazy GPU menu i podniesionego
MENU sprawdzono wizualnie. Raporty:
[CTest](../build/capture/artifacts/vr-five-fixes-20261104.xml),
[Python](../artifacts/vr-five-fixes-python-20261104.xml),
[profil](../artifacts/vr-five-fixes-profile-20261104.json).
Python zgłasza wcześniejsze ostrzeżenie konfiguracji pluginu pytest-asyncio;
testy przechodzą. Replay protokołu v3 nadal weryfikuje bity chodzenia;
nowy obrót jest testowany osobno w HandTurn i body_yaw. W ostatniej sesji
identyfikacja natywnego yaw ustaliła `sign=1, offset=0`; nowy obrót także
czeka na istniejący warunek poprawnie zidentyfikowanego mapowania.

Nowe zachowanie nie było jeszcze uruchamiane w Stormworks ani na goglach.

## Proste pięści, joystick bez pinchu i dalsze menu — 2026-11-04

Po próbie `headset-xr-20261104-212750-270` użytkownik zgłosił za bliskie menu,
nieskuteczny bieg i przechwytywanie środkowego pinchu przez Questa. Gra została
zamknięta na polecenie użytkownika; dalsze zmiany wykonano offline.

Domyślny tryb DWIE PIESCI teraz biegnie od razu po rozpoznaniu obu zamkniętych
dłoni i zatrzymuje się po otwarciu jednej. Nie ma wymachów, strefy przy ciele
ani sekwencji wstępnego otwierania. Joystick chwyta się całą wybraną dłonią;
otwarcie kończy ruch, ponowny chwyt ustawia neutralny punkt. Kciuk jest pomijany.
Pięść może korzystać z poprawnych estymacji zasłoniętych końców palców.

Ochrona geometryczna Questa wymaga otwartych palców; nie blokuje samej
zamkniętej pięści. Na nagraniu poprzednia ochrona wyłączyła wejście w 3543
próbkach mimo śledzonych nadgarstków; nowa pozostawia 2709 prób otwartej dłoni
w pozycji systemowej. Symulacja nowych gestów rozpoznaje bieg w 2696 próbkach.
To wynik offline, a nie dowód rzeczywistego przemieszczenia.

Panel jest 1 m przed głową (INI: `menu_distance_m`, 0,6–1,2 m). Działa dotyk
lub wskazanie przez 0,8 s z zielonym paskiem, bez szczypnięcia. Widoczny MENU
jest 65 cm przed głową. Komunikaty opisują stan zamiast żądać kolejno otwarcia,
zaciskania i wymachów. Preset domyślnie wybiera `-Movement Fists`.

Nagrania v3 jawnie zapisują aktywację. V1/v2 bez nowych pól nadal używają
historycznych detektorów przy replay: cała ostatnia sesja odtworzyła się z
0 rozbieżności. Release bez ostrzeżeń, 48/48 CTest, 45/45 Python, 68 guardów
oraz tracer i pasek postępu GPU. Nowe testy obejmują rzeczywisty adapter
requestów sprintu/release, pauzę, chwyty obu rąk, przerwy, czas XR i dwell.
**Nie uruchamiano gry po tych zmianach.** [Aktualne sterowanie](hand-locomotion.md).

## Próba i poprawka menu dotykowego — 2026-11-04

Próba `headset-xr-20261104-204959-345` została zatrzymana na życzenie użytkownika
i proces 47284 zamknięto kontrolowanie. Log: 20 818 par stereo, 4 przyjęte
T-pose, 1 przycisk przyjęty/zwolniony, zero faults i holding=0. Ruch nie był
potwierdzony (0 requestów). Nagranie dłoni osiągnęło limit 64 MiB.

Użytkownik zgłosił nieskuteczne klikanie menu i konflikt gestów z Questem.
Replay starego panelu: 12 rozpoczętych naciśnięć, 3 kliknięcia ZAMKNIJ i 9
anulowań przez zmianę celu przy szczypnięciu. Dodano bezpośredni dotyk
wskazującym z ponownym uzbrojeniem po odsunięciu, panel 50 cm przed głową,
widoczny przycisk MENU i zdalny klik kciuk–środkowy. Gest dłoni przy twarzy
usunięto. Opcjonalny sygnał systemowy OpenXR i zachowawcza geometria dłoni
blokują wejście moda zamiast przejmować menu systemowe.

Przygotowano preset gry + natywny autosave (`-continue`) + automatyczny VR.
Nowe wejście i preset: Release bez ostrzeżeń, 47/47 CTest, 45/45 Python,
68 guardów i izolowany tracer GPU. Preflight presetu nie uruchamia gry.
**Nie uruchamiano gry ponownie po zatrzymaniu.**
[Obsługa, ograniczenia i dowody](offline-controls-20261104.md).

## Poprawki po przeglądzie kodu — offline 2026-11-04

Usunięto wyścig domyślnego zegara w odczycie lokomocji, blokowanie drugiej
dłoni przez niepełne śledzenie preferowanej i gubienie znaczników utraty
śledzenia podczas blokady wejścia. Trzy nowe regresje najpierw odtworzyły
błędy, następnie przeszły po poprawkach. Jawne cofnięcie zegara nadal resetuje
ruch; istniejące przytrzymanie nie jest przejmowane przez drugą dłoń.

Przechowywanie geometrii znaków, jednej transformacji na napis i bufora CPU
zmniejszyło syntetyczny koszt tekstu menu z 0,991 do 0,105 ms (około 9,5×).
Obrazy GPU menu i klawiatury są identyczne. Testy obejmują zmianę i ponowne
użycie treści/pozy/podświetlenia. Release bez ostrzeżeń, 46/46 CTest,
40/40 Python, 68 guardów EXE i samodzielny tracer GPU; gry nie uruchamiano.
[Pomiary i zakres testów](offline-controls-20261104.md#weryfikacja).

## Menu VR i diagnostyka wejścia — offline 2026-11-04

Dodano menu wywoływane obiema otwartymi dłońmi przy twarzy, wybór dłoni
joysticka i pierwszeństwa wskazywania, odtwarzacz HTML ze szkieletem i osią
czasu oraz dziennik v2 rejestrujący natywne blokady, fizyczne maski i anulowania.
Przygotowano również testowany adapter protokołu klawiatur 5/6. Pozostaje
niepodłączony: pełny zakres właściciela i życia sesji UI wymaga potwierdzenia.
Lokalna klawiatura nie wysyła wartości do gry.

Release bez ostrzeżeń, 46/46 CTest, 40/40 Python, 68 guardów EXE i samodzielny
tracer GPU. Menu obejrzano na obrazie testu GPU; logikę HTML sprawdzono w Node.
Host przeglądarki był niedostępny, więc wizualna kontrola HTML pozostaje ręczna.
Nie uruchamiano gry ani OpenXR. [Obsługa i wyniki](offline-controls-20261104.md).

## Goal wygody obsługi 1–5 — offline 2026-11-04

Dodano pasek i wskazówki T-pose, trwały profil proporcji ciała z usuwaniem,
zmianę parametrów/trybu po zwolnieniu wejścia, nagranie pełnego śledzenia
i samodzielny odtwarzacz detektora oraz lokalną klawiaturę numeryczną VR.
Prototyp OK zapisuje wynik wyłącznie lokalnie; integracja z typami 5/6 gry
pozostaje osobnym zadaniem. Nie uruchamiano gry.

Release bez ostrzeżeń, 43/43 CTest, 36/36 Python, 66 guardów EXE oraz
samodzielny tracer GPU przeszły. [Opis i podgląd](offline-usability-goal-20261104.md).
Wczytanie proporcji, przeładowanie i ergonomia panelu wymagają próby w goglach.


## Goal offline 1–5 ukończony — 2026-11-04

Bez uruchamiania Stormworks wdrożono zwolnienie ruchu przed znanymi
przejściami postaci/sceny, konfigurację gestów w INI, lewą dłoń/Q,
tekst w obu oczach i raporty JSON/Markdown po próbie. Wykrywanie wymachów
pozwala zaciskać pięści kolejno i chroni przed kliknięciem, gdy jedna
rozpoznana pięść pozostaje widoczna przy utracie drugiej dłoni.

Release bez ostrzeżeń, 38/38 CTest, 31/31 Python, 66 guardów EXE oraz
samodzielny tracer GPU przeszły. Testy przejść obejmują rzeczywiste ABI
wrapperów, wspólnego obserwatora, zmiany podczas alokacji/handoff oraz
zachowanie fizycznych klawiszy. Q ma test całego cyklu i retirement.
[Pełny zakres oraz raporty](offline-goal-20261104.md). Nowe funkcje nadal
wymagają oceny w goglach. Dalsza treść jest dziennikiem historycznych prób.


Cel projektu: mod VR do Stormworks na Quest 3, ze stereoskopią, głową 6DoF i śledzeniem gołych dłoni. Nieukończony.

Aktualizacja z 2026-11-02 obejmuje port na build 26294447, natywne zwolnienie
przed zmianą sceny/postaci oraz retirement źródłowej kolejki. Aktualny wynik po kalibracji i poprawkach startu to
27/27 CTest i 20/20 testów Python. Starsze wyniki
i wzmianki o zakazie uruchamiania gry zachowują stan z momentu ich zapisu;
aktualną zgodę i gotowość opisano poniżej.

## Drugi wariant sprintu — pięści i wymachy, offline 2026-11-03

Na prośbę użytkownika dodano osobne `-ArmSwingRun` do porównania z joystickiem
`-HandLocomotion`. Obie pięści i trzy przeciwne półwymachy przy ciele włączają
natywny sprint do przodu. Otwarcie dowolnej dłoni zatrzymuje od razu,
brak wymachów po około 250 ms. Tryby wybiera się osobno przy uruchomieniu.

Testy odtwarzają fałszywy start przy obracaniu całego ciała i potwierdzają
poprawkę: wspólny sztywny obrót rąk/głowy jest pomijany. Są też przypadki
drżenia, nieruchomych pięści, pojedynczego sięgnięcia, ruchu obu rąk razem,
samego ruchu w pionie, skręcania podczas biegu, utraty śledzenia, zmiany LOCAL
i poprawnego rearm. W obszarze aktywacji pięści tłumią interakcje pinch.

`native_locomotion` obejmuje nowy gest aż do native requestu i zwolnienia.
Supervisor sprawdza tryb, co najmniej jeden rozpoznany start i drain.
Release i **37/37 CTest** przeszły offline; nie uruchamiano gry.
Diagnoza przyszłej próby: `#arm_swing_totals` (powody odrzuceń i rytm),
raport `build/capture/artifacts/arm-swing-20261103.xml`.
[Sterowanie i ograniczenia](hand-locomotion.md).

## Chodzenie i sprint dłonią — prace offline 2026-11-03

Na polecenie użytkownika zbadano rozwiązania VR (HL2VR, Natural Locomotion,
VRChat i dokumentację Ultraleap). Wybrano wirtualny joystick: lewy
middle–thumb pinch z punktem neutralnym, martwą strefą 3 cm i sprintem
od 12 cm do przodu (wyłączenie poniżej 9 cm). Prawy index pinch zachowuje
interakcje. Opcjonalne `-HandLocomotion` włącza również BodyYaw.

Nowy hook samplera requestów `868410`, tylko callerzy `86A36B/86CDEB`,
dopisuje kopię natywnego `set_input_buttons` (typ 100, 0x20 B) do aktualnej
kolejki. Nie zapisuje stanu klawiszy w Windows ani źródłowej maski gry.
Otwarcie/utrata śledzenia/fokusu, stare dane, pauza/fotel/UI i fizyczne
wejście anulują ruch. Zmiana aktora z nierozliczonym ruchem daje blokadę.
Supervisor czeka na `locomotion_drain complete`, bez potwierdzania odbioru
requestu przez serwer.

Release, **36/36 CTest**, **23/23 Python** i **61 guardów EXE** przeszły.
Testy obejmują rzeczywisty adapter z atrapami natywnego alokatora/kolejki,
przepuszczanie oryginału, oba callery, kierunki, sprint, zwolnienie, konflikty
i niepewny handoff. **Stormworks nie uruchamiano**; działanie w grze,
komfort i koszt dodatkowych requestów pozostają do oceny.

[Instrukcja i dowody](hand-locomotion.md), [badanie rozwiązań](vr-running-research.md),
raport `build/capture/artifacts/hand-locomotion-20261103.xml`.

## Późniejsze prace offline 2026-11-03 — gotowość XR i utrzymanie T-pose

Bez uruchamiania Stormworks i bez gogli. Bazowa weryfikacja: Release,
32/32 CTest, 23/23 Python, 54 guardy EXE; po zmianach **34/34 CTest**.

- `XrReadinessMonitor`: po 5 s stałego oczekiwania zapisuje
  `#openxr_readiness reason=session|focus|tracking|render waited_ms=...`;
  powtarza najwyżej co 30 s i zgłasza `reason=ready` po odzyskaniu gotowości.
  Supervisor przekazuje przyczyny i wskazówki do konsoli, bez kradzieży fokusu.
  Błąd `xrCreateInstance` -2 otrzymuje podpowiedź sprawdzenia SteamVR,
  Steam Link i runtime; kod nie jest traktowany jako dowód jednej przyczyny.
- `AvatarCalibrationHold`: wydzielono automat z `gl_trace.cpp`. Poprzedni
  kod sprawdzał dłuższą przerwę tylko na niepoprawnej próbce, więc poprawna
  próbka po zawieszeniu renderowania mogła zaliczyć przerwę do 1,5 s T-pose.
  Teraz każda przerwa >300 ms rozpoczyna nowe utrzymanie. Zmiana postaci,
  modelu, LOCAL i utrata fokusu również kończą bieżące utrzymanie.
  Zachowana kalibracja proporcji ciała nadal przetrwa recenter.
- Testy `xr_readiness`, `avatar_calibration` oraz rozszerzony
  `preview_supervisor` obejmują granice czasowe, odwrócenie zegara,
  przerwy renderowania, odzyskanie gotowości i brak powielania ostrzeżeń
  przez odczyty tego samego logu. Nowe zachowanie wymaga oceny w goglach.

Raport CTest: `build/capture/artifacts/offline-development-20261103.xml`.

## Próby 2026-11-03 wieczór — interakcja, chodzenie, kalibracja

Relacja użytkownika z ostatniej próby (`headset-xr-20261103-201512-390`,
`-HandButtons -BodyYaw -AvatarPose -ManualStop`): **throttle lever przesuwa
się szczypnięciem, przyciski działają, chodzenie z obrotem postaci za głową
działa**; ciało „w miarę ok”, ręce postaci trochę za krótkie.
- Throttle lever to przycisk typu 4 (`targeted_types`); HandButtons i
  zwolnienie przyjmują teraz typy 0, 1 i 4. 8 naciśnięć dłonią, 0 błędów.
- `-BodyYaw`: ~98 s aktywne, płynne podążanie za głową (bez oscylacji).
- Koniec pomiaru na znak użytkownika (`-ManualStop`, `stop-vr-preview.ps1`).
- Kalibracja T-pose: automatyczna po 1,5 s (rozpiętość ≥1,2 m), ramka w
  środku widoku (żółta/zielona/czerwona), powód odrzucenia w logu. Barki
  biorą z T-pose tylko wysokość; długość ramion uwzględnia wysunięcie dłoni
  do przodu (wcześniej 77% drawów miało ograniczony zasięg).
- Obrót myszą (`headset-xr-20261103-203158-086`): gra dodaje obrót myszy do
  `C+0x2A0` między klatkami, więc pomiar po oryginalnym update zawsze dawał 0,
  a podążanie za głową cofało obrót. `follow_headset_yaw_turning` mierzy zmianę
  od wartości po poprzednim update i obraca nią przestrzeń gry
  (`mouse_turn_deg≈1098`). Użytkownik potwierdził: obrót myszą działa.
- W tej samej próbie T-pose nie zaliczył się: żółta ramka pojawiła się tylko na
  chwilę (utrzymanie <1,5 s). Teraz utrata poprawnej pozy do 300 ms nie przerywa
  utrzymania, a `#avatar_calibration_totals` loguje powody odrzucenia i
  najdłuższe utrzymanie.
- Na starcie tej próby sesja VR przez ~2 min nie miała fokusu SteamVR
  (6915 pustych klatek przed kalibracją originu) — przyczyna po stronie
  runtime, obraz ruszył po odzyskaniu fokusu.

## Czwarta próba 2026-11-03 (`-InputProbe -BodyYaw`) i poprawki

`headset-xr-20261103-190124-349`, 180 s. Relacja: dźwignia (E/Q z klawiatury)
i przyciski nie działały, obrót postaci działał słabo.
- Bez HandRay gra celowała własną kamerą, która w VR nie idzie za głową:
  jedyne E z logu miało cel=brak. Sonda wejścia wyczerpała limit 10000 wierszy
  po ~30 s (puste odczyty kolejki), więc dźwigni nie zarejestrowała.
- `-BodyYaw` skalibrował się (`sign=1 offset=0`) i obrócił postać o 232°, ale
  nadpisywał yaw co klatkę i walczył z myszą; przy chodzeniu obraz nie obracał
  się myszą wcale (anchor usuwa yaw gry). 16 recentrów, 16% pustych klatek.
- **Poprawki offline:** (1) promień wzroku w moście dłoni i zapasowy wybór
  celu wzrokiem, gdy dłoń nie wskazuje (klawiatura Q/E w VR; nie certyfikuje
  szczypnięć); (2) model obrotu VR: przyrost yaw z myszy po zapisie moda obraca
  całą przestrzeń (anchory kamery, promień, awatar), fizyczny obrót obraca
  postać; (3) HandButtons liczy typy wskazywanych przycisków (`targeted_types`)
  — pokaże natywny typ throttle lever; (4) przyczyny pustych klatek w
  `#openxr_frame_reuse`. 32/32 CTest, 23/23 Python, 54 guardy EXE.

## Trzecia próba 2026-11-03 — potwierdzenie poprawek

`headset-xr-20261103-182026`, okno, AvatarPose + HandButtons, 180 s. Relacja
użytkownika: **dłonie postaci podążają za dłońmi, obraz nie mruga, przycisk
Push działa, ciało w porządku**; kilka razy recenter przez Oculusa.
- `proximity_posed=19652` (każde oko), `paired_provenance_misses=0`.
- Klatki: 9826 nowych, 334 powtórzone, 103 puste (wcześniej 24% pustych).
- **HandButtons pierwszy raz w grze:** 13 naciśnięć i 13 zwolnień, 13
  requestów rozliczonych przez localhost, 0 błędów.
- Poprawki po próbie: walidacja startu w trybie okienkowym przyjmuje obszar
  klienta (ta sama szerokość, do 120 px niżej); kalibracja T-pose przetrwa
  recenter (unieważnia ją tylko zmiana aktora/modelu).
- Release bez ostrzeżeń, 31/31 CTest, 23/23 Python, testy skryptów przechodzą.
  Podsumowanie: `artifacts/headset-gameplay-summary-20261103-3.json`.

## Druga próba 2026-11-03 (okno, AvatarPose + HandButtons) i poprawki

Próba `headset-xr-20261103-175656-052`, tryb okienkowy, 180 s, kalibracja
T-pose przyjęta (`wrist_span=1.478`, `body_scale=1.20`). Supervisor oblał tylko
HandButtons: 34 prawe szczypnięcia, `selections=13114`, ale `certified=0`.
- Lokalne rekordy zawsze miały selektor głowy (`local_mesh_other_clip=0`).
  Pozowane nadgarstki odsunięte od natywnych średnio o 37 cm. Na każde oko
  jest jednak **jeden** draw `opaque_anim` z 19 kośćmi, clip -1 i obcym meshem,
  niepozowany: najpewniej druga kopia siedzącego ciała. To wyjaśnia dłonie na
  kierownicy przy ruszających się ramionach i wcześniejszy widok wnętrza głowy.
  **Poprawka:** rekordy 19-kostne bez selektora są wiązane jako `proximity` i
  pozowane tylko, gdy ich natywna głowa leży ≤0,35 m od oczu; na czas drawu
  dostają selektor głowy 3/0,25 (przywracany). Liczniki `proximity_posed/rejects`.
- Użytkownik zgłosił mruganie na czarno co ~0,5 s. XR zaczęło 10524 klatek,
  obraz był w 8021. Puste klatki (m.in. celowo pomijane co 120. wywołanie
  renderera i klatki bez wywołania renderera) dawały czarny obraz.
  **Poprawka:** taka klatka wysyła ponownie ostatni obraz z pozami, z którymi
  go wyrenderowano (`XrRuntime::end_frame` z `XrViewOverride`), do 250 ms
  i tylko w tej samej przestrzeni LOCAL; kompozytor reprojektuje. Log
  `#openxr_frame_reuse repeated= empty=`.
- Tryb okienkowy usunął wstrzymanie obrazu na starcie.
- Release bez ostrzeżeń, **31/31 CTest**, **23/23 Python**, **54 guardy EXE**.
  Obie poprawki czekają na próbę w goglach.

## Próba w goglach po pracach offline — 2026-11-03

Za zgodą użytkownika: Gameplay, `-AvatarPose`, limit głowy włączony, ręczny start
w kokpicie. **Pełne 300 s**, 17028 par stereo, po 15020 próbek obu dłoni,
zamknięcie przez supervisor z kodem 0.

- **`depth_draws` 0 → 1510720**, `bound_depth_palettes=185652`. Wiązanie palet
  cieni działa w grze; `skinned_unrebased=0` potwierdza statyczną analizę kolejności.
- Tożsamość postaci ważna w **100%** oczu (wcześniej 82%), zero odrzuceń łańcucha.
- `reach_limited` spadło z 88% do 39% drawów, bez kalibracji T-pose (sygnał
  kalibracji nie został wysłany, `body_calibrated=0`).
- Limit głowy zadziałał w 7910 z 18394 klatek, `max_extent=4.42`, największa
  korekta 0,365 m. LOCAL zmienił się 5 razy i origin był za każdym razem
  kalibrowany w bieżącej pozycji.
- Na starcie okno gry było zminimalizowane (`suspended_tiny_source`); po
  przywróceniu okna obraz wznowił się bez błędu, co potwierdza poprawkę targetów 1×1.
- Ocena wizualna (cienie, komfort limitu, ręce) czeka na relację użytkownika.
  Podsumowanie: `artifacts/headset-gameplay-summary-20261103.json`.

## Prace offline — 2026-11-03

Bez uruchamiania gry i gogli. Pełny opis: [cele offline](goals-offline.md).

- Przyczyna `depth_draws=0` ustalona statycznie: jedyny draw `depth_anim*` to
  przebieg cieni `0x6de000`, który rysuje **kopię** palety z osobnej listy
  `scene+0x378` (producent `0x6734e0`). Palety cieni lokalnej postaci są teraz
  wiązane jako `depth_only` po dokładnej zgodności meshu, macierzy świata i palety.
  Dowody: `artifacts/avatar-shadow-pass-20261103.json`.
- Nowe liczniki odrzuceń drawów skinningu i diagnostyka etapów tożsamości
  (`#avatar_identity_totals`). Test założeń o shaderach gry.
- Limit głowy w Gameplay (punkt szyi, miękka elipsoida, wspólna korekta widoku,
  dłoni i awatara); domyślnie włączony, `-NoHeadLimit` wyłącza. Odpowiedź na
  zgłoszenie wychodzenia kamery poza ciało; wartości limitów wymagają oceny w goglach.
- Refaktor `gl_trace.cpp`: wydzielone `trace_report` i `head_mask_sources` z testami.
- Release build bez ostrzeżeń, **30/30 CTest**, **23/23 Python**, **54 guardy EXE**
  bez błędów. Nie potwierdza to działania w grze ani w goglach.

## Kalibracja i start — późniejsza aktualizacja 2026-11-02

- Przyczynę powrotu do 1280×720 potwierdzono w natywnym loaderze `2799F0`:
  niezerowy `write_key` wymusza tryb okienkowy i 720p. Preview zapisuje kopię
  konfiguracji, ustawia 3440×1440/fullscreen i zeruje znacznik. Po wymuszonym
  zamknięciu własnego procesu usuwa znacznik, zachowując pozostałe ustawienia.
  Walidacja wymaga rozmiaru pierwszego źródła renderowania. Historyczny ślad
  zaczynający od 720p i później przechodzący na 3440×1440 prawidłowo oblewa test.
  Dowód natywny: `artifacts/graphics-reset-20261102.json`.
- Test GPU odtworzył brak modyfikacji depth po pobraniu lokalizacji uniformów
  przed inicjalizacją adaptera. Adapter odkrywa teraz lokalizacje przy pierwszym
  uploadzie, bez wymagania wcześniejszego callbacku w tym samym kontekście.
  Test przechodzi, z zachowaniem odmowy obcych palet i przywracaniem uniformów.
  Dodano liczniki pojedynczych uploadów/draw i odmów powiązania dla dalszej diagnozy.
  Nie dowodzi to jeszcze przyczyny `depth_draws=0` w historycznej próbie.
- Jawna kalibracja T-pose zapisuje rozstaw nadgarstków i płaszczyznę barków.
  Model wylicza stały współczynnik proporcji 0.65–1.75 z bind pose i skali świata;
  IK nie rozciąga potem ramion według bieżącego celu. Odrzucane są niepełne
  oraz przekrzywione próbki. Zmiana aktora, modelu lub przestrzeni unieważnia
  kalibrację. Biodra/nogi i punkt zaczepienia oczu pozostają niezmienione.
  Sygnał: `scripts/calibrate-avatar.ps1`; instrukcja w `game-preview.md`.
- Gameplay ma domyślnie 300 s pomiaru od inicjalizacji XR, po czasie na wejście
  do kokpitu. Ostatnia wcześniejsza próba pięciominutowa nie rozpoczęła XR.
- Bieżąca próba Menu `fake-xr-20261102-234127-500.tsv` zakończyła proces
  kodem 0 przed pomiarem XR. **Nie potwierdza pierwszej klatki 3440×1440.**
  Konfiguracja na dysku zachowała 3440×1440 i pusty znacznik.
  Użytkownik polecił nie uruchamiać gry; nie wykonywać kolejnych prób bez
  nowej instrukcji. Stormworks po tej próbie nie działa.
- Pełny Release build, **27/27 CTest**, **20/20 Python** i **50 guardów EXE**
  przeszły. Raport: `artifacts/avatar-calibration-tests-20261102.xml`.
  Ocena nowych proporcji, clippingu i depth w goglach pozostaje do wykonania.

## Powiązanie modelu z VR — implementacja, 2026-11-02

- Na polecenie użytkownika wdrożono opcjonalne `-AvatarPose` w Gameplay.
  Włącza również HandRay i maskę głowy. Głowa awatara podąża za obrotem oraz
  pozycją gogli, a klatka piersiowa i częściowo brzuch za fizycznym wychyleniem.
  Punkt oczu jest kalibrowany względem kości głowy; nie dodano stałego offsetu.
  Biodra i nogi zachowują natywną animację.
- Nadgarstki podążają za śledzonymi dłońmi. Dwusegmentowe IK wyznacza łokcie,
  zachowując długości ramion. Cele poza zasięgiem są ograniczane i liczone
  jako `reach_limited`. Orientacja dłoni pochodzi z geometrii śledzonych stawów;
  uproszczone kości palców reagują na uśrednione zgięcie palców z OpenXR.
- `AvatarRetargeter` operuje na prywatnych kopiach palet. Obliczenia uwzględniają
  skalę modelu, odbicie osi gry i podwójną precyzję przed przesunięciem początku
  renderowania. Oba temporalne endpointy kości górnej części ciała dostają tę
  samą przewidywaną pozę VR, żeby interpolacja nie mieszała dłoni ze starą
  natywną pozycją. Nie oznacza to pełnej integracji motion vectors.
- Model male/female jest odczytywany z aktualnej lokalnej postaci w żywym
  callbacku Selection. Sprawdzane są vtable, lokalna flaga, head index i układ
  ustawień. Snapshot wygasa po 100 ms. Poza callbackiem nie dereferencjonuje się
  zapamiętanej postaci. Zmiana postaci, modelu lub przestrzeni zeruje kalibrację.
- Dostęp do palet jest wiązany z bieżącymi rekordami sceny opaque/glass/additive
  o lokalnej sygnaturze head index 3, weight .25 i 19 kościach. Adresy palet
  służą dalej tylko jako tokeny porównania. Dzięki temu kości można podstawić
  także w depth-only shaderach bez uniformu `bone_clip_index`. Nieznane palety,
  NPC, niewłaściwy kontekst i rysowanie poza zakresem oka XR są pomijane.
  Po każdym draw dokładne natywne wartości uniformów zostają przywrócone.
- Statycznie porównano nowy profil z zachowanym EXE: rig loader, obaj producenci
  lokalnej postaci, trzy konstruktory rekordów, ich wspólny append o stride
  `118` oraz draw helpers. Zgodność sygnatur i referencji: **50 kontroli**.
  Oba rzeczywiste pliki animacji przeszły parser i walidację hierarchii.
- Pełny Release build, **26/26 CTest** oraz **20/20 testów Python** przeszły.
  Nowe testy sprawdzają powiązanie oczu z głową, IK, skalę, odbicia, duże
  współrzędne, utratę śledzenia, bounded scene lists, wygasanie tożsamości,
  rzeczywiste uniformy GPU, depth-only, odmowę NPC i dokładne przywracanie stanu.
  Supervisor wymaga obserwowanych rysowań głowy, obu rąk i depth pass.
- Próba `AvatarPose` na goglach trwała **120031 ms**, przesłała **6760 par**
  i zebrała po 6233 próbki obu dłoni. Odnotowano 54120 rysowań pozy, po 54100
  z lewą i prawą ręką oraz 47634 rysowania z ograniczeniem zasięgu.
  **Próba nie zaliczyła wymaganej kontroli depth: `depth_draws=0`.** Nie ma
  jeszcze wizualnego potwierdzenia poprawnej anatomii ani usunięcia clippingu.
  Ślad: `headset-xr-20261102-155510-302.tsv.xr.txt`.

Dowody: [audyt profilu](../artifacts/avatar-profile-20261102.json),
[guardy](../artifacts/avatar-profile-guards-20261102.json),
[CTest](../artifacts/avatar-tests-20261102.xml).
Uruchomienie opisano w [eksperymencie Gameplay](game-preview.md).

## Quest: Gameplay, pozycja ciała i szkielet — 2026-11-02

- Po zgodzie użytkownika uruchomiono Gameplay z ręcznym startem VR,
  `-HandRay -InputProbe`, bez HandButtons. Użytkownik wszedł do kokpitu
  i potwierdził założenie gogli przed sygnałem startu. SteamVR/OpenXR 2.18.4
  zgłosił system `oculus`, pozycję/orientację oraz śledzenie dłoni.
- Próba przesłała **7488 par stereo w 95406 ms**; każda dłoń miała 7744 próbki
  aktywnego śledzenia. Zliczono 2 lewe i 23 prawe szczypnięcia. Culling oczu
  aktualizowano 7488 razy. Maska lokalnej głowy miała 614016 aktywnych wywołań;
  to licznik wywołań, nie dowód liczby usuniętych pikseli.
- Log promienia po starcie VR zawiera 5766 wyników `hand_ray`, w tym 2595 próbek
  z niezerowym celem. Log ma limit i niepełny ostatni wiersz po zamknięciu;
  nie jest to licznik wszystkich wywołań całej sesji. Wskazywanie działało
  w natywnej selekcji. Nie testowano naciskania przycisków.
- **Nie zaliczono pełnych 120 s.** Gra zmieniła target z 3440×1440 na 1×1.
  Podział desktopowego obrazu na pół dawał wtedy zerową szerokość i błąd
  `Eye rectangle`. XR zamknął się z `exception`, referencje oczu zostały
  zwolnione, a supervisor zamknął własny PID 32948. Przyczyna samej zmiany
  rozmiaru nie została potwierdzona przez użytkownika.
- Poprawiono przejściowe targety 1-pikselowe: pomijane są stereo i rekonstrukcja
  pakietów, bieżąca klatka XR kończy się pusto, historia oraz zamiar dłoni są
  zerowane. Zachowane zasoby można ponownie wykorzystać po powrocie rozmiaru.
  Log rozróżnia `suspended_tiny_source` i `resumed_drawable_source`.
  Test regresji najpierw odrzucił dotychczasową akceptację 1×1, następnie
  przeszedł dla 1×1, 1×720, 1280×1 i powrotu do 1280×720, z jednokrotnym
  zwolnieniem zasobów. Release build i **23/23 CTest** przeszły.
  **Poprawka nie miała jeszcze ponownej próby w grze.**
- Użytkownik potwierdził poprawną orientację, lecz kamera wychodzi poza model
  przy obrocie i fizycznym wychylaniu głowy. Poprosił też o powiązanie dłoni
  VR z rękami postaci. Obecny mod zmienia kamery i pokazuje nakładkę dłoni;
  nie steruje jeszcze szkieletem awatara. Nie uznajemy pozycji ani pełnego
  Gameplay 6DoF za zweryfikowane.
- Odczytano szkielety obu zainstalowanych `survivor_male/female.anim`:
  po 19 kości, `head=3`, prawe bark/łokieć/dłoń `4/5/6`, lewe `9/10/11`.
  Łańcuchy i linki rodzic–dziecko są spójne. To dowód danych zasobu,
  **nie zweryfikowane powiązanie z żywą paletą lokalnej postaci**. Potrzebne
  będą kalibracja szkieletu, głowa/tułów i IK rąk. Palce modelu są uproszczone.

Artefakty: [podsumowanie próby](../artifacts/headset-gameplay-summary-20261102.json),
[log XR](../artifacts/headset-xr-20261102-141452-322.tsv.xr.txt),
[CTest po poprawce](../artifacts/headset-followup-tests-20261102.xml),
[szkielety zasobów](../artifacts/avatar-rigs-20261102.json).
Odtworzenie odczytu szkieletów: `python tools/inspect_avatar_rig.py`.

## Port profilu gry — 2026-11-02

- Wdrożono profil zainstalowanego EXE: Steam build **26294447**, SHA-256
  `c38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d`.
  Nadal obowiązują pełny hash i dokładne sygnatury. Zmieniono adresy funkcji,
  vtable, filtry callerów oraz pola kolejki/kontekstów ownera przesunięte o 8 B.
- Audyt statyczny: 85 adresów, 46 zakresów funkcji, 24710 operandów/wpisów tablic,
  RTTI i używane sloty vtable. Jawnie przeanalizowano zmianę helpera `589390`,
  który usuwa duplikaty przed append. Audyt ma ograniczony zakres i nie
  potwierdza całego grafu wywołań gry.
- Release build, **23/23 CTest**, **20/20 testów Python** oraz wszystkie **46 kontroli sygnatur i referencji**
  względem zainstalowanego EXE przeszły. Testy portu odrzucają nieznane zmiany pól,
  rejestrów, stałych i indeksów tablic. [Opis portu](game-profile.md),
  [raport CTest](../artifacts/profile-port-tests-20261102.xml).

## Próba aktualnej gry bez gogli — 2026-11-02

- Pierwszy pomiar `fake-xr-20261102-140226-402.tsv` zaakceptował hash i hooki,
  lecz nie rozpoczął XR w limicie 90 s. Wykryto niewłaściwy adres globalnego
  licznika renderera: `CFD3B4→CFD3C4`. Odczyt starego zera stale pomijał stereo.
  Nowy adres potwierdzają native load i store; poprawiono profil i dodano
  regresję odrzucającą niezakotwiczone dane BSS. Nie zwiększano timeoutu.
- Po poprawce ta sama komenda `run-game-preview.ps1 -FakeRuntime -QuietTrace
  -Scene Menu -PreviewSeconds 30 -TimeoutSeconds 90` zakończyła pełny pomiar:
  **3471/3471 poprawnych par stereo w 30000 ms**, zero błędów walidacji,
  różne nieczarne obrazy oczu i kolory obu syntetycznych dłoni w każdym oku.
  Utworzono i zniszczono dwa swapchainy; 6944 acquire odpowiada 6944 release,
  bez nieprawidłowych release. Natywne pakiety oczu miały odrębne zasoby/FBO
  i zwolniły referencje. Skrypt zamknął własne dziecko PID 39352.
  [Log](../artifacts/fake-xr-20261102-140735-223.tsv.xr.txt),
  [walidacja GPU](../artifacts/fake-xr-20261102-140735-223.tsv.fake.json).
- To rzeczywista gra z atrapą OpenXR, **bez gogli**. HandButtons, HandRay
  i sondy wejścia były wyłączone; culling Gameplay i maska głowy nie zostały
  sprawdzone tą próbą menu. Nie jest to pomiar wydajności na Queście.
- Można podłączyć Questa przez Steam Link i uruchomić SteamVR. Następna próba
  wymaga użytkownika w kokpicie i ręcznego włączenia VR. Powinna sprawdzić
  orientację/pozycję głowy, maskę oraz wskazywanie przed osobną próbą przycisków.

## Natywne przejścia — stan przed portem, 2026-11-02

Adresy w tej sekcji dotyczą historycznego profilu `f7d605e3…`.
Odpowiedniki nowego buildu opisuje [port profilu](game-profile.md).

- Wdrożono pre-cleanup w `7ACA40` dla rozpoznanych callerów `84C671/84DF34`.
  ABI obejmuje cztery argumenty: simulation, graphics, D i żywy `owner+358`.
  Owner jest wyprowadzany z bieżącego R9, a następnie weryfikowany z simulation
  i D. Statyczny caller wyjścia wykonuje tę deaktywację przed resetem D+280
  pod `84C73B`; nie korzystamy z historycznego ownera jako źródła obiektu.
- Hook `5F2920` zachowuje sześć argumentów oraz zwracane RAX. Działa dla
  callera `852C86`, lokalnego player ID spod `client_context+338` i zgodnego
  bieżącego zakresu pętli logiki. Przed oryginalną zmianą wysyła dedykowany
  cancel-release własnego held i czyści własne C+558/C+4BF. Zdalny gracz,
  nieznany caller albo brak żywego zakresu nie uprawniają do zgadywania release.
- Przejście unieważnia poprzednie certyfikaty i wymaga otwarcia dłoni przed
  następnym press. Oczekujące eventy pozostają rozpoznawalne do natywnej
  destrukcji. Aktywne callbacki lifecycle są liczone w pending, więc drain nie
  kończy się przed ich powrotem. Akcja własna będąca jeszcze w trakcie wykonania
  daje `lifecycle_operation_in_flight`, zamiast domniemanego cleanup.
- Retirement obejmuje teraz także reset Q (`24005B`) i destrukcję Q (`23EB68`).
  Rejestr zapamiętuje źródłową kolejkę przed enqueue. Wymagane są: dokładna Q,
  własne ID alokacji, aktualne membership bez parenta, dopasowany native delete
  i zakończony clear. Oryginał usuwa request; mod zwalnia tylko własny payload,
  dokładnie raz. Inna kolejka, child przeniesiony do parenta, obcy caller,
  zagnieżdżony nieznany clear i niepewny powrót zachowują quarantine.
- Totals zawierają `lifecycle_releases`, `source_reset` i `source_destroy`.
  Source-Q retirement potwierdza zwolnienie pamięci po odrzuceniu requestu,
  **nie jego doręczenie do serwera**. Walidacja działającej interakcji nadal
  wymaga obserwowanej ścieżki WRITE lub localhost.
- Testy odtworzyły brak cleanup source-Q i przedwczesny idle w trakcie callbacku,
  a po poprawkach przeszły. Rozszerzono próby o oba callery deaktywacji,
  lokalną/zdalną zmianę postaci, pending, obcy kontekst, wyjątki, odmowę
  nieznanego callera, rollback dodatkowych hooków oraz guardy prefiksów/vtable/CALL.
  Łączony test wykonał siedem press/release, dwa pre-cleanup lifecycle,
  trzy WRITE, dwa localhost oraz po jednym source reset/destruction, z pustym
  ledgerem i zbilansowanymi alokacjami. Pełny Release build i **23/23 CTest**
  przeszły: [raport](../artifacts/offline-transitions-tests-20261102.xml).
  [Dowody ABI i kolejności](../artifacts/native-transitions-evidence-20261102.json).
- Użytkownik dopuścił przyszły test gry i poprosił o sygnał, kiedy podłączyć
  Questa. Nie rozpoczęto testu. Zainstalowany EXE ma SHA
  `c38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d`,
  a obsługiwany w tamtym momencie profil był nadal `f7d605e3…`. Implementację sprawdzono na zachowanych
  dowodach tej obsługiwanej wersji. Przed testem gry konieczna jest odrębna
  weryfikacja profilu aktualnego EXE; gogle nie są jeszcze potrzebne.
- Nie obejmuje to wszystkich możliwych teardown ani rekonstrukcji obiektów pod
  tymi samymi adresami. Przejście wykryte dopiero po utracie żywego kontekstu
  nadal pozostawia wyraźną blokadę, bez spóźnionych wywołań starej sceny.

## Weryfikacja offline — 2026-11-01

- Pełna konfiguracja i kompilacja Release w `build/capture` przeszły. Pierwszy
  CTest dał 22/23: fixture `native_hand_buttons` zakładała istnienie podkatalogu
  `build` w bieżącym katalogu roboczym. Przy uruchomieniu przez CTest brak tego
  katalogu powodował błąd otwarcia logu przed próbą instalacji hooków. Fixture
  tworzy teraz własny katalog `artifacts`, tak jak test integracyjny.
- Odtworzono reset dokładnie między ostatnią bramką a `mark_submitted()`.
  Rejestr odmawiał przekazania anulowanego eventu, ale backend oznaczał jego
  własność jako niepewną i nie zwalniał alokacji, mimo że native push nie był
  jeszcze wywołany. Nowa ścieżka usuwa rekord i zwalnia własny event raz,
  czyści pending i traktuje tę sytuację jako anulowanie przed handoff.
  Nie zmieniono obsługi niepewnego wyniku po rzeczywistej próbie push.
- Test `reset_before_submit` najpierw zgłosił wyciek, a po poprawce przeszedł.
  Sprawdza brak enqueue i pozostawionych alokacji, brak blokady backendu oraz
  pełny kolejny gest press/release. Punkt synchronizacji jest kompilowany
  wyłącznie dla jednostkowego celu z `SWVR_HAND_BUTTONS_TESTING`; produkcyjna
  DLL i test integracyjny nie zawierają tego callbacku.
- Po pełnej przebudowie przeszły **23/23 CTest**, w tym supervisor,
  HandButtons, retirement, łączony test integracyjny, testy GPU i atrapa OpenXR.
  Raport: [offline-tests-20261101.xml](../artifacts/offline-tests-20261101.xml)
  (23 testy, 0 failures, 0 skipped). Przeszły też **13/13 testów Python** przez
  `python -m unittest discover -s tests -p 'test_*.py'` oraz osobny
  `swvr_trace_test.exe` z prawdziwymi hookami DLL we własnym oknie OpenGL
  (obrazy oczu, stan GL, limit logu i append/rearm).
- Stormworks ani próby gogli nie uruchamiano. Powyższy wynik potwierdza
  weryfikację offline; test rzeczywistych kontrolek, transmisji i kamery
  w grze nadal wymaga zmiany zakazu użytkownika.

### Rozszerzenie cyklu życia HandButtons — 2026-11-01

- Dodano 18 izolowanych scenariuszy wewnątrz `native_hand_buttons`: zmiana
  świata/postaci podczas hold i przed odbiorem eventu, utrata/przywrócenie
  kontekstu, brak postaci, zmiana postaci pomiędzy dispatcherem a akcją i wewnątrz
  natywnych callbacków, utrata celu, pauza/UI, nowe hover, fizyczne przejęcie
  wejścia oraz zakończenie XR z pending i hold. Próby pauzy sprawdzają pięć flag
  kontrolera, a UI/trybu — osiem warunków. Usunięty przycisk ma odebrany dostęp
  do pamięci przez `MEM_DECOMMIT`; fixture nie pozwala legalnie odczytać go ponownie.
- Testy najpierw odtworzyły wykonanie starego press po zmianie postaci,
  pozostawione trzymanie po pauzie/UI i opóźnione wykrycie utraty celu.
  Odtworzono też zmianę postaci podczas przygotowania/przekazywania release.
  Poprawiony `Live` obejmuje token aktualnej postaci z rozpoznanej ścieżki
  `world+BB9F8 → node(type=4)+90 → holder → actor`. Token aktora służy do
  porównania, bez późniejszej dereferencji. Brak postaci blokuje nowy press.
- Zmiana tożsamości i utrata żywego kontekstu unieważniają wcześniejsze eventy.
  Dispatcher wyprowadza kontekst z aktualnej pętli, zamiast dereferencjonować
  historycznego ownera z certyfikatu. Kontekst jest ponownie sprawdzany przed
  akcją, po jej przyjęciu, po pobraniu payloadu i po przekazaniu release.
- Pauza, photo/death/sleep, przechwycenie UI, benchmark lub nieaktywny stan
  kończą nadal własne trzymanie dedykowanym cancel-release w żywym zakresie
  logiki. Bramka cleanup pomija warunki dotyczące nowego hover i własnych
  down/held; przesunięcie promienia na inny cel nie przekazuje mu trzymania.
  Fizyczne wejście zachowuje oryginalną drogę, a niepewne sprzątanie nie jest
  ponawiane.
- Nierozliczone hold przy zmianie świata/postaci lub utracie celu pozostaje
  zachowaną metadaną i blokuje dalsze operacje. Pierwsza przyczyna jest dostępna
  w `NativeHandButtonsStats.last_fault`, logu oraz `.xr.txt` w totals. Przykłady:
  `scene_changed_with_hold`, `live_context_lost`, `held_target_lost`,
  `cleanup_identity_changed`, `release_handoff_identity_changed`. Kolejne ticki
  nie zastępują pierwszej przyczyny ani nie wysyłają ponownego release.
- Łączony test rzeczywistych modułów obejmuje teraz także shutdown hold przez
  WRITE i localhost, shutdown queued-press z obcym wejściem w FIFO oraz pauzę
  ustawioną przez natywny event podczas trzymania. Reset nie wywołuje helperów
  gry; kolejny żywy przebieg logiki wysyła release raz. Ledger pozostaje zajęty
  aż do właściwego retirement. Łącznie potwierdzono pięć press/release,
  trzy końce WRITE i dwa localhost, bez phase0 i nierozliczonych alokacji.
- Pełny Release build i **23/23 CTest przeszły** po rozszerzeniu. Raport:
  [offline-lifecycle-tests-20261101.xml](../artifacts/offline-lifecycle-tests-20261101.xml).
  Nowe podprzypadki są częścią istniejących testów; liczba programów CTest
  pozostaje 23. Stormworks i testu gogli nie uruchamiano.
- Pozostaje ograniczenie: zmiana sceny po przejętym hold nie ma jeszcze
  udowodnionej ścieżki sieciowego zwolnienia w starej scenie. Blokada powoduje
  nieudaną próbę drain, zamiast zgłaszać sukces. Porównanie tokenów nie dowodzi
  nowego czasu życia obiektu odtworzonego pod identycznymi adresami; pełne
  pokrycie teardown wymaga odrębnych natywnych dowodów i przyszłych prób gry.

## Bieżące ograniczenie i diagnoza kamery

Historycznie użytkownik polecił **„póki co nie odpalaj samej gry”**. Dnia
2026-11-02 dopuścił przyszły test i poprosił o sygnał do podłączenia gogli.
Aktualną przeszkodą jest niezgodność zainstalowanego EXE z profilem, opisana
na początku dokumentu. Poniższe pomiary i ograniczenia kamery pozostają aktualne.

- Próba gameplay `headset-xr-20261028-225232-514.tsv`, PID 33968, przesłała 2987
  par i zastosowała wspólne płaszczyzny cullingu 2987 razy, ale po 54.5 s zakończyła
  się błędem `Native source target configuration changed`. Gra została zamknięta.
  Dodano odtworzenie własnych pakietów po obsługiwanej zmianie konfiguracji;
  test jednostkowy sprawdza zwolnienie obu generacji dokładnie raz.
- Aby użytkownik mógł dojść do pojazdu, dodano ręczny start przez
  `Local\StormworksVR.Start.<PID>` oraz `scripts/start-vr-preview.ps1`.
  `run-game-preview.ps1 -ManualStart` zostawia normalny obraz i sterowanie
  klawiaturą/myszą do sygnału startu. Czas próby jest ustawiany przez
  `-PreviewSeconds` / `SWVR_XR_SECONDS`; nie zaczyna się podczas ładowania świata.
- Kolejna próba, `headset-xr-20261028-230241-819.tsv`, PID 4204, została uruchomiona
  dopiero po zgłoszeniu użytkownika „jestem w pojeździe”. Źródłowy obraz miał już
  3440×1440. Przesłano 9768 par przez 120031 ms, z 9768 aktualizacjami cullingu;
  odebrano 9/31 rozpoczęć i puszczeń szczypnięć lewej/prawej dłoni. Proces został
  zamknięty. **Test obrazu jest niezaliczony:** użytkownik zgłosił lekkie
  przechylenie na bok oraz przenikanie widoku przez własną głowę i postać.
- `.camera-world.tsv` zawiera pierwotne kamery świata. Zestaw ma 10102 próbek,
  roll około -16..+16 stopni. Nie zapisywał look ani origin XR, więc sam nie
  rozstrzyga, jaka część przechyłu pochodziła z pojazdu, zwykłej kamery i kalibracji.
  Nie uznawać liczby poprawnych wywołań OpenXR za dowód poprawnego 6DoF.
- Odtworzono konkretny błąd polityki kalibracji testem
  `swvr_seated_calibration_tests.exe`: początkowy roll 7° powodował później -7°
  przy wyprostowanej głowie. Test najpierw był czerwony. Nowa
  `gravity_aligned_origin` zachowuje środek oczu i yaw, bez zapamiętywania pitch/roll;
  test przechodzi. Kalibracja wymaga teraz również fokusu sesji.
- `make_head_anchor` odwraca rozpoznane odsunięcie kamery `719F20`, usuwa tylko
  rotację look i zachowuje punkt oczu oraz bank pojazdu. Ma testy 48 kombinacji
  trybów i precyzji przy 1e9. Podłączono go wyłącznie do kopii kamer VR i cullingu;
  poprzedni look jest zapisywany przed natywną aktualizacją. Rozszerzono przyszły
  log o surową kamerę, anchor, look, tryb/backoff i origin. **Nowe poprawki nie
  były jeszcze sprawdzone w grze ani w goglach.**
- Natywne shadery mają selektywną maskę głowy lokalnego gracza, ograniczoną do
  0.25 m od kamery. Analiza i testy przygotowywanej zmiany są zapisane w
  `docs/first-person-head-visibility.md`; nie ukrywać wszystkich postaci ani
  zwiększać globalnego near-plane jako zamiennika prawidłowej maski.

### Wskazywanie dłonią — wdrożone, testowane poza grą

- Publikacja promienia używa tego samego poprawionego anchor jak kamery VR.
  Nowy `native_hand_ray` podmienia początek/kierunek tylko w znanej natywnej
  selekcji i dokładnie przywraca 12 bajtów kierunku kontrolera. Sprawdza profil,
  caller, świeżość, fokus, generację anulowania, pięć flag trybu i rozpoznane
  `c_application_data+233` (benchmark). Transakcje są serializowane także między
  wątkami; zagnieżdżona selekcja ma własny zakres. Nie emituje żadnych akcji.
- `run-game-preview.ps1 -HandRay` wymaga Gameplay i wyklucza InteractionProbe.
  Rysuje zielony promień prawego palca; nie oznacza on trafienia ani zasięgu gry.
  Początek sesji VR odnawia limit zapisu `.hand-ray-native.tsv`, dopisując nową
  epokę bez nadpisywania dotychczasowych rekordów. Pozwala to zachować pomiar po
  wcześniejszej nawigacji na monitorze. Skryptu uruchamiającego grę nie wykonano.
- Dodano monotoniczne `pinch_gesture_id`, diagnostyczny `pinch_down` oraz
  `consume_selected(sequence)`. Rozpoczęcie/trzymanie wymaga próbki faktycznie
  użytej przez selekcję; niezgodność nie konsumuje gestu. Release/cancel nie są
  blokowane przez niezgodną selekcję. Regresja najpierw wykazała scalanie szybkiego
  release/repress w stary hold; po poprawce stary hold jest anulowany raz, a nowy
  wymaga ponownego otwarcia dłoni. Sam most publikuje stan gestu; opcjonalny
  backend HandButtons opisany poniżej przekłada go na natywne akcje.
- Pełna kompilacja `build/capture` Release i **17/17 CTest przeszły**. Testy GPU
  korzystają z własnych ukrytych okien; test OpenXR używa atrapy. Stormworks nie
  uruchomiono. Nie potwierdza to jeszcze osi promienia w żywym pojeździe ani
  usunięcia zgłoszonego błędu obrazu.
- Dodano pasywny `native_input_probe`, dostępny przez `-InputProbe` w Gameplay.
  Obserwuje cztery rozpoznane wywołania pop i dwa dispatch, zachowując ABI oraz
  wynik oryginału. Przed dalszymi odczytami sprawdza żywe powiązania
  owner/simulation/controller/application. Loguje stany UI i typ przycisku;
  wynik jest obserwacją, nie udowodnionym zezwoleniem na input. Nie wysyła ani
  nie tłumi eventów. Sonda może pracować obok HandRay. Oba logi mają append/rearm
  także przez istniejące żądanie `capture-scene.ps1`; samego skryptu gry nie wykonano.
- `HandEventRegistry` ma stałą pojemność i zachowuje rozpoznanie własnego eventu
  po reset aż do destruktora. Chroni ponowne użycie adresu tokenem/generacją,
  dopuszcza najwyżej jeden claim i zachowuje informację o wykonaniu nawet przy
  anulowaniu w trakcie wywołania. Test obejmuje FIFO z wejściem fizycznym,
  pusty pop, teardown, brak miejsca, niepewny handoff i dwa wątki. Rejestr jest
  już używany przez backend HandButtons do natywnej alokacji, push i rozpoznania
  własnego eventu przy wykonaniu oraz destrukcji; zakres opisano poniżej.
- Nowy test sondy ujawnił brak `UNWIND_INFO` w sztucznych wywołaniach x64;
  niezłapany wyjątek `E06D7363` został odtworzony. Uzupełniono rejestrację
  unwind w trzech fixture, w tym jawną próbę wyjątku Origin. Po poprawce komplet
  17/17 jest zielony, a native_input_probe/native_hand_ray/interaction_probe
  przeszły po **10 kolejnych powtórzeń**. Poprawka dotyczy programów testowych.
- Analiza akcji wykazała, że natywny release może przed zwolnieniem starego
  przycisku wykonać dodatkowe działanie na bieżącym wskazaniu, a przy śmierci
  odpaść przed sprzątaniem. Nie należy włączać na tej podstawie ślepego phase=0
  jako ogólnego cancel. `held-button-cleanup.md` dokumentuje zakres lokalnego
  cleanup i brak dowodu sieciowego release po śmierci; szczegóły własności i
  dalszej integracji są w `hand-action-integration.md` oraz `hand-event-registry.md`.
- Rozpoznano osobne żądanie zwolnienia typu 10, rozmiar 0x48. Jego slot vtable0
  jest serializatorem, nie destruktorem; pole +34 pomija co najmniej callback
  Lua `onButtonPress`. Natywne oddalenie używa pressed=0 i tego pola=1.
  Dalsze rozpoznanie odbiorcy i dwóch dróg finalnego clear opisuje aktualizacja
  poniżej. Nie przenosić tu modelu własności key_action. Podstawa układu:
  `button-release-request.md`.
- Po końcowej kompilacji przeszedł również `swvr_trace_test.exe`: rzeczywiste
  hooki DLL we własnym oknie OpenGL, piksele, limit oraz append/rearm. Odczyt
  procesów potwierdził, że Stormworks nie działa. Dalsze testy gry wymagają
  zmiany wyraźnego zakazu użytkownika; goal pozostaje nieukończony.

### Dedykowane zwolnienie — builder, ABI i bieżący held

- `NativeButtonReleaseRequest` buduje dokładne 0x48 bajtów requestu type10,
  wyłącznie pressed=0. Payload jest niezależną natywną kopią z terminatorem;
  capacity oznacza bajty z NUL, a wire dopuszcza najwyżej 65535 bajtów danych.
  Są jawne prepare/discard/submit. Próba enqueue przenosi stan na nieustaloną
  własność przed callbackiem; brak retry/free po tej próbie. Destructor C++ nie
  wywołuje gry. Opis: `native-button-request.md`.
- Windowsowa biblioteka `swvr_button_native` wiąże rzeczywiste allocate,
  reserve, aligned-free, sized-delete i push po zgodności SHA/prefiksów/vtable.
  Nie instaluje backendu wejścia. `capture_release` najpierw sprawdza identyczność
  żywego kontekstu i faktycznego obecnego C+558 z wcześniej przyjętym press.
  Odczytuje vehicle/section/grid oraz zapisany hit, kopiuje payload przez znany
  getter i zwalnia jego temporary raz. Wynik zawiera wartości i std::string,
  bez utrwalania native pointerów do późniejszej dereferencji. Niczego nie
  zapisuje w kontrolerze. Opis: `native-button-bindings.md`.
- Łączony test wykonuje capture → builder → push przez te same bindings,
  kontrolując surowe 72 bajty, stary hit zamiast nowego hover oraz niezależność
  obu kopii stringu. Fixture używa własnego obrazu VirtualAlloc i funkcji C++
  pod adresami odpowiadającymi ABI; nie ładuje ani nie wywołuje Stormworks.
- **19/19 CTest przeszło** po pełnym Release build. Core i nowe dwa testy mają
  `/W4 /WX /permissive-`. Raport: `artifacts/offline-button-tests-20261029-090323.xml`
  (19 testów, 0 failures, 0 disabled). Odczyt procesów potwierdził brak gry.
- Odbiorca packet0x1A z skip1 wywołuje osobny reset Button+278/+279, omijając
  phase0/short-click i health gate. Nie cofa już zatrzaśniętego Toggle i nie
  czyści C+558/C+4BF; lokalne zakończenie własnego trzymania pozostaje odrębne.
  Dowód: `button-release-receiver.md`.
- Domknięto dwie normalne drogi lifetime: po WRITE zniszczony jest parent D0,
  a własny request/string wymagają rozliczenia; przy local-host typed clear
  oryginał usuwa również request, więc mod może rozliczyć wyłącznie własny
  string. Tryb musi wynikać z zaobserwowanej trasy i identyfikatora alokacji,
  nie z aktualnej flagi trybu gry. Szczegóły: `button-request-last-reader.md`.
- Późniejsze moduły `native_hand_buttons` i `native_button_retirement` łączą
  te elementy w opcjonalny backend opisany poniżej. Poprawki kamery, promień
  w ruchomym pojeździe, obsługa UI i chodzenie nadal wymagają pracy/weryfikacji.
  Zakaz uruchamiania gry pozostaje w mocy.

### HandButtons i retirement — podłączone, weryfikowane poza grą

- `src/gl_trace.cpp` instaluje opcjonalne hooki `native_hand_buttons` oraz
  `native_button_retirement` przed globalnym włączeniem hooków. Wymagają
  `SWVR_HAND_BUTTONS=1`, zgodności pełnego SHA profilu i sygnatur natywnych
  funkcji. Domyślnie backend jest wyłączony. `run-game-preview.ps1 -HandButtons`
  wymaga Gameplay, włącza HandRay i wyklucza InputProbe oraz InteractionProbe.
- Backend dopuszcza prawą dłoń i dokładnie rozpoznany Push (`button_type=0`).
  Certyfikat łączy gest z zakończoną selekcją, celem i generacją sceny. Własny
  key_action Action20/phase1 przechodzi przez natywną kolejkę; bramki świeżości,
  fokusu, pauzy/UI, benchmarku, zgodności celu i konfliktu wejścia są ponownie
  sprawdzane przy logicznym dispatchu oraz przed oryginalną akcją. Natywna akcja
  zachowuje własne warunki postaci, zdrowia i odległości. Obce eventy zachowują
  oryginalną drogę obsługi.
- Po przyjętym press backend śledzi własne trzymanie. Otwarcie dłoni lub
  anulowanie prowadzi w żywym zakresie logiki przez capture → builder → push
  dedykowanego release type10, po czym czyści własne C+558/C+4BF, o ile stan
  nadal można przypisać tej operacji. Nie generuje phase0/short-click do cleanup.
  Utrata kontekstu albo niepewny handoff blokują dalsze działania; backend nie
  ponawia operacji ani nie zwalnia pamięci na podstawie domysłu.
- `native_button_retirement` rejestruje własność przed enqueue i rozpoznaje
  końce WRITE oraz localhost. Po WRITE zwalnia własny payload i request; po
  localhost typed clear zwalnia tylko własny payload, ponieważ request usuwa
  oryginał. Reset zachowuje nierozliczone rekordy. Nieznane delete/teardown lub
  wyjątki mogą pozostawić quarantine, która zajmuje slot ledgeru i uniemożliwia
  uznanie go za idle. Szczegóły: [native-button-retirement.md](native-button-retirement.md).
- Zamknięcie XR anuluje zamiar, a sprzątanie wykonują nadal żywe ścieżki logiki.
  Nadzór czeka do 5 s na brak własnego hold, pending eventów i retained records;
  błędy lub quarantine oznaczają nieudaną próbę. Skrypt wymaga dodatkowo co
  najmniej jednego przyjętego press, release i zakończonego retirement.
- CMake zawiera testy `native_hand_buttons`, `native_button_retirement` oraz
  `native_hand_buttons_integration`. Łączony test używa rzeczywistych modułów
  bridge/ray/buttons/capture/bindings/builder/retirement z atrapą natywnego
  środowiska i MinHook. Obejmuje zwykłe zwolnienie przez WRITE, utratę fokusu
  przez localhost, pauzę przed własnym eventem w FIFO, obce wejście i bilans
  alokacji bez phase0. Zachowane logi
  `artifacts/hand-buttons-integration-46068-305915203.hand-buttons.tsv` i
  `artifacts/hand-buttons-integration-46068-305915203.button-retirement.tsv`
  zawierają dwa przyjęte press, dwa release, odrzucenie press przez tryb
  kontrolera oraz po jednym `write_retired` i `localhost_retired`.
  Opis testów samodzielnego retirement znajduje się w jego dokumentacji.
- **Brak testu HandButtons w Stormworks i na goglach.** Zapisany raport 19/19
  poprzedza dodanie powyższych trzech celów; aktualny zestaw przeszedł 23/23
  po poprawce resetu opisanej na początku dokumentu. Następny test gry wymaga
  zmiany zakazu użytkownika i powinien
  zweryfikować wybór celu w ruchu, rzeczywiste bramki UI/wejścia, transmisję
  release oraz pełne rozliczenie zasobów. Backend nie obejmuje wszystkich
  kontrolek ani nieznanych ścieżek teardown.

Poniższe sekcje zachowują historię wcześniejszych pomiarów i stanów goal.

### Maska głowy — podłączona i sprawdzona bez gry

- Dodano `head_mask_shader` i `GlHeadMask`. Znane shadery animowane dostają w
  pamięci przełącznik rozszerzający lokalną maskę głowy poza pierwotne 0.25 m.
  Ustawienie wymaga aktywnego renderowania oka VR, istniejącego nieujemnego
  `bone_clip_index` oraz wagi 0.25. Nie zmienia to globalnego near-plane ani
  widoczności wszystkich postaci. Domyślna wartość nowego uniformu to 0.
- Podłączono hooki źródła/kompilacji/linkowania shaderów, uniformów i draw.
  Źródła wieloczęściowe respektują jawne długości. Przy błędzie kompilacji
  zmienionego shadera wraca jego oryginalne źródło. Kopie źródeł mają limit;
  zmiana źródła, usunięcie shadera i kontekstu czyszczą właściwe cache.
- `swvr_head_mask_injection_tests.exe` faktycznie ładuje DLL i sprawdza piksele
  po przejściu przez jej hooki: daleka głowa jest maskowana tylko w VR, zwykły
  obraz wraca, ujemny indeks i inna waga nie uruchamiają maski, a testowy eksport
  nie działa bez jawnego trybu w osobnym nazwanym programie. Test nie uruchamia
  Stormworks. Początkowy błąd fixture dotyczył oczekiwania zera od `flush`, który
  zwraca liczbę zdarzeń; poprawiono kontrakt testu, nie kod zwracający wynik.
- **14/14 CTest przeszło**, podobnie jak test tracera i test poprawki na trzech
  rzeczywistych plikach shaderów odczytanych z instalacji. Plików gry nie zmieniano.
  Trace pełnego testu hooków: `build/capture/Release/head-mask-injection-37668-270849312.tsv`.
- Maska nadal wymaga natywnego selektora lokalnej głowy w pierwszej osobie.
  Ujemny indeks w trybie trzeciej osoby nie jest arbitralnie nadpisywany. Nie ma
  jeszcze dowodu, że problem zgłoszony przez użytkownika zniknął w jego scenie;
  obowiązuje zakaz uruchamiania gry do kolejnej instrukcji użytkownika.

## Najnowszy pomiar interakcji użytkownika

### Późniejsze podłączenie Questa i test sprzętowy

Aktualizacja po powtórzeniu testu: użytkownik potwierdził widoczny obraz świata
Stormworks i brak menu w goglach. Brak menu jest znanym ograniczeniem obecnego
przechwycenia: UI powstaje po przechwyceniu obu obrazów świata, na monitorze.
`artifacts/headset-xr-20261028-223835-549.tsv.xr.txt`: 2418 przesłanych par w
30015 ms, po 2419 próbek dłoni, 3 rozpoczęcia/puszczenia szczypnięcia lewej oraz
2 prawej dłoni. PID 20704 zamknięto automatycznie; brak działającej gry.
Potwierdza to rzeczywisty obraz Stormworks oraz odbiór dłoni i gestów przez mod,
ale nie sterowanie kontrolkami, UI w VR, poprawność ruchu/komfortu ani rozgrywkę.

- Użytkownik podłączył Quest 3 przez Steam Link. `artifacts/quest-connected-capabilities-01.json` potwierdza system HMD SteamVR/OpenXR 2.18.4, obsługę pozycji i orientacji oraz `supports_hand_tracking=true`; rekomendowane obrazy 2528×2704 na oko.
- Rzeczywisty demonstrator `build/capture/Release/swvr_xr_demo.exe` uruchomiono na 45 s. `artifacts/quest-hardware-demo-01.txt`: 3073 klatki stereo, 3132 klatki z oboma flagami śledzenia pozycji i orientacji głowy, 2521 śledzonych próbek każdej dłoni. Zarejestrowano rozpoczęcia i puszczenia szczypnięcia obu rąk oraz anulowanie przy utracie ważności/fokusu. Program zakończył się kodem 0.
- Użytkownik potwierdził dla demonstratora: „Widzę scenę i obie dłonie”, odpowiadając na test po odłożeniu kontrolerów. To pierwsze potwierdzenie obrazu i rzeczywistych dłoni na sprzęcie; wcześniejsze próby z atrapą nie były takim dowodem.
- Następnie uruchomiono faktyczną scenę menu Stormworks na tym samym zestawie: `artifacts/headset-xr-20261028-223400-895.tsv.xr.txt`, PID 43132. **2116 par obrazów w 30047 ms**, po 2130 aktywnych śledzonych próbek końców palców każdej dłoni. W tym uruchomieniu nie zarejestrowano szczypnięć. Własne pakiety oczu przeszły inicjalizację i realizację, a ich referencje zwolniono na końcu.
- Test menu pracował z rzeczywistym SteamVR, bez testowego loadera. Średni czas pomiędzy próbami klatek 13.028 ms, CPU obu renderów 2.164 ms; nie są to wyniki opóźnienia gogli ani gwarantowanego odświeżania. Program przekazał obrazy OpenXR; osobne potwierdzenie użytkownika, że świat Stormworks i jego ruch wyglądają prawidłowo w goglach, pozostaje oczekujące przy zapisie tego wpisu.
- Gra i demonstrator są zamknięte; SteamVR pozostawiono uruchomiony. Pomiary dotyczyły demonstratora i sceny menu, nie ukończonego VR kokpitu. Ruch głowy w rozgrywce, culling, historia i aktywowanie kontrolek dłonią nadal wymagają implementacji/weryfikacji. Cały goal nie został wznowiony w interfejsie ani oznaczony jako ukończony.

Użytkownik wstrzymał goal w interfejsie. Później zgłosił gotowość do konkretnego pomiaru w pojeździe; wykonano tę próbę, bez wznowienia całego celu. Stan goal pozostaje `paused`.

- Pierwotna sesja PID 38332 nie miała modułu sondy. Zgodnie z wcześniejszą zgodą na restarty uruchomiono nową sesję wyłącznie z obserwacją interakcji: PID 45428, `artifacts/cockpit-input-live-01.tsv`. OpenXR, stereo i zmiany kamer były wyłączone.
- Po zgłoszeniu użytkownika „Wgotowe” zapisano końcowy bufor przez istniejący eksport `swvr_trace_flush` i zamknięto zmierzony proces. Launcher potwierdził zakończenie; wymuszone zakończenie było wcześniej autoryzowane.
- `artifacts/cockpit-input-live-01-summary.json`: 7606 pełnych wierszy, 0 niepełnych, limit nieosiągnięty; 7408 selekcji i 198 zdarzeń akcji w około 124.8 s. Każda selekcja odczytała jeden początek promienia. Maksymalny błąd długości jednostkowego kierunku około 1.58e-7.
- Zarejestrowano wyłącznie akcję **0x14 (20)**: 99 naciśnięć i 99 puszczeń. W 89 cyklach gra ustawiła zapamiętany cel pojazdu, a następnie zwolniła dokładnie ten sam cel. Nie znaleziono rozbieżności ani pozostawionego trzymania. To cykle natywnych zdarzeń, nie dowód liczby fizycznych kliknięć użytkownika.
- Wskazanie objęło 23 różne adresy komponentów pojazdu. Sonda nie zapisuje etykiet i nie pozwala przypisać im pewnie typów przycisk/przełącznik/dźwignia. Akcja 0x13 pozostaje niezweryfikowana w wykonaniu.
- Selektor i dyspozytor działały na dwóch zaobserwowanych identyfikatorach wątków: 48424 i 3424. Nie zakładać jednego stałego wątku wejścia; przyszłe gesty należy przekazywać przez ustaloną ścieżkę logiki, nie wywoływać akcji z wątku renderowania.
- Użyto zwykłego wejścia użytkownika, nie optycznych dłoni. Pomiar potwierdza natywną drogę wskazywania i press/release w kokpicie; nie potwierdza gotowej interakcji VR. Opisy wcześniejszych etapów poniżej są historyczne.

Status goal został wznowiony przez użytkownika do `active`.

Użytkownik wyraził zgodę na samodzielne zamykanie i ponowne uruchamianie Stormworks podczas testów moda, także wymuszone zakończenie procesu w razie potrzeby. Ta zgoda obowiązuje dalej w tej rozmowie; nie trzeba pytać o każdy restart.

## Stan po wznowieniu i zgodzie użytkownika

Prace wznowiono przez zatwierdzoną powłokę poza sandboxem. Zwykła powłoka nadal sporadycznie nie może utworzyć procesu; interfejs sterowania oknami pozostaje niedostępny. Historyczna blokada poniżej nie opisuje już dostępności pracy nad kodem.

- Pierwsza sesja launchera zakończyła się kodem 1 i pustym logiem. Poprawiono natychmiastowy zapis nagłówka tracera; test przeszedł.
- Drugie uruchomienie Stormworks zapisało rzeczywisty ślad: 1113 swapów, 115504 wywołania macierzy, 81932 drawElements i 7524 drawArrays. Podsumowanie: `artifacts/stormworks-startup-02-summary.json`. Ostatnio potwierdzony PID: 45172; sprawdzić przed kolejnym uruchomieniem.
- Poprawiono interpretację uniformów o lokalizacji -1; test regresji przeszedł. Znaleziono kandydatów do analizy kamery przy RVA 0x67AA07, 0x68ED84 i 0x68B197. To adresy powrotu, nie zweryfikowane punkty podłączenia VR.
- Zainstalowano lokalnie Node 22.23.4, REA 6.8.0, Ghidrę 12.1.4 oraz pełny JDK 21.0.12.1. Sumy SHA-256 archiwów Ghidry i JDK są zgodne z metadanymi oficjalnych wydań. REA uruchamia się przez `scripts/rea.ps1`, bez trwałych zmian zmiennych systemowych.
- `doctor --provider ghidra --format json`: `healthy=true`. Ogólny audyt wylicza też nieużywane silniki i rejestracje MCP; nie są wymagane do lokalnego CLI.
- Pełna analiza REA przekroczyła najpierw 2 GB pamięci Javy, a po podniesieniu limitu do 8 GB — 330 s czasu startu. Ograniczona analiza Ghidry z `-noanalysis` i własnym skryptem zakończyła się poprawnie: trzy funkcje shaderów, aktualizacja kamery `0x194680` i renderer `0x6D4DB0`. Wyniki: `artifacts/camera-targeted.json` i `artifacts/camera-update-renderer.json`. Skrypt: `scripts/ghidra/ExportCameraFunctions.java`.
- Steam zmienił instalację przed drugim pomiarem: aktualny build to `26275574`, SHA-256 `f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`. Wcześniejszy raport `game-report.json` dotyczy starej wersji. Ślad nr 02 nie miał jeszcze własnego SHA, więc jego tożsamość jest oznaczona jako niezweryfikowana przez nowy mapper.
- `camera_bridge.cpp` przelicza zaobserwowaną kamerę gry na widok oka, zachowuje odbicie układu osi oraz współczynniki głębokości, wylicza VP i inverse VP. Testy rzeczywistych macierzy z klatki 1085, IPD, pozycji głowy, niepoprawnych danych i rekonstrukcji głębokości przeszły. To sam moduł obliczeniowy, jeszcze nie steruje obrazem gry.
- Tracer zapisuje SHA pliku oraz końcowy znacznik i wszystkie 250000 zdarzeń po osiągnięciu limitu. Test GPU odtworzył błąd przed poprawką i przeszedł po niej; zgodność SHA potwierdzono niezależnie.
- Obserwacyjny hook aktualizacji kamery, włączany tylko przy zgodnym SHA i 24 bajtach instrukcji wejściowych, przeszedł test w rzeczywistym Stormworks: `artifacts/camera-probe-01.json`, 120 pełnych próbek, 0 błędów, maksymalny błąd relacji VP = P × V około 8.8e-8. Nie modyfikuje kamery. Pomiar dotyczył sceny 3D menu, nie kokpitu.
- W scenie menu wywołanie `0x8406DA` aktualizuje kamerę źródłową, a renderer aktualizuje trzy lokalne kopie w `0x6D4F50`, `0x6D4FF7`, `0x6D509A`. Wszystkie trzy argumenty kamer w zaobserwowanym wywołaniu menu wskazują ten sam obiekt źródłowy. Nie należy zakładać tego dla rozgrywki.
- Odczyt 640 bajtów zaobserwowanego obiektu kamery, z kontrolą vtable i dwóch identycznych odczytów, potwierdził relację `V * T = diag(1,1,-1,1)`, błąd około 1.6e-6. Dowód: `artifacts/camera-basis-check.json`.
- Dodano nowy pomiar na żądanie przez lokalne zdarzenie Windows. `scripts/capture-scene.ps1` sygnalizuje moduł; na granicy klatki dopisuje on nowy `capture_epoch` do tego samego pliku, zachowując poprzedni zapis i nazwy shaderów wczytanych po osiągnięciu limitu. Analizatory mają `--epoch`, aby nie pomylić menu z kokpitem.
- Najnowszy build: `build/capture/Release`. Kompilacja, 2 testy CTest, 13 testów Python, test GPU tracera (w tym nowy pomiar i późne shadery) i test launchera przechodzą. Walidator poprawnie odrzuca brak hooka lub nieistniejącą epokę zamiast używać wyników z menu.
- Użytkownik potwierdził wejście do kokpitu. Pomiar `artifacts/stormworks-cockpit-01.tsv`, epoka 2, przeszedł walidację: 328 próbek, 0 błędów, 8 adresów kamer, maksymalny błąd VP około 7.2e-8. Raport: `artifacts/camera-cockpit-02.json`. Epoka 1 uchwyciła wyłącznie UI i nie ma pełnych próbek kamery; nie traktować jej jako testu kokpitu.
- Kamery źródłowe kokpitu aktualizują się przy `0x71AC89`, `0x71ACF9`, `0x71AD69`, `0x71ADD9`. To fragmenty jednej funkcji zaczynającej się w `0x71A140` i kończącej w `0x71AF47`, ustalone przez chained unwind. Wcześniejszy eksport zaczynający się w `0x71AC50` jest niepełny; używać `artifacts/cockpit-camera-owner.json`. Dodatkowa kamera aktualizowana jest w `0x6D391A`, a trzy lokalne kopie w rendererze mają przesunięty początek współrzędnych i inne near/far. Role temporalne wymagają dalszej analizy, nie zakładać że to oczy.
- Na wyraźne polecenie użytkownika zamknięto grę po pomiarach. PID 48468 zakończony; ostatnia kontrola nie znalazła żadnego procesu Stormworks. Logi i raporty zachowane. Użytkownik nadal zezwala na restarty podczas przyszłych testów; grę zamykać po zakończeniu pomiarów.

## Najnowszy eksperyment stereo i moduł OpenXR

- `GlEyeTargets` przechowuje dwa niezależne obrazy RGBA8. Test GPU sprawdza różne obrazy oczu, odczyt, kopiowanie i przywracanie stanu hosta, w tym oddzielne FBO read/draw, scissor, sRGB oraz bufory i ustawienia pakowania pikseli.
- Opcja procesowa `SWVR_STEREO_PREVIEW=1` uruchamia eksperymentalny podgląd stereo wyłącznie dla wywołania menu `0x84074B`. Hook pozostaje zabezpieczony SHA pliku i sygnaturą funkcji. Trzy kamery każdego oka są kopiowane, a przesunięcie IPD jest nanoszone na transformację double przed natywną aktualizacją kamer. Rozgrywka nadal wywołuje renderer jeden raz.
- Test w rzeczywistym Stormworks: `artifacts/stereo-menu-01.tsv`, 15 kompletnych par, zmierzony rozstaw kamer 0.0639953–0.0640106 m. Zapisane obrazy 1280×720: `artifacts/stereo-menu-01.tsv.left.png` i `.right.png`. Oba sprawdzono wizualnie: przedstawiają scenę 3D menu, z różnymi punktami widzenia. Raport: `artifacts/stereo-menu-01-result.json`. Różnica pikseli sama nie dowodzi stereo, ponieważ wpływają na nią także pogoda i stan renderera; rozstaw potwierdzają macierze kamer.
- Ekran startowy pokazany przez użytkownika był przejściowym etapem tego uruchomienia. Gra przeszła do renderowania sceny menu. Po zapisie pomiarów zamknięto PID 18808; późniejsza kontrola nie znalazła działającego Stormworks.
- Eksperyment współdzieli pakiet celów renderowania i stan renderera pomiędzy oczami; licznik renderera zwiększa się dwukrotnie. Nie potwierdzono poprawności cullingu, wody, historii ani czasu symulacji całego grafu wywołań. Nie traktować tego jako działającego VR kokpitu. Audyt: `docs/stereo-renderer-audit.md`.
- Ważne doprecyzowanie analizy: pola `+48/+50/+54/+58` badanego obiektu celu opisują kontener załączników G-buffer. Wcześniejsze podejrzenie, że jest to temporalny pierścień tekstur, nie zostało potwierdzone. Pakiety celów głównego i pomocniczego renderowania znajdują się w rendererze pod `+2628/+2718`; własne pakiety wymagają ustalenia konstruktora i własności zasobów.
- Nowa biblioteka `swvr_xr` używa istniejącego HDC/HGLRC, zarządza cyklem sesji, dwiema swapchainami, ważnością śledzenia i 26 stawami każdej dłoni. Testy atrapowego loadera obejmują pełne/częściowe/puste klatki, utratę fokusu, zmianę przestrzeni LOCAL, STOPPING, błąd pobrania obrazu, timeout oraz sprzątanie. Nie potwierdzają działania na goglach.
- Na tym etapie build `build/capture/Release` miał 3/3 CTest oraz wcześniejsze 13 testów Python i test GPU. Poniżej zapisano późniejsze połączenie natywnego hooka z OpenXR.

## Połączenie renderera menu z OpenXR

- `GlXrPresenter` kopiuje dwa obrazy do pożyczonych tekstur XR, sprawdza oba cele przed zapisem, zachowuje stan hosta i nie przejmuje własności tekstur. Testy na prawdziwym GPU obejmują RGBA8, sRGB, skalowanie, błędne cele i przywrócenie stanu.
- `apply_game_eye_projection` zmienia tylko P, float VP i double VP w rozpoznanym obiekcie kamery. Zachowuje współczynniki głębokości oraz double view; testy sprawdzają asymetrię FOV, precyzję przy pozycji 1e9 i nienaruszenie pozostałych bajtów obiektu. Aktualizacja jest stosowana także po trzech wewnętrznych przeliczeniach kamer floating-origin; brak któregokolwiek przerywa test.
- Hook może uruchomić `SWVR_OPENXR_PREVIEW=1` z absolutnym `SWVR_OPENXR_LOADER`. Kalibruje LOCAL do środka pierwszej poprawnie śledzonej pary oczu, przelicza pozy oczu, wysyła obrazy i odczytuje stawy oraz szczypnięcia. Nie rysuje dłoni ani nie aktywuje nimi kontrolek. Tryb nadal ograniczony do menu; docelowy VR kokpitu jest nieukończony.
- `scripts/run-game-preview.ps1` uruchamia własną kopię gry, nadzoruje 30-sekundową sesję i zamyka tę kopię po pomiarze. `-FakeRuntime` używa testowego loadera z prawdziwymi teksturami GPU; `-QuietTrace` wyłącza szczegółowe zdarzenia GL. Dokumentacja: `docs/game-preview.md`.
- Pierwszy test `artifacts/fake-xr-20261028-210842-809.tsv` przesłał 59 par, bez błędów. Walidator rzeczywistych kamer: 106 pełnych próbek, 0 błędów, maksymalny błąd VP około 8.8e-8 (`...-camera.json`). Obrazy obu oczu obejrzano. Odczyt końcowych tekstur atrapy potwierdził różne, nieczarne obrazy. PID 16748 zamknięto.
- Druga próba `fake-xr-20261028-211220-465.tsv` ujawniła błąd nadzoru: po dwóch klatkach konserwacji, celowo pomijanych przez stereo, kod uznawał menu za nieaktywne i kończył sesję. 59 par oznaczało około sekundę renderowania, nie wydajność 2 FPS. Poprawiono aktualizację znacznika menu także dla tych klatek; dodano rzeczywisty czas sesji i powód zamknięcia. PID 42744 zamknięto.
- Pełna poprawiona próba: `artifacts/fake-xr-20261028-211405-926.tsv.fake.json` i `.tsv.xr.txt`. **1739/1739 poprawnych par w 30031 ms, 0 błędów, 3478 pobranych i oddanych obrazów, 2 zniszczone swapchainy**. Powód końca `duration_elapsed`. Średni odstęp między parami 17.248 ms, CPU wywołań obu renderów 2.455 ms, kopiowania obu przechwyceń 0.041 ms, przekazania do XR 0.062 ms. To pomiar menu z atrapą, bez kompozytora i bez dowodu osiągów na Queście. PID 12920 zamknięto.
- `NativeEyeResources` tworzy własne pakiety przez jawnie przekazane natywne funkcje i czyści je na żądanie. Testy atrap sprawdzają mapowanie 18 argumentów, niezależne zasoby, opóźnioną realizację FBO, aliasy, niepełne init i sprzątanie. Helper odrzuca niepotwierdzone konfiguracje i nie wywołuje natywnych funkcji z destruktora C++. Nie jest jeszcze używany przez hook; gra nadal współdzieli własny pakiet między oczami.
- Użytkownik potwierdził, że Quest się ładuje i sam zgłosi jego podłączenie. Nie powtarzać pytania o gotowość. Żadna z powyższych prób nie potwierdza obrazu w goglach ani optycznego śledzenia dłoni.
- Końcowy pełny build i 5/5 CTest przeszły: presenter GPU, protokół OpenXR, tracking, kamery i własność natywnych zasobów. Cel pozostaje aktywny i nieukończony; bieżąca tura przyniosła wdrożenie oraz pomiar całego eksperymentalnego przepływu menu → OpenXR.

## Osobne zasoby oczu — wykonany test natywny

- Poprzednia tura przyniosła postęp: natywna integracja menu z OpenXR i pełny pomiar 30 s. Bieżąca tura także przyniosła postęp; nie ma blokady pracy nad kodem.
- Podłączono `NativeEyeResources` do gry. Hook init pakietów zapamiętuje rzeczywisty manager; oryginalny init, resize, release i destruktor kontenerów są chronione SHA i sygnaturami. Funkcje zarządzają wyłącznie własnymi pakietami.
- Próba `fake-xr-20261028-211822-193.tsv` została odrzucona przed rysowaniem, ponieważ pierwsza wersja helpera przyjmowała tylko trzy attachmenty. Gra używa dwóch. PID 19212 zamknięto. Nie traktować tej próby jako sukcesu stereo.
- Dodano wariant dwóch attachmentów przez natywne resize bez zmiany rozmiaru ani pozostałych ustawień; test potwierdza, że referencja wymienionego G-buffera nie jest zwalniana ponownie. Nadal odrzucamy niepotwierdzone E0/E8 i zmiany konfiguracji podczas sesji.
- Udana próba: `artifacts/fake-xr-20261028-212156-818.tsv.xr.txt` oraz `.fake.json`. Źródło 1280×720, 2 attachmenty, CC=1, C9=0. Własne pakiety przeszły etap pending → gotowe odrębne zasoby/FBO. **1739 poprawnych par w 30000 ms, 1 pusta klatka oczekiwania, 0 błędów, 3480 pobranych i oddanych obrazów.** Referencje własnych zasobów zostały zwolnione. Oba PNG obejrzano. PID 19940 zamknięto.
- W tej próbie średni odstęp między parami wyniósł 17.231 ms, CPU obu renderów 2.612 ms. To menu z atrapą OpenXR, nie pomiar kompozytora/gogli. `run-game-preview.ps1` domyślnie używa teraz własnych pakietów; dawny tryb porównawczy dostępny przez `-NativeEyeResources:$false`.
- Porównanie z oficjalnym nagłówkiem SDK wykryło błędne indeksy końców palców w adapterze runtime: 4/9 oznaczały stawy dalsze, właściwe końce to **5/10**. Poprawiono stałe i dokumentację oraz dodano `static_assert` względem enum SDK. Samodzielny `xr_demo` już używał poprawnych enumów. Wcześniejsze liczniki próbek syntetycznych dłoni nie dowodziły poprawnego gestu ani optycznego śledzenia.
- Równoległa analiza wykazała, że culling używa skalarnego FOV i wektorów kamer przed obecnym punktem podłączenia obrazu, a jedna z kamer zachowuje poprzednią transformację rozglądania. Pełne 6DoF wymaga wcześniejszego wprowadzenia pozy głowy i spójnej historii; szczegóły zostaną zapisane w `docs/culling-camera-plan.md`. Badana jest też natywna ścieżka wskazywania i aktywowania kontrolek, bez syntetycznego wejścia systemowego.

## Szkielety dłoni w końcowych teksturach XR

- `GlHandOverlay` jest dołączony po skopiowaniu obrazów gry i przed oddaniem obrazów XR. Rysuje tylko segmenty z aktywnych, ważnych i śledzonych pozycji stawów. Używa pozy i FOV każdego oka w tej samej przestrzeni LOCAL, prywatnych shaderów/VAO/VBO/FBO i zachowuje zmieniany stan hosta, w tym clip distances i stencil.
- Jest to diagnostyczny szkielet liniowy, bez zasłaniania przez geometrię świata, animowanej siatki i sterowania kontrolkami. Nie utożsamiać go z ukończoną obsługą dłoni w grze.
- Test GPU `gl_hand_overlay` sprawdza rzeczywiste piksele, paralaksę, stawy skrajne, utratę aktywności/ważności, NaN, błędne drugie oko, stan hosta i własność tekstur. Kompilacja i **6/6 CTest przeszły**.
- Pełny test w grze: `artifacts/fake-xr-20261028-213306-709.tsv.fake.json`, 1733 poprawne pary w 30032 ms, 1 pusta klatka oczekiwania na zasoby, 0 błędów i 3468 obrazów pobranych/oddanych. Obie testowe dłonie są widoczne w każdym oku: liczby pikseli cyan/orange 335/367 oraz 321/356. Końcowe obrazy `.fake.json.left.png` i `.right.png` obejrzano: scena menu wraz z obiema dłońmi. Wcześniejsze `.tsv.left.ppm`/`.right.ppm` zawierają tylko przechwycenie gry przed nałożeniem dłoni.
- PID 50036 został zamknięty po pomiarach. Test używał syntetycznych pozycji, nie Questa; śledzenie optyczne, rzeczywiste szczypnięcia i opóźnienie nadal nie są zweryfikowane.
- Zapisano konkretne dalsze miejsca integracji: `docs/culling-camera-plan.md` oraz `docs/cockpit-input-plan.md`. Dla kontrolek odnaleziono wybór promieniem, natywny dyspozytor press/release i kolejkę na wątek logiki. Następny etap interakcji to obserwacyjny pomiar w kokpicie, zanim zostaną wywołane działania z gestów.

Quest 3 jest potrzebny do testu sprzętowego: stereo w goglach, głowa i aktywne stawy obu dłoni. Analiza renderera gry może postępować bez gogli. Eksperymentalne dwa widoki menu wraz ze szkieletami dłoni są przekazywane do atrapy XR; VR rozgrywki i interakcje dłonią z kokpitem nadal są nieukończone.

## Historia wcześniejszych etapów

Poniższe wpisy zachowują wyniki i ograniczenia z momentu wykonania; bieżący stan opisano powyżej.

## Rozpoznanie lokalne

- CPU: Intel Core i5-13600KF; GPU według rejestru sterownika: NVIDIA GeForce RTX 5070. To identyfikacja, nie benchmark.
- Steam build Stormworks: `25122525`; EXE x64 `stormworks64.exe`.
- SHA-256 EXE: `f9206d85c82f4d02fd0ac391781d19c5c68394a9ffc48accca0e1f5966db8699`.
- Importy potwierdzają `OPENGL32.dll`, `wglGetProcAddress` i `SwapBuffers`.
- Inspekcja znalazła 195 plików shaderów. Występują m.in. `mat_view_proj`, macierze inverse/prev/next, pozycje kamery i macierze kamery wody. To kandydaci do analizy; żadnego hooka nie zweryfikowano.
- Aktywny runtime w rejestrze: SteamVR (`steamxr_win64.json`). Log identyfikuje SteamVR 2.18.4.
- Enumeracja OpenXR zakończona; `XR_EXT_hand_tracking` obecne. `xrCreateInstance` zwraca -2. Log klienta podaje `VRInitError_Driver_WirelessHmdNotConnected`.
- Nie można na tej podstawie potwierdzić `supportsHandTracking`, 6DoF ani aktywnych stawów. Test trzeba powtórzyć z połączonym zestawem.

## Weryfikacja

Rdzeń C++ kompiluje się MSVC 19.44 x64; testy sprawdzają transformacje kokpitu, rozstaw oczu, macierze widoku/projekcji, histerezę szczypnięcia, utratę śledzenia i fokusu, stare/nieprawidłowe dane. Diagnostyka gry została wykonana na lokalnej instalacji. Próba runtime jest nieudana z powodu wskazanego wyżej stanu połączenia.

## Ograniczenia środowiska pracy

Bezpośrednie pobieranie repozytorium przez git nie działało. W kolejnym kroku pobrano oficjalne nagłówki SDK 1.1.53 przez dostępne połączenie GitHub. Zapisano je wraz z licencją i SHA blobów w `vendor/OpenXR`. Ta przeszkoda została usunięta.

Pierwsze wywołanie SteamVR nie mogło utworzyć logu w katalogu Steam. Zastosowano procesową zmienną `VR_LOG_PATH` kierującą logi do `artifacts/steamvr-logs`; to umożliwiło enumerację rozszerzeń. Nie zmieniono systemowego runtime ani plików gry.

## Najbliższe kryterium odbioru

Po połączeniu Quest 3 uzyskać poprawny system OpenXR, dwie konfiguracje widoków i `supportsHandTracking`, następnie zweryfikować gotowy do testu demonstrator. Cel końcowy pozostaje nieukończony.

## Kolejny krok wdrożenia

- Poprzednia tura: postęp (utworzenie i testy rdzenia oraz diagnostyka). Bieżąca: postęp (oficjalne SDK i natywny demonstrator).
- Dodano `src/xr_demo.cpp`: dynamiczne ładowanie loadera OpenXR, sesja OpenGL, dwie swapchainy sRGB/depth, pętla zdarzeń/klatek, widoki oczu, stawy dłoni i detektor szczypnięcia. Scena jest niezależna od gry.
- Build MSVC i CTest przeszły. `--graphics-check` uruchomił prawdziwe OpenGL 4.6 NVIDIA 617.14 i potwierdził rysowanie panelu przez odczyt pikseli FBO. Nie testuje to stereoskopii ani gogli.
- Próba natywnego `xrCreateInstance` nadal zwraca -2; log `xrclient_swvr_xr_demo.txt` wskazuje `VRInitError_Driver_WirelessHmdNotConnected`. Pętla sesji, stereo i stawy nie zostały jeszcze wykonane na sprzęcie.
- Uruchomienie po połączeniu: `scripts/run-vr-demo.ps1 -Seconds 30`. Wyniki w `artifacts/native-demo-test.txt` i `artifacts/steamvr-logs`.

## Rozpoznanie renderera i tracer

Kolejna tura przyniosła postęp: analiza lokalnych shaderów ustaliła zależność oświetlenia od buforów geometrii oraz kamerowe zależności wody. Dodano `swvr_gl_trace.dll`, test GPU oraz `tools/summarize_trace.py`. Test uchwycił uniform `mat_view_proj`, 16 wartości macierzy, draw i swap oraz potwierdził poprawny zielony piksel FBO przy aktywnych hookach. Źródła MinHook przypięto do commitu i sprawdzono SHA blobów.

Pełny opis zakresu pomiaru i następnego kroku: `docs/renderer-integration.md`. Brak gogli nadal blokuje test VR na sprzęcie, ale nie zablokował prac nad integracją.

## Launcher — postęp i bieżący uchwyt

Dodano i skompilowano `swvr_trace_launcher.exe`. Test inicjalizacji DLL przed wejściem programu testowego przeszedł, dziecko zakończyło się kodem 0. Launcher następnie uruchomił Stormworks i zgłosił inicjalizację tracera przed wejściem gry: PID 44912, sesja narzędzia 69531, ślad `artifacts/stormworks-startup-01.tsv`.

Sesja 69531 pozostała aktywna przy ostatnim `write_stdin`. Nie należy restartować gry na podstawie samego upływu czasu. Nowe polecenia powłoki zwracają `CreateProcessAsUserW failed: 5 (Odmowa dostępu.)`; UI zgłasza niedostępny native pipe. Przyczyna nieustalona. Zawartość śladu i stan okna pozostają niezweryfikowane. Użytkownik dostał pytanie o widoczność menu/błędu gry. Tura przyniosła postęp, nie oznacza ukończenia celu.

## Ponowna weryfikacja blokady narzędzi

W kolejnej turze uchwyt sesji 69531 ponownie potwierdził działający launcher (bez nowego wyjścia). Próba odczytu śladu przez powłokę nadal kończy się błędem CreateProcessAsUserW 5, a `cua.getState()` nadal zgłasza niedostępny native pipe. Nie uruchamiano drugiej kopii gry. Poproszono użytkownika o zwykłe zamknięcie testowej gry, aby sprawdzić możliwość odzyskania dostępu; nie ma dowodu, że gra spowodowała awarię narzędzi.

Audyt blokady: to druga kolejna tura z tą samą przeszkodą (pierwsza obejmowała jeszcze ukończenie launchera). Bieżąca tura to zweryfikowane oczekiwanie, bez postępu implementacyjnego. Nie dopisujemy kolejnych niezweryfikowanych modułów. Jeżeli ta sama blokada zostanie potwierdzona w trzeciej turze i nie pojawi się bezpieczna droga dalszej pracy, oznaczyć goal jako blocked. Przed decyzją ponownie sprawdzić żywy uchwyt i dostęp narzędzi.

## Wynik trzeciej weryfikacji

Ta sama blokada została potwierdzona w trzeciej kolejnej turze: `write_stdin` nadal potwierdza żywą sesję 69531 bez nowego wyjścia; nowe polecenie odczytu śladu ponownie kończy się CreateProcessAsUserW 5; `cua.getState()` ponownie zgłasza niedostępny native pipe. Brak odpowiedzi użytkownika o oknie lub zamknięciu gry. Cel oznaczany jako `blocked`, nie jako ukończony. Nie przerwano ani nie uruchomiono ponownie procesu testowego.

Do wznowienia potrzebne jest odzyskanie dostępu do powłoki/okien. Użytkownik został poproszony o zwykłe zamknięcie testowej gry; nie ustalono, czy to przywróci narzędzia. Po wznowieniu najpierw sprawdzić sesję 69531 i aktualny proces, odczytać istniejący ślad i dopiero zdecydować o kolejnym uruchomieniu. Weryfikacja sprzętu VR wymaga także połączonego Quest 3.
