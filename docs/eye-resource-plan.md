# Własne pakiety celów dla oczu — dowody i kontrakt

## Późniejsza weryfikacja w grze

`NativeEyeResources` został podłączony do hooka inicjalizacji pakietów, który
zapamiętuje rzeczywisty manager. Cztery używane funkcje mają kontrolę pełnego SHA
EXE i 24 bajtów wejściowych. Nie wywołujemy dodatkowego kroku managera: pierwsza
klatka XR jest odrzucana, dopóki zwykła pętla gry nie nada FBO własnym zasobom.

Pomiar źródła: 1280×720, 2 attachmenty, CC=1, C9=0. Po inicjalizacji własnego
pakietu funkcja resize z identycznymi skalarami zmienia jego G-buffer z trzech
attachmentów na dwa. Stara referencja tego G-buffera jest zwalniana przez resize;
helper usuwa ją ze swojego rejestru przed wywołaniem i rejestruje nową po powrocie,
aby nie zwolnić starej ponownie. Zmiany rozmiaru oraz E0/E8 nadal są wyłączone.
Źródło z trzema attachmentami pozostaje przy samym init, także gdy jego skalary
przy przyszłym resize wybrałyby dwa: odwzorowujemy faktyczny stan źródła.

Test `artifacts/fake-xr-20261028-212156-818.tsv`: 30 s, 1739 poprawnych par,
1 pusta klatka oczekiwania na zasoby, 0 błędów atrapy OpenXR. Sprawdzono unikalność
wskaźników zasobów i nazw FBO między grą oraz oboma oczami. Po teście zwolniono
referencje własnych zasobów i tablice pakietów przez natywne funkcje. Obrazy obu
oczu obejrzano; przedstawiają pełną scenę menu. Gra została zamknięta. Jest to
wykonany test pakietów w menu, nie dowód izolacji całego stanu renderera, cullingu
ani temporalnych kamer. Poniższa część zachowuje ustalenia wcześniejszej analizy.

Analiza statyczna pliku SHA-256 `f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`, Steam build 26275574. RVA odnoszą się wyłącznie do tej wersji. Nie uruchamiano gry podczas tego audytu i nie sprawdzono jeszcze własnego pakietu w działającym rendererze.

## Granica integracji

Piąty argument renderera `6D4DB0` jest wskaźnikiem do pakietu wielkości `0xF0`. Renderer ma dwa takie pakiety: `renderer+2628` i `renderer+2718`. Pierwszy służy głównemu obrazowi; drugi jest używany przez pomocniczą scenę. Nie wolno przejmować pomocniczego pakietu dla prawego oka, bo gra nadal go używa.

Init `6D1690` wywołuje `66E370` na pierwszym pakiecie przy `6D22FF`, a na drugim przy `6D2386`. Pierwszy otrzymuje rozmiary `graphics+8F0/+8F4`; drugi 1152×640. Odpowiednia rekonfiguracja w `6D2760` wywołuje `66EC10` na obu pakietach.

W menu renderer to `owner+570`, a pakiet to `owner+2B98`, czyli również `renderer+2628`. Potwierdza to instrukcja `840720` przed wywołaniem `840746`.

## Konstrukcja pustego obiektu

Znaleziono rzeczywisty konstruktor renderera `898210–89904D`. Fragment `898C3F–898CD1` inicjalizuje dwa osadzone pakiety bez osobnego konstruktora lub vtable:

| Przesunięcie względem pakietu | Stan początkowy |
| --- | --- |
| `+48` qword | 0 — tablica pierwszego kontenera |
| `+50` qword | 0 — pojemność i początek kontenera |
| `+58` dword | 0 — liczba wpisów |
| `+60` qword | 0 — tablica drugiego kontenera |
| `+68` qword | 0 — pojemność i początek kontenera |
| `+70` dword | 0 — liczba wpisów |
| `+E0`, `+E4`, `+E8`, `+EC` dword | 2, 2, 16, 1 |

Świeży, wyzerowany i wyrównany blok `0xF0`, z tymi czterema wartościami domyślnymi, odtwarza zaobserwowane wymagania konstrukcji. To wniosek z zapisów konstruktora i odczytów init, nie test wykonania. Nie kopiować kontenerów lub wskaźników zasobów z pakietu gry. Samo wyzerowanie nie tworzy zasobów GPU.

## ABI inicjalizacji i rekonfiguracji

`66E370–66EC02` tworzy zasoby. `66EC10–66FA64` rekonfiguruje już zainicjalizowany pakiet; wcześniejszy eksport kończący się na `66F800` był niepełny. Obie funkcje mają 18 pozycji argumentów zgodnych z ABI Windows x64. Dekompilator generuje niepewne typy; poniższa tabela wynika także z bezpośrednich instrukcji odczytu/zapisu. Wszystkie liczby i flagi są przekazywane w całkowitoliczbowych slotach, bez argumentów XMM.

| Nr | Wartość / pole źródłowe pakietu | Szerokość użyta przez funkcję |
| --- | --- | --- |
| 1 | docelowy pakiet | wskaźnik w RCX |
| 2 | resource manager | wskaźnik w RDX |
| 3 | szerokość (`+C0`) | uint32 w R8D |
| 4 | wysokość (`+C4`) | uint32 w R9D |
| 5 | `+D0` | uint32 |
| 6 | `+D4` | uint32 |
| 7 | `+E0` | uint32; init ignoruje i ustawia 2 |
| 8 | `+E4` | uint32, dzielnik rozmiaru; nie może być 0 |
| 9 | `+C8` | uint8 |
| 10 | `+CA` | uint8; tworzenie dwóch pięcioelementowych piramid |
| 11 | `+C9` | uint8 |
| 12 | `+CC` | uint32 |
| 13 | `+DC` | uint32 |
| 14 | `+D8` | uint8 |
| 15 | `+D9` | uint8 |
| 16 | `+E8` | uint32; init ignoruje i ustawia 16 |
| 17 | `+EC` | uint32 |
| 18 | `+DA` | uint8 |

Na wejściu funkcji argument 5 jest w `[entry_RSP+28h]`; kolejne sloty mają 8 bajtów. Należy użyć poprawnie zadeklarowanego typu funkcji, a nie ręcznie budować stos. Dla kopii konfiguracji trzeba odczytać wyłącznie powyższe skalary z istniejącego pakietu. Nazw funkcjonalnych większości flag nadal nie ustalono.

Init zawsze tworzy G-buffer z trzema attachmentami i domyślnym `E0=2`, `E8=16`. Gdy źródłowy pakiet ma inną konfigurację, wywołanie init z jego argumentami nie daje jeszcze identycznego rezultatu. Rekonfiguracja poprawia m.in. liczbę attachmentów do 2 lub 3 i obsługuje niestandardowe E0/E8. Zmiana E0 może zmienić także format zasobów `+20/+28` (gałąź bez zmiany rozdzielczości używa formatu 4, gałąź zmiany rozmiaru 7); nie należy zakładać, że dowolna sekwencja init/resize da identyczny układ. Pierwszy test powinien logować konfigurację i FBO przed użyciem.

## Własność zasobów i materializacja GPU

Fabryki `1B2630` i `1B2940` tworzą obiekty odpowiednio 0x58 i 0x70 bajtów. Po konstrukcji zwiększają refcount pod `resource+8` i rejestrują obiekt w pending liście `manager+C0` przez `1AD130`. Konstruktor bazy `1ADE30` ustawia FBO pod `resource+28` na 0. Wymiary znajdują się pod `+2C/+30`; zero wejściowe jest zamieniane na 1.

Pakiet zawiera zasoby pod następującymi przesunięciami:

`00, 08, 10, 18, 20, 28, 30, 38, 40, 78, 80, 88, 90, 98, B0, B8`.

`+40` może być null. `+98` jest pierwszym polem podstruktury tworzonej przez `66E1E0`; pola `+A0..+AC` to cztery rozmiary uint32, nie wskaźniki. Dodatkowe zasoby są w kontenerach `+48/+60`, po pięć wpisów, gdy dana funkcja jest włączona. Wpis wylicza się jako `array[(begin+i)%capacity]`. Nie mylić kontenerów z listą zasobów głównych.

`1B1B60(manager, graphics+8)` obsługuje kolejkę zasobów. Dla każdego obiektu z pending listy `manager+C0` wywołuje metodę `vtable+8`, następnie przenosi go na listę `manager+D8`. Dla single target metoda to `1AE530`, a dla MRT `1AEC00` (wskaźniki z vtable odpowiednio `B0A610` i `B5C490`). Samo wywołanie fabryki nie jest dowodem gotowości tekstur/FBO.

Główna ścieżka gry wywołuje `1B1B60` przy `7B0228`, potem `1B20D0` przy `7B0235`, zanim aktualizuje kamerę i rysuje świat. Późniejsze `1B20D0` także realizuje oczekujące cele oraz wykonuje inne zadania i odroczone usuwanie. Nie należy wywoływać go dowolnie pomiędzy oczami tylko po to, aby przyspieszyć inicjalizację własnych celów.

Praktyczna ścieżka pierwszego testu: zapamiętać manager z argumentu 2 prawdziwego init; utworzyć własne pakiety w kontrolowanym momencie; pozwolić zwykłej pętli managera je zrealizować; do tego czasu renderować normalnie. Przed użyciem własnego pakietu sprawdzić niezerowe i poprawne FBO wszystkich wymaganych zasobów, rozmiary, unikalność względem zasobów gry i między oczami. Nie wyprowadzać managera przez zgadywanie adresu listy `resource+20`, ponieważ obiekt zmienia listę w trakcie życia.

## Sprzątanie

Destruktor renderera `89A340` wywołuje `89ADD0` na `renderer+2718`, następnie `renderer+2628`. **`89ADD0` zwalnia tylko pamięć dwóch kontenerów `+48/+60` i zeruje ich liczniki. Nie zmniejsza referencji do celów GPU.** Samo wywołanie tego destruktora na własnym pakiecie spowodowałoby utratę dostępu do zasobów piramid.

Funkcja `8D7AC0(manager+C0, resource)` zmniejsza refcount. Przy przejściu do 0 usuwa zasób z indeksu managera; jeśli był na liście gotowych `manager+D8`, przenosi go na listę usuwania `manager+F0`. Jeśli był jeszcze na pending liście `manager+C0`, używa `1AD1A0`. Funkcja zakłada niezerowy wskaźnik i natychmiast odczytuje `resource+8`: null należy pominąć w kodzie wywołującym.

Proponowane sprzątanie własności: gdy pakiet nie jest już używany przez renderer, zwolnić każdą niezerową własną referencję zasobów (16 pól i wpisy kontenerów) przez `8D7AC0`, wyzerować te pola/wpisy, następnie wywołać `89ADD0` dla samych tablic i zwolnić własny blok. Nie usuwać natywnych obiektów przez C++ `delete` ani nie kasować ich GLuint bezpośrednio. Zachowanie odroczonego usuwania i awarii częściowego init wymaga testu; lista ta nie uprawnia do zwalniania obiektów należących do gry.

## Główna ścieżka kokpitu

Powrót `7B097C` ma mocne potwierdzenie jako główny obraz rozgrywki. Funkcja `7B0170` wywołuje `71A140(owner+CE8, ...)` przy `7B0411`; to zmierzony właściciel kamer kokpitu. Przed wywołaniem renderera przy `7B0977` przekazuje kamery `owner+1010`, `owner+1510`, `owner+1290`, czyli odpowiednio `camera_owner+328`, `+828`, `+5A8`, oraz pakiet `renderer+2628`.

Powrót `7B0A6A` jest odrębną gałęzią pomocniczą: dostaje `scene+E60`, pakiet `renderer+2718` i trzy wskaźniki do tej samej kamery zbudowanej na stosie. Nie włączać stereo dla obu callerów jako jednego głównego obrazu.

To przypisanie wynika ze spójności statycznego przepływu z wcześniejszym pomiarem kamer; nadal potrzebny jest pomiar hooka renderera w kokpicie, aby powiązać caller i końcowy obraz w wykonaniu.

## Pozostałe ryzyka integracji

Własne pakiety izolują opisane cele i ich zawartość, ale nie stan CPU całego renderera, obiekty shaderów, macierze poprzedniego obrazu czy przygotowany wcześniej culling. Nie stwierdzono, że podwójne wywołanie renderera jest pełnym krokiem symulacji; nie dowiedziono też braku wszystkich efektów ubocznych. Należy mierzyć to osobno. Nie przywracać całego renderera przez memcpy.

Pola `target0+48/+50/+54/+58` są kontenerem attachmentów G-buffer, a nie potwierdzonym pierścieniem historii temporalnej. Init dodaje formaty 5, 4 i 10 przez `1AEE70`. Wcześniejsze podejrzenie temporalnego charakteru tego kontenera zostało odrzucone.

## Materiał dowodowy

- `artifacts/eye-lifetime-audit.json`: konstruktor renderera, destruktor renderera, instrukcje inicjalizacji pustych pakietów.
- `artifacts/eye-release-audit.json`: destruktor kontenerów, pełna rekonfiguracja, konstruktor bazowego celu.
- `artifacts/eye-resource-owner.json`: użycie init/resize przez właściciela.
- `artifacts/eye-target-ctor.json`, `eye-target-14066e370.c`: pełny init; część resize w starszym JSON jest niepełna.
- `artifacts/eye-target-ownership.json`: fabryki, refcount, release, podstruktura +98.
- `artifacts/eye-resource-realize.json`: kolejka realizacji i sprzątania zasobów.
- `artifacts/renderer-callers.json`, `eye-gameplay-caller.c`: główne i pomocnicze wywołanie renderera.
- `artifacts/eye-resource-signatures.json`: pierwsze 24 bajty punktów wejścia, wraz z SHA pliku. Sygnatury są dodatkową kontrolą po zgodności całego SHA, nie samodzielnym identyfikatorem wersji.

Wyniki dekompilatora służą do nawigacji. Decyzje o typach, granicach funkcji i ownership należy weryfikować z zapisanymi instrukcjami oraz pomiarem wykonania.
