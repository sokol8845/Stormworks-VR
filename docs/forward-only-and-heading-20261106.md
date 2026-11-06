# Chodzenie tylko do przodu i początkowy kierunek ciała — 2026-11-06

## Wynik próby w goglach

Użytkownik potwierdził, że **animacja kierownicy Driver seat działa**.
Sesję `headset-xr-20261106-200021-563` zakończono na jego prośbę:
`user_stop`, pełne zwolnienie wejścia i kontrolowane zamknięcie procesu.
Log zawiera 22430 par obrazów, 978 pakietów kierownicy (950 niezerowych),
3 otwarcia i 3 zatwierdzenia klawiatury, bez błędów natywnych adapterów.
Sama obecność tych liczników nie zastępuje oceny klikania po OK przez gracza.

Zgłoszony problem dotyczył stania na początku sesji: po T-pose głowa jest
obrócona w poziomie kilkanaście stopni względem ciała; pomaga obrót myszą.

## Diagnoza i poprawka kierunku

Minimalny test wykonuje rzeczywisty ciąg `LookYawCalibration::observe` →
`follow_headset_yaw_turning`, używany przez aktualizację kamery. Natywne
patrzenie zaczyna na 0 rad, widok HMD różni się o 15°. Po dopasowaniu następna
klatka musi utrzymać zgodność głowy i ciała bez sztucznego obrotu świata.

Przed poprawką:

```text
swvr_body_yaw_tests.exe 0
startup: head/body remain 15 degrees apart until mouse movement

swvr_body_yaw_tests.exe 0.3
PASS: body yaw mapping identification and headset following
```

Zmiana wyłącznie początkowego kąta z 0 na 0,3 rad usuwała problem. Nie trzeba
zmieniać środka T-pose ani kodu utrzymującego dopasowanie w kolejnej klatce.
Przyczyną jest oczekiwanie na jednoznaczny znak osi: dawniej próbkowanie
odrzucało kąty bliskie wielokrotności 90° (`abs(sin(field)) < .2` lub
`abs(cos(field)) < .2`). Gdy znak nie był jeszcze rozpoznany, dopasowanie
ciała i obrót drugą dłonią pozostawały zablokowane aż do ruchu myszy.

Obsługiwany profil EXE ma teraz jawny, zmierzony `look_yaw_sign=1`.
Potwierdza go również observer z tej sesji: `locked sign=1 offset=0`.
Host korzysta z niego dopiero po sprawdzeniu pełnego SHA-256 i sygnatur
kamery. Nadal wymagane jest 10 kolejnych zgodnych próbek żywej macierzy;
przesunięcie kąta jest rozpoznawane, a niespójny lub nieważny odczyt resetuje
okno. Ogólny wariant bez zweryfikowanego profilu nadal czeka na jednoznaczny
pomiar. Podczas przenoszenia profilu na inną wersję gry znak trzeba ponownie
zweryfikować. T-pose zachowuje dotychczasowy kierunek świata.

Oba powyższe wywołania przechodzą po poprawce. Regresje obejmują także
0/±90°/±180°, oba znaki, oba przesunięcia, reset i niespójny odczyt.
To odtworzony błąd przy starcie; potwierdzenie poprawy w goglach pozostaje
do następnej próby.

## Przełącznik joysticka

**MENU → CHODZENIE → TYLKO PRZOD: WL / WYL**. Na tej stronie są też
`JOY LEWA` i `JOY PRAWA`, wcześniej dostępne na stronie głównej.
Stan przełącznika i dłonie ruchu/obrotu są widoczne nad przyciskami.

Po włączeniu przy `JOY LEWA`:

- lewa pięść wychylona do przodu włącza chodzenie, dalej — sprint;
- powrót do środka lub cofnięcie dłoni zatrzymuje ruch, bez cofania postaci;
- wychylenie w bok nie powoduje ruchu bokiem ani nie blokuje sprintu do przodu;
- prawa pięść obraca postać jak dotąd, również podczas chodzenia i sprintu;
- otwarcie lewej dłoni kończy chodzenie; niezależne blokady fokusu,
  śledzenia, menu, fotela i nieaktualnych próbek nadal obowiązują.

Po wybraniu `JOY PRAWA` role dłoni zamieniają się. Wyłączenie przełącznika
przywraca przód/tył/boki. Tryb dwóch pięści i kierownica zachowują swoją
obsługę. Opcja zapisuje się z pozostałymi ustawieniami po zwolnieniu wejścia:

```ini
[joystick]
forward_only=true
```

Brak klucza w starym pliku oznacza `false`. Nagrania zapisują tę preferencję,
a stare nagrania zachowują dotychczasową interpretację. Tryb `?` opisuje
przełącznik bez jego uruchamiania. Lokalne ustawienia zmienione przez gracza
w ostatniej sesji nie zostały nadpisane.

## Weryfikacja

Release, **56/56 CTest**, **51/51 pytest**, **78 kontroli EXE** przechodzą.
Testy chwytu sprawdzają oba przypisania dłoni, dodatni sygnał ruchu, brak
cofania i ruchu bokiem, sprint przy bocznym odchyleniu, równoczesny obrót,
puszczenie i ponowny chwyt, brak fokusu, stare próbki oraz powrót do pełnego
joysticka. Testy menu sprawdzają przełączanie, stan, nawigację i pomoc;
testy konfiguracji/nagrań — trwałość preferencji i zgodność ze starymi plikami.

Następna próba: bez ruszania myszy skalibrować ciało na stojąco; sprawdzić
zgodność kierunku głowy i ciała oraz obrót prawą dłonią. Włączyć opcję w menu,
sprawdzić przód/bok/cofnięcie lewej ręki i chodzenie z obrotem prawej.
