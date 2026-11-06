# Kierownica, profile i diagnostyka offline — 2026-11-06

Wdrożono pięć uzgodnionych rozszerzeń. Gra pozostawała zamknięta.
Wejście do nowych opcji: **MENU → NARZEDZIA**. Każda opcja ma opis pod `?`.

## Kierownica

**KIEROWNICA → KAT / STREFA / FILTR**:

| Ustawienie | Znaczenie | Zakres | Domyślnie |
|---|---|---|---|
| Kąt | Obrót dłoni od środka do pełnego skrętu kół, osobno w lewo/prawo | 30–180° | 90° |
| Strefa | Martwa strefa wokół środka | 0–15° | około 1,43° |
| Filtr | Stała czasowa wygładzania; zero wyłącza filtr | 0–200 ms | 40 ms |

Mniejszy kąt oznacza czulszy skręt. Większy filtr łagodzi drżenie dłoni,
ale spowalnia reakcję. Animacja Driver seat korzysta z tego samego kąta co
sterowanie, również powyżej 90°. Zmiana ustawień zwalnia chwyt i wymaga
ponownego złapania kierownicy. Dotychczasowe wartości domyślne zachowano.

Plik konfiguracji obsługuje nową sekcję:

```ini
[steering]
full_lock_degrees=90
dead_zone_degrees=1.4323945
smoothing_ms=40
```

Brak sekcji w starszym pliku oznacza wartości domyślne. Błędne, zduplikowane
i niefinitywne wartości odrzucają cały plik.

## Profile sterowania

**PROFILE STEROWANIA** zawierają trzy niezależne miejsca: **CHODZENIE**,
**JAZDA**, **UZYTKOWNIK 2**. To własne zapisy, początkowo puste.

1. Ustaw sterowanie i komfort, wybierz nazwę i naciśnij **ZAPISZ**.
2. **WCZYTAJ** stosuje zapis po zwolnieniu ruchu i przycisków.
3. Kolejny zapis zachowuje poprzednią wersję jako `.bak`.
4. **WCZYTAJ KOPIE** stosuje poprzednią wersję; **ZAPISZ** utrwala ją w profilu.

Sam wybór nazwy niczego nie stosuje. Profile obejmują wszystkie ustawienia
`GestureSettings`, w tym tryb ruchu, obrót, kierownicę i interfejs. Nie obejmują
proporcji ciała, położenia świata ani sesyjnego przełącznika diagnostyki.
Po zmianie użytkownika nadal trzeba wykonać kalibrację ciała.

Pliki trafiają do `config/control-profiles/` obok aktywnego `gestures.ini`;
katalog jest pomijany przez Git. Zapis jest atomowy. Uszkodzony profil nie
zostaje automatycznie nadpisany, aby zachować możliwość odzyskania kopii.
Wczytane ustawienia zapisują się również jako aktywne preferencje następnej sesji.

## Panel diagnostyczny

**DIAGNOSTYKA: WL / WYL** włącza panel do odczytu po prawej stronie widoku.
Pokazuje rozpoznanie pięści, przypisanie dłoni do ruchu/obrotu/kierownicy,
wartości osi, sprint, stan fotela, liczbę przyjętych i odrzuconych kliknięć.
Wskazuje blokadę przez menu, klawiaturę, kalibrację, zmianę ustawień,
brak śledzenia/fokusu, gest systemowy Questa lub natywny stan przycisku.

`OST. ODMOWA` oznacza ostatnią zarejestrowaną odmowę, nie bieżący test celu.
Gotowy promień nie jest potwierdzeniem trafienia w natywny przycisk.
Panel nie przechwytuje gestów; odczyt odświeża się około 10 razy na sekundę,
a nieaktualny obraz panelu znika po 250 ms. Domyślnie jest wyłączony.

## Ostatnie minuty i zapis problemu

Przy włączonym nagrywaniu dłoni (standardowy preset testowy) obok sesji
powstaje katalog `<nazwa-sesji>.tsv.flight/`:

- Sześć rotujących segmentów po około 30 sekund: po rozgrzaniu około
  150–180 sekund historii. Każdy segment ma limit 8 MiB; limit rozmiaru
  może skrócić zachowane okno.
- Próbka co około 100 ms zawiera pełne pozy oczu/dłoni, flagi śledzenia,
  aktualne ustawienia i obserwowane wartości diagnostyczne.
- **ZGLOS PROBLEM - ZAPIS** zamraża bieżące okno w `incident-1`, `incident-2`
  lub `incident-3`. Zachowywane są trzy najnowsze zgłoszenia danej sesji;
  czwarte zastępuje pierwsze. Komunikat potwierdza dopiero zakończony zapis.
- Zapis i kopiowanie wykonuje osobny wątek. Kolejka ma limit 256 pozycji;
  pominięte próbki i błędy są jawnie liczone. Zamknięcie opróżnia kolejkę.
- Limit plików danych to 48 MiB bufora i po 48 MiB na każde z trzech
  zgłoszeń (plus niewielkie manifesty). Dane pozostają lokalnie.

Każdy segment zawiera własną konfigurację i pełne próbki. To obserwacje
stanu, **nie zapis wewnętrznego stanu automatów rozpoznających gesty**.
Dotychczasowe deterministyczne dzienniki `.hands.tsv` i `.controls.tsv`
zachowano z limitami 64 MiB. Nowy bufor działa również po ich zapełnieniu.
Próbkowanie 10 Hz może pominąć bardzo krótkie zdarzenia; do diagnozy należy
zachować również główny log sesji.

Podgląd bez gry i połączenia sieciowego:

```powershell
python tools/inspect_flight.py "artifacts/<sesja>.tsv.flight/incident-1" --html artifacts/problem.html
```

Można wskazać też główny katalog `.flight` po zakończeniu sesji. Odtwarzacz
pokazuje oś czasu, stan sterowania, ustawienia i dłonie z przodu/z góry.
Odrzuca ucięte segmenty. Pliku wynikowego nie nadpisuje.

## Raporty i sprawdzenie offline

Raport sprawdza zwolnienie przycisków zarówno dla starego `installed`, jak
i bieżącego `hooks_created`, również gdy pozostały same liczniki końcowe.
Wyświetla natywną klawiaturę, animację i sterowanie kierownicą, oba dzienniki
oraz rotujący bufor. Natywna klawiatura nie jest już opisywana jako lokalny
prototyp. Liczniki wywołań nadal nie stanowią dowodu wyniku widocznego w grze.

Regresje raportowania najpierw dały `2 failed, 9 passed`, po poprawce przeszły.
Zweryfikowano również prawdziwy zapis sesji 20:00: trzy wywołania zatwierdzenia
klawiatury, zakończenie zwalniania przycisków i oba osiągnięte limity nagrań.

Walidacja obejmuje Release, 58 testów CTest, 64 testy Python i 78 kontroli
profilu EXE. Testy obejmują konfigurację i granice kierownicy, obrót GPU ponad
90°, kopie profili, uszkodzone pliki, samodzielne segmenty, rotację czasową
i rozmiarową, zamrażanie i ponowne użycie miejsc zgłoszeń, błędy zapisu,
pomoc na ośmiu stronach menu oraz odtwarzacz HTML. Renderer offline sprawdza
oba oczy i zachowanie stanu OpenGL; wygenerowano obrazy nowych stron.

Do następnej próby w goglach pozostaje ocena czytelności diagnostyki,
wygody kąta/filtra, zmiana profilu w kokpicie i zapis problemu podczas sesji.
Poprzednia poprawka kierunku początkowego i tryb „tylko przód” również
nadal wymagają potwierdzenia użytkownika.
