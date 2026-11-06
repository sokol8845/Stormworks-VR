# Stormworks VR — Quest 3

Eksperymentalny mod PCVR pod Windows x64, Meta Quest 3 i OpenXR ze śledzeniem głowy i gołych dłoni. Projekt jest w trakcie prób na goglach; obsługa pojazdu wymaga dalszej weryfikacji.

**Dla współtwórców:** [kompilacja, testy offline i praca na gałęziach](docs/development.md).

## Obecny stan

Najświeższy skrót: [stan pracy z 2026-11-06](docs/stan-pracy.md).
Dodano analogowe kręcenie kierownicą po oburęcznym chwycie oraz panel VR
połączony z małymi i dużymi klawiaturami numerycznymi pojazdów.
W najnowszej próbie użytkownik potwierdził skręt kół i puszczanie chwytu.
Naprawiono blokadę przycisków po OK klawiatury i dodano
[animację modelu kierownicy Driver seat](docs/driver-wheel-animation-20261106.md).
Aktualne testy offline: **56/56 CTest**, **51/51 Python**, **78 kontroli EXE**,
w tym obrót geometrii prawdziwego fotela na GPU. Użytkownik potwierdził potem
działanie animacji kierownicy w goglach. Dodano też
[przełącznik chodzenia tylko do przodu i poprawkę początkowego kierunku ciała](docs/forward-only-and-heading-20261106.md),
sprawdzone offline. Obsługa i ograniczenia są
na początku `docs/stan-pracy.md`. Starsze opisy poniżej są historyczne.
Potwierdzono w goglach stereo bez mrugania, ruch ciała/rąk, przyciski,
dźwignię typu 4 prawą dłonią oraz obrót podczas chodzenia. Najnowsze prace
offline dodają ostrzeżenia gotowości OpenXR, testowany automat utrzymania
T-pose i opcjonalne [chodzenie/sprint lewą dłonią](docs/hand-locomotion.md)
(`-HandLocomotion`) oraz drugi wariant biegu: dwie pięści (`-ArmSwingRun`, bez wymachów);
Goal offline 1–5 dodaje też konfigurację gestów bez rekompilacji, lewą dłoń/Q,
podpowiedzi w goglach i raport po próbie. [Zakres](docs/offline-goal-20261104.md).
Następny [goal wygody obsługi](docs/offline-usability-goal-20261104.md) dodaje
postęp T-pose, zapis proporcji ciała, zmianę ustawień w sesji, nagrania śledzenia
i lokalny prototyp klawiatury numerycznej.
Kolejne wdrożenie dodaje menu VR, wybór dłoni, wizualny replay z pełniejszym
dziennikiem wejścia i testowany, jeszcze niepodłączony adapter klawiatur 5/6.
[Obsługa i podgląd](docs/offline-controls-20261104.md).
Po pierwszej próbie menu dodano dotyk palcem, widoczny przycisk MENU i ochronę
gestów systemowych. Po drugiej próbie odsunięto panel na 1 m, dodano wskazanie
przez 0,8 s i usunięto pinch z menu oraz ruchu. Joystick chwyta się całą dłonią.
Preset [`scripts/test-vr.cmd`](scripts/test-vr.cmd) uruchamia autosave i VR;
preflight `start-vr-test.ps1 -DryRun` działa bez gry.
Ostatnie wdrożenie offline dodaje stabilne uśrednianie kalibracji, regulację
obrotu w menu, trwałe preferencje, replay obrotu/menu i raport CPU z P95/P99.
Preset przywraca zapisany tryb ruchu. [Obsługa](docs/offline-controls-20261104.md).
Najnowsze [rozszerzenie komfortu VR](docs/vr-comfort-20261105.md) dodaje ukrywanie
dłoni bez fokusu, opcjonalny ruch głowy 1:1 z wygaszaniem, regulację czasu wskazania
i skali menu, potwierdzenia kliknięć, obrót skokowy, winietę, profil siedzący
i pomiary GPU/budżetu klatki. Nowe strony: OBROT / MENU → INTERFEJS / KOMFORT.
Płynny obrót i dwie pięści pozostają domyślne.
Po próbie 2026-11-05 kalibracja jest potwierdzona przez użytkownika.
[Najnowsza poprawka menu](docs/menu-help-20261105.md) dodaje **? → POMOC**,
natychmiastowe zastosowanie odległości/skali/czasu i stabilniejsze wskazywanie.
Release, **51/51 CTest**, **51/51 Python** i **68 guardów EXE** przechodzą.
Zmiany menu czekają na kolejną próbę. Poniższy opis zachowuje starsze wyniki.

Stan z 2026-11-02: wdrożono profil zainstalowanego builda Steam **26294447**, SHA `c38c03ff…`, oraz opcjonalne powiązanie awatara z głową i dłońmi VR (`-AvatarPose`). Pełny Release build, **27/27 CTest**, **20/20 testów Python** i **50 kontroli sygnatur i referencji EXE** przeszły. [Zakres portu](docs/game-profile.md), [implementacja awatara i wyniki prób](docs/status.md).

Próba menu z atrapą OpenXR przesłała **3471 poprawnych par stereo w 30 s**. Następnie próba kokpitu na Queście przesłała **7488 par** i potwierdziła natywne wskazywanie dłonią. Użytkownik ocenił orientację jako poprawną, ale zgłosił wychodzenie kamery poza ciało przy obrocie i wychylaniu głowy. Próba zatrzymała się po 95 s przy zmianie targetu na 1×1; poprawka przeszła testy offline. Późniejszy `AvatarPose` przesłał **6760 par w 120 s**, z 54120 rysowaniami pozy i obiema rękami. Próba nie zaliczyła kontroli głębokości (`depth_draws=0`), a ograniczenie zasięgu było częste. Dodano jawną kalibrację proporcji rąk i poprawkę wykrywania uniformów, zweryfikowane poza grą; wygląd i pokrycie depth wymagają ponownej próby. HandButtons również wymaga testu gry.

- Działa biblioteka C++ `swvr_core`: transformacje położenia oczu do kokpitu, macierze widoku i asymetrycznej projekcji OpenGL.
- Działa detektor szczypnięcia: histereza, anulowanie po utracie śledzenia/fokusu, odrzucanie starych danych i ponowne uzbrojenie otwartą dłonią. Wymaga późniejszej kalibracji na prawdziwych dłoniach.
- Działa diagnostyka instalacji Steam/Stormworks, importów EXE, shaderów i możliwości OpenXR. Skrypt Python używa ograniczonego ABI `ctypes`; nie jest pętlą renderowania VR.
- Natywny `swvr_xr_demo` przeszedł test na Queście 3 przez Steam Link: obraz stereo, śledzenie pozycji/obrotu głowy, obie dłonie i szczypnięcia. Użytkownik potwierdził widoczność sceny i dłoni.
- Adapter kamery i hook zabezpieczony SHA wersji przeszły pomiary w rzeczywistym kokpicie: 328 pełnych próbek, 0 błędów relacji VP = P × V. Transformacje zachowują precyzję pozycji świata oraz odbicie układu osi gry.
- Rzeczywisty obraz Stormworks dotarł do Questa przez Steam Link; użytkownik potwierdził świat w goglach. Menu/HUD nadal pozostają na monitorze. [Stan pomiarów](docs/status.md).
- Wcześniejszy tryb Gameplay przesłał 9768 par w dwie minuty i wykonał wspólny culling oczu. Użytkownik zgłosił wtedy przechylenie i widok wnętrza głowy. Po poprawkach kalibracji, punktu widzenia i maski próba z 2026-11-02 potwierdziła poprawną orientację, ale nie dopasowanie kamery do ciała. Nie traktować tych wyników jako poprawnego, grywalnego 6DoF.
- `swvr_gl_trace.dll` ma odrębne, chronione profilem tryby Menu/Gameplay oraz ręczne włączenie VR już po wejściu do pojazdu. Chodzenie pozostaje niewdrożone; eksperymentalna obsługa przycisków gestami jest podłączona i testowana poza grą. Sonda potwierdziła natywną drogę ręcznego press/release, a most promienia dłoni ma testy precyzji i komunikacji między wątkami. [Ograniczona próba](docs/game-preview.md), [integracja renderera](docs/renderer-integration.md).
- Opcjonalne `-HandRay` podłącza prawy palec do natywnego wyboru celu, przywraca kierunek gry po wywołaniu i pokazuje zielony promień diagnostyczny. Backend przeszedł testy izolowane oraz częściową próbę kokpitu na Queście: log po starcie VR zawiera 5766 wyników `hand_ray`, w tym 2595 próbek z niezerowym celem. Nie wysyła kliknięć. Most gestów rozróżnia kolejne szczypnięcia oraz wiąże rozpoczęcie z konkretną selekcją. [Wskazywanie](docs/native-hand-ray.md).
- Opcjonalne `-HandButtons` w Gameplay włącza również HandRay i łączy szczypnięcie prawej dłoni z natywnym naciśnięciem przycisku typu Push (`button_type=0`). Backend ponownie sprawdza cel, świeżość gestu, pauzę/UI i konflikt z fizycznym wejściem przy wykonaniu akcji. Zwolnienie używa dedykowanego requestu type10 oraz sprzątania własnego lokalnego stanu trzymania; hooki retirement rozliczają własny request/payload po rozpoznanej ścieżce WRITE lub localhost. Test integracyjny poza grą obejmuje zwykłe zwolnienie, utratę fokusu, pauzę w FIFO i obce wejście. **Obsługa nie została jeszcze zweryfikowana w Stormworks ani na goglach.** Opcja wyklucza InputProbe i InteractionProbe. [Próba i ograniczenia](docs/game-preview.md), [kod zwolnienia](docs/native-button-request.md), [ABI](docs/native-button-bindings.md), [własność requestów](docs/native-button-retirement.md).
- Osobne natywne pakiety celów renderowania oczu działają w eksperymencie menu: pełna próba 30 s przesłała 1739 poprawnych par i zwolniła referencje własnych zasobów. To nie izoluje jeszcze całego stanu renderera ani historii kamer. [Analiza zasobów](docs/eye-resource-plan.md).
- Końcowe obrazy XR zawierają już proste szkielety śledzonych dłoni; test syntetycznych dłoni w grze przeszedł 30 s i został sprawdzony wizualnie. Nakładka nie uwzględnia zasłaniania dłoni przez geometrię; interakcję z przyciskami obsługuje osobny, opcjonalny backend HandButtons. [Nakładka dłoni](docs/hand-overlay.md).
- Opcjonalne `-AvatarPose` dobiera szkielet lokalnego survivor male/female, kalibruje punkt oczu względem kości głowy i prowadzi nadgarstki przez IK bark–łokieć–dłoń. Obsługuje uproszczone zgięcie palców oraz przebiegi koloru/głębokości, przywracając natywne uniformy po rysowaniu. Tułów reaguje na wychylanie; biodra/nogi pozostają w animacji gry. Są testy matematyczne, tożsamości i GPU. Dopasowanie wyglądu wymaga teraz pomiaru kokpitu. [Uruchomienie](docs/game-preview.md).

## Kompilacja i testy

Wymagane: CMake 3.24+, Visual Studio 2022 Build Tools z C++, Windows SDK. Python 3.10+ x64 do diagnostyki. Rdzeń nie wymaga pobierania zależności.

```powershell
.\scripts\build.ps1
```

Powstają `build/Release/swvr_core.lib`, `swvr_core_tests.exe` i `swvr_xr_demo.exe`; skrypt uruchamia CTest. Testy pozostają aktywne w Release (nie używają wyłączanych makrem `assert`). Oficjalne nagłówki OpenXR SDK 1.1.53 są w `vendor/OpenXR` wraz z licencją i pochodzeniem plików. Loader jest odczytywany z instalacji SteamVR.

CTest zawiera również `native_hand_buttons`, `native_button_retirement` i `native_hand_buttons_integration`. Testy te korzystają z fixture poza grą. Weryfikacja z 2026-11-01: pełny Release build `build/capture`, **23/23 CTest**, **13/13 testów Python** i osobny test GPU/DLL tracera przeszły. Naprawiono sprzątanie własnego eventu anulowanego tuż przed przekazaniem do kolejki; test regresji sprawdza brak wycieku i możliwość przyjęcia kolejnego gestu. Raport: [offline-tests-20261101.xml](artifacts/offline-tests-20261101.xml). Wcześniejszy wynik 19/19 z 2026-10-29 pozostaje wynikiem historycznym. Nie uruchamiano Stormworks ani próby gogli.

Późniejsze rozszerzenie HandButtons z tego samego dnia dodaje 18 izolowanych scenariuszy cyklu życia w istniejącym teście jednostkowym. Backend rozpoznaje zmianę aktywnej postaci przy tym samym kontrolerze, anuluje trzymanie po otwarciu pauzy/UI i wykrywa utratę własnego celu. Sprawdza też kontekst po natywnych callbackach. Zmiana świata/postaci z nierozliczonym trzymaniem pozostawia blokadę z przyczyną `last_fault`; nie potwierdza zwolnienia w starej scenie. Pełna kompilacja Release i **23/23 CTest** ponownie przeszły: [raport cyklu życia](artifacts/offline-lifecycle-tests-20261101.xml). Liczba CTest obejmuje programy testowe, a nowe scenariusze wykonują się wewnątrz nich.

Wdrożenie z 2026-11-02 dodaje zwolnienie własnego przycisku przed rozpoznaną natywną deaktywacją symulacji i zmianą lokalnej postaci. Reset/destrukcja źródłowej kolejki rozlicza request i payload oddzielnie, bez podwójnego delete. Nierozpoznane lub już utracone konteksty nadal dają blokadę. Pełny Release build i **23/23 CTest** przeszły: [raport przejść](artifacts/offline-transitions-tests-20261102.xml). Nie testowano tego w grze ani na goglach.

## Natywna scena testowa

Po połączeniu Quest 3 przez Steam Link:

```powershell
.\scripts\run-vr-demo.ps1 -Seconds 30
```

Program wyświetla siatkę w metrach, zielony panel i punkty stawów dłoni. Używa przestrzeni LOCAL, osobnych projekcji oczu oraz tego samego `predictedDisplayTime` do widoków i dłoni. Otwórz dłoń przed szczypnięciem, żeby uzbroić gest. Zdarzenia gestów i liczby klatek/próbek są wypisywane w konsoli. To demonstrator, bez interakcji z grą. Kod wyjścia 0 wymaga przesłania obrazu stereo i przynajmniej jednej śledzonej próbki głowy oraz każdej dłoni; nie zastępuje wizualnej oceny, testu płynności ani potwierdzenia optycznego śledzenia z odłożonymi kontrolerami.

Test samego renderera bez uruchamiania OpenXR:

```powershell
.\build\Release\swvr_xr_demo.exe --graphics-check
```

## Diagnostyka gogli

Preferowany pierwszy transport dłoni: **Quest 3 → Steam Link → SteamVR**. Źródła i ograniczenia Quest Link: [rozpoznanie](docs/research-vr.md).

1. Uruchom SteamVR na PC i połącz Quest przez Steam Link w goglach.
2. Włącz śledzenie dłoni w goglach i przekazywanie dłoni w Steam Link, jeśli dostępne; odłóż kontrolery.
3. Uruchom `scripts/diagnose.cmd` lub:

```powershell
python tools/diagnose.py
```

Raporty trafiają do `artifacts/diagnostics.json`, `game-report.json`, `openxr-report.json` i `steamvr-logs/`. Skrypt nie zmienia rejestru runtime ani plików gry; samo wywołanie OpenXR może uruchomić usługę SteamVR. Limit czasu procesu diagnostycznego wynosi 30 s. `--skip-xr` pomija wywołania runtime; `--game-dir` i `--loader` pozwalają wskazać nietypowe instalacje.

`hand_extension=true` potwierdza wyłącznie dostępność API. Dopiero `supports_hand_tracking=true`, a później sesja z aktywnymi stawami, potwierdzają kolejne poziomy obsługi. `capabilities_only` **nie** oznacza działającego obrazu VR lub śledzenia dłoni.

## Plan kolejnych prac

1. Powtórzyć pełny pomiar Gameplay po poprawce przejściowych targetów 1×1, także z powrotem do normalnego rozmiaru. Ocenić nowy limit głowy (domyślnie włączony, `-NoHeadLimit` wyłącza) i cienie awatara (`depth_draws`); prace offline z 2026-11-03: [cele offline](docs/goals-offline.md).
2. Powtórzyć `AvatarPose` po jawnej kalibracji T-pose i poprawce uniformów: powiązanie żywych palet lokalnej postaci, punkt oczu, proporcje modelu, IK, palce, utratę śledzenia i zmianę przestrzeni. Nowe zmiany przeszły testy poza grą; wynik nie potwierdza jeszcze dopasowania w kokpicie.
3. Sprawdzić promień na poruszającym się pojeździe oraz eksperymentalny HandButtons: naciśnięcie/trzymanie/zwolnienie Push, utratę śledzenia, pauzę/UI, zmianę postaci, konflikt z fizycznym wejściem i rozliczenie requestów. Nieznane teardown i niepewna własność pozostają ograniczeniami; testy poza grą nie potwierdzają działania kokpitu.
4. Dodać chodzenie i wejście do pojazdu oraz interfejs w VR. Zweryfikować culling, wodę, odbicia, efekty temporalne, skalę i komfort.
5. Zmierzyć wydajność oraz przygotować instalację i pakowanie moda dopiero po tych próbach.

Szczegóły lokalnego pomiaru i ograniczenia: [stan projektu](docs/status.md).
