# Przegląd praktyk VR: SteamVR i Meta Quest 3 — 2026-11-05

Zakres: mod PCVR C++/OpenGL/OpenXR uruchamiany w Stormworks, obraz przez
SteamVR i Steam Link do Questa 3. To analiza i propozycje; nie zmieniono kodu
ani nie uruchomiono gry. Stan projektu: [stan-pracy.md](stan-pracy.md).

Aktualizacja po tej analizie: uzgodniona następna paczka pięciu rozszerzeń
została [wdrożona i sprawdzona offline](vr-comfort-20261105.md).
Poniższy tekst zachowuje stan i propozycje z chwili przeglądu.

Wymagania API OpenXR, warunki publikacji w sklepach i zalecenia projektowe
to odrębne rzeczy. Meta VRC opisuje ocenę aplikacji Meta Horizon Store;
poniżej służy także jako punkt odniesienia dla jakości naszego moda PCVR,
nie jako deklaracja certyfikacji. Konfiguracja strony Steam/launch options
dotyczy wydawcy aplikacji, nie ustawień, które mod powinien sam zmieniać.
[Meta VRC](https://developers.meta.com/vr/resources/publish-quest-req/),
[Steamworks: konfiguracja VR](https://partner.steamgames.com/doc/features/steamvr/settings).

## Co już jest

Projekt ma blokady wejścia po utracie fokusu i przy gestach systemowych,
wybór dłoni, dwie pięści do biegu, płynny obrót drugą dłonią, dotyk/dwell,
pionowe menu z regulacją odległości, kalibrację z odrzucaniem skoków,
trwałe ustawienia i replay. Istniejące pomiary P95/P99 są pomiarami CPU.
Źródła: [runtime](../src/xr_runtime.cpp), [menu](../src/vr_settings_menu.cpp),
[panel](../src/pointing_panel.cpp), [obrót](../src/hand_turn.cpp),
[raport](../include/swvr/performance.hpp), [integracja](../src/gl_trace.cpp).

Poniższe punkty rozszerzają tę bazę. Uzgodnione płynne obracanie i bieg
dwiema pięściami pozostają dostępne; nie proponujemy zamiany ich domyślnie
na inne gesty.

## Priorytety

### 1. Pełny przebieg utraty fokusu i powrotu do gry

Meta wymaga w aplikacjach Quest ignorowania wejścia i ukrycia rąk/kontrolerów
po utracie fokusu przy dalszym renderowaniu. OpenXR rozróżnia VISIBLE i FOCUSED;
utrata fokusu nie jest automatycznie poleceniem zakończenia renderowania.
Gest systemowy nie powinien uruchamiać równoległej akcji aplikacji.
[Meta VRC Input.4/8](https://developers.meta.com/vr/resources/publish-quest-req/),
[OpenXR: cykl sesji](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/session.adoc).

**Potwierdzony brak w kodzie:** [GlHandOverlay::draw](../src/gl_hand_overlay.cpp)
nie uzależnia rysowania szkieletów dłoni od fokusu, choć promień taką blokadę ma.
Runtime może nadal zwrócić śledzone dłonie. Obecny test utraty fokusu wyłącza
`active_hands`, więc nie sprawdza tego przypadku. To ustalenie ze źródeł,
nie obserwacja nowej próby w goglach. Docelowo ukryć te szkielety przy
przejęciu wejścia przez system. [Meta Input.4](https://developers.meta.com/vr/resources/vrc-quest-input-4/).

**Propozycja:** rozszerzyć test integracyjny na sekwencję bieg/obrót → dashboard
SteamVR lub menu Quest → powrót, oraz utratę dłoni/HMD i zmianę LOCAL.
Sprawdzać zwolnienie natywnych akcji, brak dodatkowego kliknięcia, reset środka
joysticka i znikanie wizualnych dłoni, gdy wejście jest przejęte przez system.
Obecne guardy już blokują akcje; nie należy traktować tego jako całkiem nowej
funkcji. Ewentualne wymaganie nowego chwytu po odzyskaniu śledzenia może być
osobną opcją, bo uzgodniony tryb dwóch pięści ma reagować na ich stan.

Offline: automat, testy i zapis/replay przejść. Gogle: rzeczywiste gesty Quest,
dashboard SteamVR, zdjęcie/założenie gogli oraz zerwanie/odnowienie połączenia.

### 2. Wyraźniejsze kliknięcie i rozdzielenie dotyku od wskazywania

Meta zaleca informację wizualną i dźwiękową dla dotyku, która zastępuje brak
fizycznego czucia przycisku. Przy UI w świecie 3D ostrzega przed niejasnym
przejściem między dotykiem a promieniem; szczególnie przy panelu w odległości
około 0,5–0,8 m. To wskazówki ergonomiczne, nie sztywna norma dla każdego użytkownika.
[Hands Interaction Types](https://developers.meta.com/vr/design/hands-interaction-types/),
[Spatial UI w scenie 3D](https://developers.meta.com/vr/design/hands-3d-best-practices/).

**Propozycja:** krótki opcjonalny dźwięk kliknięcia, widoczny stan wciśnięcia
utrzymywany około 100–150 ms, regulacja czasu dwell i skali tekstu/przycisków.
Rozróżnić ikoną/kursorem tryb DOTYK i WSKAZYWANIE. Podane czasy są naszymi
punktami wyjścia do testów, nie wymaganiami Meta.

Domyślnego panelu 1 m nie przybliżać ponownie bez wyboru użytkownika.
Opcjonalny profil dotyku może umieszczać go w indywidualnie dobranym zasięgu,
a profil wskazywania zachować dalej. Położenie przycisku MENU regulować
opcjonalnie, zachowując obecny domyślny wariant nad oczami uzgodniony po próbie.

Offline: geometria, czas potwierdzenia, dźwięk, testy rozdzielenia wejścia.
Gogle: czytelność, zasięg obu dłoni, wysokość MENU, zmęczenie i trafność kliknięć.

### 3. Pomiar GPU i porównanie z budżetem klatki runtime

Khronos opisuje `predictedDisplayPeriod` jako przewidywany odstęp wyświetlenia
kolejnych klatek aplikacji; nie jest to niezawodny odczyt fizycznego odświeżania
panelu. Valve rozdziela pomiary CPU/GPU, powtórzenia klatek, pominięcia
i reprojekcję. Te wielkości są potrzebne obok obecnych percentyli CPU.
[Khronos: XrFrameState](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFrameState.html),
[Valve: Compositor_FrameTiming](https://github.com/ValveSoftware/openvr/wiki/Compositor_FrameTiming).

**Propozycja:** nieblokujące pomiary GPU z opóźnionym odczytem, zapis
`predictedDisplayPeriod`, procent przekroczeń budżetu oraz powiązanie raportu
z danymi SteamVR. Osobno pokazywać czas aplikacji, compositor/reprojekcję
i problemy transmisji; czas CPU moda nie obejmuje całego toru do gogli.
90 Hz odpowiada matematycznie 11,11 ms, ale nie jest uniwersalnym wymogiem
ani częstotliwością, którą mod powinien wymuszać.

Offline: mierniki, format raportu i testy na izolowanym GL. Gogle: porównanie
z Advanced Frame Timing SteamVR i próby w tym samym zapisie/scenie przy
wybranym przez użytkownika odświeżaniu i rozdzielczości.

### 4. Opcje komfortu bez zmieniania wybranego sterowania

Meta wymienia stabilny framerate, obrót skokowy i winietę jako techniki
ograniczania dyskomfortu lokomocji; równocześnie wskazuje, że gwałtowne zmiany
widoku mogą dezorientować. W nowszym opisie winiety Meta używa kątów widzenia,
żeby efekt nie zależał przypadkowo od rozdzielczości lub modelu gogli.
[Komfort lokomocji](https://developers.meta.com/vr/design/locomotion-comfort-usability/),
[Winieta komfortu](https://developers.meta.com/vr/documentation/unity/unity-isdk-locomotion-comfort/).

**Propozycja:** opcjonalna winieta podczas sztucznego ruchu/obrotu, regulowana
siła i możliwość wyłączenia, oraz dodatkowy tryb obrotu skokowego, np. 30°/45°.
Obecny płynny obrót pozostaje wyborem użytkownika. Nie tłumić fizycznego obrotu
głowy filtrami przeznaczonymi dla wirtualnego joysticka.

Offline: automat i shader, symetria obu oczu, brak modyfikacji hostowego stanu GL.
Gogle: siła efektu i komfort; test syntetyczny nie daje odpowiedzi o mdłościach.

### 5. Porównanie ogranicznika głowy z wygaszaniem przy granicy

Aktualny [limit_head](../src/tracking.cpp) i jego użycie w
[gl_trace.cpp](../src/gl_trace.cpp) ograniczają pozycję widoku przez korektę
obu oczu. Meta wskazuje niedopasowanie widoku do ruchu fizycznego jako źródło
dyskomfortu. **Wniosek projektowy:** warto porównać obecny wariant z zachowaniem
ruchu głowy i łagodnym wygaszaniem obrazu przy wyjściu poza dopuszczalny obszar.
Nie znaleziono podstaw do ogłoszenia takiego wygaszania obowiązkowym standardem.
[Komfort i śledzenie](https://developers.meta.com/vr/design/locomotion-comfort-usability/).

Offline: wariant eksperymentalny, zgodność stereo, progi i test powrotu obrazu.
Gogle: konieczna ocena wychylania, wnętrza ciała/kokpitu, widoczności świata
i zachowania recenter. Nie usuwać dotychczasowej ochrony bez porównania.

### 6. Profil siedzący i opcjonalne kontrolery jako zapasowe wejście

Meta zaleca uwzględnienie siedzenia, ograniczonego zakresu ruchu i zmęczenia.
Steamworks wskazuje, że sugerowane wiązania kontrolerów OpenXR wpływają na
kompatybilność różnych urządzeń. To osobny zakres od wykrywania szkieletu dłoni.
[Komfort: seated/accessibility](https://developers.meta.com/vr/design/locomotion-comfort-usability/),
[Steamworks: wiązania OpenXR](https://partner.steamgames.com/doc/features/steamvr/settings).

**Propozycja:** osobny profil siedzący/stojący z wygodnym zasięgiem UI
i recenter bez powtarzania pełnej T-pose; krótki instruktaż pokazujący gesty
i ich rozpoznanie. W kolejnym etapie akcje OpenXR dla kontrolerów Touch,
mapowanie menu/ruchu/obrotu oraz poprawne przekazanie wejścia między kontrolerem
a dłonią. Kontrolery są alternatywą, nie warunkiem korzystania z moda dłonią.

Offline: model akcji, profile i testy arbitrażu. Gogle: śledzenie kontrolerów,
mapowania SteamVR, przełączanie metod, ergonomia na siedząco.

### 7. Raport możliwości rzeczywiście aktywnego runtime

OpenXR udostępnia listę dostępnych rozszerzeń. Sama nazwa Quest 3 nie oznacza,
że każde rozszerzenie Meta jest udostępnione przez wybrany runtime PCVR.
Flagi gestów systemowych należą do opcjonalnego `XR_FB_hand_tracking_aim`.
[Enumeracja rozszerzeń](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEnumerateInstanceExtensionProperties.html),
[Flagi hand aim](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrHandTrackingAimFlagBitsFB.html).

**Propozycja:** ekran diagnostyczny: runtime i wersja, dostępność rąk,
czy działają flagi systemowe czy tylko ochrona geometryczna, stan fokusu
i śledzenia, rozdzielczości oczu, przewidywany okres klatki. Testy regresyjne
z obecnym i nieobecnym rozszerzeniem, bez automatycznego zmieniania runtime
systemowego lub parametrów SteamVR. Kod już wykrywa część tych możliwości;
potrzebna jest czytelna prezentacja i testy całej ścieżki.

Offline: ekran, raport i macierz atrap runtime. Gogle: potwierdzenie na
Steam Link + SteamVR; inne ścieżki PCVR dopiero po osobnej próbie.

## Zalecana następna paczka

Najpierw poprawka widoczności dłoni przy utracie fokusu i testy przejęcia wejścia,
wariant 1:1 głowy z wygaszaniem do porównania, wyraźne kliknięcia i pomiary
GPU/budżetu oraz opcjonalne ustawienia komfortu i siedzenia. Alternatywa
ogranicznika głowy wymaga próby porównawczej. Kontrolery to większy następny etap. Teleportacja,
passthrough i przenoszenie całego projektu na SDK Unity nie są konieczne do
tego etapu i nie wynikają automatycznie z wytycznych Steam/Meta.
