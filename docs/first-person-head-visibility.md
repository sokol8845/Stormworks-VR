# Widoczność własnej głowy w pierwszej osobie

Analiza statyczna buildu Steam 26275574, EXE SHA-256
`f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`.
Adresy poniżej to RVA. Nie uruchamiano gry ani nie zmieniano jej plików/pamięci.

## Ustalenie

Gra ma już selektywną maskę kości głowy lokalnego gracza, ale shader odrzuca
oznaczone fragmenty **tylko poniżej 0,25 m głębokości widoku**. Nie usuwa całej
głowy niezależnie od pozycji oka. Samo poprawne wyrównanie kamery z natywnym
początkiem promienia nie gwarantuje więc braku widocznego własnego awatara przy
fizycznym przesunięciu głowy. To potwierdzona możliwość wynikająca z kodu; nie
pomiar przyczyny konkretnego obrazu zgłoszonego przez użytkownika.

Nie znaleziono uzasadnienia dla globalnego usunięcia postaci lub zwiększenia near
plane. Maska głowy i kalibracja kamery są dwoma niezależnymi zagadnieniami.

## Wybór lokalnej postaci i nazwanej kości

`c_scene_object_character` ma type descriptor `CE1AC0`, COL `BE3F28` i vtable
`B764E8` (RTTI, nie nazwa wymyślona na podstawie sąsiednich funkcji).

`5F2610` porównuje identyfikator peer z argumentu z `network+338`. Przy zgodności
zapisuje deskryptor postaci do `owner+BB9F8` oraz do
`*(network+1F0)+98`, a `descriptor+4D8` ustawia na 1. Analogiczną ścieżkę ma
`5F2920`. Deskryptor jest znajdowany po identyfikatorze `descriptor+98`.
`53A0A0` przekazuje `descriptor+4D8` jako argument 12 do `534CC0`; ta funkcja
zapisuje go do `character+2448`. Uchwyt scenowy pod `descriptor+90` prowadzi do
tej postaci. Wartości muszą być ponownie potwierdzone na żywej scenie przed
jakąkolwiek zmianą; nie należy używać samego adresu współdzielonego mesha jako ID.

`4D5F80` ładuje szkielet `meshes/survivor_female.anim` lub
`meshes/survivor_male.anim`, rozwiązuje nazwę `head` przez `299B20` i zapisuje
wskaźnik w `character+15B8`. `head_node+10` jest indeksem używanym do adresowania
palet macierzy kości. Indeks nie jest stałą wspólną dla wszystkich zasobów.

`78C770`, gałąź akcji `0x60` przy `7906C3–79078A`, przełącza
`character+2408`, z uwzględnieniem ustawień dopuszczających widok z trzeciej osoby
osobno w fotelu i pieszo. Nie jest to uprawnienie do zmieniania tej flagi przez mod:
wpływa także na natywną kamerę i inne zachowanie postaci.

## Konkretny punkt budowania maski

Duży producent sceny `6BE180–6CD008` ma pełny eksport instrukcji. Jego pełna
dekompilacja przekroczyła limit 45 s; poniższy warunek wynika z instrukcji,
nie z niepełnego pseudokodu. `R13` wskazuje scenę, `R14 = scene+708` wskazuje
bitfield, a w tej części pętli `R15` wskazuje postać.

Przy `6BF2D6` lokalny indeks zaczyna jako -1, a XMM9 jako 0. Następnie:

```text
jeżeli character[2448] != 0
    i character[2408] == 0
    i bit 3 w scene[708] == 0:
        head_index = character.head(15B8)->index(10)
        weight_threshold = 0.25
```

Odczyt kości: `6BF306`; zapis indeksu: `6BF310`; próg:
`6BF313`, stała float `BB30BC = 0.25`, sprawdzona bezpośrednio z EXE.
Nie przypisano jeszcze semantycznej nazwy bitowi 3. Nie należy go globalnie
zerować, ponieważ obejmuje też rozróżnienie przejść renderowania.

Parametry trafiają do pomocników zasobów ubioru/mesha, np. call `6C018E → 6D0BF0`
ma indeks na **caller RSP+70** i próg na **caller RSP+78**.
Call `6C0CDB → 6D0F50` przenosi analogiczne wartości przez caller RSP+58/+60.
To różne ABI; dekompilator pomija część argumentów float. Nie kopiować jego
prototypów bez weryfikacji instrukcji.

Budowniczowie rekordów zapisują indeks/próg w tych samych polach:

| Pomocnik | Lista sceny | Rekord |
| --- | --- | --- |
| `6726D0` | `scene+1E0`, opaque animated | `+10C`: int index, `+110`: float threshold |
| `672960` | `scene+240`, glass animated | te same pola |
| `672C20` | `scene+1F8`, additive animated | te same pola |

Rekord ma też wskaźnik mesha `+0`, macierze świata `+10/+50`, palety kości
`+E8/+F8` i licznik kości `+108`. Wskaźnik mesha może być wspólny dla lokalnego
gracza i NPC; do korelacji potrzeba kontekstu lokalnej postaci, palet i transformów.
Ścieżka opaque iteruje rekordy stride `118`, potwierdzone również w cullingu.
Nie przenosić tego stride na nierozpoznane listy.

`6D4DB0` odczytuje te pola i przekazuje je do draw helpers:

| Helper | Sloty obiektu programu: index / threshold | Wywołania ustawiające wartości |
| --- | --- | --- |
| opaque `682FA0` | `+90 / +98` | `68311E / 683137` |
| glass `683950` | `+B0 / +B8` | `683A4D / 683A66` |
| additive `683F40` | `+70 / +78` | `684074 / 68408A` |

Slot wskazuje rekord uniformu; jego lokalizacja GL jest pod `+8`. Rejestrację nazw
`bone_clip_index` / `bone_clip_weight` potwierdzają konstruktory
`682930`, `683260`, `683B90`. To nie są same funkcje rysujące. Oryginalne ślady GL
zawierają nazwy tych uniformów, ale nie rejestrują ich wartości int/float.

## Co faktycznie robi shader

Trzy zainstalowane pary `opaque_anim`, `glass_anim`, `additive_anim` mają identyczną
zasadę wyboru: jeśli jedna z dwóch składowych skinningu wskazuje
`bone_clip_index` i jej waga jest większa niż `bone_clip_weight`, vertex shader
ustawia `vertex_color_out.a = 0`. Fragment shader sprawdza:

```glsl
if(vertex_color_out.a <= 0 && decode_depth_linear(gl_FragDepth) < 0.25)
{
    discard;
}
```

`depth_utils.glslh` wyprowadza to z `pos.w` i jawnie opisuje wynik jako metry.
Jest to głębokość w kierunku patrzenia, nie sfera o promieniu 25 cm wokół oka.
W `depth_anim` i `depth_anim_cheap` nie ma `bone_clip_index`; nie ma podstaw do
wyłączania całego modelu również w cieniach/przejściach głębokości.

Istotne ograniczenie: alpha=0 jest znacznikiem współdzielonym z kolorem oryginalnego
wierzchołka. Sam fragment shader nie rozróżnia „alpha wyzerowana przez maskę
głowy” od „alpha=0 zapisana w zasobie”. Rozszerzenie tej reguły musi pozostać
ograniczone do potwierdzonych lokalnych rekordów z poprawnym indeksem głowy.

## Bezpieczny następny pomiar obserwacyjny

1. Po zgodności SHA i prefiksów kodu zapisać deskryptor lokalny, jego ID `+98`,
   uchwyt postaci/vtable, `2448/2408`, nazwany head pointer/index oraz `scene+708`.
   Nie zmieniać żadnego z tych pól. Próbki powiązać z numerem symulacji/XR i
   główną sceną `6D4DB0`, a nie z menu lub kamerą pomocniczą.
2. Powiązać rekordy tworzone z palet tej postaci z `+10C/+110`, meshem, paletami,
   liczbą kości i transformem. Dla lokalnych rekordów wymagać
   `0 <= index < bone_count <= 64`; odnotować oddzielnie przypadki -1 i
   nierozpoznane zasoby. Drugą postać pozostawić jako kontrolę.
3. Przy trzech powyższych draw helpers lub nazwanych wywołaniach `glUniform1i/1f`
   zapisać faktyczne index/threshold, program i oko. Zachować wszystkie argumenty.
   Nie opierać się na samej nazwie uniformu, bo programy są współdzielone.
4. W potwierdzonym widoku pierwszoosobowym porównać neutralną pozę z małym
   fizycznym przesunięciem. Logować odległość/głębokość głowy i nie zmieniać
   kalibracji między wariantami. Oddzielnie zanotować przejście do trzeciej osoby
   i obecność NPC; nie automatyzować przełączania bez uzgodnionego testu.
5. Jeżeli maska jest wyłączona, najpierw wyjaśnić flagę/callera. Jeżeli jest
   poprawna, a problem koreluje z ograniczeniem 25 cm, dopiero następny,
   odwracalny wariant może rozszerzyć **lokalną maskę kości** w dedykowanym
   shaderze VR. Nie podnosić globalnego near plane; nie ukrywać wszystkich postaci
   ani wszystkich drawów skinned. Widoczność torsu przy wejściu fizyczną głową w
   ciało jest osobną kwestią, której maska jednej kości nie rozwiązuje.

Użytkownik polecił obecnie nie uruchamiać Stormworks. Ten pomiar jest planem na
później; analiza nie stanowi zgody na start gry ani potwierdzenia naprawy.

## Dowody

- `artifacts/first-person-head-evidence.json`: SHA EXE, prefiksy instrukcji,
  float progu, hashe i ponumerowane fragmenty zainstalowanych shaderów.
- `artifacts/first-person-character-rtti.json`: identyfikacja klasy.
- `artifacts/first-person-head-selection.asm`: warunek wyboru głowy i call ABI.
- `artifacts/first-person-head-functions-a/c/e/f/h/i.json`: dekompilacje i
  instrukcje inicjalizacji, selekcji, konstrukcji rekordów, tożsamości i drawów.
- `artifacts/head-140*.c`: teksty funkcji z tych eksportów.

Surowe skany rel32 i stałych służyły wyłącznie nawigacji; kluczowe wnioski powyżej
opierają się na instrukcjach lub rzeczywistym kodzie shaderów. `6BE180` ma pełne
instrukcje, ale nie udaną pełną dekompilację. Nie zaobserwowano jeszcze wartości
maski ani jej skuteczności w zgłoszonym przebiegu VR.

## Zaimplementowany pomocnik modyfikacji źródła w pamięci

`include/swvr/head_mask_shader.hpp` i `src/head_mask_shader.cpp` udostępniają:

```cpp
swvr::HeadMaskShaderPatch patch = swvr::patch_head_mask_fragment(source);
// patch.patched == false: patch.source jest bajtowo identyczne z source.
// patch.patched == true: przekazać patch.source do kompilacji fragment shadera.
```

Pomocnik nie odczytuje/zapisuje plików, nie wywołuje OpenGL, nie instaluje hooków
i nie uruchamia gry. Przy rozpoznanej strukturze jednej z trzech znanych rodzin
fragment shaderów zmienia wyłącznie dokładny predykat podany wyżej na:

```glsl
if(vertex_color_out.a <= 0 && (swvr_vr_head_mask != 0 || decode_depth_linear(gl_FragDepth) < 0.25))
```

Przed `void main()` dodaje deklarację `uniform int swvr_vr_head_mask = 0;`.
Pozycja `#version` pozostaje bez zmian. Przy wartości 0 warunek zachowuje
dotychczasową semantykę. Wartość 1 usuwa wyłącznie ograniczenie głębokości z
istniejącego testu alpha; selekcja kości i próg wag w vertex shaderze pozostają
niezmienione. Globalny near plane ani projekcja nie są modyfikowane.

Akceptowane są nagłówkowe szablony gry zaczynające się od `#include` oraz kompletne
źródła desktop GLSL 130, 140, 150, 330 i 400–460 (prawidłowe wersje co 10).
Wersje ES/nieznane, nieprawidłowe lub powtórzone dyrektywy wersji, brak znanych
deklaracji fragment shadera, nieunikalny predykat/main, niezbilansowane klamry,
uszkodzone komentarze i zmieniony blok odrzucenia pozostają nietknięte. Komentarze
nie stanowią dowodu obecności predykatu. Wystąpienie nazwy nowego uniformu także
powoduje pozostawienie źródła bez zmiany, zapewniając idempotencję. To ostrożne
rozpoznawanie wspieranej postaci źródła, nie pełny walidator języka GLSL; kompilator
i linker nadal muszą zaakceptować wynik.

Integracja należąca do modułu GL musi:

1. Działać wyłącznie po zgodności SHA/profilu i przekazać kompletne, prawidłowo
   połączone segmenty źródła z uwzględnieniem długości `glShaderSource`.
2. Sprawdzić wynik kompilacji/linkowania i obecność nowego uniformu w każdym
   użytym programie; lokalizacja -1 nie oznacza działającej maski.
3. Ustawiać wartość 1 wyłącznie podczas potwierdzonych drawów lokalnej postaci,
   dla których natywne `bone_clip_index` odpowiada jej głowie, jest nieujemne i
   mieści się w palecie. Wyłączać wartość po drawie i przed obrazem monitora,
   sceną pomocniczą, inną postacią lub wyjściem z VR. Nie wystarcza globalne
   ustawienie wartości 1 dla wszystkich animowanych drawów.
4. Zachować dotychczasowy wybór części ciała. Zerowa alpha pochodząca bezpośrednio
   z mesha jest nadal nierozróżnialna od znacznika kości w tym wariancie. Jeżeli
   pomiar wykaże takie wierzchołki poza głową, potrzebny będzie osobny jawny
   varying maski w vertex/fragment shaderze, zamiast traktowania tej wersji jako
   bezwarunkowo poprawnej dla każdego zasobu.

Test `tests/head_mask_shader_tests.cpp` obejmuje trzy małe szablony, zachowanie
przy wyłączonym uniformie, odmowę nieobsługiwanych wejść, idempotencję oraz
odtworzenie dokładnego oryginalnego źródła przez cofnięcie tylko dwóch zmian.
Uruchomienie z katalogiem zainstalowanych shaderów dodatkowo sprawdziło wszystkie
trzy rzeczywiste pliki, odczytywane bez zmian:

```text
PASS head-mask source patch: three variants, defaults, strict refusals, idempotency; installed fixtures=3
```

Kompilacja MSVC x64 `/W4 /WX /permissive-` przeszła. Plik źródłowy należy dodać
do biblioteki C++20 publikującej `include`; nie wymaga SDK OpenXR ani bibliotek
GL. Sam test źródła nie potwierdza działania GPU, poprawności hooka ani ukrycia
głowy w natywnej scenie. W ramach tej zmiany nie uruchamiano Stormworks.

## Zaimplementowane sterowanie uniformem przy draw

`include/swvr/head_mask_gl.hpp` i `src/head_mask_gl.cpp` dodają `GlHeadMask`.
Klasa nie posiada shaderów, programów, FBO ani tekstur. Nie wiąże innego programu
lub kontekstu. Jedyny zmieniany stan GL to `swvr_vr_head_mask` aktualnego programu.

```cpp
swvr::GlHeadMask mask;
mask.configure(resolve_original_gl_proc);

// Po każdym glLinkProgram, przed następnym użyciem programu:
mask.program_linked(program);

// Przy rzeczywistym draw, po ustawieniu przez grę uniformów indeksu/wagi:
const bool activated = mask.before_draw(vr_draw_active);
original_draw(...);
// Jeżeli polityka hooka zeruje znacznik bezpośrednio po draw:
mask.before_draw(false); // gdy ten program nadal jest bieżący

// Przy usuwaniu HGLRC, zanim jego wartość zostanie ponownie wykorzystana:
mask.context_destroyed(native_gl_context);
```

Resolver ma typ `void*(*)(const char*)` i musi zwracać **oryginalne** adresy
`glGetUniformLocation`, `glGetUniformiv`, `glGetUniformfv`, `glUniform1i` oraz
`glGetProgramiv`. Użycie wrappera `glUniform1i` z tego samego hooka grozi rekurencją.
Nie należy wywoływać ponownie `configure` w środku draw; resetuje ono cache.

Na każdym wywołaniu odczytywany jest aktualny indeks i waga natywnej maski, ponieważ
program może kolejno rysować różne postaci. Wartość 1 jest ustawiana wyłącznie gdy:

- program ma wstrzyknięty uniform oraz oba natywne uniformy;
- aktywny jest draw VR;
- `bone_clip_index >= 0`;
- waga jest skończona i `abs(bone_clip_weight - 0.25) <= 1e-6`.

Pozostałe drawy wspieranego programu dostają 0, również po poprzednim drawie z
włączoną maską. Program bez nowego uniformu pozostaje nietknięty. Nieaktywny
kontekst lub program 0 są pomijane. Lokalizacje są cache'owane według
HGLRC/programu; ponowne linkowanie unieważnia nazwę programu we wszystkich
cache'owanych kontekstach, obejmując możliwe współdzielenie. Przy nieudanym
linkowaniu pomocnik nie próbuje odczytywać lokalizacji. Obsługa błędu linkera i
wybór działającego programu pozostają obowiązkiem integracji.

Ta sygnatura odpowiada zbadanej gałęzi lokalnej głowy w konkretnym buildzie.
Nie jest niezależną identyfikacją entity ani dowodem, że każdy program z takim
zestawem uniformów dotyczy lokalnej głowy. Nadal obowiązują zgodność profilu,
ograniczenie do przebiegu VR oraz opisane wyżej ograniczenie znacznika alpha.
Hooki zmian uniformów mogą dodatkowo odświeżać flagę, lecz nie zastępują końcowego
sprawdzenia przy draw: indeks i waga mogą być ustawiane w osobnych wywołaniach.

`stats()` zwraca `calls`, `patched_program_calls`, `candidate_calls`, `masked_calls`,
`uniform_writes` i `program_cache_misses`. Kandydat to program z pasującą natywną
sygnaturą, niezależnie od aktywności VR; masked to kandydat z aktywnym VR.
Są to liczniki **wywołań helpera**, a nie automatycznie liczba drawów czy
usuniętych pikseli. Wywołania przy uniformach lub zerowaniu po draw też są liczone.

`tests/head_mask_gl_tests.cpp` tworzy ukryty kontekst WGL core i rzeczywiście
kompiluje wynik czystego patchera. Mały wierny shader testowy zachowuje predykat
gry i selekcję przykładowej kości; funkcje kodowania głębokości są celowo
tożsamościowe, aby osobno sprawdzić warunek 0,25 bez zależności od projekcji gry.
Odczyt pikseli potwierdził:

- daleka oznaczona głowa jest widoczna przy 0 i odrzucana przy aktywnej masce VR;
- bliska głowa zachowuje dotychczasowe odrzucenie także poza VR;
- indeks ujemny, inna waga i NaN nie włączają rozszerzenia;
- zmiana wybranej kości nie usuwa nieoznaczonych wierzchołków;
- wyłączenie VR przywraca 0, a niepatchowany program pozostaje bez zmiany;
- relink i identyczna numeryczna nazwa programu w drugim, niewspółdzielonym
  kontekście nie wykorzystują starych lokalizacji;
- program/bindings i natywne uniformy pozostają bez zmiany, resolver oryginalnych
  funkcji jest używany, a istniejący błąd GL nie jest konsumowany.

Test przeszedł MSVC x64 `/W4 /WX /permissive-`. Do biblioteki Windows dodać
`src/head_mask_gl.cpp`, zależność `opengl32` oraz istniejący moduł patchera;
test dodatkowo wymaga `user32` i `gdi32`. To zweryfikowana semantyka GPU na
kontrolowanym shaderze. Nie jest to jeszcze pomiar ani naprawa widoczności awatara
w prawdziwej scenie Stormworks. Gra pozostaje nieuruchamiana na polecenie użytkownika.
