# Sterowanie ruchem dłoni — aktualizacja 2026-11-06

Po próbie użytkownika uproszczono aktywację: żaden domyślny tryb ruchu
nie wymaga szczypnięcia. Gra pozostaje zamknięta; poniższe zmiany sprawdzono
na nagraniu i w testach offline. Faktyczny ruch wymaga następnej próby.

## Bieg: dwie zamknięte pięści

Włącz **DWIE PIESCI** w menu VR albo `start-vr-test.ps1 -Movement Fists`.
Preset bez parametru przywraca ostatnio zapisany tryb (początkowo Fists).
**Obie pięści zamknięte = bieg do przodu ze
sprintem. Otwarcie jednej dłoni = stop.** Nie trzeba wymachiwać rękami,
trzymać ich w określonej strefie przy ciele ani wcześniej otwierać dłoni.

Po odzyskaniu poprawnego śledzenia zamknięte pięści znów działają. Pauza,
menu VR, fotel, fizyczne klawisze, brak fokusu i brak świeżych danych nadal
blokują ruch. Wyświetlane stany to BIEG, TRYB PIESCI - STOP lub przyczyna
blokady — bez sekwencji sprzecznych poleceń otwierania i zaciskania dłoni.

`-ArmSwingRun` pozostaje zgodnym aliasem uruchamiacza; wewnętrzny identyfikator
`arm_swing` i nazwa liczników zachowują kompatybilność ze starszymi raportami.
W aktualnej konfiguracji oznaczają stan dwóch pięści.

## Joystick: chwyt całą dłonią

Wybierz **JOYSTICK** i zaciśnij wybraną dłoń (domyślnie lewą). To chwyta
wirtualny joystick w bieżącej pozycji nadgarstka. Przesunięcie ponad 3 cm
steruje przód/tył/boki; 12 cm do przodu włącza sprint, poniżej 9 cm go wyłącza.
Otwarcie dłoni kończy ruch. Ponowne zaciśnięcie ustawia nowy punkt neutralny.

W **MENU → CHODZENIE** wybierz `JOY LEWA` oraz **TYLKO PRZOD: WL / WYL**,
aby lewa ręka poruszała postać wyłącznie do przodu. Cofnięcie ręki zatrzymuje
ruch, a ruch boczny jest ignorowany. Prawa dłoń nadal obraca postać.
Sprint działa przy większym wychyleniu do przodu. Wyłączenie przełącznika
przywraca przód/tył/boki; ustawienie jest zapisywane między sesjami.
[Szczegóły i poprawka początkowego dopasowania głowy do ciała](forward-only-and-heading-20261106.md).

Kciuk może być odsunięty od palców; nie ma kontaktu kciuk–środkowy ani
kciuk–wskazujący. Ta sama zasada działa dla lewej i prawej dłoni. Aktywny
joystick rezerwuje wybraną rękę, żeby nie naciskała równocześnie przycisków Q/E.

Kierunek jest liczony względem postaci podążającej za głową (`BodyYaw`).
Przesunięcie głowy razem z dłonią i sam obrót głowy nie wychylają joysticka.
Po przerwaniu śledzenia nowy chwyt zaczyna od neutralnej pozycji.

**Druga dłoń służy do płynnego obrotu** w trybie JOYSTICK. Zaciśnij ją w
wygodnej pozycji i przesuń w lewo/prawo. Domyślne progi to 3–12 cm;
pełne wychylenie daje 60°/s. W **OBROT / MENU** można niezależnie od chodzenia
zmieniać prędkość 15–120°/s, martwą strefę i zakres wychylenia.
Otwarcie dłoni zatrzymuje obrót, kolejny chwyt
ustawia nowy środek. Można obracać się stojąc albo podczas chodzenia i biegu.
Po wybraniu prawej dłoni do chodzenia obrót przechodzi na lewą. Chwyt obrotu
także rezerwuje rękę przed Q/E. Pauza, menu, fotel i utrata śledzenia blokują
obrót. Ta nowa funkcja jest przetestowana offline; komfort i kierunek wymagają
sprawdzenia w goglach. Historyczny wariant `middle_pinch` zachowuje dawną obsługę.

W **OBROT / MENU → KOMFORT** można dodatkowo włączyć obrót skokowy 30° lub 45°.
Wychylenie wykonuje jeden skok, następny wymaga powrotu do martwej strefy.
Płynny obrót pozostaje domyślny. Tam też są opcjonalna winieta, wariant głowy
1:1 z wygaszaniem oraz profil siedzący i USTAW SRODEK.
W **INTERFEJS** można zmienić czas wskazania 300–2000 ms, skalę 100–140%
i włączyć dźwięk kliknięć. [Pełna obsługa i ograniczenia](vr-comfort-20261105.md).

## Śledzenie pięści i wejście systemowe

Detektor wymaga aktywnej dłoni, śledzonego nadgarstka oraz poprawnych pozycji
wskazującego i co najmniej dwóch innych palców. Ukryte końce palców mogą być
bieżącymi estymacjami runtime; nie jest wymagane optyczne śledzenie każdego
końca palca wewnątrz pięści. Brak dostatecznej liczby poprawnych pozycji
zatrzymuje ruch. Kciuk nie bierze udziału w rozpoznaniu chwytu.

Flagi systemowe runtime zawsze blokują wejście moda. Ochrona geometryczna
wymaga teraz również otwartych palców, więc zamknięta pięść skierowana do
użytkownika nie jest sama w sobie gestem systemowym. SteamVR w ostatniej
próbie nie udostępniał flag Meta; dlatego pozostaje ostrożna ochrona z pozycji.

## Menu i konfiguracja

Panel jest domyślnie **1 m przed głową**. Wskaż pozycję przez **0,8 s**
(zielony pasek postępu) albo dotknij jej palcem. Menu i klawiatura moda także
nie wymagają szczypnięcia. Przycisk MENU znajduje się 80 cm przed głową,
18 cm powyżej oczu. Panele zachowują pion mimo przechylenia głowy. Niewielkie
drżenie przy krawędzi przycisku nie zeruje postępu wskazania. Zielony promień
menu wskazuje aktywny cel dłoni z paskiem postępu. Drugi promień oraz promień
bez trafienia są szare; druga dłoń nie uruchamia w tle innego przycisku.

Kalibracja T-pose mierzy rozstaw dłoni wzdłuż ramion, niezależnie od kierunku
z początku sesji. Po przyjęciu ustawia od nowa środek głowy i karku, zachowując
kierunek świata. Wybór KALIBRUJ rozpoczyna nową próbę także po poprzednim sukcesie.
Wynik jest uśredniany ze stabilnego odcinka T-pose, z odrzucaniem pojedynczych
skoków śledzenia. Komunikat USTABILIZUJ RECE I GLOWE oznacza zbyt duże wahania;
utrzymaj pozycję, aż ramka stanie się zielona.

W [`config/gestures.ini`](../config/gestures.ini):

```ini
[joystick]
forward_only=false
activation=whole_hand
[arm_swing]
activation=two_fists
[input]
joystick_hand=left
pointing_priority=right
menu_distance_m=1.0
movement_mode=fists
[turn]
speed_degrees=60
dead_zone_m=0.03
full_deflection_m=0.12
```

Odległość menu można ustawić w **OBROT / MENU** w zakresie 0,6–1,2 m.
Zmiana obowiązuje przy następnym otwarciu panelu. Wybór trybu, dłoni,
prędkości/progów obrotu i odległości zapisuje się automatycznie do INI.
PRZYWROC DOMYSLNE resetuje pełną konfigurację sterowania; WCZYTAJ odczytuje plik.
Reszta progów ruchu pozostaje w INI. Walidacja odrzuca literówki, duplikaty
i nieprawidłowe liczby.

## Natywna ścieżka i dowody

Wyłącznie profil Steam 26294447, SHA `c38c03ff…`. Hook `0x868410` zachowuje
oryginalne trzy argumenty i wywołanie. Własne wejście dopisuje wyłącznie dla
callerów `0x86a36b/0x86cdeb`, do bieżącej kolejki przekazanej w R8, po ponownym
sprawdzeniu tożsamości. Argument RDX jest żywym kontekstem requestów D+0x280.
Nie zapamiętuje kolejki ani aktora do późniejszego wywoływania.

`NativeMovementRequest` odtwarza natywny `set_input_buttons`: rozmiar 0x20,
typ 100, vtable `0xb5ee18`, serializator `0x234870`, maski 128 bitów pod +0x10.
Mapowanie samplera `0x947760` i nazw ustawień `0x213e30`:

| Ruch | Ustawienie klawisza | Bit |
|---|---|---|
| Przód | +0x3f0 (`action_up`) | 0x4 |
| Tył | +0x448 (`action_down`) | 0x8 |
| Lewo | +0x4a0 (`action_left`) | 0x1 |
| Prawo | +0x4f8 (`action_right`) | 0x2 |
| Sprint | +0x600 (`action_sprint`) | 0x80 |
| Powolny chód (wykrywany jako fizyczny konflikt) | +0x658 (`action_walk`) | 0x100 |

Źródłowe maski są odczytywane z bieżącego sim+0x1a10/0x1a18. W aktywnym ruchu
każdy rozpoznany przebieg wysyła ich kopię z dodanymi bitami gestu. To zapobiega
nadpisaniu gestu zwykłą migawką wejścia; koszt dodatkowych requestów trzeba
zmierzyć w grze. Alokacja i enqueue pochodzą z natywnych, chronionych prefiksem
helperów. Po handoff mod nie odczytuje ani nie zwalnia requestu i nie ponawia
niepewnego enqueue. Własność po przekazaniu podlega standardowej drodze gry;
nie dodano osobnego potwierdzenia dostarczenia/retirement tych requestów.

Dowód statyczny: [native-locomotion-evidence-20261103.json](../artifacts/native-locomotion-evidence-20261103.json).
Guardy EXE: [locomotion-profile-20261103.json](../artifacts/locomotion-profile-20261103.json).

## Weryfikacja i ograniczenia

Release, **48/48 CTest**, **45/45 Python**, **68 guardów EXE** i test tracera
GPU. Testy sprawdzają natychmiastowy start ze statycznych pięści, otwarcie
ręki, brak wymogu wstępnego otwierania, utratę fokusu/śledzenia, powtórzony
czas XR, chwyt joysticka obu rąk, punkt neutralny i natywne pakiety/release.
Menu ma regresje wskazania bez szczypnięcia, pojedynczej aktywacji po 0,8 s,
przerw śledzenia i zachowania dotyku.

Odtworzenie ostatniej próby wykazało 3543 próbki z wejściem dłoni wyłączonym
mimo śledzonych nadgarstków. Przy nowej ochronie 2709 z tych ramkowych
stanów nadal odpowiada otwartej dłoni skierowanej do głowy; zamknięte chwyty
nie są już odrzucane. Symulacja nowych gestów na zapisanych pozycjach
rozpoznaje bieg w 2696 próbkach, ale nie wysyła niczego do gry i nie potwierdza
faktycznego sprintu. Natywne wejście jest odrębnie testowane z atrapami.

`#locomotion_totals` liczy requesty, a nie metry ruchu ani potwierdzenia serwera.
Nowe zachowanie i brak przypadkowego menu Questa wymagają próby na goglach.

## Starsze nagrania

Nowe nagrania `SWVR_HANDS 3` zapisują jawnie sposób aktywacji obu trybów.
Odtwarzacz nadal czyta v1/v2: brakujące historyczne ustawienia oznaczają
stary pinch i rytm wymachów, więc porównanie nie zmienia dawnych wyników.
Algorytmy historyczne pozostają dla tego odtwarzania; bieżący preset i INI
wybierają `whole_hand` i `two_fists`.

`-RecordHands`, `swvr_replay_hands.exe` i `scripts/view-hand-recording.ps1`
pozwalają sprawdzać nagrania offline. [Obsługa](offline-controls-20261104.md),
[pierwotne porównanie rozwiązań VR](vr-running-research.md).
