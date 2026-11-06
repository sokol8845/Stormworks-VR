# Menu VR, preferencje i diagnostyka offline — aktualizacja 2026-11-05

Wdrożono kolejne pięć uzgodnionych zadań bez uruchamiania Stormworks.
Najnowsze uproszczenia po dwóch próbach wymagają ponownej oceny w goglach. Przygotowanie natywnej
klawiatury obejmuje analizę protokołu i testowany adapter; połączenie z żywą
sesją panelu gry pozostaje do wykonania po potwierdzeniu jej cyklu życia.

## 1. Menu w goglach

Menu jest dostępne w trybie Gameplay. **Dotknij palcem wskazującym widocznego
przycisku MENU powyżej środka widoku**.
Przycisk znajduje się 80 cm przed głową, 18 cm powyżej oczu; podąża za pozycją
i kierunkiem poziomym głowy. Panel nie przejmuje jej przechylenia ani pochylenia.
Samo poruszenie głową w nieruchomy palec nie naciska przycisku. Poprzedni gest
obu otwartych dłoni przy twarzy został usunięty po zgłoszonej kolizji z Questem.

Główny panel ustawiany jest domyślnie 1 m przed głową i pozostaje w przestrzeni LOCAL.
**Pozycje menu i cyfry można naciskać bezpośrednio końcem palca**, bez
szczypnięcia i bez śledzenia kciuka. Cofnij palec o około 3 cm między
naciśnięciami. Przytrzymanie lub przeciąganie po wciśnięciu nie powtarza akcji;
po utracie śledzenia trzeba najpierw odsunąć palec od panelu.

Alternatywa z odległości: **wskaż przycisk przez 0,8 sekundy**, bez żadnego
szczypnięcia. Zielony pasek pokazuje postęp. Po wyborze trzeba odsunąć
promień, zanim ten sam przycisk zadziała ponownie. Dotyczy to także MENU.
Można użyć dowolnej dłoni. Panel zawiera:

- wybór joysticka lub trybu DWIE PIESCI, jeżeli uruchomiono backend lokomocji;
- wczytanie parametrów z pliku wybranego przy starcie;
- kalibrację T-pose oraz usunięcie profilu z osobnym potwierdzeniem;
- wybór dłoni joysticka i pierwszeństwa wskazywania;
- stronę OBROT / MENU: prędkość, strefę i zakres obrotu, odległość panelu oraz
  przywrócenie domyślnych ustawień;
- podstrony INTERFEJS (czas wskazania, skala, dźwięk) i KOMFORT (opcjonalny
  obrót skokowy, winieta, głowa 1:1, profil siedzący i recenter);
- otwarcie lokalnej klawiatury numerycznej i zamknięcie menu.

[Nowe opcje, wartości domyślne i podglądy](vr-comfort-20261105.md).

**?** w prawym górnym rogu przełącza pomoc: wskazanie lub dotyk opcji pokazuje
krótki opis zamiast ją uruchamiać. Ponowne **?** przywraca klikanie. Odległość,
skala i czas wskazania stosują się teraz bez ponownego otwierania panelu.
[Wyjaśnienie STREFY, ZAKRESU, CZASU i ROZMIARU](menu-help-20261105.md).

Kalibracja jest dostępna z `-AvatarPose`: po naciśnięciu KALIBRUJ panel znika,
a użytkownik ma **15 sekund** na T-pose. Przez ten czas lokomocja jest blokowana.
Nie jest wymagane jednoczesne wskazywanie i utrzymywanie T-pose.

Zmiana trybu/parametrów najpierw wstrzymuje źródło gestów i rozlicza własny
ruch oraz przyciski. Menu pokazuje oczekiwanie, zastosowanie lub błąd. Zmiana
nie jest stosowana przy niepotwierdzonym zwolnieniu; po 5 s pozostaje blokada.
Otwarty panel przejmuje wejście dłoni i zatrzymuje ruch. Po zamknięciu aktywny
tryb dwóch pięści działa od razu po rozpoznaniu zamkniętych rąk; joystick
ustawia nową pozycję neutralną chwytu. Dwa panele nie są otwierane jednocześnie.

Menu nie uruchamia backendu ruchu w sesji, która wystartowała bez
`-HandLocomotion` albo `-ArmSwingRun`; przyciski zmiany trybu są wtedy nieaktywne.
Preferencje dłoni i lokalna klawiatura nadal są dostępne.

Podgląd z izolowanego testu GPU:
[menu VR](../artifacts/vr-settings-menu-20261104.png),
[przycisk MENU do dotknięcia](../artifacts/vr-menu-touch-button-20261104.png).

Meta rezerwuje otwartą dłoń skierowaną do gogli i szczypnięcie wskazującego
dla systemu. Mod respektuje `SYSTEM_GESTURE`/`MENU_PRESSED` z opcjonalnego
`XR_FB_hand_tracking_aim`, gdy runtime je udostępnia. Dodatkowo śledzona
orientacja dłoni skierowanej wnętrzem ku głowie **z otwartymi palcami**
blokuje wejście obu rąk. Zamknięta pięść sama w sobie nie jest blokowana.
SteamVR może nie udostępniać flag Meta; wtedy działa tylko ta zachowawcza
ochrona geometryczna. To nie wyłącza ani nie przejmuje gestów systemu Questa.
[Zasady Meta](https://developers.meta.com/vr/resources/vrc-quest-input-8/),
[flagi OpenXR](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrHandTrackingAimFlagBitsFB.html).

## Preset: gra + autosave + VR

Uruchom [`scripts/test-vr.cmd`](../scripts/test-vr.cmd) albo:

```powershell
.\scripts\start-vr-test.ps1
```

Preset przekazuje natywne `-continue`, które w buildzie 26294447 wybiera
**autosave**. Nie zmienia ani nie kopiuje zapisów. Włącza AvatarPose, HandButtons,
BodyYaw, zapisany tryb ruchu i nagranie dłoni; VR rusza po pierwszym renderze świata.
Domyślne `-Movement Saved` przywraca zapisany tryb (początkowo dwie pięści).
`-Movement Joystick` wymusza joystick, `-Movement Fists` wymusza dwie pięści;
`-Movement ArmSwing` pozostaje zgodnym aliasem nazwy. `-ManualStart` pozostawia ręczny
start obrazu. Koniec pomiaru nadal sygnalizuje `stop-vr-preview.ps1`;
limit bezpieczeństwa wynosi 30 minut, a przed startem świata 10 minut.

`start-vr-test.ps1 -DryRun` sprawdza konfigurację i polecenie bez otwierania gry.
Próba 212750 przekazała `-continue`, weszła do świata i uruchomiła VR
automatycznie. Nowe uproszczenia sterowania po tej próbie sprawdzono offline.
[Dowód analizy EXE](../artifacts/native-continue-evidence-20261104.json).

## 2. Wybór dłoni

[`config/gestures.ini`](../config/gestures.ini) zawiera:

```ini
[input]
joystick_hand=left
pointing_priority=right
menu_distance_m=1.0
```

Dozwolone wartości to `left` i `right`. Joystick chwyta się **całą pięścią wybranej dłoni**.
Otwarcie dłoni zatrzymuje ruch; kciuk nie musi dotykać żadnego palca. Pierwszeństwo celowania rozstrzyga równoczesne rozpoczęcie
gestów; nie odbiera aktywnego celu drugiej dłoni. Po zwolnieniu nie przejmuje
gestu, który druga ręka już trzymała. Lewa dłoń nadal oznacza natywną akcję Q,
prawa E — wybór pierwszeństwa nie zamienia tych akcji.

Zmiany menu zapisują się automatycznie do INI po zwolnieniu własnego wejścia.
Dotyczy to trybu, dłoni, obrotu i odległości. Błąd zapisu jest widoczny w menu;
nowa konfiguracja pozostaje aktywna do końca sesji. `menu_distance_m` przyjmuje
0,6–1,2 m i obowiązuje przy następnym otwarciu panelu. WCZYTAJ przywraca wartości z pliku.
Zmiana pierwszeństwa anuluje poprzedni gest i wymaga ponownego otwarcia dłoni.

Strona **OBROT / MENU** pozwala regulować prędkość co 15°/s (15–120),
martwą strefę co 5 mm (5–100) i pełne wychylenie co 10 mm (20–400).
Zakres zawsze pozostaje większy od martwej strefy. Wiersz stanu pokazuje
prędkość, strefę/zakres w mm oraz odległość panelu w cm.
**PRZYWROC DOMYSLNE** przywraca całą konfigurację gestów, w tym bieg dwiema
pięściami, lewy joystick, pierwszeństwo prawej dłoni, 60°/s i panel 1 m.
[Podgląd strony regulacji](../artifacts/vr-controls-menu-20261105.png).

Nowa kalibracja T-pose uśrednia stabilne próbki przez około 1,5 s i odrzuca
skoki. Przy większych wahaniach prosi o ustabilizowanie rąk/głowy; nie zatwierdza
ostatniej przypadkowej pozycji. Środek głowy jest wyznaczany z tego samego okna.

## 3. Wizualny odtwarzacz offline

Z nagrania `.hands.tsv` można utworzyć samodzielny plik HTML:

```powershell
.\scripts\view-hand-recording.ps1 -Recording artifacts\proba.hands.tsv `
  -Output artifacts\proba-viewer.html
```

Skrypt uruchamia wyłącznie odtwarzacz offline i Python; wypisuje ścieżkę HTML.
Plik otwiera się ręcznie w przeglądarce. Działa bez sieci i bibliotek z CDN.
Są szkielety obu dłoni i głowa, obrót widoku myszą, kamery z przodu/z boku,
odtwarzanie z prędkością 0,25–4×, przewijanie oraz znaczniki śledzenia,
aktywacji pięści, startów sprintu, blokad natywnych i resetów.

`-GestureConfig config\inne.ini` i `-Mode Joystick` lub `-Mode ArmSwing`
pozwalają porównać detektor na tym samym materiale. Wynik oznaczany jest jako
odtwarzanie ze zmienionymi ustawieniami. Istniejące pliki nie są nadpisywane.
Pliki pośrednie i diagnostyka pozostają w unikalnym katalogu `tmp/hand-viewer-*`.

[Demonstracja HTML](../artifacts/hand-viewer-demo-20261104.html) zawiera
**dane syntetyczne**, a nie nagranie gogli ani dowód ruchu postaci.
Prawdziwy układ strony w przeglądarce wymaga ręcznego obejrzenia: dostępny
host automatyzacji przeglądarki był niedostępny. Logika interfejsu przeszła
test Node z atrapami DOM/Canvas; nie jest to test rzeczywistej przeglądarki.

## 4. Pełniejszy dziennik wejścia

`-RecordHands` zapisuje teraz format `SWVR_HANDS 3`. Oprócz pozycji dłoni
i ustawień zapisuje w kolejności pod wspólną blokadą źródła lokomocji:

- próbki XR, dozwolenie chodzenia, wstrzymanie i zmiany konfiguracji;
- reset/anulowanie z przyczyną i blokady natywne;
- pauzę, pojazd, UI, konflikt fizycznego wejścia i nieprawidłowy kontekst;
- obie fizyczne maski wejścia oraz wynik odczytu detektora przy callbacku gry.

Zdarzenia mają numer sekwencji, czas obserwacji oraz czas źródłowego API.
Pozwala to odtworzyć również przeplot wątku renderowania i callbacków gry.
Callbacki natywne zapisują tylko do bufora pamięci, bez operacji plikowych.
Bufor ma 1024 zdarzenia; przepełnienie oznacza nagranie jako niepełne zamiast
pomijać zdarzenia. Zapis jest ograniczony domyślnie do 64 MiB. Osiągnięcie
limitu kończy poprawny prefiks; błąd lub przepełnienie nie dopisują `END`.

Format v3 zapisuje jawnie sposób aktywacji joysticka i biegu. V1/v2 bez tych
pól zachowują stare detektory przy odtwarzaniu, więc aktualizacja nie zmienia
historycznych wyników. Odtwarzacz nadal czyta format v1, ale stare nagranie nie zawiera blokad,
których wtedy nie zapisano. W v2 wynik kolejnych natywnych odczytów jest
porównywany z nagranym. Rozbieżność bez zmiany parametrów daje kod błędu,
znacznik `#diverged` i wskazania w JSON/podglądzie. Niedokończony zapis nie
jest przedstawiany jako pełne odtworzenie.

```powershell
.\build\capture\Release\swvr_replay_hands.exe artifacts\proba.hands.tsv `
  artifacts\proba-replay.tsv --json artifacts\proba-replay.json
```

Odtwarzanie sprawdza detektor i bramki wejścia. Nie wykonuje requestów gry,
nie odtwarza fizyki i nie potwierdza rzeczywistego sprintu ani dostarczenia
requestu przez sieć.

### Obrót i menu w replay

Nowe próby z `-RecordHands` tworzą także **`.controls.tsv`**. Zawiera klatki
źródłowe i oczekiwane wyniki obrotu drugą dłonią oraz paneli menu, MENU i
klawiatury: otwarcie, anulowanie, pierwszeństwo dłoni, cel, postęp wskazania
i wybraną akcję. Format `SWVR_CONTROLS 3` obejmuje czas wskazania, skalę, zmianę
otwartego panelu, pomoc i ustawienia obrotu; czytnik zachowuje zgodność z wersjami 1–2. Odtwarzacz uruchamia
te same moduły bez gry:

```powershell
.\build\capture\Release\swvr_replay_controls.exe artifacts\proba.controls.tsv `
  artifacts\proba-controls-replay.tsv
```

Kod wyjścia 0 oznacza zgodność, 1 rozbieżność, 2 uszkodzony plik/błąd operacji.
Raport TSV wskazuje klatkę i porównane rezultaty. Plik ma osobny limit 64 MiB;
`limited=1` / `#prefix` oznacza zgodny zapis tylko do osiągnięcia limitu.
Niepełny zapis po przerwaniu lub awarii jest odrzucany.
`view-hand-recording.ps1` automatycznie sprawdza plik towarzyszący, jeśli istnieje,
i wypisuje ścieżkę raportu. HTML nadal pokazuje śledzenie i lokomocję;
wyniki obrotu i paneli są w osobnym TSV. Starsze nagrania bez tego pliku
pozostają obsługiwane. Replay potwierdza logikę wejścia, nie wykonanie akcji gry.

### Raport wydajności

Po zakończeniu VR obok logów powstaje **`.performance.json`**. Zawiera średnią,
P95, P99, maksimum i do 10 najwolniejszych próbek wraz z numerami klatek XR
dla odstępów klatek, obliczeń pozy avatara, overlay, zapisu nagrań i renderowania
każdego oka. Są też trafienia/chybienia cache i liczby powtórzonych/pustych klatek.

Percentyle korzystają z histogramu o stałej pamięci i rozdzielczości 0,25 ms.
Jeśli percentyl trafia do ostatniego kubełka ≥127,75 ms, raport podaje maksimum
jako górną granicę. Koszt obliczeń pozy obejmuje chybienia cache, koszt nagrań
obejmuje formatowanie i zapis w klatkach, bez końcowego zamknięcia plików.
Kanały mogą się nakładać (render oka obejmuje obliczenia pozy); nie należy
ich sumować. W schemacie 2 obok tych czasów CPU są praca klatki CPU bez
`xrWaitFrame`, nieblokujące znaczniki czasu GPU, przewidywany okres OpenXR
i liczba/procent przekroczeń budżetu. Podsumowanie `.summary.md` pokazuje
te dane automatycznie; starszy schemat 1 pozostaje obsługiwany.
Zakres GPU może zawierać przerwy między poleceniami i nie obejmuje kompozytora
ani Steam Link. Wyniki nie dowodzą wzrostu FPS.
[Dokładny zakres i porównanie ze SteamVR](vr-comfort-20261105.md#diagnostyka-i-replay).

## 5. Przygotowanie natywnych klawiatur 5/6

[Analiza EXE i protokołu](native-keypad-integration-research-20261104.md)
potwierdziła osobne otwieranie UI i wysłanie dwóch requestów typu 11 po OK.
Pola +0x4c8/+0x4cc to dwie bieżące wartości, a nie minimum/maksimum.
Nawet mała klawiatura wysyła oba kanały; adapter zachowuje jej ukrytą wartość.

Dodano testowany port `NativeKeypadSubmission`: ABI 0x30, pola i serializator,
certyfikat sesji, kontrola pełnego SHA i świeżości, alokacja obu requestów przed
wysłaniem oraz ponowna walidacja tożsamości przed każdym przekazaniem.
Niepewnego przekazania nie ponawia i nie zwalnia obiektu po utracie własności.
Wprowadzanie przyjmuje skończone liczby dziesiętne ASCII, bez wykładników,
NaN i częściowo rozpoznanych napisów. Jest to celowo węższy format niż parser gry.

**Port nie jest podłączony do produkcyjnego hooka.** Nie potwierdzono jeszcze
pełnej ścieżki właściciel/aktor i czasu życia sesji UI. Typy 5/6 nadal nie są
dodane do HandButtons, ponieważ release typu 10 jest dla nich niewłaściwy.
OK w lokalnej klawiaturze VR nadal zapisuje wynik wyłącznie lokalnie.

## Weryfikacja

Release bez ostrzeżeń; **49/49 CTest**, **45/45 Python**, **68 guardów EXE**
i samodzielny test tracera GPU. Nowe scenariusze obejmują menu, przejęcie
wejścia panelu, gest wywołania, wybór dłoni, wierny replay konfliktu klawiatury,
niepełne nagrania oraz błędy alokacji i przekazania requestów klawiatury.

- [Raport CTest](../build/capture/artifacts/controls-20261104.xml)
- [Raport Python](../artifacts/controls-python-20261104.xml)
- [Profil EXE i 68 kontroli](../artifacts/controls-profile-20261104.json)
- [Podgląd menu](../artifacts/vr-settings-menu-20261104.png)
- [Podgląd lokalnej klawiatury](../artifacts/keypad-prototype-20261104.png)

Po przeglądzie kodu dodano regresje dla współbieżnego publikowania/odczytu
aktywnego sprintu, obu ustawień priorytetu dłoni przy brakującym śledzeniu
palca i zdegenerowanym promieniu oraz równoczesnych znaczników śledzenia,
pięści, sprintu i blokady. Domyślny czas odczytu ruchu jest pobierany pod
blokadą; jawnie przekazany cofnięty zegar nadal przerywa ruch. Wybór dłoni
uwzględnia użyteczność promienia i nie odbiera istniejącego przytrzymania.
Znaczniki mają niezależne kategorie; niezmienna blokada nie tworzy znacznika
przy każdej próbce.

Renderer przechowuje jednostkową geometrię znaków i pojemność bufora CPU.
Orientacja jest przeliczana raz na napis. Syntetyczny pomiar 1000 przebiegów
etykiet menu w Release: **990,7 → 104,7 µs CPU**, około **9,5× szybciej**.
Pomiar nie obejmuje GL ani gry. Maksymalna różnica położeń względem poprzedniej
wersji to 0,18 µm; porównanie obrazów 1024×1024 menu i klawiatury wykazało
**0 zmienionych pikseli**. Test GPU obejmuje też ponowne rysowanie oraz zmianę
wartości, pozy i podświetlenia panelu.

[Regresje po poprawkach](../build/capture/artifacts/review-fixes-20261104.xml),
[Python](../artifacts/review-fixes-python-20261104.xml),
[guardy EXE](../artifacts/review-fixes-profile-20261104.json),
[pomiary i porównanie obrazów](../artifacts/review-fix-performance-20261104.json).

Próba `headset-xr-20261104-204959-345` została zakończona na polecenie
użytkownika: 20 818 par stereo, 4 przyjęte kalibracje, 1 pełny cykl natywnego
przycisku i 0 błędów zwalniania. Użytkownik zgłosił problemy z klikaniem menu
i kolizję gestów z Questem. Replay początkowej części nagrania wykazał 12
rozpoczętych naciśnięć, 3 ukończone (wyłącznie ZAMKNIJ) i 9 utraconych po
zmianie celu podczas szczypnięcia. Nagranie osiągnęło limit 64 MiB.

Po tej próbie dodano dotyk, widoczny przycisk MENU, szczypnięcie środkowego
palca i ochronę systemową. Regresje obejmują obie dłonie, obrócony panel,
powtarzanie dopiero po odsunięciu, tracking/fokus/przerwę czasową,
odrzucenie skoku śledzenia, ruch głowy przy nieruchomym palcu oraz opcjonalną
flagę systemową runtime. Nowy przycisk i panele przeszły izolowane testy GPU.
[CTest](../build/capture/artifacts/touch-menu-20261104.xml),
[Python](../artifacts/touch-menu-python-20261104.xml),
[profil EXE](../artifacts/touch-menu-profile-20261104.json),
[diagnoza nagrania](../artifacts/menu-touch-diagnosis-20261104.json).

Druga próba, `headset-xr-20261104-212750-270`, dała 13 414 par stereo,
2 kalibracje i 0 requestów ruchu. Użytkownik zgłosił zbyt bliskie menu,
przeczące sobie instrukcje biegu i przechwytywanie środkowego pinchu przez Questa.
Dlatego bieżąca konfiguracja używa dwóch pięści bez rytmu, chwytu joysticka
całą dłonią, menu 1 m i wskazania przez 0,8 s. Nie ma szczypnięć w obsłudze
ruchu ani paneli moda. Przyciski pojazdu Q/E mają osobny, dotychczasowy interfejs.

Nowy zestaw: [CTest](../build/capture/artifacts/simple-controls-20261104.xml),
[Python](../artifacts/simple-controls-python-20261104.xml),
[profil EXE](../artifacts/simple-controls-profile-20261104.json),
[diagnoza i symulacja](../artifacts/simple-controls-diagnosis-20261104.json).
**Po tych uproszczeniach gry nie uruchamiano.** Zachowanie nadal wymaga próby
na Queście; wyniki odtwarzacza nie są dowodem ruchu postaci.
