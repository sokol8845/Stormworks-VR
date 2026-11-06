# Integracja renderera Stormworks — stan rozpoznania

Aktualnie analizowany build Steam `26275574`, 64-bit OpenGL, SHA-256 `f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`. Pierwszy raport instalacji dotyczył starszego buildu `25122525`. Rozpoznanie obejmuje pliki shaderów, ślad wywołań działającej gry i ograniczoną analizę kodu natywnego.

## Fakty istotne dla stereo

- `rom/graphics/shaders/ocean.glslv` używa `mat_view`, `mat_view_proj`, `camera_position` oraz `world_offset`. Odległość od kamery wpływa na wysokość wody, więc sama podmiana projekcji nie aktualizuje całego efektu.
- `light.glslf` czyta `gnormal`, `gcolor`, `gdepth` oraz `mat_view_proj_inverse`. Wskazuje to na etap oświetlenia korzystający z wcześniej przygotowanych buforów geometrii. Jego rekonstrukcja pozycji musi odpowiadać projekcji tego samego oka.
- Widoczne są współdzielone funkcje głębokości, macierze poprzedniej/następnej klatki, macierze kamery wody, `camera_position_world` i `graphics_offset`. Konkretna kolejność wywołań, culling i aktualizacja kamery wymagają pomiaru w procesie gry.

## Moduł pomiarowy

`swvr_gl_trace.dll` eksportuje `swvr_trace_start(void*)`, przyjmujący absolutną ścieżkę UTF-16 do logu, oraz `swvr_trace_flush(void*)`. Start musi nastąpić poza DllMain, przed pobraniem wskaźników funkcji GL przez aplikację. Po starcie DLL musi pozostać załadowana do końca procesu; nie wolno odłączyć jej podczas pracy hooków.

Moduł przechwytuje `wglGetProcAddress`, `SwapBuffers`, `glViewport`, `glDrawArrays` i `glDrawElements`. Podstawia obserwacyjne wrappery dla `glUseProgram`, `glBindFramebuffer`, `glGetUniformLocation`, `glUniformMatrix4fv`. Rejestruje kontekst, program, FBO, adres wywołujący i wartości macierzy; oryginalne argumenty przekazuje bez zmiany. W tej wersji nie rejestruje wszystkich komend OpenGL ani rysowania instancjonowanego. Wskazania macierzy są kandydatami do dalszej analizy.

Limit wynosi 250000 zdarzeń, aby ograniczyć zapis. Rejestracja z zapytaniami GL i synchronicznym I/O zwiększa czas klatki, dlatego nie służy do benchmarków. Moduł wymaga współczesnego kontekstu OpenGL; sprawdzony na NVIDIA OpenGL 4.6.

Po poprawce limit zapisuje ostatnie zdarzenia i `#state limit_reached` bez oczekiwania na kolejny swap. Nowe ślady zawierają `exe_sha256`, znacznik czasu PE i rozmiar obrazu. `tools/map_trace_calls.py` odrzuca niezgodny SHA, a ślad bez niego oznacza jako niezweryfikowany. Zmiana nie uzupełnia identyfikacji już istniejących śladów.

Wersja po analizie natywnej opcjonalnie przechwytuje aktualizację kamery. Profil w `include/swvr/game_profile.hpp` wymaga zgodności SHA i prefiksu kodu, w przeciwnym razie pozostaje wyłącznie pomiar GL. Hook wywołuje oryginalną funkcję, a następnie odczytuje widok, projekcję i VP. Rejestruje `camera_view`, `camera_projection`, `camera_view_projection`, wskaźnik obiektu i adres wywołujący. Przeszedł test zarówno w menu (120 próbek), jak i w potwierdzonym przez użytkownika kokpicie (328 próbek, 0 błędów relacji VP = P × V).

`scripts/capture-scene.ps1` wyzwala nowy pomiar na najbliższej granicy klatki przez lokalne zdarzenie Windows należące do modułu. Dopisuje do istniejącego logu kolejną `capture_epoch`, resetuje limit i odtwarza nazwy znanych uniformów. Oba analizatory przyjmują `--epoch N`. Test z shaderem utworzonym po zakończeniu pierwszego pomiaru i test z rzeczywistą grą potwierdziły tę ścieżkę.

Pomiar kokpitu: `artifacts/stormworks-cockpit-01.tsv --epoch 2`; walidacja: `artifacts/camera-cockpit-02.json`. Źródłowe kamery mają duże współrzędne świata, a kopie renderera mają przesunięty początek oraz zmienione near/far. Cztery wywołania źródłowe znajdują się w pełnej funkcji `0x71A140–0x71AF47`; `0x71AC50` jest fragmentem chained unwind, nie wejściem funkcji. Po pomiarze gra została zamknięta na polecenie użytkownika.

## Wynik ograniczonej analizy natywnej

Pełna analiza REA kończyła się wyczerpaniem sterty 2 GB, a po zwiększeniu do 8 GB — limitem czasu 330 s. Skrypt `scripts/ghidra/ExportCameraFunctions.java` z Ghidrą `-noanalysis -readOnly -deleteProject` dekompilował wskazane zakresy poprawnie. Nie wykonuje pełnej analizy referencji ani symboli, a zrekonstruowane typy argumentów są przybliżone.

- `0x194680`: aktualizacja kamery, argumenty wskaźnik kamery i float proporcji widoku. Widok float: `+0x88`, projekcja float: `+0xC8`, VP float: `+0x108`. Pola `+0x148` i `+0x1C8` zawierają macierze **double**, nie inverse float. Wektory orientacji, pozycja double oraz parametry frustum wymagają spójnej aktualizacji przy docelowym stereo.
- `0x67A990`, `0x68ECF0`, `0x68B0A0`: funkcje ustawiające uniformy. Druga korzysta z VP jednej kamery oraz widoku/projekcji drugiej; nie wolno globalnie podmieniać wszystkich uniformów o tej samej nazwie.
- Wszystkie trzy są wywoływane wewnątrz renderera zaczynającego się w `0x6D4DB0`. Zakres `0x6D692C–0x6DD2AE` ma chained unwind do tego wejścia, więc nie jest niezależną funkcją. Renderer tworzy lokalne kopie kamer i wywołuje ich aktualizację.
- Rzeczywista klatka 1085 ma widok o wyznaczniku -1. Moduł `camera_bridge.cpp` zachowuje tę bazę, mnożąc widok gry przez odwrotną pozę oka w przestrzeni kamery. Kopiuje współczynniki głębokości istniejącej projekcji i zastępuje tylko człony kątowe FOV. Nie usuwa odbicia przez konwersję na quaternion.

Testy modułu obliczeniowego przechodzą dla zaobserwowanych macierzy, IPD, obrotu i przesunięcia głowy oraz inverse VP. Nadal wymagane: potwierdzenie żywego hooka, aktualizacja pełnej kamery i cullingu, dwa renderowania tej samej chwili symulacji, osobne bufory/stan temporalny oraz podłączenie sesji OpenXR. Nie uzyskano jeszcze stereo Stormworks.

Po zwykłym zamknięciu poprzedniej gry testowej następny pomiar można uruchomić najnowszym buildem:

```powershell
.\build\next\Release\swvr_trace_launcher.exe 'C:\Program Files (x86)\Steam\steamapps\common\Stormworks\stormworks64.exe' "$PWD\artifacts\stormworks-camera-01.tsv"
python tools/verify_camera_probe.py artifacts/stormworks-camera-01.tsv 'C:\Program Files (x86)\Steam\steamapps\common\Stormworks\stormworks64.exe' --output artifacts/camera-probe-01.json
```

Walidator wymaga aktywnego hooka, identycznego SHA pliku, pełnych próbek kamery i relacji VP = P × V. Zwraca adresy wywołań do rozpoznania etapów renderowania. Wynik pozytywny nie oznacza jeszcze stereo ani zgodności wszystkich etapów gry.

Źródła MinHook pobrano z oficjalnego repozytorium autora i przypięto do commitu w `vendor/minhook/provenance.json`, wraz z licencjami. Launcher oraz zapis śladu w Stormworks zostały zweryfikowane.

## Zweryfikowany test

```powershell
.\build\Release\swvr_trace_test.exe
python tools/summarize_trace.py artifacts/trace-test.tsv
```

Test tworzy własny kontekst GL i program shaderów z macierzą `mat_view_proj`, rysuje trójkąt do FBO, sprawdza zielony piksel, a następnie weryfikuje log uniformu, macierzy, draw i swap. Pierwszy test używał ukrytego okna jako celu renderowania; poprawiono go na FBO, którego piksele nie zależą od widoczności okna. Test przeszedł po tej korekcie. Potwierdza zachowanie modułu w programie testowym, nie zgodność ze Stormworks.

## Następny krok

Przyporządkować zarejestrowane macierze do etapów świata/wody/UI, zweryfikować scenę z kokpitem, a następnie znaleźć możliwość ponownego wywołania renderowania świata bez drugiego kroku symulacji. Nie utożsamiać kopiowania obrazu końcowego z pełnym stereo 6DoF.

## Launcher i test w procesie gry

`swvr_trace_launcher.exe` uruchamia wyłącznie nowy `stormworks64.exe` lub projektowy `swvr_trace_test.exe`. Nie przyłącza się do istniejącego PID. Pod debuggerem zatrzymuje dziecko na wejściu PE (po inicjalizacji loadera Windows), przywraca oryginalny bajt i RIP, wstrzymuje główny wątek, odłącza debugger, ładuje DLL i wywołuje start w oddzielnym wątku. Dopiero potem wznawia wejście aplikacji. Nie edytuje plików gry. Inicjalizacja nie odbywa się pod blokadą DllMain.

```powershell
.\build\Release\swvr_trace_launcher.exe 'C:\Program Files (x86)\Steam\steamapps\common\Stormworks\stormworks64.exe' "$PWD\artifacts\stormworks-capture.tsv"
```

Log musi mieć nową nazwę. Launcher trzyma uchwyt procesu i czeka na zamknięcie gry. Ścieżka DLL pochodzi z katalogu launchera; adres funkcji loadera jest ustalany względem odpowiadającego modułu w dziecku, bez założenia identycznych adresów bazowych.

Test launchera z `swvr_trace_test.exe` zakończył się kodem 0 i potwierdził zapis śladu oraz niezmieniony odczyt piksela. Pierwsze uruchomienie gry zakończyło się bez użytecznego śladu. Po odzyskaniu dostępu przez zatwierdzoną powłokę poza sandboxem poprawiono opróżnianie bufora logu przy inicjalizacji i wykonano drugi pomiar.

`artifacts/stormworks-startup-02-summary.json` zawiera 1113 swapów, 115504 wywołania macierzy, 81932 drawElements, 7524 drawArrays i 5407 zmian FBO. Pomiar obejmuje ładowanie oraz macierze perspektywiczne, ale widok kokpitu nie został potwierdzony wizualnie. Analizator pomija 9698 wywołań z lokalizacją uniformu -1 (OpenGL je ignoruje). Zapis jest bliski limitowi; brak końcowego znacznika nie dowodzi pełnego przechwycenia.

Adres bazowy procesu: `0x7FF74D980000`; baza obrazu PE: `0x140000000`, rozmiar `0xD8E000`. Kandydaci to adresy powrotu z wywołań, nie początki funkcji:

| RVA | Zaobserwowany uniform |
| --- | --- |
| `0x67AA07` / `0x67AA22` | mat_view / mat_view_proj |
| `0x68ED84` / `0x68EDA2` | mat_view / mat_proj |
| `0x68EDCD` / `0x68ED69` | inverse projekcji / mat_view_proj |
| `0x68B197` / `0x68B1B2` | mat_view / mat_proj |

Lokalny REA 6.8.0 z Ghidrą 12.1.4 jest dostępny przez `scripts/rea.ps1`. Wywołanie `doctor --provider ghidra --format json` potwierdziło gotowość Node, JDK, dekompilatora oraz zabezpieczeń procesu Windows. Analiza statyczna musi ustalić funkcje zawierające te adresy i zależności stanu renderowania przed podłączeniem kamery VR.

Przy ostatnim sprawdzeniu gra testowa miała PID 45172. Przed kolejnym startem sprawdzić aktualny proces. Zwykła powłoka nadal zwraca `CreateProcessAsUserW failed: 5`, a UI jest niedostępne; zatwierdzona powłoka poza sandboxem działa. Przyczyna awarii narzędzi nie jest ustalona.
