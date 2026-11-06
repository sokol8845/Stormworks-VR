# Goal 1–5: wygoda obsługi i diagnostyka — 2026-11-04

Wszystkie pięć zadań wdrożono i sprawdzono offline. Nie uruchamiano
Stormworks ani próby gry z fałszywym runtime.

- [x] Pasek postępu T-pose oraz wskazówki dotyczące brakującej dłoni,
  rozstawu, wysokości i głębokości rąk. Krótka utrata śledzenia zamraża
  postęp; długa przerwa resetuje go. Utrata fokusu przerywa utrzymanie.
- [x] Zapis zaakceptowanych proporcji ciała, wczytanie po ponownym starcie
  oraz usuwanie zapisu i aktywnej kalibracji.
- [x] Przeładowanie pliku gestów oraz zmiana joystick ↔ wymachy podczas
  sesji, po zwolnieniu własnego ruchu i przytrzymania przycisków.
- [x] Opcjonalne nagrywanie pozycji głowy i wszystkich stawów dłoni oraz
  samodzielny odtwarzacz detektora, również z innymi progami.
- [x] Lokalny prototyp klawiatury numerycznej: panel, wskazywanie obiema
  rękami, pinch, edycja liczby, OK i anulowanie.

## Kalibracja i profil proporcji

T-pose nadal wymaga 1,5 s poprawnej pozy, z tolerancją przerw do 300 ms.
W trakcie widać pasek i tekst. Przy przerwaniu pojawia się wskazówka, np.
`POKAZ LEWA DLON`, `WYROWNAJ WYSOKOSC DLONI` lub `ROZLOZ RECE SZERZEJ`.
Po przyjęciu pasek jest pełny i pojawia się potwierdzenie.

Przy `-AvatarPose` uruchamiacz domyślnie wybiera
`config/avatar-calibration.ini`; `-AvatarProfile <plik>` zmienia tę ścieżkę.
Plik powstaje dopiero po zaakceptowanej kalibracji. Zawiera wersję formatu,
wariant modelu, rozpiętość nadgarstków i przesunięcie środka barków względem
głowy. Nie zawiera wskaźników postaci, kalibracji LOCAL ani współrzędnych świata.
Wczytane proporcje są wiązane z aktualną lokalną postacią dopiero po uzyskaniu
jej zweryfikowanej tożsamości. Inny wariant modelu nie przyjmuje tego profilu.
Nowa zaakceptowana kalibracja zastępuje poprzedni zapis.

Zapis używa pliku roboczego w projektowym `tmp/` i atomowego zastąpienia
docelowego pliku na tym samym woluminie. Błąd lub niepoprawny profil pozostawia
VR dostępne bez zapisanej kalibracji; szczegóły trafiają do `avatar_profile_error`.

Usunięcie bez uruchomionej gry:

```powershell
.\scripts\reset-avatar-profile.ps1
```

Usunięcie zapisu i kalibracji aktywnej podczas **późniejszej, dozwolonej sesji**:

```powershell
.\scripts\reset-avatar-profile.ps1 -GameProcessId <PID>
```

Sygnał jest przetwarzany przy następnej klatce XR. Sam skrypt nie uruchamia gry.
W logu powinno pojawić się `#avatar_profile cleared`. Nadal dostępne jest jawne
żądanie kalibracji przez `scripts/calibrate-avatar.ps1`.

## Zmiana ustawień w sesji

Wymaga uruchomienia próby z `-HandLocomotion` albo `-ArmSwingRun`; skrypt nie
instaluje nowego backendu w sesji, która wystartowała bez lokomocji.
Po edycji pliku wskazanego przez `-GestureConfig`:

```powershell
.\scripts\set-vr-locomotion.ps1 -GameProcessId <PID> -Mode Reload
.\scripts\set-vr-locomotion.ps1 -GameProcessId <PID> -Mode Joystick
.\scripts\set-vr-locomotion.ps1 -GameProcessId <PID> -Mode ArmSwing
```

Wszystkie warianty ponownie czytają ten sam plik. Niepoprawne wartości są
odrzucane z zachowaniem dotychczasowej konfiguracji. Dla poprawnej zmiany
adapter blokuje publikację gestów, kończy bieżące naciśnięcia i czeka na
zwolnienie we własnych żywych callbackach. Dopiero potem aplikuje ustawienia
i loguje `gesture_reload applied_after_release`. Po zmianie trzeba otworzyć
dłonie przed kolejnym gestem. Limit oczekiwania to 5 s; po błędzie/timeout
ruch pozostaje wstrzymany. Kolejne poprawne żądanie może wznowić zmianę po
rozliczeniu wejścia; trwały błąd natywnego adaptera wymaga nowej sesji.

Potwierdzenie oznacza lokalne przekazanie zwolnienia do natywnej kolejki,
nie dostarczenie requestu do serwera ani pomiar zatrzymania postaci.

## Nagranie i odtwarzanie śledzenia

`-RecordHands` w Gameplay tworzy `<capture>.hands.tsv`, domyślnie do 64 MiB.
Nagranie jest opcjonalne, bo zapis i serializacja mają koszt diagnostyczny.
Zawiera:

- oba położenia oczu, 52 stawy dłoni i ich flagi śledzenia;
- czas XR oraz osobny czas publikacji, numer klatki i rewizję LOCAL;
- bramkę ruchu z renderera, stan wstrzymania oraz konfiguracje gestów;
- pełne bity liczb float, także NaN lub -0 w wadliwych próbkach.

Nie zapisuje nazw tekstur OpenGL ani natywnych wskaźników. Zakończenie
zapisuje `END`; przerwany lub uszkodzony plik jest odrzucany przez odtwarzacz.
Limit kończy zbieranie kolejnych próbek i jest widoczny w raporcie.

```powershell
.\build\capture\Release\swvr_replay_hands.exe artifacts/<capture>.hands.tsv artifacts/replay.tsv
.\build\capture\Release\swvr_replay_hands.exe artifacts/<capture>.hands.tsv artifacts/replay-inne-progi.tsv --config config\gestures.ini
.\build\capture\Release\swvr_replay_hands.exe artifacts/<capture>.hands.tsv artifacts/replay-wymachy.tsv --mode arm_swing
```

Odtwarzacz nie ładuje gry ani OpenXR. Wyniki TSV obejmują zamiar ruchu, sprint,
rezerwację rąk i komunikat detektora. Zmienione progi/tryb są oznaczane jako
override. Istniejący plik wynikowy i plik źródłowy są chronione przed nadpisaniem.

To odtwarzanie detektora na próbkach z renderera. Nie odtwarza fizyki,
natywnych zdarzeń klawiatury, zmian UI pomiędzy klatkami ani sieci. Ograniczenia
natywnego backendu i anulowania z wątku logiki trzeba porównywać z logiem próby.
`native_bits` oznacza mapowanie zamiaru na maskę; odtwarzacz jej nigdzie nie wysyła.

## Klawiatura numeryczna — prototyp

Opcja startowa: `-VrKeypadPrototype` w Gameplay. W późniejszej dozwolonej sesji:

```powershell
.\scripts\toggle-vr-keypad.ps1 -GameProcessId <PID>
```

Panel pojawia się około 75 cm przed głową i zostaje w miejscu w przestrzeni
LOCAL. Wskazuj palcem i złącz kciuk ze wskazującym; klawisz zatwierdza się po
otwarciu nad tym samym polem. Można używać dowolnej ręki; druga nie przejmuje
rozpoczętego kliknięcia. Dostępne są cyfry, minus, kropka, DEL, CLR, OK i ANULUJ.
Bufor ma maksymalnie 16 znaków. OK wymaga przynajmniej jednej cyfry.

Utrata fokusu/śledzenia głowy lub zmiana LOCAL zamyka panel bez zatwierdzenia.
Utrata śledzenia dłoni lub długa przerwa anuluje rozpoczęte kliknięcie.
Otwarty panel blokuje gesty chodzenia i nowe kliknięcia w kokpicie.

**OK zapisuje tylko lokalny wynik `keypad_prototype accepted_local value=…`
w logu. Nie wysyła liczby do gry i nie emituje klawiszy systemowych.** Natywne
klawiatury typów 5/6 nadal wymagają osobnej integracji i pozostają blokowane.

![Podgląd panelu z testu GPU](../artifacts/keypad-prototype-20261104.png)

## Weryfikacja

Release bez ostrzeżeń, **43/43 CTest**, **36/36 Python**, **66 guardów EXE,
0 błędów**; samodzielny tracer GPU również przeszedł.

- [CTest JUnit](../build/capture/artifacts/usability-goal-20261104.xml)
- [Python JUnit](../artifacts/usability-goal-python-20261104.xml)
- [Profil EXE](../artifacts/usability-goal-profile-20261104.json)

Nowe testy obejmują m.in. zapis i zastąpienie profilu, brak wskaźników w pliku,
wiązanie świeżej tożsamości, barierę zwolnienia, timeout i błędy konfiguracji,
odtwarzanie sprintu i puszczenia, alternatywne progi, niepełne nagranie,
sterowanie zdarzeniami należącymi do testu oraz wskazywanie klawiatury obiema
rękami. GPU sprawdza oba obrazy, przyrost wypełnienia paska, klawisz hover,
zamykanie panelu i przywrócenie stanu OpenGL.

Prawdziwa czytelność w goglach, ergonomia panelu, dokładność proporcji po
wczytaniu i działanie przeładowania w żywej sesji wymagają późniejszej próby.
