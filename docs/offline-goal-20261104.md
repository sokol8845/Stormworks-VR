# Cel offline 2026-11-04

Pięć punktów zatwierdzonych przez użytkownika jako jeden goal. Gry nie uruchamiano.

- [x] Zwalnianie ruchu przed rozpoznaną dezaktywacją symulacji lub zmianą lokalnej postaci; wspólne hooki z przyciskami albo osobne przy samej lokomocji.
- [x] `config/gestures.ini`, walidator `swvr_config_check`, `-GestureConfig` i zapis parametrów w logu.
- [x] Lewy pinch wskazujący → Q (19), prawy → E (20), jeden aktywny cel; oddzielne pola przytrzymania i zapisanej pozycji. Gesty lokomocji rezerwują dłonie.
- [x] Tekst w obu oczach: śledzenie, otwarcie dłoni, pięści, pozycja rąk, rytm, bieg i blokada przez grę. Test pikseli i zachowania stanu OpenGL.
- [x] `tools/summarize_preview.py`: JSON i Markdown z logów; automatycznie po próbie, również po błędzie/timeout.

Weryfikacja: pełny Release bez ostrzeżeń, **38/38 CTest**, **31/31 Python**,
**66 guardów EXE, 0 błędów** oraz samodzielny test tracera GPU. Zamykanie
pięści może nastąpić w różnych klatkach. Pozostała widoczna pięść nadal
rezerwuje interakcje po utracie drugiej dłoni lub odrzuceniu ruchu.

- [JUnit CTest](../build/capture/artifacts/offline-goal-20261104.xml)
- [JUnit Python](../artifacts/offline-goal-python-20261104.xml)
- [Profil EXE](../artifacts/offline-goal-profile-20261104.json)
- [Opis Q lewej dłoni](left-hand-q.md)
- [Przykład raportu ze starszego logu](../artifacts/offline-goal-historical-summary-20261104.md) — analiza zapisanej próby, bez nowego uruchomienia.

Zakres potwierdzenia: automaty i integracja natywnych adapterów w fixture offline. Wysłane requesty nie potwierdzają ruchu postaci ani dostarczenia zwolnienia do serwera. Czytelność tekstu, śledzenie pięści, komfort i działanie dźwigni Q wymagają późniejszej próby w goglach za osobną zgodą.
