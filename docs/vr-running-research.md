# Chodzenie i bieganie w VR — rozpoznanie 2026-11-03

Cel: dobrać sterowanie dla Questa 3 z gołymi dłońmi. To porównanie źródeł
i propozycja projektu; nie potwierdza działania nowej funkcji w Stormworks.

## Rozwiązania istniejące

| Przykład | Sterowanie i zakończenie ruchu | Znaczenie dla moda |
|---|---|---|
| **Half-Life 2: VR Mod** | Płynny ruch drążkiem niedominującej dłoni. Kliknięcie drążka włącza sprint do powrotu drążka do neutralu. Kierunek można oprzeć na głowie albo dłoni. | Rozdziela zamiar ruchu od sprintu i daje jednoznaczny neutralny stan zatrzymania. Wymaga kontrolera. [Instrukcja twórców](https://halflife2vr.com/manual/). |
| **Natural Locomotion** | Aplikacja emuluje wejście drążka gry; wolne ruchy kontrolerów dają chód, szybkie bieg. Opisuje także aktywator gestowy i opcjonalne trackery ciała/stóp. | Źródło pomysłu arm-swing i zachowania natywnej symulacji gry. Lista obsługiwanych urządzeń wymienia kontrolery; opis „hands mode” nie dowodzi obsługi gołych dłoni OpenXR Questa. [Opis producenta Myou na Steam](https://store.steampowered.com/app/798810/Natural_Locomotion/). |
| **VRChat — finger tracking** | Lewa dłoń, wnętrze dłoni do góry: zetknięcie środkowego palca z kciukiem tworzy punkt odniesienia; przesunięcie nadal zaciśniętych palców względem niego steruje kierunkiem ruchu. Prawa dłoń z tym samym gestem służy do obrotu. | Konkretny wzorzec wirtualnego joysticka bez kontrolerów. Palec środkowy oddziela ruch od index–thumb pinch używanego do UI. Źródło nie opisuje tutaj osobnego progu sprintu. [Oficjalna strona informacyjna VRChat](https://wiki.vrchat.com/wiki/Finger_Tracking/en). |

Oficjalna instrukcja VRChat zaleca utrzymywanie dłoni w zasięgu kamer i dobre
oświetlenie. To dodatkowy argument, by rozwiązania wymagające szerokiego
wymachu ramion sprawdzić osobno na gołych dłoniach. Nie należy przenosić
deklaracji o śledzonych kontrolerach na optyczny hand tracking.
[VRChat — wskazówki śledzenia](https://wiki.vrchat.com/wiki/Finger_Tracking/en).

H3VR potwierdza drugą realizację rodziny arm-swing: oficjalna aktualizacja 85
dodawała osobne prędkości bazowe lewej/prawej dłoni i opcję skoku bezwładnościowego.
Sam changelog nie wystarcza jednak do odtworzenia pełnego algorytmu ani obsługi
gołych dłoni. [Komunikat twórcy na Steam](https://store.steampowered.com/news/posts/?appids=450540&enddate=1584150282&feed=steam_community_announcements).

## Wnioski z dokumentacji hand tracking

Ultraleap rozdziela ciągłą lokomocję na włączenie, ruch i wyłączenie.
Zaleca aktywowanie jej tylko wtedy, gdy jest potrzebna; stale aktywne gesty
mogą kolidować z manipulacją. Wprost odradza prosty pinch-teleport obok innych
interakcji otwartej dłoni i pokazuje jawne aktywatory: uchwyt na nadgarstku,
gest lub przycisk dłoniowy. Zwolnienie gestu może wyłączać ruch ciągły.
[Ultraleap — Locomotion](https://docs.ultraleap.com/xr-guidelines/Interactions/locomotion.html).

## Rekomendacja dla Stormworks (wniosek projektowy)

Pierwszy wariant: **lewy middle–thumb pinch przytrzymywany jako wirtualny
joystick**, z punktem neutralnym ustalanym na rozpoczęciu gestu. Prawy
index–thumb pinch zachowuje dotychczasowe interakcje kokpitu. Ruch dłoni
wyznacza kierunek w płaszczyźnie poziomej; powrót do martwej strefy zatrzymuje,
otwarcie dłoni kończy sterowanie. Przy większym wychyleniu do przodu można
włączyć natywny sprint z histerezą. To proponowane połączenie wzorca VRChat
i rozdzielenia chodu/sprintu z HL2VR, **nie opis istniejącego sprintu VRChat**.

Warunki integracji wynikające z obecnego projektu:

- Wyłącznie pieszo w pierwszej osobie. Fotel, pauza, UI, kalibracja T-pose
  oraz nieznany kontekst blokują nowe wejście ruchu.
- Utrata fokusu lub śledzenia, stara próbka i zmiana LOCAL/postaci anulują
  ruch. Powrót wymaga ponownego otwarcia dłoni i świadomego rozpoczęcia.
- Natychmiastowy stop po zwolnieniu gestu. Wygładzanie zwiększania prędkości
  nie może przedłużać ruchu po anulowaniu.
- Wykorzystać zweryfikowane natywne akcje chodu/sprintu zamiast bezpośrednio
  przesuwać postać lub modyfikować jej prędkość. Mapowanie wymaga osobnego
  potwierdzenia w profilu aktualnego EXE.
- Najpierw testy offline na sztucznych próbkach: neutralny gest, histereza,
  konflikty, przerwy, reset i dokładne zwolnienie własnych wejść. Prędkości,
  progi i komfort pozostają do oceny w późniejszej, jawnie dozwolonej próbie.

Arm-swing warto zachować jako późniejszy, opcjonalny wariant po pomiarze
ciągłości śledzenia dłoni i odróżniania wymachu od normalnego sięgania po
przełączniki. Sam wykryty ruch rąk nie powinien domyślnie rozpoczynać biegu.
