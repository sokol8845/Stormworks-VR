# Wspólna praca nad Stormworks VR

Repozytorium: <https://github.com/sokol8845/Stormworks-VR>.
Aktualne ograniczenia i wyniki prób: [stan pracy](stan-pracy.md).
To eksperymentalny mod PCVR, wymagający lokalnej instalacji Stormworks.

## Pierwsze uruchomienie projektu

Wymagania: Windows x64, Visual Studio 2022 / Build Tools z C++ i Windows SDK,
CMake 3.24+, Python 3 z pytest. Node.js jest opcjonalny, potrzebny do testu
przeglądarkowego odtwarzacza nagrań. Nagłówki OpenXR i MinHook są w `vendor`
wraz z licencjami. Testy offline nie wymagają uruchamiania Steam ani gry.

```powershell
git clone https://github.com/sokol8845/Stormworks-VR.git
cd Stormworks-VR
cmake -S . -B build/capture -G "Visual Studio 17 2022" -A x64
cmake --build build/capture --config Release --parallel
ctest --test-dir build/capture -C Release --output-on-failure
python -m pytest tests -q
```

Uruchamiaj pytest dla `tests`, aby nie zbierać testów z lokalnie pobranych
narzędzi analitycznych. Wynik kompilacji to m.in.
`build/capture/Release/swvr_gl_trace.dll`.

## Próba z grą i goglami

Wymagany jest obsługiwany [profil EXE](game-profile.md). Połącz Quest 3 przez
Steam Link z uruchomionym SteamVR i włącz przekazywanie śledzenia dłoni.

```powershell
# Tylko sprawdzenie parametrów; nie uruchamia gry:
./scripts/start-vr-test.ps1 -DryRun
# Świadomie rozpoczęta próba, uruchamia autosave oraz VR:
./scripts/start-vr-test.ps1
```

Każda osoba wykonuje własną kalibrację. `config/avatar-calibration.ini`,
`artifacts`, `build`, `tmp` oraz lokalne instalacje narzędzi są pomijane przez
Git. Historyczne odsyłacze do `artifacts` wskazują lokalne dowody z prób;
nie są częścią publicznego repozytorium. Pliki EXE gry nie są dystrybuowane.

## Zmiany w dwie osoby

Pracujcie na osobnych gałęziach i łączcie zmiany przez pull requesty.
Przed zmianą adresów natywnych sprawdźcie pełny SHA-256 EXE i profil.
Testy muszą sprawdzać zachowanie oraz anulowanie wejścia po utracie fokusu,
śledzenia, zmianie postaci lub fotela. Wynik testu na atrapach nie potwierdza
reakcji prawdziwego pojazdu; zapiszcie osobno wynik próby w goglach.

```powershell
git switch main
git pull --ff-only
git switch -c fix/opis-zmiany
# Po zmianach i testach:
git add <zmienione-pliki>
git commit -m "Opis zmiany i przyczyny"
git push -u origin fix/opis-zmiany
```
