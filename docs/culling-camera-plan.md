# Kamery źródłowe i culling przed renderowaniem VR

Analiza statyczna Steam build 26275574, SHA-256 `f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`. Wszystkie adresy są RVA. Nie uruchamiano gry w ramach tego badania. Opis wskazuje punkty do ograniczonego eksperymentu; nie dowodzi poprawności pełnej integracji 6DoF.

## Najważniejsze ustalenie

Podmiana P/VP w obecnym hooku nie zmienia głównego testu widoczności: `2FA570` buduje sześć płaszczyzn ze skalarnego FOV, near/far, wektorów orientacji i pozycji kamery, bez czytania macierzy projekcji. Główna rozgrywka wykonuje culling `6D3830` **przed** wejściem do `6D4DB0`. Jest więc konkretny wcześniejszy punkt dla pozy głowy oraz osobny punkt dla frustum obejmującego oba oczy.

Nie należy przenosić na gameplay założenia o `6CD080`: jedyny znaleziony bezpośredni call tej funkcji jest w menu `840714`. `6E8B90` inicjalizuje kontenery i pola sceny; sam w sobie jest konstruktorem, nie dowodem odrzucania geometrii.

## Kolejność głównej ścieżki

Właściciel rozgrywki oznaczony jest niżej jako `owner`; obiekt kamer jako `C = owner+CE8`.

| Punkt | Wywołanie i dane |
| --- | --- |
| `7B1FC4` | `7B1E20(owner, ...)`: synchronizacja i odbiór gotowej sceny pod `owner+5C0`. |
| `7B1F21` wewnątrz odbioru | `71AF50(C, scene+5E8)`: kopiuje dwa wejściowe endpointy pozy do `C+0` i `C+80`. |
| `7B2040` | `7B0170(owner, graphics, renderer, scene, commands, ...)`: główna ścieżka prezentacji. |
| `7B0228`, `7B0235` | Manager zasobów, przed aktualizacją kamer. |
| `7B03F1` | `71B600(C, scene[83D], dt)`: aktualizacja wejścia kamery, m.in. kątów patrzenia i FOV. |
| `7B0411`, powrót `7B0416` | `71A140(C, graphics, scene[89C], owner[3C42])`: tworzy trzy źródłowe kamery i warunkowo czwartą. |
| `7B0420` | `71BE60(C, ...)`: wysyła rekord pozy/orientacji oparty na pierwszej kamerze do kolejki. Pełna rola odbiorcy nieustalona. |
| `7B05DA` | `2FA570(planes, owner+1790, graphics[900])`: test opcjonalnej sceny pomocniczej. Kamera to `C+AA8`. |
| `7B063E` | `6D3830(renderer, graphics, scene, C+AA8)`, gdy flaga `owner[3C41]` jest aktywna: culling list sceny. |
| `7B0977`, powrót `7B097C` | `6D4DB0` z kamerami `C+328`, `C+828`, `C+5A8` i głównym pakietem celów. |
| `7B0A65`, powrót `7B0A6A` | Oddzielna scena pomocnicza, osobna kamera stosowa i pakiet pomocniczy. |

Przygotowanie zawartości sceny odbywa się wcześniej; nie udowodniono, że zawiera wszystkie obiekty świata. Jednak `6D3830` jest już konkretnie rozpoznanym etapem cullingu, nie tylko domysłem wynikającym z nazwy „scena”.

## ABI wczesnego punktu koordynacji

`71A140–71AF47` odpowiada deklaracji:

```cpp
using CameraOwnerUpdate = void(*)(void* camera_owner, void* graphics,
                                  float interpolation_alpha, std::uint8_t update_fourth);
```

Na wywołaniu `7B0411`: RCX=`owner+CE8`, RDX=`graphics`, XMM2=`float(scene+89C)`, R9B=`byte(owner+3C42)`. Prolog zapisuje odpowiednio RDI, R12, XMM9 i R13D. Trzeci parametr nie jest wskaźnikiem ani argumentem R8: dekompilator bez pełnej analizy błędnie drukuje `undefined8`.

Sygnatura pierwszych 24 bajtów:

`4c 8b dc 55 57 49 8d ab f8 fc ff ff 48 81 ec f8 03 00 00 41 0f 29 7b a8`.

Warunkiem użycia musi pozostać również zgodność całego SHA. Hook przed oryginałem może pobrać jedną klatkę OpenXR i ustawić stan TLS na czas budowania kamer. Główne renderowanie później kończy tę samą klatkę. Nie wykonywać ponownego `xrWaitFrame` w rendererze. Nieukończoną klatkę anulować także wtedy, gdy główne renderowanie zostanie pominięte.

## Role czterech kamer: nie są czterema równoważnymi kopiami

`71C1E0(A,B,alpha,out)` interpoluje pozycję jako `A+(B-A)*alpha`; rotację interpoluje przez reprezentację quaternion. `71A140` pobiera A z `C+80`, B z `C+0`. Oba endpointy pochodzą z przyjętego rekordu sceny: `71AF50` kopiuje `record+80 → C+80` i `record+0 → C+0`. Nie przesuwa samodzielnie poprzedniego `C+0` do `C+80`. Nazwy „poprzedni/bieżący krok fizyki” są prawdopodobną interpretacją, ale producent rekordu nie został zbadany.

| Kamera | Składniki transformacji przed `719F20` | Powrót z aktualizacji |
| --- | --- | --- |
| `C+328` | Interpolacja A→B oraz aktualny transform patrzenia `C+220`. | `71AC89` |
| `C+828` | Endpoint A oraz transform patrzenia `C+108`. | `71ACF9` |
| `C+5A8` | Endpoint B oraz aktualny transform patrzenia `C+220`. | `71AD69` |
| `C+AA8` | Endpoint B oraz aktualny transform patrzenia `C+220`; tylko gdy `update_fourth != 0`. | `71ADD9` |

Na końcu `71A140` transform `C+220` jest kopiowany do `C+108`. Stąd **druga kamera zachowuje poprzedni transform patrzenia**. To mocny dowód temporalnej różnicy między kamerami; stosowanie aktualnej pozy głowy do wszystkich trzech usuwałoby tę różnicę dla ruchu głowy. Nie ustalono jeszcze pełnego znaczenia tych kamer we wszystkich shaderach.

W zmierzonym kokpicie widoki `71ACF9`, `71AC89` i `71AD69` były różne; `71AD69` i `71ADD9` były identyczne. Jest to spójne z opisanym przepływem, ale pojedynczy pomiar nie zastępuje śladu historii.

`719F20` wykonuje korekty pozycji, wpisuje `C+1C4 → camera+278`, a następnie robi tail jump przez `camera.vtable[0]`, czyli rozpoznane `194680`. Dlatego `_ReturnAddress` w hooku `194680` pozostaje jednym z czterech adresów `71AC89/...`, a transformacja wejściowa jest wtedy świeżo zbudowana. Czwarta kamera nie jest aktualizowana w każdej klatce: ślepe przesuwanie jej po każdym wyjściu z `71A140` spowodowałoby kumulację offsetu na starym obiekcie.

## Jak uniknąć podwójnego ruchu głowy

Najmniejszy spójny eksperyment z wczesną aktualizacją:

1. W hooku `71A140`, ograniczonym do callera `7B0416`, pobrać bieżące oczy raz. Zachować bieżący środek H oraz poprzedni środek Hprev z ostatniej zakończonej klatki; po utracie przestrzeni/kalibracji zainicjalizować Hprev=H.
2. Na czas oryginalnej funkcji włączyć TLS z oczekiwanym właścicielem i czterema konkretnymi adresami kamer. Przesuwać tylko rzeczywiście zaobserwowane źródłowe aktualizacje. Na `71AC89`, `71AD69`, `71ADD9` zastosować H; na `71ACF9` eksperymentalnie Hprev, zachowując rozpoznaną poprzednią orientację patrzenia. Ostateczne powiązanie temporalne wymaga pomiaru shaderów.
3. Najprościej zmienić świeży double transform **przed** oryginalnym `194680`, który przeliczy wszystkie cache raz. Jeśli implementacja robi to po aktualizacji, musi jeszcze wywołać oryginalny trampoline ponownie dla cache, bez rekurencji hooka.
4. W rendererze nie stosować ponownie pełnego H. Dla kamery z już zastosowanym H dodać tylko `inverse(H) * eye_pose`; dla historycznej kamery użyć odpowiadającego Hprev i poprzedniej pozy oka. Jest to resztkowy offset oka względem środka głowy, a nie stałe ±IPD przy dowolnej orientacji.
5. Przechowywać informację, które źródła rzeczywiście zmodyfikowano w tej klatce. Nie traktować starej czwartej kamery jako świeżej i nie aktualizować Hprev przed zakończeniem głównego renderowania.

Jeżeli czwarta kamera była zmodyfikowana w poprzedniej klatce, a w nowej nie przechodzi przez `71ADD9`, może nadal zawierać poprzedni H. Samo pominięcie ponownego offsetu zapobiega kumulacji, ale nie zapewnia aktualnego cullingu. Przy wymaganym cullingu bez świeżej czwartej kamery należy przerwać tę klatkę eksperymentu albo użyć osobnej kopii z zachowaną, jednoznacznie bazową transformacją. Nie odgadywać, że flagi `3C41` i `3C42` zawsze są zgodne.

Ta propozycja zachowuje zaobserwowaną strukturę temporalną, ale nie dowodzi poprawności macierzy historii `renderer+2BB0/+2CD8`. Wpływ wysyłanego przez `71BE60` rekordu na resztę gry też wymaga obserwacji. Oddzielnym, mniejszym eksperymentem może być modyfikowanie wyłącznie kopii kamery użytej przez culling, bez zmieniania źródeł gry; wtedy renderer nadal stosuje pełną pozę oczu do swoich kopii.

## Dokładna projekcja frustum `2FA570`

ABI: `void(void* six_planes, const void* perspective_camera, float aspect)`, RCX/RDX/XMM2. Funkcja czyta:

- `+278`: **pełny pionowy FOV w radianach**;
- `+26C/+270`: near/far;
- `+248/+254/+260`: trzy wektory float orientacji;
- `+68/+70/+78`: double pozycja.

Oblicza `t = tan(FOV * 0.5)`, `near_height = 2*t*near`, `near_width = near_height*aspect`. Stałe `BB31BC` float i `BB3600` double są równe 0.5. Rzeczywisty `camera-memory-01.bin`: FOV=1.0, P11=1.83048773 ≈ 1/tan(0.5), P00=0.76625073, potwierdzając jednostki i relację aspektu. Nie czyta P/VP ani asymetrycznych przesunięć projekcji.

W `6D3830` kopia kamery dostaje przesunięcie początku świata z pierwszych trzech double sceny. Aktualizacja kamery wraca do `6D391A`, a budowanie płaszczyzn wraca do `6D3947`. Near jest mnożony przez 0.25, far ustawiany na 20100. Używać efektywnego near/far tej kopii przy wyznaczaniu objętości widoczności, nie domyślnych near/far źródła.

Płaszczyzna ma stride 24 bajty: double D pod +0, float normalna XYZ pod +8/+C/+10, padding na końcu. Wnętrze spełnia `dot(n,p) >= D`. `186AB0(AABB,plane)` wybiera najdalszy w kierunku normalnej narożnik pudełka i zwraca odrzucenie, gdy jego iloczyn skalarny jest `< D`. W `6D3830` odrzucenie przez dowolną z sześciu płaszczyzn zeruje flagę widoczności obiektu; licznik wszystkich obiektów jest pod `scene+8D8`, widocznych pod `scene+8DC`.

Osobna funkcja `2FAE20` czyta również pola kamery `+27C/+280/+284` i buduje frustum z jawnych wymiarów, zamiast `tan(FOV/2)`. Nie wolno globalnie traktować jej argumentu jako rozpoznanego obiektu perspective o rozmiarze 0x280. Jej wywołania z renderera wymagają osobnej klasyfikacji.

## Konserwatywna objętość obu oczu

Samo `max(abs(angleLeft), angleRight, angleUp, abs(angleDown))` pomija przesunięcie oczu i aspekt. Dla równoległych oczu i wspólnego near n, prosty wystarczający limit stycznych uwzględnia `abs(eye_offset.x)/n` oraz `abs(eye_offset.y)/n`; niewielkie n może znacząco poszerzyć frustum.

Bardziej ogólna metoda, także dla canted views, bez zmiany formatu sześciu płaszczyzn:

1. Zbudować osiem narożników frustum każdego oka przy efektywnych near/far i rzeczywistych asymetrycznych FOV.
2. Przekształcić je do wspólnego układu kamery cullingu, uwzględniając różnicę jej endpointu B względem interpolowanej kamery renderowania. Culling używa `C+AA8`, obraz główny `C+328`; pominięcie tej różnicy może nadal odcinać geometrię przy ruchu pojazdu.
3. Dla wszystkich narożników z dodatnią głębokością `d=-z` wyliczyć `tx=max(abs(x)/d)`, `ty=max(abs(y)/d)`, `near_c=min(d)`, `far_c=max(d)`.
4. Przy aspekcie a używanym przez natywny builder ustawić `FOV_c = 2*atan(max(ty,tx/a))`, z małym marginesem numerycznym. Wszystkie narożniki spełniają sześć liniowych nierówności tej bryły; wypukłe frusta obu oczu są w niej zawarte.

Jeżeli jakikolwiek narożnik ma `d<=0`, skończony stożek z tym początkiem nie jest poprawnym ograniczeniem; nie clampować go do pozornie poprawnej wartości. Można użyć konserwatywnego pudełka z sześcioma płaszczyznami albo zrezygnować z tej ścieżki eksperymentu. Zmiany near/far i FOV powinny dotyczyć kopii używanej tylko do cullingu, aby nie zmieniać głębi renderowania i parametrów innych shaderów.

Najbardziej ograniczony hook frustum to `2FA570` z callerem `6D3947` oraz osobno `7B05DF`. Zwrócenie frustum obejmującego oba oczy umożliwi użycie jednej wspólnej listy widoczności przed dwoma renderami. Nie nadpisywać wszystkich callerów globalnie: wewnętrzne `6D6C35`, ścieżki pomocnicze i kamery specjalne mają inne role.

## Następny pomiar

Zapisać jednoznaczny numer klatki XR i alpha przy `71A140`; cztery adresy i oryginalne/zmienione T; informację o aktualizacji czwartej kamery; sześć płaszczyzn oraz liczniki `scene+8D8/+8DC`; przekazane trzy źródła w głównym rendererze. Test syntetycznej translacji/rotacji powinien wykazać zmianę widoczności przy krawędzi ekranu oraz brak podwójnego H. Dopiero potem porównać historyczne uniformy i ruch obrazu między klatkami.

## Materiał dowodowy

- `artifacts/culling-seams-a.json`: `719F20`, `71BE60`, `6D3830`, `2FA570`, `7B1F90`, konstruktor sceny.
- `artifacts/culling-seams-b.json`: `71C1E0`, `71B600`, `7B1E20`, `186AB0`, `2FAE20`.
- `artifacts/culling-seams-c.json`: `71AF50`, odbiór obu endpointów.
- `artifacts/culling-140*.c`: osobne teksty dekompilacji tych funkcji.
- `artifacts/culling-xrefs*.json`: kandydaci bezpośrednich wywołań i pełne granice wyliczone z chained unwind. Surowe wyszukiwanie rel32 służy nawigacji; kluczowe miejsca sprawdzono w instrukcjach Ghidry.
- `artifacts/cockpit-camera-owner.json`, `renderer-callers.json`, `cockpit-camera-layout.json`, `camera-memory-01.bin`: wcześniejsze instrukcje i pomiary.

Nie analizowano pełnego producenta sceny, wszystkich odbiorców kamer, odbić ani historii renderera. Wnioski o samym cullingu i ABI są bezpośrednie; znaczenie endpointów fizyki i pełna polityka historii pozostają do weryfikacji.

## Zaimplementowany wariant: niezależne półprzestrzenie stereo

Dodano czysty helper `make_stereo_cull_planes` w `include/swvr/stereo_culling.hpp` i `src/stereo_culling.cpp`. Przyjmuje natywną, właściwą sztywną transformację double, dwie względne pozy oczu, dwa FOV oraz dodatnie near/far. Nie zmienia pamięci kamer ani sceny. Zwraca sześć obiektów `NativeCullPlane` z potwierdzonym przez `static_assert` układem 24 bajtów: double D, trzy float normalnej, cztery bajty zerowego paddingu. Wnętrze to nadal `dot(n,p)>=D`.

Ten wariant usuwa ograniczenie wspólnego wierzchołka opisanej wyżej metody stożka. Tworzy 16 narożników przez `gameT * S * eyePose * cornerXR`. Wybiera sześć kierunków normalnych w średniej orientacji oczu, z nominalnym połączonym zakresem FOV (near, far, left, right, bottom, top). Przekształca i normalizuje kierunki, a **następnie zapisuje je jako float**. Dla każdej z tak zapisanych normalnych wyznacza `D=min(dot(stored_normal, corner)) - margin`. Każdy narożnik leży we wszystkich półprzestrzeniach. Ponieważ półprzestrzenie są wypukłe, zawierają także oba pełne skończone frusta i ich wypukłą otoczkę. Kierunki normalnych wpływają na szczelność objętości, nie na jej konserwatywność. Nie trzeba dzielić przez głębokość narożników ani wymuszać wspólnego dodatniego wierzchołka.

Margines wynosi `1e-5 + 64*epsilon_double*world_dot_term_scale + 8*epsilon_float*local_frustum_extent` metrów, a D jest dodatkowo zaokrąglane przez `nextafter` na zewnątrz. Pierwszy składnik to 10 mikrometrów. Drugi pokrywa utratę dokładności double przy dużych współrzędnych. Trzeci toleruje małe różnice lokalnej arytmetyki float używanej przez natywne projekcje i pozy, w tym float `tan(FOV/2)`; przy odległych płaszczyznach różnice te mogą wynosić milimetry. Nie używa się `epsilon_float * pozycja_świata`, które przy pozycji 1e9 niepotrzebnie rozszerzałoby bryłę o setki metrów. Kwantyzacja normalnych jest już uwzględniona w samym minimum D. Margines nie jest dowodem ograniczenia wszystkich błędów i przekształceń całego renderera.

Testy `tests/stereo_culling_tests.cpp` przeszły MSVC `/W4 /WX /permissive-`. Używają niezależnego wzoru macierzy obrotu oraz natywnej dokładności float stycznych. Sprawdzają narożniki i punkty wewnętrzne obu frustów, asymetrię, IPD, canted views, ruch/obrót głowy, obrót bazy, współrzędne 1e9, zmianę znaku quaternionu, poprawny kierunek native +Z, odrzucenie punktu poza objętością i niepoprawne wejścia. Przypadek regresji pokazuje narożnik widoczny w jednym oku, odrzucony przez monocularne frustum i zachowany przez stereo. Osobny szeroki przypadek przechodzi mimo narożników za płaszczyzną wierzchołka centralnej kamery.

Helper nie jest jeszcze dowodem poprawnego cullingu w grze. Wywołujący musi dostarczyć transformacje obu rzeczywistych renderowanych oczu w tym samym układzie world/floating-origin co AABB sceny, odpowiednie clip distances oraz poprawny numer klatki. Różnica interpolowanej kamery obrazu i endpointu używanego przez culling nadal musi zostać uwzględniona przez koordynator.
