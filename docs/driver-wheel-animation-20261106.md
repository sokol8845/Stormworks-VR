# Animacja kierownicy Driver seat — 2026-11-06

Dodano obrót widocznej obręczy, trzech szprych i piasty standardowego
**Driver seat** zgodnie z kątem dłoni. Dotychczas działał sygnał skrętu kół,
ale geometria fotela pozostawała statyczna. Animacja włącza się automatycznie
w istniejącym presecie VR z obsługą kierownicy. Zakres wynosi ±90°; puszczenie
obu dłoni przywraca położenie neutralne. Jedna trzymająca dłoń nadal steruje.

## Powiązanie z modelem

Model jest częścią statycznej geometrii pojazdu. Nie obracamy całego fotela
ani wspólnego zasobu. Przy starcie odczytywany jest lokalny plik
`rom/meshes/component_seat_racing.mesh`, bez dołączania zasobu gry do repo.
Zweryfikowany plik ma 80198 bajtów, SHA-256
`e3e7e3defcceceecf4205ce7178eef03b0ff8b9ff230733973c75fc7358274c7`
i FNV-1a64 `715c44215e6dc24c`. Parser łączy trójkąty i zduplikowane pozycje:
pięć części kierownicy to **606 wierzchołków / 180 unikalnych pozycji**,
łącznie z szarymi zakończeniami piasty. Fotel i kolumna są wyłączone z maski.

Środek obrotu w modelu to `(0.0000105, 0.4817497, 0.2528192)` m,
a oś `(0, sin(15°), -cos(15°))`. Obrót ma ten sam znak i nieskalowany kąt
co `HandSteering`, przed martwą strefą i wygładzeniem sygnału osi pojazdu.

Observer `native_driver_seat` korzysta z aktualizacji komponentu fotela
RVA `3487D0`, vtable `B09B18 + 358`. Weryfikuje pełny profil EXE, prefiks
funkcji, wpis vtable oraz `definition + 7F8 == 5` (`seat_pose`). Przekazuje
oryginałowi wszystkie 16 argumentów x64 bez zmian, dokładnie raz.
Argument 5 zawiera macierz sekcji `+18`, argument 6 przesunięcie sekcji;
siatka komponentu jest w `+18`, macierz obrotu w `+30`. Transformacja:
`section * [component_rotation, grid * 0.25 - section_offset]`.
To kopia danych bieżącego lokalnego fotela, ważna najwyżej 100 ms.
Wątek renderowania nie dereferencjonuje natywnych wskaźników.

## Renderowanie

`driver_wheel_shader` rozszerza w pamięci zgodne shadery `opaque`,
`opaque_damaged`, `depth` i `depth_cheap`. Wierzchołki są przeliczane do
układu fotela, odrzucane przez prosty obszar ograniczający, a następnie
porównywane ze zweryfikowanymi pozycjami z tolerancją 0,75 mm. Dzięki temu
działa też geometria fotela połączona z meshem całej sekcji pojazdu.

Obracane są pozycje i normalne. Aktualny i poprzedni kąt są wspólne dla
obu oczu; poprzednie pozycje zachowują poprawną historię dla motion vectors.
Głębia/cień korzystają z tego samego obrotu. Observer kamery koryguje zmianę
początku układu współrzędnych przed rysowaniem. Po każdym draw call własny
uniform aktywacji wraca do zera. Brak aktualnej identyfikacji, brak fokusu,
zmiana fotela albo niepoprawna macierz wyłącza animację.

Punkty maski są wysyłane raz na program, ponownie po linkowaniu. Nie ma
odczytów GPU podczas zwykłego działania. Nie zmienia się VBO, VAO, tekstur,
modelu kolizji ani plików gry. Błąd kompilacji przywraca oryginalny shader;
zmiana fingerprintu modelu wyłącza tę animację z wpisem diagnostycznym.
To wdrożenie dotyczy standardowego Driver seat, nie Helm ani innych modeli.

Log: `driver_wheel=ready_local_driver_seat_geometry`,
`driver_wheel_shader`, `driver_wheel_error`, `driver_wheel_totals`.
Licznik `draws` oznacza rysowania z aktywną maską, nie dowód wybrania
wierzchołków w konkretnej scenie. `identified` potwierdza świeżą kopię fotela.

## Weryfikacja offline

- Release: kompilacja zakończona powodzeniem.
- **56/56 CTest**, **51/51 pytest**, **78 kontroli profilu EXE**.
- Osobny test DLL/renderera `swvr_trace_test.exe`: PASS.
- Test observera: ABI wszystkich 16 argumentów, oryginał wywołany raz,
  lokalna tożsamość, rotacja komponentu, COM, odbicie natywnego układu,
  przeterminowanie, nieprawidłowa definicja i wyjście z fotela.
- Test GPU przez transform feedback: syntetyczna scena bez plików gry
  oraz wszystkie **2558 wierzchołków prawdziwego modelu** z czterema
  shaderami z lokalnej instalacji. Sprawdzono skręt ±0,7 rad, nieporuszanie
  fotela/kolumny, normalne, poprzednie pozycje, oba oczy, puszczenie,
  obcy/stary fotel, rebasing, błędne uploady i ponowne linkowanie.

Powtórzenie testu z lokalnymi zasobami (gra pozostaje zamknięta):

```powershell
./build/capture/Release/swvr_driver_wheel_tests.exe 'C:/Program Files (x86)/Steam/steamapps/common/Stormworks/rom/meshes/component_seat_racing.mesh'
./build/capture/Release/swvr_driver_wheel_gl_tests.exe 'C:/Program Files (x86)/Steam/steamapps/common/Stormworks/rom'
```

Wdrożenie i testy offline zakończone. **Obraz podczas rzeczywistej jazdy
w goglach wymaga potwierdzenia**: chwycić oburącz Driver seat, skręcić
w obie strony, puścić jedną i obie dłonie, powtórzyć podczas jazdy oraz
po wyjściu i wejściu do fotela. Sprawdzić też pobliski pusty fotel.
