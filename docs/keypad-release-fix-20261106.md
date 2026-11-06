# Blokada przycisków po klawiaturze — 2026-11-06

Próba `headset-xr-20261106-182731-928` potwierdziła działanie skrętu kół
i puszczania chwytu. Użytkownik zgłosił, że przyciski pojazdu przestają
działać po **OK klawiatury numerycznej**, podczas gdy menu VR nadal działa.
Nieruchoma grafika kierownicy dotyczy standardowego **Driver seat**.
Grę zamknięto; poniższa poprawka nie została jeszcze sprawdzona w goglach.

## Przyczyna i odtworzenie

W obsługiwanym EXE (profil SHA-256 w `game-profile.md`) natywna akcja
`78C7A0` zapisuje cel w `controller+558` dla E albo `+560` dla Q oraz
ustawia bajt naciśnięcia `+4BF/+4C0`. Robi to **przed** rozgałęzieniem
na zwykły przycisk i klawiaturę typu 5/6. Klawiatura dodatkowo zapisuje
`controller+518` i emituje Show35; nie wysyła zwykłego press type10.

Dotychczasowy adapter rozpoznawał Show, ale nie zapamiętywał trzymania
klawiatury ani nie czyścił tego stanu. Odbiornik Clear24 pod RVA `79337E`
zeruje wyłącznie `controller+518`. Dlatego OK zamykało panel, pozostawiając
natywne Q/E wciśnięte. Bramka kolejnego naciśnięcia poprawnie odmawiała
przejęcia zajętej dłoni.

Wcześniejsza atrapa akcji klawiatury zapisywała tylko `+518`, co ukrywało
błąd. Po dopasowaniu jej do oryginału poniższy test, jeszcze przed poprawką,
zwrócił kod 1:

```powershell
build/capture/Release/swvr_native_hand_buttons_tests.exe keypad_then_button
# native hand buttons: vehicle button stayed blocked after keypad OK/Clear24
```

Test otwiera klawiaturę, wykonuje reset interakcji panelu, modeluje dokładny
efekt Clear24, a potem wykonuje nowy gest na zwykłym przycisku. Zamknięcie
sesji i ponowne uzbrojenie gestu nie usuwały błędu. Dopiero zwolnienie
przypisanego stanu dłoni przywróciło następne naciśnięcie.

Lokalne dowody: `tmp/dispatcher.c`, `tmp/keypad-close.asm`,
`tmp/keypad-clear.asm`. Ten ostatni odtwarza
`scripts/ghidra/ExportInstructions.java` dla zakresu VA
`14079337E..140793385`; nie dołącza się EXE ani pełnej dekompilacji do repo.

## Zmiana

Po potwierdzonym własnym otwarciu klawiatury adapter zwalnia lokalny
wskaźnik trzymania oraz bajt Q/E w tym samym zakresie dispatchu.
Pozostawia `+518`, aby istniejąca obsługa OK/ANULUJ nadal zakończyła sesję.
Nie wysyła type10 ani ogólnej fazy 0, która mogłaby uruchomić inne akcje.

Przed zapisem ponownie sprawdza postać, świat, właściciela, cel, stan
obu dłoni i brak obcego wejścia. Przy niejednoznacznej własności zachowuje
stan i raportuje `keypad_hand_ownership_changed`, bez ponawiania operacji.

Testy integracyjne przechodzą przez rzeczywisty most gestów, selekcję
promienia, hooki przycisków i rozliczanie natywnych requestów. Obejmują
Q/E, Small/Large Keypad oraz następny zwykły przycisk. Dodatkowe przypadki
sprawdzają zmianę postaci, konkurencyjne trzymanie i reset w trakcie akcji.

Diagnostyka zapisuje `selection_denied` z powodem pierwszej odmowy dla
danego gestu, bez powtarzania wpisu w każdej klatce. Podsumowanie zawiera
`selection_denials`, `last_selection_denial` i `keypad_hand_releases`.
Poprzednia sesja miała limit logu promienia osiągnięty przed otwarciem
klawiatury, dlatego nie zawierała natywnego powodu odmowy po OK.

Weryfikacja: Release, 53/53 CTest, 51/51 Python, 76 kontroli profilu EXE
i osobny test DLL/renderera `swvr_trace_test.exe`.
Odtworzenie na atrapach i kontraktach natywnych nie zastępuje próby
z rzeczywistą klawiaturą w pojeździe.

## Następna próba

1. Otworzyć klawiaturę prawą dłonią, wpisać wartość i wybrać OK.
2. Nacisnąć i puścić zwykły przycisk, przełącznik oraz dźwignię.
3. Powtórzyć lewą dłonią i po ANULUJ; sprawdzić małą i dużą klawiaturę.
4. Powtórzyć otwieranie i zamykanie kilka razy, bez ponownej kalibracji.

## Grafika kierownicy — pozostaje otwarta

Użytkownik potwierdził **Driver seat**, a nie Helm ani własną konstrukcję.
`rom/data/definitions/seat_racing.xml` wskazuje
`meshes/component_seat_racing.mesh`; pozostałe trzy meshe definicji to
nakrycia głowy. Obecny `HandSteering` oblicza kąt i oś, a
`native_locomotion` przekazuje ją do fotela. Nie ma połączenia tego kąta
z geometrią kierownicy w rendererze. Ta poprawka przycisków tego nie dodaje.

Dalsze wdrożenie wymaga zidentyfikowania geometrii koła i osi obrotu
wewnątrz modelu oraz powiązania jej z konkretnym fotelem zajętym przez
lokalnego gracza. Sam obrót całego mesha obróciłby też fotel; zmiana
wspólnego zasobu wpływałaby na inne pojazdy. Nie potwierdzono jeszcze
osobnego natywnego kanału animacji ani bezpiecznego przypisania takiego
wywołania rysowania do lokalnego fotela.
