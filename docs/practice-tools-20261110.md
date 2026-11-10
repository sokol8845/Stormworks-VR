# Potwierdzenie celu, regulacja chodzenia, podglądy i ćwiczenia — 2026-11-10

Dokończono pięć rozszerzeń wstrzymanych 2026-11-07. Gra pozostawała zamknięta;
wszystko poniżej jest sprawdzone offline i czeka na ocenę w goglach.

## 1. Kolor promienia = odpowiedź gry

Promień palca wskazującego nie jest już zawsze zielony.

| Promień | Znaczenie |
|---|---|
| Szary | Brak potwierdzenia: promień celuje w powietrze albo w element, którego mod nie naciska |
| Zielony | Gra wybrała ten przycisk i szczypnięcie zostanie przyjęte |
| Bursztynowy + krótki napis | Pod promieniem jest przycisk, ale kliknięcie jest teraz niemożliwe albo przycisk jest trzymany |

Napisy pojawiają się tylko przy bursztynowym promieniu, np. `TEN TYP ELEMENTU
NIE JEST OBSLUGIWANY`, `INTERFEJS GRY BLOKUJE KLIK`, `OTWORZ DLON PRZED KOLEJNYM
KLIKIEM`, `PRZYCISK TRZYMANY - OTWORZ DLON`.

Kolor pochodzi z natywnego wyboru celu gry i z tej samej bramki, która
decyduje o przyjęciu kliknięcia. Potwierdzenie jest ważne najwyżej 100 ms
i tylko dla tej samej dłoni; ruch promienia o więcej niż 1 cm lub około 2°,
recenter, reset albo utrata śledzenia cofają je do szarego. Zielony promień
nie gwarantuje reakcji pojazdu na przycisk — mówi tylko, że gra przyjmie
naciśnięcie.

## 2. Regulacja chodzenia

**MENU → CHODZENIE → REGULACJA CHODZENIA**. Linia pod tytułem pokazuje
bieżące wartości w milimetrach.

| Przyciski | Znaczenie | Krok | Zakres | Domyślnie |
|---|---|---|---|---|
| STREFA −/+ | Martwa strefa: mniejszy ruch pięści nie rusza postaci | 5 mm | 5–100 mm | 30 mm |
| ZAKRES −/+ | Wychylenie dające pełną oś ruchu; mniej = czulszy joystick | 10 mm | 20–400 mm | 120 mm |
| BIEG START −/+ | Wychylenie do przodu włączające sprint | 10 mm | do 400 mm | 120 mm |
| BIEG STOP −/+ | Poniżej tego wychylenia sprint się wyłącza | 10 mm | od 10 mm | 90 mm |

Zawsze obowiązuje: strefa < bieg stop < bieg start oraz strefa < zakres.
Przycisk, który złamałby tę kolejność, zatrzymuje się na granicy. Zmiana
stosuje się po zwolnieniu ruchu, jak pozostałe ustawienia, i zapisuje się
w `gestures.ini` oraz w profilach.

## 3. Podgląd joysticków

**MENU → CHODZENIE → PODGLAD JOYSTICKOW: WL / WYL** (domyślnie wyłączony).
Dwa okręgi poniżej środka widoku: `L RUCH` / `P OBROT` (albo odwrotnie przy
joysticku w prawej dłoni). Szary okrąg to joystick nieuchwycony. Po zaciśnięciu
pięści okrąg robi się niebieski, wewnętrzny pierścień pokazuje martwą strefę,
a mały znacznik — bieżące wychylenie; przy sprincie kolor zmienia się na
bursztynowy. W trybie „tylko przód” znacznik nie pokazuje ruchu w bok, bo gra
go nie dostaje. Podgląd nie działa w trybie dwóch pięści.

## 4. Chwyt kierownicy

**MENU → NARZEDZIA → KIEROWNICA**:

- **USTAW CHWYT** — tylko w fotelu. Kasuje ustalony środek kierownicy, zamyka
  menu i przez 10 s pokazuje strefę chwytu. Trzeba otworzyć obie dłonie
  i złapać ponownie; zaciśnięte pięści nie są przejmowane.
- **POKAZ CHWYT: WL / WYL** (domyślnie wyłączony) — stały podgląd umownej
  obręczy: bursztynowa, gdy wolna, zielona, gdy trzyma ją dłoń, z małym
  okręgiem przy każdej dłoni. To strefa detektora gestu, nie podświetlenie
  modelu kierownicy w pojeździe.

Przełączniki na stronach CHODZENIE, NARZEDZIA i KIEROWNICA pokazują teraz
własny stan na przycisku (`…: WL` albo `…: WYL`).

## 5. Ćwiczenia sterowania

**MENU → NARZEDZIA → CWICZENIA STEROWANIA**. Cztery etapy: chodzenie, obrót,
kierownica, przycisk testowy. Panel ma **POPRZEDNI**, **DALEJ**
i **ZAKONCZ CWICZENIA**; etap zaliczony dostaje dopisek `ZALICZONE`.

- Podczas ćwiczeń gesty **nie sterują grą**: ruch, obrót, kierownica,
  kliknięcia dłonią, MENU, kalibracja T-pose i klawiatura pojazdu są
  wstrzymane. Fizyczna klawiatura i mysz działają normalnie.
- Wejście: jeśli coś było trzymane (ruch, kierownica, przycisk), ćwiczenia
  czekają na jego zwolnienie w grze, potem na obie otwarte dłonie przez 0,2 s.
- Wyjście: **ZAKONCZ CWICZENIA**, potem obie otwarte dłonie przez 0,2 s. Dzięki
  temu zaciśnięta pięść nie trafia do gry jako polecenie.
- Utrata śledzenia którejś dłoni lub głowy, utrata fokusu, recenter albo
  przerwa w klatkach kasują postęp etapu i wymagają ponownego otwarcia dłoni.
  Czubki palców schowane w pięści nie są traktowane jako utrata śledzenia —
  tak samo jak w zwykłym sterowaniu.
- Błąd natywnego wejścia zatrzymuje symulację z komunikatem, ale wyjście
  zawsze pozostaje możliwe. Jeśli gra nie pozwoli wznowić ruchu, zostaje on
  wstrzymany z komunikatem `BLAD WZNOWIENIA - RUCH WSTRZYMANY`; można wtedy
  spróbować **WCZYTAJ** w menu, a przy trwałym błędzie adaptera — nowej sesji.
- Ćwiczenia wymagają obu dłoni w polu widzenia gogli.

## Plik ustawień

```ini
[input]
joystick_visual=false
[steering]
show_grip=false
```

Starsze pliki bez tych wierszy wczytują się z wyłączonymi podglądami.
Lokalny `config/gestures.ini` nie został zmieniony.

## Poprawki znalezione przy dokańczaniu

- Z ćwiczeń nie dało się wyjść, gdy natywne wejście zgłaszało błąd lub trwałą
  blokadę przycisków; menu też było wtedy niedostępne. Wyjście działa teraz
  zawsze.
- Celowanie w powietrze albo w element niebędący przyciskiem pokazywało
  bursztynową „blokadę” z napisem. Teraz promień jest szary.
- Napis pod promieniem był rysowany stale, także `BRAK POTWIERDZONEGO CELU`.
  Zostały tylko powody blokady.
- Czcionka w goglach nie miała znaków `,` `=` `;` — w opisach pomocy
  i ćwiczeń zostawały puste miejsca. Dodano je.
- Linia wartości regulacji chodzenia ucinała jednostkę przy największych
  ustawieniach.
- Klawiatura VR pojazdu mogła otworzyć się pod panelem ćwiczeń, jeśli
  natywną klawiaturę wywołano w tym czasie fizycznym klawiszem.
- Kierunek promienia, który nie jest wektorem jednostkowym, nie potwierdza celu.
- Trzy błędy były w samym nowym teście `practice_tools`, nie w kodzie:
  przypadek „inna dłoń” ustawiał tę samą dłoń, symulowane wskazywanie
  odrywało palec od nadgarstka, a pętla klikania kończyła się od razu na
  już zaliczonym etapie. Czwarty przypadek testu wymagał resetu przy
  niewidocznym czubku palca w pięści, wbrew zasadzie zwykłego sterowania;
  test sprawdza teraz utratę śledzenia nadgarstka i osobno potwierdza, że
  schowany czubek palca nie przerywa ćwiczenia.

## Walidacja offline

Release bez ostrzeżeń, **60/60 CTest**, **64/64 Python**, **78 kontroli EXE**,
samodzielny test DLL oraz test wstrzykiwania DLL również z
`SWVR_PROGRAM_TRACKING=0`.

- `practice_tools`: przebieg ćwiczeń, wejście/wyjście, utrata śledzenia
  i fokusu, błąd natywny, potwierdzenia celu, podglądy, ustawienia.
- `native_hand_buttons` (prawdziwy adapter na atrapie gry): gotowość przed
  szczypnięciem, typ nieobsługiwany, brak celu, element niebędący przyciskiem,
  natywnie wciśnięta dłoń, ruch dłoni, reset, wygaśnięcie po 100 ms, trzymanie.
- `native_locomotion` (prawdziwy adapter): wejście w ćwiczenia podczas sprintu
  i podczas trzymania kierownicy zwalnia wejście w grze; gesty w trakcie
  ćwiczeń nie wysyłają niczego; po wyjściu sterowanie wraca.
- `vr_settings_menu`, `menu_feedback`: dziewiąta strona, każdy przycisk ±,
  granice, stan przełączników, opis `?` dla każdej opcji na każdej stronie.
- `offline_tools`: nowe pola w profilach i w ich kopiach, starszy plik.
- `gl_hand_overlay` (GPU, oba oczy, stan OpenGL): trzy kolory promienia,
  wygaśnięcie, lewa/prawa dłoń, okręgi joysticków po właściwej stronie,
  próbka z innej klatki lub przestrzeni, obręcz wolna i trzymana, brak fokusu.

Obrazy z testu GPU obejrzano: `artifacts/vr-movement-menu-20261110.png`,
`vr-walking-tuning-menu-20261110.png`, `vr-steering-menu-20261106.png`,
`vr-practice-waiting-20261110.png`, `vr-practice-movement-20261110.png`,
`vr-wheel-grip-open/held-20261110.png`, `vr-target-ray-*-20261110.png`.
Strony menu obejrzano przy skali 1,4, panel ćwiczeń i podglądy przy 1,0;
teksty i elementy nie nachodzą na siebie. Napis blokady leży tuż nad
okręgami joysticków, bez nakładania.

Test na atrapach nie potwierdza reakcji prawdziwej gry ani pojazdu.

## Do oceny w goglach

1. Czy promień zieleni się na przyciskach Push/Toggle i dźwigni, a pozostaje
   szary w powietrzu; czy 100 ms nie powoduje migania koloru przy drżeniu dłoni.
2. Czy napisy blokady są czytelne i nie przeszkadzają nad okręgami joysticków.
3. Regulacja chodzenia: wygoda kroków 5/10 mm i progów sprintu.
4. Podgląd joysticków: położenie okręgów, czy nie zasłaniają kokpitu.
5. `USTAW CHWYT` w fotelu i zgodność obręczy z miejscem, w którym naprawdę
   trzymasz dłonie.
6. Ćwiczenia: czytelność panelu, przejście przez cztery etapy, wyjście,
   brak ruchu postaci i pojazdu w trakcie; wejście podczas jazdy.
7. Z poprzedniego etapu: `render_eye_cpu` i `gl_program_tracking` po
   optymalizacji hooków.
