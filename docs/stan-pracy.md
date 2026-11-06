# Stan pracy — Stormworks VR (2026-11-06)

## Animacja Driver seat wdrożona

Widoczna kierownica standardowego Driver seat jest teraz powiązana z kątem
dłoni: obręcz, szprychy i piasta obracają się wokół pochylonej osi, a fotel
i kolumna pozostają nieruchome. Działa z istniejącym presetem, z jedną lub
dwiema trzymającymi dłońmi; po puszczeniu wraca do zera. Dodano obsługę
normalnych, głębi/cieni i historii ruchu wspólnej dla obu oczu.

Release, **56/56 CTest**, **51/51 Python**, **78 kontroli EXE** i osobny
test DLL/renderera przechodzą. Test GPU sprawdził wszystkie 2558 wierzchołków
prawdziwego modelu z shaderami gry, w tym niezmienność fotela i kolumny.
Gra pozostaje zamknięta; animacja podczas jazdy wymaga próby w goglach.
[Implementacja, dowody i lista próby](driver-wheel-animation-20261106.md).

## Najnowsza próba 18:27: koła działają, poprawiono blokadę po OK

Użytkownik potwierdził skręt kół i puszczanie kierownicy. Przyciski pojazdu
blokowały się po **OK klawiatury**, a menu VR pozostawało sprawne.
Odtworzono błąd: natywne otwarcie klawiatury zostawia wskaźnik trzymania
i wciśnięte Q/E, których Clear24 nie zwalnia. Adapter czyści teraz własny
stan przy otwarciu, zachowując sesję klawiatury. Regresja najpierw nie
przechodziła; po poprawce przechodzi, podobnie jak integracja obu dłoni
i obu wielkości klawiatury. Release, **53/53 CTest**, **51/51 Python**
i **76 kontroli EXE** przechodzą. Wymagana ponowna próba OK/ANULUJ →
przycisk pojazdu. Gra jest zamknięta.

Grafika kierownicy była nieruchoma podczas tej próby; późniejsze wdrożenie
opisano powyżej.
Dowody, testy i pozostała praca:
[blokada po klawiaturze i stan kierownicy](keypad-release-fix-20261106.md).
Lokalna zmiana prędkości obrotu użytkownika z 45 na 60 została zachowana.

## Wcześniejsza poprawka: kliknięcia przy ruchu głowy i analogowy skręt

Użytkownik doprecyzował: nie działały **przyciski pojazdu**, nie obracała się
ani kierownica, ani koła. Naprawiono dwa odtworzone błędy: reset kliknięcia
przez ciągłą korektę widoku głowy oraz brak flagi włączającej analogowy
skręt fotela. Regresje najpierw odtwarzały błędy, następnie przeszły po poprawkach.
Szczegóły, ograniczenia testów i lista kolejnej próby:
[diagnoza kokpitu](cockpit-fixes-20261106.md).
Nie uruchamiano ponownie gry. Weryfikacja rzeczywistego pojazdu pozostaje otwarta.

Weryfikacja końcowa: czysta kopia plików repozytorium kompiluje się w Release,
**53/53 CTest**, **51/51 Python**, **76 kontroli EXE** i osobny test DLL/renderera
przechodzą. Test menu tworzy teraz własny katalog tymczasowy, dzięki czemu
nie zależy od plików wcześniejszych prób. Poprawiona DLL jest w `build/capture`.

Kod przygotowano do wspólnej pracy na
<https://github.com/sokol8845/Stormworks-VR>;
[instrukcja dla współtwórców](development.md).

Próbę `headset-xr-20261106-172903-416` zakończono kontrolowanie na prośbę
użytkownika; gra jest zamknięta. Użytkownik zgłosił całkowity brak obracania
kierownicy oraz duże problemy z naciskaniem mimo wielokrotnych T-pose
i ponownego ustawiania widoku Questa. **Nowego sterowania nie traktować
jako potwierdzonego w goglach.**

Log: 17501 par obrazów, 5 przyjętych kalibracji, 2808 pakietów kierownicy,
24 zwolnienia osi; 4 przyjęte akcje dłoni, w tym otwarcie klawiatury,
3 zwolnienia przycisków. Sesja klawiatury: opened=1, submitted=1, faults=0.
Same liczniki requestów nie potwierdzają skrętu, animacji kierownicy ani
wartości po stronie pojazdu. Faktyczne wartości osi i przyczyny wszystkich
odrzuconych interakcji nie zostały zapisane.

Odtworzenie paneli: `swvr_replay_controls.exe` dla tej sesji dało 8360
kontroli obrotu, 7789 paneli, 0 rozbieżności; wynik w
`artifacts/cockpit-clicks-replay-20261106.tsv`. To zgodność z dotychczasowym
zachowaniem, **nie** test wykrywający objaw użytkownika. Oba nagrania mają
limit 64 MiB i obejmują tylko początek próby. Analiza pomocnicza w
`tmp/cockpit_session_stats.py`. W zarejestrowanej selekcji 6652 próbek użyło
promienia dłoni, 3071 wróciło do wzroku przy niedostępnej dłoni.

Wyjaśnienie użytkownika oraz ustalone później przyczyny i poprawki zapisano
na początku tego dokumentu. Poniższy opis dotyczy wcześniejszego wdrożenia.

Dodano kierownicę chwytną oraz natywne klawiatury numeryczne pojazdów.
To wdrożenie jest sprawdzone offline; nowa obsługa kokpitu wymaga próby w goglach.
Poprzedni test VR z 2026-11-06 uruchomił się po odzyskaniu fokusu SteamVR;
użytkownik potwierdził działanie i poprosił o zamknięcie gry. Gra jest zamknięta.

- **Kierownica:** zajmij fotel sterujący, pokaż otwarte dłonie, następnie
  zaciśnij obie po bokach kierownicy (rozstaw 16–65 cm, przed sobą).
  Pierwszy chwyt ustala środek i pozycję neutralną; nie ma jeszcze dopasowania
  do geometrii konkretnego modelu kierownicy. Obrót w prawo/lewo przekazuje
  analogową oś 1 fotela (standardowo A/D); ±90° daje pełne wychylenie.
  Po złapaniu można obracać jedną dłonią. Puszczenie obu zwalnia oś.
  Fizyczne A/D lub kontroler mają pierwszeństwo. Zmiana fotela, pauza,
  brak świeżego śledzenia lub fokusu anuluje chwyt.
- **Klawiatury pojazdów:** aktywuj Small/Large Keypad jak przycisk dłonią
  lub natywnym Q/E. W goglach automatycznie pojawi się panel powiązany
  z tą sesją klawiatury. Cyfry działają dotykiem lub wskazaniem z paskiem.
  Pierwsza cyfra zastępuje obecną wartość. **POLE 1 / 2** zmienia edytowany
  kanał dużej klawiatury; **OK** wysyła obie liczby przez requesty type11,
  **ANULUJ** niczego nie wpisuje. Mała klawiatura zachowuje ukryty kanał.
  Zmiana sesji/postaci, utrata fokusu, pauza przed OK lub zewnętrzna
  aktualizacja klawiatury blokują zatwierdzenie starego wpisu.
- Przycisk KLAWIATURA w menu opisuje otwarcie komponentu pojazdu.
  Stary lokalny prototyp jest dostępny wyłącznie przez dotychczasowy
  diagnostyczny skrypt `toggle-vr-keypad.ps1`.

Preset z `HandButtons` i ruchem dłoni instaluje nowe moduły automatycznie.
Weryfikacja: Release bez ostrzeżeń, **53/53 CTest**, **51/51 Python**,
**76 guardów EXE**, osobny test GPU/DLL tracera oraz obejrzany render
`artifacts/vehicle-keypad-20261106.png`. Testy obejmują native tick/UI,
obie wielkości klawiatur, częściowe alokacje/handoff, nowe Show/Hide/Update,
chwyt jedną/dwiema dłońmi, skręt, zwolnienie i zmianę fotela.
Nie uruchamiano ponownie Stormworks po tym wdrożeniu.

Kod: `hand_steering`, rozszerzony `native_locomotion`, `native_vehicle_keypad`,
`numeric_keypad`, osobna ścieżka otwierania 5/6 w `native_hand_buttons`.
Protokół klawiatury opisuje `native-keypad-integration-research-20261104.md`;
wcześniejszy brak dowodu właściciela frontendowego uzupełnia analiza
`artifacts/cockpit-frontend-evidence-20261106.json`: `84D010(owner, graphics, D)`
wywołuje `7B6060(owner+CE190, graphics, D, D+280, owner+CE6B8, event)`.
Submit i natywne Cancel4 wykonują się tylko w tym aktualnym kontekście.

Mod VR dla Stormworks: DLL `swvr_gl_trace.dll` wstrzykiwany do
`stormworks64.exe` (Steam build 26294447), obraz przez OpenXR → SteamVR →
Steam Link → Quest 3. Szczegółowy dziennik prób: `docs/status.md`; cele
offline: `docs/goals-offline.md`.

Historyczny stan buildu z 2026-11-05: Release `build\capture`, **51/51 CTest**, **51/51 Python**,
68 guardów profilu EXE (`tools/verify_game_profile.py`).

Po próbie `headset-xr-20261105-131205-804` użytkownik potwierdził poprawną
kalibrację głowy i ramion. Zgłosił brak widocznej reakcji regulacji menu,
niepewne kliknięcia i potrzebę objaśnień. Dodano **? → POMOC** na każdej stronie:
wskazanie/dotyk pokazuje opis bez wykonywania akcji. Odległość, skala i czas
stosują się teraz w otwartym panelu; krótkie zejście promienia wstrzymuje postęp,
a pomarańczowy wskaźnik mówi o konieczności odsunięcia po kliknięciu.
Regresje offline przeszły. Gry ponownie nie uruchamiano.
[Obsługa, diagnoza i dowody](menu-help-20261105.md).

Najnowsze pięć rozszerzeń VR wdrożono offline: ukrywanie dłoni po utracie fokusu,
wariant głowy 1:1 z wygaszaniem, wyraźne kliknięcia/regulacja skali i czasu
wskazania, pomiary GPU i budżetu klatki, opcjonalna winieta, skoki obrotu oraz
profil siedzący z recenter. Nowe strony w OBROT / MENU: INTERFEJS i KOMFORT.
Dotychczasowy obrót płynny, dwie pięści i ogranicznik głowy pozostają domyślne;
winieta i dźwięk są wyłączone. [Obsługa i wyniki](vr-comfort-20261105.md).
Gry nie uruchamiano; odsłuch, komfort i wydajność PCVR wymagają próby w goglach.

Wdrożenie offline 2026-11-05: kalibracja uśrednia stabilny odcinek T-pose
i odrzuca skoki śledzenia; OBROT / MENU reguluje prędkość, strefę i zakres
obrotu oraz odległość panelu. Zmiany menu zapisują się do INI, preset odczytuje
ostatni tryb ruchu, dostępne jest przywrócenie domyślnych ustawień.
Nagranie `.controls.tsv` odtwarza obrót i działania paneli; raport
`.performance.json` zawiera P95/P99, najwolniejsze klatki, koszty CPU i cache.
[Obsługa i ograniczenia](offline-controls-20261104.md), [wyniki](status.md).
Gry nie uruchamiano; komfort i zachowanie na Queście wymagają próby.

Przegląd 2026-11-05: poprawiono uruchamianie niewidocznego dwell drugiej dłoni,
zwalnianie chwytu obrotu przy utracie śledzenia w powtórzonej próbce oraz
synchronizację pierwszego małego obrotu postaci i widoku. Cache avatara
wykorzystuje identyczne macierze z różnych zweryfikowanych buforów: w pomiarze
syntetycznym 400 operacji koszt spadł z 5,59 do 2,26 ms. Pełne testy przeszły;
zysk FPS i zachowanie na goglach pozostają niezmierzone. [Wyniki](status.md).

Po próbie `headset-xr-20261104-220955-549` wdrożono pięć poprawek:
druga dłoń w JOYSTICK płynnie obraca postać i widok (domyślnie do 60°/s), T-pose
nie wymaga kierunku z początku sesji i resetuje punkt głowy/karku, menu
toleruje drżenie wskaźnika i szybkie dotknięcia, MENU jest nad oczami,
a panele zachowują pion. Powtórny wybór KALIBRUJ rozpoczyna nową próbę.
**Te poprawki są zweryfikowane offline; kolejna próba w goglach pozostaje
do wykonania.** Gra nie została ponownie uruchomiona.
[Szczegóły i dowody](status.md), [obsługa](hand-locomotion.md).

Po drugiej próbie uproszczono sterowanie: **dwie pięści = bieg**, bez wymachów
ani wstępnego otwierania rąk. Joystick chwyta się **całą dłonią**, bez pinchu.
Menu jest 1 m przed głową; działa dotyk lub wskazanie przez 0,8 s z paskiem
postępu. Ochrona Questa nie odrzuca już zamkniętej pięści tylko za orientację
wnętrzem do głowy. Nowe zachowanie jest sprawdzone offline, nie w goglach.
[Obsługa](hand-locomotion.md), [menu i nagrania](offline-controls-20261104.md).

Preset [`scripts/test-vr.cmd`](../scripts/test-vr.cmd) / `start-vr-test.ps1`
uruchamia grę z autosave i automatycznym VR. Domyślnie wybiera zapisany tryb
(`Saved`, a przy braku preferencji Fists); `-Movement Joystick` wymusza chwyt joysticka. Poprzednia próba automatycznie
weszła do świata i VR. Gra została zamknięta na polecenie użytkownika.

Po przeglądzie poprawiono wyścig odczytu czasu przerywający bieg, wybór drugiej
dłoni przy częściowej utracie śledzenia preferowanej oraz nakładające się
znaczniki odtwarzacza. Geometria znaków jest przechowywana i ponownie używana;
syntetyczny pomiar tekstu menu: 0,991 → 0,105 ms CPU (około 9,5× szybciej).
Obrazy menu i klawiatury z testu GPU są identyczne piksel po pikselu.
[Pomiary](../artifacts/review-fix-performance-20261104.json),
[regresje](../build/capture/artifacts/review-fixes-20261104.xml).

Najnowsze pięć zadań wdrożono offline: menu ustawień w goglach, wybór dłoni,
wizualny replay, dziennik wejścia z blokadami natywnymi oraz przygotowanie
testowanego adaptera klawiatur 5/6. Adapter pozostaje niepodłączony do gry;
lokalna klawiatura nie wysyła wartości do Stormworks.
[Obsługa, podgląd i ograniczenia](offline-controls-20261104.md).

Ostatni goal **1–5 dotyczący wygody obsługi wykonano offline**:
pasek i wskazówki T-pose, zapis proporcji ciała między uruchomieniami,
przeładowanie ustawień/trybu ruchu, nagrywanie i odtwarzanie śledzenia oraz
lokalny prototyp klawiatury numerycznej.
[Obsługa, podgląd i wyniki testów](offline-usability-goal-20261104.md).

Poprzedni goal obejmował zwalnianie ruchu przed zmianą postaci/sceny,
konfigurację INI, lewą dłoń/Q, podpowiedzi biegu i raporty po próbie.
[Poprzedni zakres](offline-goal-20261104.md).

Dwa warianty ruchu pozostają opcjonalne: **`-HandLocomotion`** (cała pięść wybranej dłoni,
domyślnie lewej) albo **`-ArmSwingRun`** (dwie zamknięte pięści; stara nazwa flagi). Oba włączają BodyYaw. Progi można
dobierać bez rekompilacji i przeładować w aktywnej sesji przez `set-vr-locomotion.ps1`. [Obsługa i ograniczenia](hand-locomotion.md).
Nowe zachowanie wymaga pierwszej próby w goglach; requesty w fixture nie
potwierdzają faktycznego ruchu ani komfortu.

Typowe uruchomienie próby (zawsze za zgodą użytkownika):

```powershell
.\scripts\run-game-preview.ps1 -Scene Gameplay -AvatarPose -HandButtons -BodyYaw `
  -ManualStart -ManualStop -QuietTrace -TimeoutSeconds 600 -WindowMode Windowed
.\scripts\start-vr-preview.ps1 -GameProcessId <PID>   # po wejściu do kokpitu
.\scripts\stop-vr-preview.ps1  -GameProcessId <PID>   # koniec pomiaru
```

## Działa (potwierdzone w goglach)

| Obszar | Stan |
|---|---|
| Obraz stereo w goglach | Działa; bez czarnego mrugania (ponowne wysyłanie ostatniej klatki z jej pozą). |
| Ciało postaci za głową/rękami (`-AvatarPose`) | Głowa, tułów i ramiona podążają za goglami i dłońmi; także cień i druga kopia ciała w fotelu (dłonie nie zostają na kierownicy). |
| Limit głowy | Kamera nie wychodzi poza ciało przy wychylaniu (wyłączany `-NoHeadLimit`). |
| Przyciski Push / Toggle dłonią (`-HandButtons`) | Szczypnięcie naciska przycisk; 28/28 przyjętych w ostatniej próbie, 0 błędów. |
| Throttle lever (przycisk typu 4) | Przesuwa się szczypnięciem prawej dłoni. |
| Celowanie wzrokiem | Gdy promień dłoni jest niedostępny, zaznaczenie idzie za wzrokiem (Q/E z klawiatury działa). |
| Obrót postaci za goglami poza pojazdem (`-BodyYaw`) | Postać i kierunek chodzenia podążają za głową. |
| Obrót myszą przy chodzeniu | Naprawiony 2026-11-03: gra dodaje obrót myszy między klatkami; teraz obraca całą przestrzeń zamiast być cofany. Potwierdzony przez użytkownika. |
| Ręczny start/stop pomiaru | `-ManualStart` / `-ManualStop` + skrypty start/stop. |
| Kalibracja T-pose — informacja zwrotna | Ramka w środku widoku: żółta (trzymaj), zielona (przyjęta), czerwona (odrzucona). |
| Kalibracja głowy i ramion, próba 2026-11-05 | Użytkownik potwierdził: „ładnie kalibruje”; log zawiera przyjętą T-pose i recenter. |
| Położenie ciała po kalibracji | „W miarę ok” — T-pose ustawia tylko skalę i wysokość barków. |

## Nie działa / niepotwierdzone

| Problem | Stan / hipoteza |
|---|---|
| Chodzenie/sprint bez klawiatury (`-HandLocomotion`) | **Wdrożone i testowane offline**: gest, natywne requesty, pauza/fotel/UI, zwolnienie, konflikt z fizycznymi klawiszami. Faktyczny ruch i komfort wymagają pierwszej próby. |
| Bieg z dwóch pięści (`-ArmSwingRun`) | Uproszczony offline po zgłoszeniu użytkownika: obie pięści zamknięte uruchamiają sprint, jedna otwarta zatrzymuje. Bez wymachów i strefy przy ciele. Nowa wersja czeka na próbę. |
| **Ręce postaci za krótkie** względem dłoni szkieletowych | Poprawka offline: długość ramion uwzględnia wysunięcie dłoni do przodu w T-pose (wcześniej 77% drawów z ograniczonym zasięgiem). **Nieprzetestowane w goglach** — w ostatniej próbie kalibracja nie zaszła. |
| Kalibracja T-pose i dopasowanie ramion | W próbie 2026-11-04 automat przyjął **4 kalibracje** i zapisał proporcje. Log potwierdza działanie detektora, ale użytkownik nie potwierdził jeszcze dopasowania długości rąk. Pasek, wskazówki, tolerancja przerw i profil pozostają dostępne. |
| Dźwignia w drugą stronę (lewa dłoń / Q) | **Wdrożone i testowane offline**: pinch lewej dłoni → Q (19), prawy → E (20), osobne pola przytrzymania/zwolnienia i arbitraż jednego celu. Do sprawdzenia w goglach. |
| Klawiatury numeryczne (typy 5/6) i inne typy przycisków | Natywne typy 5/6 nadal odrzucane. Lokalny panel VR działa offline. Protokół dwóch requestów typu 11 i adapter są testowane; podłączenie czeka na potwierdzenie właściciela/cyklu życia panelu. OK lokalnego panelu nie wysyła wartości do gry. |
| Opóźniony start obrazu | Dodano offline `#openxr_readiness` i komunikaty supervisora: przyczyna po 5 s, ponawianie najwyżej co 30 s, informacja o odzyskaniu gotowości. Przy `xrCreateInstance` -2 podpowiedź sprawdzenia SteamVR/Steam Link/runtime, bez przypisywania jednej przyczyny. **Do sprawdzenia w próbie.** |
| Przesunięcie obrazu w trakcie sesji | Recenter (`LOCAL_changed`) przesuwa widok do nowego środka — zachowanie zamierzone, ale może zaskakiwać. |

## Do zrobienia (kolejność)

1. **Próba w goglach**: kalibracja T-pose (ręce na boki, lekko przed sobą,
   do zielonej ramki), potem sprawdzić długość rąk. Odczytać
   `#avatar_calibration_totals` i `reach_limited` w `#avatar_pose_totals`.
2. Jeśli T-pose dalej się nie zalicza — użyć wskazówek/paska i logowanych powodów.
   Jawne żądanie przez `scripts/calibrate-avatar.ps1` już istnieje. Sprawdzić
   też wczytanie profilu proporcji po nowym uruchomieniu oraz jego usuwanie.
3. Sprawdzić w goglach lewą dłoń/Q i oba warianty ruchu, zatrzymywanie, pauzę,
   wejście do fotela oraz podpowiedzi. Parametry dobierać w `config/gestures.ini`.
   Po próbie odczytać automatyczny plik `.summary.md`.
4. Sprawdzić menu VR (dotyk przycisku MENU powyżej środka widoku), wybór
   dłoni, przeładowanie i lokalny panel klawiatury. Do diagnostyki użyć
   `-RecordHands` oraz `scripts/view-hand-recording.ps1`. Natywne wprowadzanie
   liczb do gry wymaga potwierdzenia sesji i podłączenia przygotowanego portu.
5. Opcjonalnie (czeka na decyzję użytkownika): jednorazowe przywrócenie
   zminimalizowanego okna gry przy starcie VR.
6. `goals-offline.md` zaktualizowano: dźwignia typu 4, potwierdzony BodyYaw
   i obrót myszą, diagnostyka startu oraz automat kalibracji.

## Zasady pracy

- Gry **nie uruchamiać bez wyraźnej zgody** na każde uruchomienie (także
  testy z fałszywym runtime).
- Pliki tymczasowe tylko w `tmp/` projektu.
- Nie obchodzić zabezpieczeń stron (np. wiki fandom).
