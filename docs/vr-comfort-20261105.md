# Komfort i obsługa SteamVR / Quest 3 — wdrożenie 2026-11-05

Wdrożono wszystkie pięć uzgodnionych rozszerzeń z
[przeglądu praktyk VR](vr-standards-review-20261105.md). Stormworks nie uruchamiano.
Zmiany opierają się na obecnym PCVR/OpenXR; nie oznaczają certyfikacji sklepu Meta.

## Obsługa

Otwórz **MENU → OBROT / MENU**. Stamtąd dostępne są **INTERFEJS** i **KOMFORT**.
Ustawienia zapisują się do wybranego przy starcie pliku INI. Starsze pliki
zachowują dotychczasowe wartości domyślne.

| Strona | Opcja | Działanie |
|---|---|---|
| INTERFEJS | CZAS − / + | Wskazanie przez 300–2000 ms, krok 100 ms; domyślnie 800 ms. Dotyczy też MENU i klawiatury. |
| INTERFEJS | ROZMIAR − / + | Skala panelu i pól trafienia 100–140%, krok 10%. |
| INTERFEJS | DZWIEK KLIKNIEC | Krótki sygnał po przyjęciu akcji; domyślnie wyłączony. |
| KOMFORT | OBROT PLYNNY / SKOKOWY | Płynny pozostaje domyślny. Skokowy działa drugą dłonią w trybie JOYSTICK. |
| KOMFORT | SKOK 30 / 45 | Kąt pojedynczego obrotu. Wymaga włączenia trybu skokowego. |
| KOMFORT | WINIETA 0 / 45 / 75 | Siła przyciemnienia obrzeży podczas ruchu i wirtualnego obrotu: wyłączona, 45% albo 75%. |
| KOMFORT | GLOWA LIMIT / 1:1 | Dotychczasowy ogranicznik albo fizyczny ruch głowy 1:1 z wygaszaniem obrazu przy granicy wychylenia. |
| KOMFORT | SIEDZE / STOJE | Zmiana profilu centruje pozycję głowy; siedzący obniża panel o 7 cm. |
| KOMFORT | USTAW SRODEK | Ustawia bieżącą pozycję głowy jako środek, zachowuje kierunek świata i zamyka menu. |

Po poprawce z próby zmiana skali, odległości, wysokości i czasu wskazania
obowiązuje w otwartym panelu. Przy zmianie pod wskazującą dłonią trzeba odsunąć
wskaźnik przed następnym kliknięciem. **?** przełącza tryb objaśnień bez wykonywania
akcji. [Pomoc i poprawki menu](menu-help-20261105.md).
Po akcji pozostaje pomarańczowe potwierdzenie przez 150 ms, jeśli panel nadal
jest widoczny. Stopka rozróżnia DOTYK i WSKAZYWANIE; pasek nadal pokazuje czas
do wyboru. Nawigacja na inną stronę daje natychmiast widok nowej strony.

Skok następuje po wychyleniu dłoni do 75% zakresu poza martwą strefą
(domyślnie około 9,75 cm od środka). Przytrzymanie nie powtarza skoków.
Przed następnym wróć do martwej strefy albo otwórz i ponownie zaciśnij dłoń.
Utrata śledzenia, fokusu, pauza, panel i fotel blokują obrót jak dotychczas.

Profil siedzący nie wymaga nowego gestu. Recenter jest lokalną kalibracją moda,
nie zmienia granic przestrzeni Questa ani ustawień SteamVR. Nie dodaje sztucznej
wysokości stojącej postaci. Dwie pięści nadal uruchamiają bieg w wybranym trybie.

Sygnał kliknięcia korzysta z domyślnego wyjścia dźwięku Windows. Dźwięk trafi
do gogli, jeśli to wyjście jest tam skierowane. Odtwarzanie nie czeka na koniec
sygnału; problem urządzenia wyłącza sam dźwięk i jest zapisywany w logu.

## Fokus i kamera

Po utracie fokusu aplikacja ukrywa nakładkę dłoni, promienie, panele i efekty
komfortu, także gdy runtime nadal przesyła aktywne stawy dłoni. Wejście jest
anulowane; świat jest dalej renderowany, kiedy runtime tego wymaga. Po powrocie
joystick ustawia nowy środek. Dwie zamknięte pięści mogą ponownie uruchomić bieg,
zgodnie z wybranym sposobem sterowania.

GLOWA 1:1 zachowuje fizyczne przemieszczenie kamery zamiast kompresować je przy
granicy. Wygaszanie rośnie od 60% dotychczasowej elipsoidy wychylenia do pełnej
czerni na jej granicy. Powrót do środka przywraca obraz. Menu i dłonie rysują się
nad wygaszeniem, żeby można było użyć USTAW SRODEK. To granica względem
skalibrowanej pozycji ciała, nie wykrywanie ścian ani system bezpieczeństwa pokoju.
`-NoHeadLimit` wyłącza także to wygaszanie. Dotychczasowy LIMIT pozostaje domyślny.

Winieta łagodnie narasta i zanika; uwzględnia osobne pola widzenia i orientacje
obu oczu. Resetuje się po utracie fokusu/śledzenia lub zmianie przestrzeni LOCAL.
Jej promień można ustawić w INI (25–60 stopni, domyślnie 40). Komfort i siła
efektu wymagają oceny w goglach.

Domyślne nowe pola w [`config/gestures.ini`](../config/gestures.ini):

```ini
[input]
dwell_ms=800
menu_scale=1.0
click_sound=false

[turn]
snap=false
snap_degrees=30

[comfort]
head_fade=false
seated=false
vignette_strength=0
vignette_radius_degrees=40
```

## Diagnostyka i replay

`.performance.json` ma teraz schemat 2, z zachowaniem wcześniejszych pól CPU.
Dodaje czas pracy klatki CPU (bez oczekiwania `xrWaitFrame`), zakres czasowy GPU,
przewidywany okres klatki OpenXR oraz liczbę i procent przekroczeń tego budżetu.
Podsumowanie `.summary.md` automatycznie pokazuje średnie, P95/P99 i maksima.
Czytnik obsługuje też schemat 1 oraz sesje bez raportu wydajności.

Pomiar GPU używa 16 par znaczników czasu i odczytuje tylko gotowe wyniki.
Nie dodaje `glFinish` ani oczekiwania na zapytania. Przy zajętej puli pomija
pomiar; raport podaje pominięcia i wyniki oczekujące przy zamknięciu.
Brak obsługi zapytań oznacza niedostępny pomiar, nie zerowy koszt GPU.

Zakres GPU obejmuje polecenia od początku pracy po pobraniu klatki XR do
zakończenia rysowania obrazu i nakładki, przed `xrEndFrame`. Może zawierać
bezczynność GPU między poleceniami CPU: nie jest pomiarem samego zajęcia GPU.
Nie obejmuje kompozytora, kodowania ani transmisji Steam Link. Przewidywany okres
OpenXR może różnić się od fizycznego odświeżania panelu. Kanałów CPU/GPU nie należy
sumować, a powtórzeń moda utożsamiać z reprojekcją SteamVR.

Przy następnej próbie należy porównać raport z wykresem czasu klatek SteamVR
z tego samego odcinka i przy tych samych ustawieniach odświeżania/rozdzielczości.
Nie ma automatycznego pobierania statystyk kompozytora; obecne wyniki offline
nie dowodzą wzrostu FPS ani dotrzymania budżetu na Queście.

Nowe nagrania to `SWVR_CONTROLS 3`: zapisują też czas wskazania, skalę paneli,
aktualizację otwartego panelu, pomoc i ustawienia obrotu skokowego. Replay obsługuje wersje 1–3. Format nagrań dłoni
v1–v3 nie zmienił się. Replay weryfikuje działania wejścia, nie renderowanie efektu
ani dźwięku i nie przemieszczenie postaci w grze.

## Weryfikacja offline

Release bez ostrzeżeń kompilatora: **50/50 CTest**, **51/51 Python**,
**68 kontroli profilu EXE**, samodzielny tracer PASS. Testy obejmują utratę fokusu
przy aktywnych dłoniach, powrót sterowania, skoki 30/45 stopni, kamerę 1:1,
winietę, nowe ustawienia, powiększone cele, dwell, potwierdzenie kliknięć,
zapis/replay i raporty budżetu. Rzeczywisty OpenGL w ukrytym oknie sprawdza efekt
w obu oczach, odchylone osie oczu, przywrócenie stanu gry i nieblokujące zapytania.
Nowe strony menu sprawdzono wizualnie; powtarzanie ich identycznego obrazu ma
regresję piksel po pikselu. Dodatkowe pięć powtórzeń testu GPU i komfortu przeszło.

- [CTest](../build/capture/artifacts/vr-standards-20261105.xml)
- [Python](../artifacts/vr-standards-python-20261105.xml)
- [Profil gry](../artifacts/vr-standards-profile-20261105.json)
- [INTERFEJS, skala 140%](../artifacts/vr-interface-menu-20261105.png)
- [KOMFORT, skala 140%](../artifacts/vr-comfort-menu-20261105.png)

Do próby na Queście pozostają ergonomia, odczucia przy obrocie/winiecie,
zachowanie rzeczywistego menu systemowego, dźwięk i wydajność całego toru PCVR.
