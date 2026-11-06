# Cele offline — od 2026-11-03

Zakres: prace bez uruchamiania Stormworks i bez prób w goglach (polecenie
użytkownika obowiązuje). Weryfikacja wyłącznie przez build, CTest, testy Python,
testy GPU poza grą i analizę statyczną zainstalowanego EXE (build 26294447).

Punkt wyjścia: build Release `build/capture` bez ostrzeżeń, 27/27 CTest,
20/20 Python, 50 guardów EXE.
Wynik G1–G4: build bez ostrzeżeń, 27/27 CTest, 23/23 Python, 54 guardy EXE, 0 błędów.
Wynik po G5–G6: build bez ostrzeżeń, **30/30 CTest**, **23/23 Python**, **54 guardy EXE, 0 błędów**.
Aktualna weryfikacja offline: Release, **48/48 CTest**, **45/45 Python**,
**68 guardów EXE**. Potwierdzenia w goglach poniżej pochodzą z wcześniej
zapisanych prób 2026-11-03. Po późniejszej próbie 2026-11-04 wdrożono offline
dotyk menu, przycisk MENU i ochronę gestów systemowych. Gra jest zamknięta.

| # | Cel | Stan |
|---|-----|------|
| G1 | `AvatarPose`: wyjaśnić statycznie `depth_draws=0` i poprawić powiązanie palet; test regresji | **zrobione offline** |
| G2 | `AvatarPose`: liczniki przyczyn odrzuceń drawów skinningu | **zrobione offline** |
| G3 | Diagnostyka tożsamości lokalnej postaci (82% oczu z ważną tożsamością) | **diagnostyka zrobiona**; zmiana polityki wygasania czeka na dane z próby |
| G4 | Test założeń o shaderach gry | **zrobione** |
| G5 | Kamera wychodzi poza ciało przy obrocie/wychylaniu: ograniczyć przesunięcie oczu względem ciała postaci, z testami | **zrobione offline** |
| G6 | Rozbić `gl_trace.cpp` na moduły bez zmiany zachowania | **zrobione (etap 1)** |
| G7 | W pojeździe dłonie postaci zostają na kierownicy, choć szkielet XR się rusza | **naprawione i potwierdzone w goglach** (druga kopia ciała, wiązanie `proximity`) |
| G10 | Mruganie na czarno co ~0,5 s | **naprawione i potwierdzone** (ponowne wysłanie ostatniego obrazu) |
| G8 | 5× `LOCAL_changed` w próbie | **zbadane: recentry**, bez zmiany zachowania |
| G9 | Poza pojazdem postać nie obraca się z graczem | **zrealizowane przez G12** |
| G11 | Interaktywność: przełączniki i dźwignia dłonią | **Push/Toggle i throttle typu 4 potwierdzone**; lewa dłoń/Q wdrożona offline 2026-11-04, czeka na próbę |
| G12 | Obrót postaci za goglami poza pojazdem | **potwierdzone w goglach**, również obrót myszą |
| G13 | Ostrzeżenia, gdy OpenXR czeka na gotowość | **zrobione i testowane offline**, próba w goglach pozostaje |
| G14 | Automat utrzymania T-pose i przerwy śledzenia/renderowania | **wydzielony i testowany offline**, ocena kalibracji ramion pozostaje |
| G15 | Chodzenie i sprint dłonią bez klawiatury | **wdrożone i testowane offline jako `-HandLocomotion`**; ruch, komfort i koszt requestów do próby |
| G16 | Drugi wariant: dwie pięści | Po próbie wymachy zastąpiono stanem dwóch pięści: zamknięte = bieg, otwarcie jednej = stop. Flaga `-ArmSwingRun` pozostała aliasem. Nowa wersja testowana offline. |

Goal 1–5 z 2026-11-04 ukończony offline: zwalnianie lokomocji przed
rozpoznanymi przejściami, konfiguracja INI, lewa dłoń/Q, podpowiedzi HMD
i automatyczne raporty. [Wyniki i ograniczenia](offline-goal-20261104.md).

Kolejny goal 1–5, dotyczący wygody obsługi, również ukończono offline:
postęp T-pose, trwały profil proporcji, przeładowanie ustawień/trybu,
nagranie i odtwarzanie śledzenia, lokalny panel klawiatury numerycznej.
[Szczegóły, instrukcje i podgląd](offline-usability-goal-20261104.md).

Następne pięć zadań wdrożono offline: menu VR, preferencje dłoni, wizualny
odtwarzacz, pełniejszy dziennik wejścia i przygotowanie adaptera natywnych
klawiatur. Port 5/6 jest testowany, ale niepodłączony do produkcyjnego hooka.
[Zakres, obsługa i ograniczenia](offline-controls-20261104.md).

## G11 — przełączniki i „dźwignie”

- Dyspozytor `78C7A0` wysyła dla przycisków typu 0–4 ten sam request typu 10
  (`use_vehicle_button`, vtable `B0FEC0`), a typy 5/6 otwierają UI klawiatury.
  HandButtons dopuszcza typy 0 (Push), 1 (Toggle) i 4 (throttle lever).
  Pozostałe typy pozostają odrzucane. Brak osobnej klasy dźwigni w RTTI
  nie oznacza braku komponentu: w próbie 20:15 wskazywany throttle miał typ 4
  i przesuwał się po szczypnięciu prawej dłoni. Przyjęto 8 naciśnięć bez błędów.
- Lewa dłoń/Q (akcja 0x13) ma osobne powiązanie certyfikatu z natywnym
  stanem trzymania i zwolnieniem, z testem pełnego cyklu obu rąk. Dla typów 5/6
  przygotowano lokalny interfejs VR i adapter dwóch requestów typu 11; do
  podłączenia potrzebne jest potwierdzenie właściciela i cyklu życia sesji UI.

## G12 — obrót postaci za goglami (`-BodyYaw`)

Analiza obecnego builda: kontroler kamery `71B630` trzyma yaw/pitch w
`C+2A0/C+2A4` (radiany, yaw zawijany do ±π, pitch ±81°); `C+1A1` to tryb
fotela (limity `C+18C..198`), `C+1A0` trzecia osoba, `C+1A4..1A6` wolne
kamery. Log z próby 17:19 pokazał, że rotacja anchora głowy jest stała, a
cała rotacja kamery leży w `L`, więc zmiana `C+2A0` nie obraca obrazu XR —
obraca postać i kierunek chodzenia.
Implementacja: poza fotelem, w pierwszej osobie, przed natywną aktualizacją
kamery `C+2A0` jest ustawiane tak, by yaw kamery gry równał się yaw gogli
(martwa strefa 1°). Odwzorowanie pola na macierz `L` (znak i przesunięcie 0/π)
jest identyfikowane w działaniu: 10 zgodnych klatek z jednoznacznym kątem, bez
tego żadnego zapisu. Log: `#body_yaw_mode`, `#body_yaw_calibration`,
`#body_yaw_totals`. Test: `body_yaw`. Opcja domyślnie wyłączona.
Użytkownik potwierdził chodzenie z obrotem za głową, a w próbie 20:31 również
obrót myszą. `follow_headset_yaw_turning` mierzy zmianę pola od wartości
pozostawionej po poprzednim update; sztuczny obrót obraca całą przestrzeń
kamery, dłoni i awatara zamiast być cofany przez podążanie za głową.

## G13–G14 — gotowość XR i automat T-pose

`XrReadinessMonitor` zapisuje przyczynę oczekiwania po 5 s i najwyżej co 30 s,
a supervisor pokazuje wskazówki w konsoli bez przejmowania fokusu. Rozróżnia
sesję, fokus, śledzenie i brak klatki; po odzyskaniu gotowości zgłasza `ready`.
Testy obejmują czasy, zmianę przyczyny i powielony odczyt logu.

`AvatarCalibrationHold` wymaga 1,5 s T-pose, tolerując przerwy do 300 ms.
Przerwa >300 ms resetuje utrzymanie także wtedy, gdy kolejną obserwacją jest
od razu poprawna poza (np. po zawieszeniu renderowania). Zmiana aktora, modelu,
LOCAL lub utrata fokusu również kończy utrzymanie. Zachowane proporcje ciała
przetrwają recenter. Nowe testy nie zastępują oceny długości rąk w goglach.

## G7 — dłonie na kierownicy: ustalenia i diagnostyka

Statycznie: ścieżka postaci w `0x6be1b0` ustawia selektor głowy (clip=3,
jedyny klucz rozpoznania lokalnej postaci) tylko gdy `actor+0x2448`,
`actor+0x2408==0` i brak bitu 3 we flagach widoku `+0x708`; w przeciwnym razie
clip=-1 i rekordy nie są wiązane (dłonie zostają w natywnej pozie).
`actor+0x2408` jest przełączany przez akcje wejścia kamery (save/restore), a
`0x6cd0b0` (zawsze clip=-1) ma jedno wywołanie spoza renderowania sceny.
Nie rozstrzygnięto, który warunek zachodzi w fotelu.
Dodana diagnostyka (testy: `avatar_pose`, `avatar_pose_gl`, `native_avatar_identity`,
`trace_report`, `diagnostic_image`):
- `#avatar_identity_totals`: `local_meshes`, `scans`, `local_mesh_clip3`,
  `local_mesh_other_clip`, `foreign_19_bone`, `scans_local_without_clip3`
  (rekordy 19-kostne z meshem lokalnego aktora, wg selektora głowy);
- `#avatar_pose_totals`: `paired_seen`, `left/right_wrist_shift_samples/mean_mm/max_mm`;
- `<log>.avatar-draws.tsv`: dziennik drawów skinningu co 120. oko (decyzja,
  wiązanie, clip także dla niedopasowanych, przesunięcie nadgarstków);
- `<log>.eye-N.ppm`: zrzut lewego oka co 30 s (pomniejszony 2×, maks. 10).

## G8 — zmiany LOCAL

Wszystkie 5 rekalibracji miało origin ≈ (0,0,0) i yaw ≈ 0 w nowym LOCAL, czyli
nowy środek LOCAL leżał na głowie — to recenter (przycisk Meta lub
automatyczny po zdjęciu/założeniu gogli). Kalibracja w bieżącej pozie jest
wtedy zamierzona; zachowanie starej kalibracji zepsułoby recenter.

## G5 — limit głowy w Gameplay

`swvr::limit_head` (`tracking.hpp`) ogranicza przesunięcie szacowanego punktu
szyi (7,5 cm pod i 8 cm za środkiem oczu) od pozycji z kalibracji. Obrót i
pochylenie głowy wokół szyi nie są ograniczane. Do 60% limitu bez zmian, dalej
miękka kompresja `tanh` do elipsoidy: przód 0,30 m, tył 0,12, boki 0,22,
góra 0,10, dół 0,30. Korekta przesuwa efektywny origin, więc widok, promień
dłoni i cel głowy awatara przesuwają się razem. Domyślnie włączony w Gameplay,
`-NoHeadLimit` / `SWVR_HEAD_LIMIT=0` wyłącza. Log: `#head_limit_mode`,
`#head_limit_totals`. Test: `head_limit` (obroty wolne, monotoniczność,
ciągłość na kolanie, asymetria osi, wspólna korekta dłoni, walidacja).
Wartości limitów to założenie; wymagają oceny w goglach.

## G6 — refaktor `gl_trace.cpp` (etap 1)

Wydzielono moduły o czystych granicach, z testami:
- `trace_report` — formatowanie linii `#*_totals`; test `trace_report` pilnuje
  unikalnych kluczy i wzorców używanych przez supervisor `run-game-preview.ps1`.
- `head_mask_sources` — wstrzykiwanie maski głowy do źródeł shaderów, kopie
  oryginałów i fallback po błędzie kompilacji; test jednostkowy z atrapą GL
  oraz istniejący test end-to-end `head_mask_injection` przez DLL.
Jedyna zamierzona różnica: ścieżka fallbacku kompilacji odrzuca teraz
wartownicze wskaźniki `glGetShaderiv` (1/2/3/-1) tak jak ścieżka źródła.
Dalszy podział (`NativeVr`, hooki renderera) wymaga wspólnego stanu globalnego
i bezpieczniej robić go po próbie w grze potwierdzającej obecne zachowanie.

## G1 — przyczyna `depth_draws=0`

Jedyny draw `depth_anim*` w grze to **przebieg cieni** `0x6de000` (kaskady,
`glPolygonOffset`). Bierze on rekordy z osobnej listy `scene+0x378`
(stride `0xe0`), a producent `0x6734e0` kopiuje paletę do **własnego bufora**
`rekord+0xc8`. `palettes()` wiązało tylko listy kolorowe (stride `0x118`), więc
token palety cienia nigdy nie pasował i każdy depth-draw był odrzucany.
Funkcja submit `0x6d0c20` przekazuje obu producentom ten sam mesh, te same
macierze i tę samą paletę current. Szczegóły: `artifacts/avatar-shadow-pass-20261103.json`.

Poprawka: rekord cienia jest wiązany jako `depth_only` tylko przy zgodności
meshu, 0x80 bajtów macierzy świata i 19 macierzy palety z zweryfikowanym
lokalnym rekordem kolorowym. Wiązanie `depth_only` nigdy nie autoryzuje drawu
kolorowego. Nowe guardy: prefiks `0x6734e0` i trzy CALL w submit.
Testy: `native_avatar_identity` (dopasowanie, inny mesh/szkielet/poza/świat,
brak lokalnego rekordu, złe granice, nieczytelna paleta) i `avatar_pose_gl`
(kopia palety na GPU, przywrócenie, odmowa dla drawu kolorowego).

Uwaga: wcześniejsze założenie, że depth pass to prepass widoku oka, było
błędne — to cienie. Pozowane cienie są pożądane, ale nie wpływają na clipping
widoku z pierwszej osoby.

## G2 — nowe pola `#avatar_pose_totals`

`skinned_unrebased`, `missing_inputs`, `paired_provenance_misses`,
`clip_rejects`, `bound_depth_palettes` (obok istniejącego
`single_provenance_misses`). Liczone tylko dla programów ze skinningiem
w aktywnym oku.

## G3 — nowy wpis `#avatar_identity_totals`

`observations`, `valid`, `invalid_snapshots`, `stale_snapshots` i
`failed_<etap>` dla etapów łańcucha: controller, world_node, node_kind, actor,
vtable, local, head, customization. Pozwoli rozstrzygnąć, czy brak 18% oczu
wynika z wygasania (100 ms), czy z odrzuceń łańcucha.

## G4 — `tests/test_game_shaders.py`

Sprawdza zbiór shaderów ze skinningiem i ich uniformy (kolorowe: dwie palety
+ selektor clip; depth: jedna paleta, bez `_prev` i clip). Test mutacyjny na
kopii z dopisanym `bone_matrices_prev` poprawnie oblał.

## Do potwierdzenia w następnej dozwolonej próbie

`depth_draws>0` i `bound_depth_palettes>0`; `skinned_unrebased` bliskie 0;
rozkład `avatar_identity_totals`; wizualnie cienie rąk zgodne z VR.
