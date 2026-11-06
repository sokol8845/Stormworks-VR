# Przyciski pojazdu i kierownica — diagnoza 2026-11-06

Po próbie `headset-xr-20261106-172903-416` użytkownik potwierdził, że problem
dotyczył przycisków pojazdu oraz braku ruchu zarówno kierownicy, jak i kół.
Grę zamknięto. Poniższe poprawki wykonano offline; potrzebna jest ponowna
próba w goglach. Nie uznawać liczników wysłanych komend za wynik testu pojazdu.

## Kliknięcia kasowane przez ruch głowy

`NativeVr` przekazywał do `HandInteractionBridge` efektywny `view_origin`
jako kalibrację. Ogranicznik głowy zmienia tę pozycję przy wychyleniu, podczas
gdy most interakcji resetował detektor szczypnięcia przy każdej zmianie
kalibracji. Promień pozostawał poprawny, ale kliknięcie nie zaczynało się
albo trzymanie było anulowane. Ponowne T-pose nie usuwało przyczyny.

Test `swvr_hand_interaction_tests.exe`, korzystający z rzeczywistego
`head_comfort_view`, przed poprawką zgłaszał:

```text
vehicle click lost while head comfort moves the view origin
```

Most dostaje teraz osobno stabilną kalibrację oraz efektywną transformację
widoku. Pierwsza odpowiada za anulowanie po recenter, druga za położenie
promienia. Regresja sprawdza rozpoczęcie i utrzymanie gestu oraz zgodność
promienia. Test integracyjny przycisków obejmuje tę samą korektę głowy,
selekcję celu, native Q/E, zwolnienie, anulowanie i rozliczenie pamięci.

## Oś skrętu bez włączonego trybu analogowego

Weryfikacja statyczna lokalnego Steam build 26294447, SHA-256:
`c38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d`.
Adresy poniżej to RVA tego profilu.

| Etap | Zachowanie potwierdzone w instrukcjach |
| --- | --- |
| Request 101, `85C858` | Przekazuje indeks osi i wartość do `22BF30`. |
| `22BF30` | Tworzy odpowiedź type168: peer +10, axis +12, value +14. |
| Odbiornik type168, `860177` | Sprawdza indeks <8 i zapisuje wyłącznie `actor+23C0+4*index`. |
| Aktualizacja fotela, `4E4972..4E49A8` | Gdy `actor+23F0` jest zerem, pomija wejścia analogowe; inaczej wybiera mocniejsze wychylenie osi lub klawiatury. |
| Request 1, `856673` → `22BD10` | Przekazuje pełny stan wejścia, w tym bajt trybu +40, do odpowiedzi type7. |
| Odbiornik type7, `85FFC3..8600FA` | Dla lokalnej postaci w fotelu kopiuje pełny stan pod `actor+23B0` przez `231920`. |
| Serializer pełnego wejścia `2319A0`, vtable `B57D80` | Natywny format requestu rozmiaru 0x60; blok wejścia zaczyna się pod +10. |

Poprzedni adapter wysyłał wyłącznie request101. Zmieniał wartość osi,
ale przy wyłączonym sterowaniu analogowym fotel jej nie używał.
Regresja rzeczywistego adaptera z modelem tego kontraktu odbiornika zgłaszała:

```text
vehicle steering stayed at zero: axis packets did not enable analog seat input
```

Adapter wysyła teraz pełny natywny stan wejścia z włączonym trybem analogowym
i własną osią skrętu. Robi to po oryginalnym senderze, więc jego odświeżenie
nie wyłącza ponownie sterowania. Nie zmienia pamięci fizycznego wejścia.
Zachowuje przyciski, kierunek patrzenia i aktywne pozostałe osie; gdy natywny
tryb analogowy jest wyłączony, nie uruchamia jego nieaktywnych, starych osi.
Puszczenie, pauza, zmiana fotela i reset przywracają aktualny stan fizyczny
wraz z jego trybem. A/D oraz aktywna fizyczna oś skrętu mają pierwszeństwo.

Regresje obejmują także natywne odświeżenie pełnego wejścia, kontroler
włączony/wyłączony, zachowanie obcych pól i dokładne przywrócenie stanu.
W raporcie dodano `steering_nonzero_packets` oraz `steering_max_axis`.
To pomiary wysyłanego wejścia, a nie odczyt kąta kół pojazdu.

Lokalne dowody analizy są w `tmp/cockpit-axisproof.asm`,
`tmp/cockpit-axisconsumer.asm` i `tmp/cockpit-fullreceiver.asm`.
Do ponownego eksportu służy `scripts/ghidra/ExportInstructions.java`.
Repozytorium nie zawiera pliku EXE ani pełnych dekompilacji gry.

## Następna próba w goglach

1. Wsiąść do fotela; pokazać otwarte dłonie i chwycić kierownicę obiema.
2. Skręcić w obie strony, sprawdzić koła oraz reakcję pojazdu.
3. Puścić obie dłonie i sprawdzić powrót sterowania; sprawdzić A/D.
4. Nacisnąć kilka przycisków pojazdu przy głowie wyprostowanej i wychylonej.
5. Sprawdzić przytrzymanie, puszczenie i otwarcie klawiatury numerycznej.

Test kontraktu odbiornika działa na atrapach i nie zastępuje punktów powyżej.

Weryfikacja końcowa: kompilacja Release z czystej kopii publikowanych plików,
53/53 CTest, 51/51 Python, 76 kontroli EXE oraz osobny test DLL i renderowania.
W czystym klonie wykryto również brak inicjalizacji katalogu w teście menu;
poprawiono przygotowanie tego testu i ponownie sprawdzono całą grupę CTest.
