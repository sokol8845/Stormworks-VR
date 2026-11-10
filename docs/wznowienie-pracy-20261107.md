# Wznowienie pracy — 2026-11-07

## Status 2026-11-10: wznowione i dokończone offline

Użytkownik polecił dokończyć prace. Punkty 1–8 z listy poniżej są wykonane:
Release, 60/60 CTest, 64/64 Python, 78 kontroli EXE i test DLL przechodzą.
Opis, znalezione błędy i lista do oceny w goglach:
[practice-tools-20261110.md](practice-tools-20261110.md). Otwarte pozostają
tylko punkt 9 (publikacja — wymaga zgody) i próba w goglach. Reszta tego
pliku to zapis stanu z dnia wstrzymania.

## Status z 2026-11-07: wstrzymane na prośbę użytkownika

Użytkownik poprosił: „wstrzymaj pracę i zapisz w pliku md co dalej należy zrobić”.
Nie kontynuować wdrażania ani uruchamiać testów do czasu polecenia wznowienia.
Gra pozostaje zamknięta. Dotychczasowy zakres prac był offline.

Zmiany robocze są zapisane na dysku, ale **nie są ukończone, przetestowane ani
zatwierdzone w Git**. Nie wykonano commita ani push tych zmian.

- Repozytorium: `C:\Users\Sokol\Documents\Python\Stormworks`.
- GitHub: <https://github.com/sokol8845/Stormworks-VR>.
- Baza: `1960a0d1ea84f2eb512cbafa4a7ba5f46df5ee2c`
  (`Add VR steering settings, control profiles and rolling diagnostics`).
- **Zachować lokalną zmianę użytkownika w `config/gestures.ini`**. Nie nadpisywać
  jej i nie włączać automatycznie do commita.
- Uruchomiona przed prośbą o wstrzymanie kompilacja Release zakończyła się.
  Log `artifacts/practice-build.log` nie zawiera zgłoszonych błędów ani ostrzeżeń;
  zbudowano m.in. nowy `swvr_practice_tools_tests.exe` oraz `swvr_gl_trace.dll`.
  **Nowych testów jeszcze nie uruchomiono.**
- Wyniki 58/58 CTest, 64/64 Python i 78 kontroli EXE opisane wcześniej dotyczą
  bazy, a nie aktualnych zmian roboczych.

## Zakres zatwierdzony do wdrożenia

1. Potwierdzenie celu przez natywny wybór gry: zielony promień dopiero po
   rozpoznaniu obsługiwanego przycisku oraz krótka informacja o blokadzie.
2. Regulacja joysticka chodzenia w menu VR: martwa strefa, zakres wychylenia,
   próg rozpoczęcia i zakończenia sprintu.
3. Opcjonalny podgląd obu joysticków: obrys, strefa neutralna i aktualne wychylenie.
4. Ponowne ustawienie chwytu kierownicy oraz opcjonalny podgląd obręczy/chwytu.
5. Ćwiczenia chodzenia, obrotu, kierownicy i klikania, bez wysyłania poleceń
   gestami do postaci lub pojazdu.

## Co jest już w kodzie roboczym

### Potwierdzenie celu

- `include/swvr/target_feedback.hpp`, `src/target_feedback.cpp`: stany
  unknown/ready/blocked/held oraz krótkie opisy po polsku, ASCII dla czcionki VR.
- `src/native_hand_buttons.cpp`: informacja pochodzi z rzeczywistego callbacku
  natywnej selekcji i istniejącego `press_gate`; reset kasuje potwierdzenie.
- Ważność maksymalnie 100 ms; kontrola ręki, rewizji przestrzeni, generacji
  anulowania, kolejności próbek i przemieszczenia promienia. Dopuszczono mały
  szum między klatkami: 1 cm i około 2 stopnie. Bez przechowywania wskaźnika celu
  w danych przekazywanych do renderera.
- Renderer rysuje szary promień bez potwierdzenia, zielony dla gotowego celu
  oraz bursztynowy dla blokady/trzymania. Nie zmieniano natywnych offsetów.

### Ustawienia i podglądy

- `input_visuals.hpp` oraz detektory `HandLocomotion`, `HandTurn`, `HandSteering`
  udostępniają kopie danych do rysowania z numerem klatki i rewizją przestrzeni.
- `GlHandOverlay` ma rysowanie joysticków i obręczy kierownicy.
- `MENU → CHODZENIE → REGULACJA CHODZENIA`: osiem przycisków ±.
- `MENU → CHODZENIE → PODGLAD JOYSTICKOW WL / WYL`.
- `MENU → NARZEDZIA → KIEROWNICA`: `USTAW CHWYT`, `POKAZ CHWYT WL / WYL`.
- Zapis nowych preferencji: `[input] joystick_visual`, `[steering] show_grip`;
  obie domyślnie wyłączone, korzystają z istniejącego zapisu ustawień/profili.
- Reset chwytu w hoście wymaga fotela, zeruje detektor i pokazuje wskazówkę
  przez 10 sekund. Ponowny chwyt wymaga otwarcia dłoni.

### Ćwiczenia

- Nowy `ControlPractice` (`include/swvr/control_practice.hpp`,
  `src/control_practice.cpp`) posiada własne detektory, bez natywnych adapterów.
- Etapy: chodzenie, obrót, kierownica, przycisk testowy. Nawigacja: poprzedni,
  dalej, zakończ. Lokalne ćwiczenia joysticka korzystają z całej pięści także
  przy historycznym ustawieniu aktywacji przez palce.
- Wejście i wyjście wymagają zwolnienia natywnego wejścia i obu otwartych dłoni
  przez 200 ms. Utrata śledzenia, fokusu, zmiana przestrzeni i nieprawidłowa
  kolejność klatek resetują postęp oraz wymagają ponownego otwarcia dłoni.
- `src/gl_trace.cpp`: wejście przez `MENU → NARZEDZIA → CWICZENIA STEROWANIA`,
  wstrzymanie natywnego ruchu, reset interakcji, blokowanie żywego ruchu, obrotu,
  kierownicy i przycisków. Podczas ćwiczeń wyłączono automatyczną T-pose,
  otwieranie klawiatury i stosowanie zmian gestów; sygnały zmiany trybu/reload
  i kalibracji są w tym czasie odbierane bez wykonania.
- Diagnostyka oznacza ćwiczenia jako wstrzymanie wejścia gry. Po nieudanym
  wznowieniu natywnego adaptera ruch ma pozostać zablokowany.
- Jest nowy plik `tests/practice_tools_tests.cpp`, zarejestrowany w CMake jako
  `practice_tools`. Zawiera testy przebiegu ćwiczeń, ustawień, potwierdzeń celu
  i podglądów; **został skompilowany, ale nie wykonany**.

### Optymalizacja hooków OpenGL (dodana 2026-11-10)

Drzewo robocze zawiera też niezależną od pięciu rozszerzeń optymalizację
hooków: `include/swvr/gl_program_tracker.hpp`, `tests/gl_program_tracker_tests.cpp`,
`interest()` w `head_mask_gl` / `avatar_pose_gl` / `driver_wheel_gl`, zmienione
hooki w `src/gl_trace.cpp` i rozszerzone testy GPU oraz `head_mask_injection`.
Opis i pomiary: `docs/stan-pracy.md`.

## Co zrobić po wznowieniu — kolejność

1. Przeczytać ten plik i `git diff`, zachować wszystkie zmiany użytkownika.
   Uruchomić najpierw `practice_tools`; poprawić wykryte problemy na podstawie
   wyników. Szczególnie sprawdzić symulację celowania w przyciski ćwiczeń,
   przechodzenie między etapami i ponowne uzbrojenie po zmianie przestrzeni.
2. Dokończyć weryfikację blokady poleceń w `gl_trace.cpp`: wszystkie źródła
   żywych gestów muszą respektować aktywność ćwiczeń. Sprawdzić wejście podczas
   trzymania przycisku/kierownicy, odroczone zwolnienie natywne, utratę fokusu,
   błędy adaptera, zakończenie sesji i nieudane wznowienie. Fizyczna klawiatura
   użytkownika nie jest blokowana przez tryb ćwiczeń.
3. Rozszerzyć istniejące testy natywne:
   - `native_hand_buttons_tests.cpp`: gotowość przed gestem kliknięcia,
     nieobsługiwany typ, niedopasowany cel, przycisk trzymany, reset, wygaśnięcie.
   - `native_locomotion_tests.cpp`: prawdziwy adapter we fixture ma nie wysyłać
     poleceń podczas symulowanych ćwiczeń; sprawdzić zwolnienie i wyjście.
   - Sprawdzić również anulowanie/pominięcie zaległych żądań klawiatury pojazdu.
4. Dokończyć testy menu i czytelność:
   - `menu_feedback_tests.cpp` pokrywa obecnie osiem stron; nowa regulacja
     chodzenia jest dziewiątą. Rozszerzyć nawigację i opisy trybu `?`.
   - `vr_settings_menu_tests.cpp`: przejście na nową stronę, przyciski ±,
     przełączniki podglądu, polecenia ćwiczeń i resetu chwytu.
   - Zapewnić czytelne wskazanie aktualnego WL/WYL dla nowych podglądów.
     Obecnie etykiety przycisków są ogólne „WL / WYL”.
   - Potwierdzić zapis i odczyt nowych pól także przez profile i ich kopie.
5. Dokończyć testy GPU w `gl_hand_overlay_tests.cpp`:
   - Stary test zakłada zielony promień po samym włączeniu rysowania. Teraz
     przypadek zielony wymaga jawnego świeżego potwierdzenia celu; dodać także
     brak potwierdzenia, blokadę i wygaśnięcie.
   - Sprawdzić oba oczy, zachowanie stanu OpenGL, brak aktywnego podglądu po
     utracie śledzenia/zmianie rewizji, przypisanie lewej/prawej dłoni.
   - Wyeksportować obrazy nowej strony ustawień, ćwiczeń i obu rodzajów
     podglądu. Obejrzeć je, poprawić ewentualne nachodzenie tekstu/elementów.
     Sam sukces kompilacji nie potwierdza poprawnego wyglądu.
6. Przejrzeć całość zmian pod kątem błędów, aktualności danych wizualnych i
   kosztu renderowania. Sprawdzić krótkie opisy blokad oraz czas ważności celu.
   Przy malformowanych wektorach promienia rozważyć dodatkową walidację normy;
   obecny kontrakt mostka przewiduje kierunki jednostkowe.
7. Wykonać pełną walidację offline: Release, CTest, Python, kontrolę profilu EXE
   i samodzielny test DLL. Nie uruchamiać gry w ramach tych prac.
8. Dopiero po poprawieniu błędów i sprawdzeniu wyników uaktualnić README,
   `docs/stan-pracy.md` oraz opis obsługi i ograniczeń. Podać rzeczywiste wyniki
   testów nowej wersji i osobną listę rzeczy do oceny w goglach.
9. Przy publikacji przejrzeć diff/staging, wykluczyć `config/gestures.ini`,
   pobrać aktualny stan GitHub (druga osoba też pracuje w repo), sprawdzić
   historię i wykonać zwykły commit/push bez force. Obecnie nic nie publikowano.

## Przydatne polecenia po wznowieniu

PowerShell, katalog główny projektu:

```powershell
git status --short
git diff --stat
ctest --test-dir build/capture -C Release -R practice_tools --output-on-failure
cmake --build build/capture --config Release --parallel
ctest --test-dir build/capture -C Release --output-on-failure
python -m pytest tests -q
python tools/verify_game_profile.py --output artifacts/practice-profile.json
```

Test DLL: `build/capture/Release/swvr_trace_test.exe`, uruchamiany z nowego
podkatalogu `tmp`, aby nie nadpisywać poprzednich artefaktów. Testy GPU mogą
zapisywać PPM; do oglądania można przekonwertować je do PNG.

Przy skryptowych edycjach plików używać jawnego UTF-8 i nowych linii LF.
Wcześniejsza domyślna strona kodowa Windows powodowała uszkodzenie polskich
znaków. Nie uruchamiać `pytest` na całym katalogu głównym — obejmuje również
skrypty wymagające środowiska Ghidra.
