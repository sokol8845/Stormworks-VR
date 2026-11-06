# Obrót postaci za goglami (G9) — projekt

Dokument historyczny. Implementacja istnieje w `src/body_yaw.cpp` i
`src/gl_trace.cpp`; od 2026-11-04 obejmuje także płynny obrót drugą dłonią
w trybie JOYSTICK. Aktualny stan i wyniki: [status](status.md),
[obsługa](hand-locomotion.md). Poniżej zachowano pierwotny projekt.

Stan: projekt i ustalenia statyczne. **Nie wdrożono**, bo wymaga zapisu stanu
kamery gry i iteracji w goglach.

## Problem

Poza pojazdem fizyczny obrót w pokoju obraca tylko kamerę XR. Postać, kierunek
chodzenia i natywne „look” zostają przy kierunku z myszy. Kamera VR to
`anchor × poza_gogli`, gdzie `make_head_anchor` usuwa rotację look `L` z `B × L`
(`docs/head-anchor-diagnosis.md`), więc yaw gogli nigdy nie trafia do gry.

## Ustalenia

- Yaw/pitch look to pola właściciela kamery `C+2A0`/`C+2A4`, zmieniane przez
  funkcję wejścia kamery (w buildzie 26275574: `0x71B600`). Yaw jest zawijany
  o stały okres (radiany), a w jednej gałęzi ograniczany do `[C+18C, C+190]`
  przy fladze `C+1A1` i zerowym `C+1A0`. To wygląda na limity rozglądania
  w fotelu, ale wymaga potwierdzenia.
- Adresy i stałe pochodzą ze starego builda; dla 26294447 trzeba przenieść
  funkcję wejścia kamery tak jak resztę profilu (`tools/audit_profile_port.py`).

## Proponowana implementacja

1. Tylko poza fotelem (wykrywanie trybu przez flagi kamery po porcie), za opcją
   `-BodyYaw`, domyślnie wyłączone.
2. W hooku `camera_owner_update` przed oryginałem: `Δ = yaw gogli względem
   originu XR` (po martwej strefie, np. 25°, albo płynnie ze stałą czasową);
   `C+2A0 += s·Δ` (znak `s` z pomiaru), a origin XR obracany o `Δ` wokół
   pionu przechodzącego przez punkt szyi. Obraz się nie zmienia, a postać,
   kierunek chodzenia i natywne look podążają za głową.
3. Wejście myszy dalej działa normalnie (dodaje się do `C+2A0`).
4. Test offline: matematyka transferu (suma yaw gry + yaw względny = yaw gogli,
   brak skoku obrazu, zawijanie kąta, martwa strefa). Test w grze: obrót o 90°
   w pokoju → postać i kierunek W obrócone o 90°, bez podwójnego obrotu.

## Ryzyka

- Jeśli ciało `B` nie podąża natychmiast za `L` (wygładzanie obrotu ciała),
  widok będzie się opóźniał. Wtedy przenosić yaw skokowo (snap) albo
  kompensować zmierzonym yaw bazy zamiast zadanym.
- Zapis `C+2A0` to pierwsza zmiana stanu gry poza requestami przycisków; musi
  być chroniony profilem SHA i wyłączany przy pauzie/UI, tak jak HandButtons.
