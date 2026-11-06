# Pochylony horyzont i pozycja wewnątrz awatara — punkt zaczepienia głowy

Zakres: odczyt lokalnych artefaktów i analiza statyczna Stormworks build 26275574, SHA-256 `f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`. Nie uruchamiano gry ani nie zmieniano kodu moda w tym badaniu.

Aktualizacja 2026-11-02: późniejsza próba na buildzie 26294447 jest opisana
w [statusie](status.md). Użytkownik potwierdził poprawną orientację, ale zgłosił
wychodzenie kamery z awatara przy obrocie i wychylaniu oraz poprosił o ruch
głowy i rąk postaci zgodny z VR. W tamtej próbie implementacja nie zmieniała
szkieletu. Później wdrożono opcjonalne `AvatarPose`, opisane w [statusie](status.md)
i [instrukcji próby](game-preview.md); ta funkcja czeka na swój pierwszy test gry.
Sam obrót fizycznej głowy może też przesuwać oczy względem szyi; zgłoszenie
nie jest dowodem na potrzebę wyłączenia translacji 6DoF ani stały offset.

Odczyt zainstalowanych modeli potwierdził po 19 kości oraz łańcuchy bark–łokieć–
dłoń po obu stronach. [Dane zasobów](../artifacts/avatar-rigs-20261102.json)
zawierają hashe, nazwy, indeksy, rodziców i surowe transformy. Vertex shadery
`opaque_anim`, `glass_anim`, `additive_anim` używają `bone_matrices[64]`
i `bone_matrices_prev[64]` oraz interpolacji przez `blend_factor`.
Synchronizacja awatara musi uwzględniać oba zestawy, transform obiektu,
skalę oraz przesunięcie początku świata. Nie należy podstawiać bezpośrednio
pozycji LOCAL z OpenXR do macierzy kości. Tych transformów ani powiązania
palety z bieżącą lokalną postacią nie zweryfikowano jeszcze dla nowej funkcji.

Użytkownik zgłosił lekko pochylony horyzont oraz widok wnętrza głowy/ciała awatara przy ruchu głową. To dwie obserwacje wymagające osobnych testów: błędna kalibracja/orientacja może przesuwać kamerę względem awatara, ale poprawnie śledzona kamera też może wejść w niewyłączoną geometrię własnej głowy. Sam clipping nie dowodzi, że potrzebna jest stała poprawka o kilka centymetrów.

## Co zawiera rzeczywisty pomiar

`artifacts/headset-xr-20261028-230241-819.tsv.camera-world.tsv` zawiera natywny `base_render` przed offsetami XR. W odczytanym pliku było 10102 próbek. Baza jest sztywna: maksymalny błąd ortonormalności około `1.95e-7`.

- Pierwsza próbka: `right.y=0.07104`, `up.y=0.31039`, `forward.y=-0.94795`. Kąt odchylenia wektora up od pionu to 71.92°, lecz większość jest pochyleniem przód/tył, nie przechyleniem na bok. Dekompozycja Y-X-Z daje pitch 71.43°, roll 12.89°.
- W całym zapisie roll Y-X-Z ma zakres około −15.97°…15.62°, medianę 1.14°. Mediana pitch to −32.72°. Pojedyncza środkowa klatka nie jest statystyczną medianą komponentów ani kątów.
- Zapis nie zawiera `origin` XR, bezwzględnych poz oczu, lokalnego patrzenia `C+220`, wcześniejszego `C+108`, flag trybu ani `C+214`. Nie można z niego rozdzielić ruchu pojazdu/ciała, patrzenia myszą i kalibracji gogli.

Powtarzalny odczyt i wybrane pełne macierze zapisano w `artifacts/head-anchor-captured-fixture.json`; odtworzenie: `python artifacts/head_anchor_analysis.py`. Kąty Y-X-Z to diagnostyczna dekompozycja macierzy, nie potwierdzone wartości natywnych ustawień.

## Potwierdzona kolejność iloczynów

`17A260(out,A,B)` oblicza **A × B** dla kolumnowego układu macierzy double. Przykładowo pierwszy element to `A[0]*B[0] + A[4]*B[1] + A[8]*B[2] + A[12]*B[3]`. To nie jest odwrotna kolejność. `1773A0` robi odpowiedni A × B dla rotacji 3×3 float.

Oznaczenia:

- `C`: właściciel kamer, czyli główny `owner+CE8`;
- `A=C+80`, `B=C+0`: dwa endpointy otrzymane z rekordu sceny;
- `I=interpolate(A,B,alpha)`, gdzie alpha pochodzi z `scene+89C`;
- `L=C+220`: aktualny lokalny transform patrzenia;
- `Lprev=C+108`: poprzedni transform patrzenia **na wejściu** do `71A140`.

Przed funkcją korekt `719F20`:

| Kamera | Transform |
| --- | --- |
| `C+328`, główny obraz | `I × L` |
| `C+828`, drugi argument kamery renderera | `A × Lprev` |
| `C+5A8`, trzeci argument | `B × L` |
| `C+AA8`, warunkowa kamera cullingu | `B × L` |

Na końcu `71A140` następuje kopiowanie **L → Lprev**. Dlatego odczyt `C+108` dopiero po oryginalnym `71A140` nie daje już poprzedniego patrzenia. Do poprawnej rekonstrukcji drugiej kamery trzeba zapisać `C+108` przed oryginalnym wywołaniem albo odczytać go przy aktualizacji `71ACF9`.

`L` obejmuje rotacje skonstruowane z pól `C+2A0`, `C+2A4`, `C+2C0`, a potem iloczyn z macierzą 3×3 pod `C+2DC`. Struktury tych trzech rotacji odpowiadają osiom Y, X i Z. Pola te są zmieniane przez wejście kamery w `71B600`; ostatnia macierz jest także używana przez gałąź efektu o parametrach `2CC..2D8`. Nie jest to wyłącznie dwa kąty myszy. Specjalna gałąź `1A4` dodatkowo interpoluje transform L względem poprzedniego; nie zakładać, że jego translacja zawsze jest zerowa.

Endpointy mogą być modyfikowane przez `71A140` w specjalnych trybach. Gałąź `1A0 && 1A2 && !1A6` z polem `19C` może m.in. pozostawić tylko poziomy kierunek lub zastąpić rotacje. Rekonstrukcja `R(T)*transpose(R(L))` odzyskuje **efektywną bazę kamery w danym trybie**, a nie gwarantowany surowy transform pojazdu.

## Translacja dodawana przez `719F20`

Ta funkcja nie obraca kamery. Po złożeniu powyższych macierzy zmienia pozycję, wpisuje FOV z `C+1C4` i wykonuje tail jump do aktualizacji `194680`.

Priorytet gałęzi i dokładne działanie:

| Warunek | Zmiana pozycji |
| --- | --- |
| `C[1A6] != 0` | `p += R(T) * (0,0,-40)`; stała double `BB4398=-40.0`. |
| `1A6 == 0` i (`1A4 != 0` lub `1A5 != 0`) | `p += (double(C+1E0), double(C+1E8), double(C+1F0))` w osiach świata. |
| powyższe nieaktywne i (`1A0 != 0` lub `float(C+214)>0`) | `p += R(T) * (0,0,-d)`, `d=float(C+214)`; stała float `BB4130=-1.0`. |
| inaczej | brak zmiany pozycji. |

`C+214` jest więc odległością cofnięcia kamery wzdłuż jej kierunku patrzenia, nie wysokością oczu. `71B600` ustala cel tej odległości na zero, gdy `1A0==0`, lub na `C+218/C+21C`, gdy flaga jest aktywna; następnie wygładza zmianę, z dodatkowym limitem `C+188`. To jest spójne z kamerą trzeciej osoby i przejściem do pierwszej, ale nazwy użytkowe flag nie są potwierdzone samymi instrukcjami.

`71AF50` kopiuje flagi z rekordu sceny: `record[10A]→C[1A0]`, `record[10B]→C[1A4]`, `record[10C]→C[1A5]`, `record[10E]→C[1A6]`. Zmiana `1A4/1A5` zeruje wektor `C+1E0..1F0`. Nie nadawać im definitywnych nazw „free camera”, „map” itp. bez testu przełączania trybów.

## Jak usunąć lokalne patrzenie bez utraty punktu oczu

Niech M będzie zmierzoną kamerą **po** `719F20`, a L odpowiednim aktualnym/poprzednim patrzeniem. Translacyjne korekty nie zmieniają rotacji, zatem:

`R_anchor = R(M) × transpose(R(L))`.

Zastąpienie całej macierzy przez `M × inverse(L)` jest niebezpieczne: usuwa również translację L. Może to przemieścić punkt oczu, nawet jeśli wyeliminowanie rotacji jest prawidłowe. Dla trybu pierwszej osoby bez cofnięcia należy zachować zmierzoną `M.position` i zmienić tylko bazę orientacji.

Jeśli pomiar potwierdzi aktywne cofnięcie d, pozycja przed korektą wynosi:

`p_pre = M.position + d * M.forward_column`.

Dla gałęzi światowej odejmuje się dokładnie zarejestrowany wektor `1E0..1F0`. Dla trybu `1A6` d=40. Usunięcie tych korekt powinno być jawną polityką obsługiwanych trybów, nie automatycznym założeniem, że każdy widok ma być przerobiony na pierwszą osobę. Dla pierwszego testu rozsądna jest odmowa niepotwierdzonego trybu lub poczekanie aż cofnięcie wygaśnie do wartości bliskiej zera.

Tak odtworzona baza zachowuje obrót ciała/pojazdu występujący w natywnym endpointcie. Zastąpienie jej macierzą „zawsze pionową względem world Y” usunęłoby również prawdziwy przechył pojazdu. Nie jest to równoważne usunięciu patrzenia myszą.

## Co może robić pełna kalibracja XR

Aktualny origin jest pełną pozą środka oczu, a względne pozy wylicza się przez `inverse(origin) * eye`. Oznacza to, że początkowe pitch/roll zostają wpisane do układu odniesienia zarówno dla orientacji, jak i translacji. Jeżeli użytkownik podczas kalibracji miał głowę pochyloną, późniejsze fizyczne wyprostowanie daje przeciwny względny obrót; przesunięcie głowy przód/tył jest przeliczane w obróconych osiach origin.

Kalibracja zachowująca pion, z pozycją środka oczu i samym yaw zamiast pełnej rotacji, jest konkretną alternatywą do testu. Nie usuwa ona jednak lokalnego patrzenia już zawartego w natywnym M. Potrzebne są obie osobno zdefiniowane decyzje: baza natywnej kamery i układ kalibracji XR. Bez zapisu origin/oczu nie ma dowodu, że początkowy roll gogli był przyczyną zgłoszonego przechylenia.

## Minimalny następny pomiar

Na tę samą klatkę i ten sam `predictedDisplayTime` zapisać:

1. Bezwzględne pozy obu oczu, pełny origin, wybrany yaw-only origin i względny środek/oczy.
2. `C+108` przed `71A140`, `C+220` po nim, alpha, endpointy A/B i trzy źródłowe T po korektach.
3. Flagi `1A0/1A2/1A4/1A5/1A6`, odległość `214` i wektor `1E0..1F0`.
4. Wynikowy anchor i ostateczne macierze każdego oka.

Sprawdzić: głowa nieruchoma przy ruchu samej myszy; fizyczny roll i powrót do pionu; fizyczna translacja bez obrotu; zmiana pierwsza/trzecia osoba; nieruchomy i przechylony pojazd. Porównać z selektywnie ukrytą głową własnego awatara — to oddzielny test geometrii, nie korekta punktu oczu.

## Powtarzalne testy, które powinny wykrywać błąd

Fixture JSON zawiera prawdziwe macierze oraz osobną sekcję **syntetycznego** rozkładu wybranej zmierzonej M na znane nieprzemienne body/look/backoff. Syntetyczne czynniki nie są odczytem tych wartości z gry. Rekonstrukcja M ma błąd `2.22e-16`; lokalna translacja testowa ma długość 12.53 cm, więc usunięcie pełnego L zamiast samej rotacji powoduje łatwo wykrywalną zmianę punktu oczu.

- Rozkład M=B×L: funkcja usuwająca look ma odzyskać B.rotation, nie `transpose(L.rotation)×M.rotation`. Nieprzemienne kąty fixture odróżniają obie kolejności.
- Niezerowa translacja L: zachowanie światowego punktu oczu musi przejść; ślepe `M×inverse(L)` ma oblać test.
- Zmienne cofnięcie d: dokładne odwrócenie znanego d ma przywrócić punkt przed cofnięciem. Jedna stała korekta w centymetrach nie może przejść dla kilku d i różnych kierunków patrzenia.
- Prawdziwy bank B: usunięcie look ma zachować bank B. Zerowanie całego roll/pitch bazy musi oblać ten test.
- Początkowy roll/pitch gogli, późniejsza pionowa głowa: polityka yaw-only ma utrzymać pion oraz poziome osie translacji. Pełna kalibracja stanowi wariant reprodukujący różnicę, ale nie dowód historycznej pozy użytkownika.
- Historia: zmienne Lprev/Lcurrent wymagają odczytu Lprev przed `71A140`. Odczyt po końcowym kopiowaniu ma oblać test rekonstruowania drugiej kamery.

Materiały: `head-anchor-math.json` i wydzielone `.c` (17A260/1773A0), `cockpit-camera-owner.json/.c`, `culling-140719f20.c`, `culling-14071b600.c`, `culling-14071af50.c`, rzeczywisty plik `.camera-world.tsv`, `head_anchor_analysis.py` i `head-anchor-captured-fixture.json`.

## Czysty helper rekonstrukcji

Zaimplementowano `make_head_anchor(final_camera, look, settings)` w `include/swvr/head_anchor.hpp` i `src/head_anchor.cpp`. Helper nie dotyka pamięci gry ani nie stosuje pozy XR. Zwraca właściwą sztywną macierz w natywnych osiach gry, przed odbiciem S używanym przez adapter OpenXR.

`HeadAnchorSettings` zawiera:

| Pole | Wartość |
| --- | --- |
| `mode_bits` | Maska kompaktowa: `head_mode_1a0=1`, `head_mode_1a4=2`, `head_mode_1a5=4`, `head_mode_1a6=8`. Wywołujący składa ją z czterech osobno odczytanych bajtów właściciela. |
| `backoff_distance` | Float z `C+214`; również ujemna wartość ma określone znaczenie w kodzie natywnym. |
| `world_shift` | Trzy double z `C+1E0/+1E8/+1F0`. |

Funkcja najpierw sprawdza skończoność i sztywność obu wejściowych macierzy, dodatni wyznacznik, postać affine oraz znane bity i skończone parametry trybu. Dla odtworzenia orientacji mnoży `R(final_camera)` przez transponowane `R(look)`. Dla pozycji odwraca dokładnie jedną gałąź o natywnym priorytecie: `1A6` przed `1A4/1A5`, następnie `1A0 || distance>0`, na końcu brak korekty. Ujemne distance jest odwracane przy aktywnym `1A0`, a ignorowane przy nieaktywnym `1A0`, o ile nie zwyciężyła inna gałąź. Korekta local-Z używa kierunku **final_camera**, ponieważ to wzdłuż złożonego patrzenia natywny kod przesunął kamerę. Użycie odzyskanej osi ciała do odwracania tego przesunięcia byłoby błędem.

Pozycja sprzed cofnięcia pozostaje w double; translacja look nie jest odejmowana, a rzeczywisty bank bazy nie jest zerowany. Wynik jest ponownie walidowany, w tym po ewentualnym przepełnieniu odejmowania światowego przesunięcia. Nieznane bity i odbite macierze są odrzucane, zamiast zgadywania ich znaczenia.

`tests/head_anchor_tests.cpp` przeszedł MSVC C++20 `/W4 /WX /permissive-`. Testy tworzą finalną kamerę niezależnie przez iloczyny macierzy i sprawdzają 48 kombinacji wszystkich 16 masek trybu z odległościami −2, 0 i 0.25, w tym priorytet gałęzi przy kilku flagach naraz. Sprawdzają współrzędne 1e9, zachowanie punktu oczu i banku ciała, nieprzemienną kolejność rotacji, błąd 12.53 cm powodowany usunięciem pełnego look, syntetyczny rozkład rzeczywistej macierzy z klatki 35476 oraz 12 przypadków niepoprawnych danych.

To weryfikacja matematyki i odtworzenia poznanego kodu `719F20`, nie test źródła zgłoszonego problemu na goglach. Podłączenie helpera nadal wymaga zgodnej wersji gry, poprawnej kolejności odczytu Lprev/Lcurrent, parametrów trybu z tej samej klatki oraz zastosowania wyników wyłącznie do właściwych kopii kamer i bazy cullingu. Nie uruchamiano Stormworks w ramach implementacji tego helpera; użytkownik zabronił obecnie uruchamiania gry.
