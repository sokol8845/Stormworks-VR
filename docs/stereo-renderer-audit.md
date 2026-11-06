# Audyt podwójnego wywołania renderera

Stan: 2026-10-28. Build Steam 26275574, SHA-256 `f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`. Adresy poniżej są RVA względem modułu EXE. Audyt statyczny i wcześniejsze ślady; podczas audytu nie uruchamiano gry.

## Wniosek dla eksperymentu

Ograniczony test dwóch renderowań **wyłącznie potwierdzonej ścieżki menu** jest uzasadniony jako diagnostyka. Nie jest dowodem poprawnego stereo w kokpicie ani izolacji historii. Każde oko powinno dostać trzy własne kopie kamer, spójnie przeliczone oryginalną funkcją aktualizacji. Wynik należy przechwycić z aktualnego końcowego FBO po powrocie renderera. Samo związanie własnego FBO przed wywołaniem nie wystarczy: renderer wiąże cel ponownie.

Test powinien być jawnie opcjonalny, ograniczony czasowo, chroniony przed rekurencją i pomijać dodatkowy przebieg, gdy `renderer+0x2BF0 != 0`. Ten bajt uruchamia inicjalizację zasobów. Odczyt/porównanie obrazów, pól i stanu GL rozstrzygnie działanie diagnostyki; poniższa analiza nie zastępuje takiego pomiaru.

## Wejście, kamery i czas życia

Pełna funkcja zaczyna się w `0x6D4DB0` i kończy przed `0x6DDB67`. Ma cztery połączone wpisy unwind: `6D4DB0–6D692C`, `6D692C–6DD2AE`, `6DD2AE–6DDA76`, `6DDA76–6DDB67`. Starszy plik `renderer-6d4db0.asm` urywa się za wcześnie; pełny zapis to `artifacts/renderer-full-audit.asm`.

Prolog potwierdza osiem argumentów w miejscach ABI x64 Windows. Pierwsze cztery to `RCX` (renderer), `RDX` (kontekst grafiki), `R8` (przygotowana scena), `R9` (obiekt o nieustalonej roli). Stos zawiera pakiet celów oraz trzy wskaźniki kamer. W prologu są to odpowiednio `[rbp+2A60]`, `[rbp+2A68]`, `[rbp+2A70]`, `[rbp+2A78]`. Typy klas i deklaracja zwracanej wartości pozostają rekonstrukcją, lecz użycie ośmiu wskaźników jest mocno poparte kodem i miejscem wywołania menu.

Kamery mają rozpoznany obszar `0x280` bajtów: transformacja double od `+8`, widok float `+88`, projekcja `+C8`, VP `+108`, widok double `+148`, VP double `+1C8`, wektory orientacji `+248/+254/+260`, near/far `+26C/+270`, FOV `+278`. `+274` nie ma potwierdzonej semantyki. Kopia musi zachować oryginalne bajty, w tym vtable; nie konstruować fikcyjnej klasy z samych macierzy.

Renderer tworzy trzy lokalne kamery, kopiuje transformacje i FOV, dodaje przesunięcie świata z pierwszych trzech double sceny, zmienia near/far i wywołuje `0x194680`. Adresy powrotu tych aktualizacji to `6D4F50`, `6D4FF7`, `6D509A`. Ważny dodatkowy odczyt: `6D50EA–6D50F8` kopiuje **źródłową** macierz VP drugiej kamery (`arg7+108`) do `renderer+2BB0`. Dlatego po przesunięciu transformacji każdej kopii trzeba wywołać oryginalną aktualizację przed wejściem renderera, mimo późniejszego przeliczenia kamer lokalnych.

Zaobserwowane użycie kamer jest synchroniczne i obejmuje kopie na stosie. Nie ustalono pełnego grafu wywołań ani formalnie nie wykluczono zachowania wskaźników przez dalszych odbiorców. Nie należy utrzymywać wskaźników do kopii stosowych poza zakresem wywołania.

## Końcowy obraz i stan GL

Pomocnik `0x6DDB70`, używany w kilku gałęziach końcowego kompozytora, odczytuje `scene+828`:

| Warunek | Cel |
| --- | --- |
| wskaźnik równy zero | FBO 0, viewport z `graphics+8F0/+8F4` |
| wskaźnik niezerowy | `object = *(*(scene+828)+8)`, FBO GLuint `object+28`, szerokość `+2C`, wysokość `+30` |

Pomocnik wiąże `GL_FRAMEBUFFER`, ustawia viewport, maskę koloru i zapis głębokości, następnie czyści kolor i głębokość. Wiązanie przed rendererem zostanie nadpisane. Przechwycenie po rendererze powinno odczytać rzeczywisty draw FBO i rozmiary; nie zakładać automatycznie ekranu/default framebuffer. Własne kopiowanie i prezentacja muszą przywracać zmieniony stan GL, zwłaszcza niezależne read/draw FBO, read/draw buffer, viewport i scissor.

`renderer+2E60` wybiera gałąź końcowego obrazu: wartość ujemna prowadzi przez normalne etapy postprocessingu, nieujemna przez kompozytor `690F40/691050` z wyborem modulo 6. To wygląda na tryb wizualizacji; **nie ma dowodu, że to licznik klatek**. W normalnej gałęzi wywołania końcowego wiązania występują m.in. przed `6DD8BE`, `6DD8E9`, `6DD949`; w pozostałej gałęzi przy `6DD997`.

`691050` odczytuje pola pierwszego celu z pakietu arg5: tablica tekstur `+48`, pojemność `+50`, początek kontenera `+54`, liczba wpisów `+58`; próbkuje także wpis `(index+1)%capacity`, a przy trzech wpisach `(index+2)%capacity`. Dalsze badanie konstruktora potwierdziło, że jest to kontener attachmentów G-buffer, **nie rozpoznany pierścień historii temporalnej**: `66E370` dodaje formaty 5, 4 i 10 przez `1AEE70`. `1ADEE0` wiąże i czyści FBO; nie przesuwa kontenera. GPU zawartości celów są współdzielone między kolejnymi wywołaniami. Szczegóły i korekta wcześniejszej hipotezy znajdują się w `docs/eye-resource-plan.md`.

## Potwierdzone mutacje i zakres izolacji

| Pole | Dowód i konsekwencja |
| --- | --- |
| `renderer+2BB0`, 64 bajty macierzy float | Kopia z arg7 VP przy `6D50F8`; przekazywana do shaderów; przy `6DDA80` warunkowo nadpisana z `+2CD8`. |
| `renderer+2CD8`, 64 bajty macierzy float | Przy `6D56E9` warunkowo nadpisana lokalną VP; przekazywana m.in. do `683E90` wraz z `+2BB0`. |
| `scene+839`, 1 bajt | Zerowany przy aktywnym `renderer+2C08`. |
| `renderer+2BF0`, 1 bajt | Jeśli ustawiony, na wejściu wywołuje `6D30B0`; na końcu zerowany przy `6DDB0D`. Nie przywracać go ślepo między oczami: może powtórzyć inicjalizację zasobów. |
| `image+CFD3B4`, uint32 | Zwiększany przy `6DDAA0` na wykonanej ścieżce końcowej. Jeśli poprzednia wartość modulo 120 wynosi 0, wykonywana jest konserwacja listy `renderer+2C10` przez `8AF240`. Nie jest to dowód kroku fizyki. |

To **minimalny zestaw znanych pól do logowania**, a nie kompletny zestaw wystarczający do przywrócenia stanu. Przywrócenie samego licznika może dwukrotnie uruchomić konserwację w tym samym okresie. Dodatkowy przebieg należy na potrzeby diagnostyki pominąć przy granicy modulo 120 lub jawnie zaakceptować i odnotować jego efekt. Trzeba logować wejściowy/wyjściowy stan obu macierzy, flag, licznika, FBO i indeksów rozpoznanych celów.

Przywrócenie tych pól nie odtwarza tekstur GPU, list, zasobów shaderów ani wszystkich mutacji w funkcjach potomnych. Nie wykonywać memcpy całego renderera. Wersja do gry wymaga własnych zasobów i historii dla każdego oka albo potwierdzonego odłączenia etapów zależnych od historii. Obecna analiza nie podaje uczciwie skończonej listy pól gwarantującej pełną izolację.

## Wywołujący, culling i symulacja

Znane bezpośrednie adresy powrotu: `7B097C`, `7B0A6A`, `7E58CB`, `823939`, `832E6B`, `84074B`. Zestaw pochodzi z `artifacts/renderer-caller-candidates.json`. Samo występowanie adresu w pliku nie przypisuje mu roli kokpitu; jest kilka ścieżek renderowania.

Ścieżka menu jest potwierdzona: funkcja `840660–8407D0`, kamera aktualizowana przed `8406DA`, renderer wraca do `84074B`. Wywołujący tworzy scenę/pakiet przez `6E8B90` i `6CD080` **przed** rendererem, a po nim rysuje UI i niszczy scenę przez `6E9400`. Oba dodatkowe wywołania renderera na tym samym stosie korzystają więc z tego samego przygotowanego zestawu sceny i nie wracają do zewnętrznej pętli symulacji. Nie zidentyfikowano jawnego wywołania kroku fizyki w tym eksperymencie; pełny graf potomków nie został jednak zbadany.

Renderer sam tworzy dodatkowe kamery i przetwarza listy sceny. Nie oznacza to ponownego przygotowania wszystkich widocznych obiektów. Pełny culling przed rendererem nie jest jeszcze sklasyfikowany; wcześniejsze przygotowanie sceny jest bezpośrednim dowodem. Małe przesunięcie IPD może posłużyć testowi różnic obrazów, lecz duży obrót/przesunięcie głowy wymaga aktualizacji sceny/frustum przed tym etapem lub wspólnego frustum obejmującego oba oczy.

W kokpicie potwierdzono kamerowe adresy powrotu `71AC89/71ACF9/71AD69/71ADD9`; pełny właściciel to `71A140–71AF47`, nie częściowy fragment `71AC50`. Kamery właściciela znajdują się przy `+328/+828/+5A8/+AA8`. Nie ustalono jeszcze, który z powyższych bezpośrednich callerów renderera jest głównym przebiegiem kokpitu. Przed włączeniem stereo w grze potrzebny jest pomiar callerów renderera, charakterystyka cullingu i log potwierdzający jeden krok symulacji przy dwóch przebiegach oczu.

## Materiał dowodowy

- `artifacts/renderer-full-audit.asm`: pełny dump kodu funkcji, łącznie z końcowym licznikiem i sprzątaniem.
- `artifacts/renderer-audit-helpers.json`: ograniczona dekompilacja i instrukcje `6DDB70`, `6D4AE0`, `690F40`, `691050`; bez pełnej autoanalizy. Nazwy parametrów/typy dekompilatora są przybliżone.
- `artifacts/renderer-callers.json` oraz `renderer-caller-candidates.json`: ścieżki wywołań.
- `artifacts/cockpit-camera-owner.c`, `camera-cockpit-02.json`, `camera-basis-check.json`: właściciel i wcześniejszy pomiar kamer kokpitu.

Nie wykorzystano tu zewnętrznych porad jako dowodu zachowania gry. Granice wnioskowania wynikają z lokalnej analizy konkretnego pliku oraz zakresu wcześniejszych pomiarów.
