# Stormworks VR / Quest 3 — rozpoznanie

Stan sprawdzenia: 2026-10-28. To analiza dokumentacji; nie potwierdza działania na lokalnym sprzęcie. Odczyt stron Geometa bywa niestabilny (404/pusta nawigacja), ale wyszukiwarka udostępniła treść oficjalnej dokumentacji komponentów.

## Najważniejsza decyzja

**Pierwszą ścieżką testowania dłoni powinien być Quest 3 → Steam Link → SteamVR jako runtime OpenXR.** Valve jawnie potwierdza przesyłanie szkieletu dłoni Quest przez `XR_EXT_hand_tracking`. Quest Link pozostaje kandydatem do testów obrazu i głowy; dostępność dłoni w zwykłej aplikacji C++ trzeba zmierzyć, zamiast ją obiecywać.

## Potwierdzone fakty i ich granice

| Obszar | Fakt ze źródła | Znaczenie dla projektu |
|---|---|---|
| Steam Link | Valve w SteamVR 2.8 (25.10.2024) wprowadziło przesyłanie dłoni z Meta Quest przez SteamVR skeletal input i `XR_EXT_hand_tracking`. | Jest udokumentowana ścieżka PCVR dla surowych danych dłoni. Nie dowodzi jeszcze działania konkretnej lokalnej konfiguracji. [Valve](https://store.steampowered.com/news/app/250820/view/4511010458676756485), [tekst komunikatu](https://store.steampowered.com/news/posts/?appids=250820&enddate=1732240796&feed=steam_community_announcements). |
| Ustawienia Steam Link | SteamVR 2.9 dodało przełącznik wyłączający przekazywanie dłoni oraz poprawki zerowania prędkości przy braku danych. | Diagnostyka musi odróżniać brak rozszerzenia, brak urządzenia i nieaktywne dłonie. [Valve 2.9](https://store.steampowered.com/news/posts/?appids=250820&enddate=1741813454&feed=steam_community_announcements). |
| Meta Link | Dokumentacja Meta z 14.09.2026 deklaruje wsparcie dłoni na Windows przez Link w Unity Editor, jako narzędzie iteracji aplikacji Quest. | Nie jest to gwarancja wsparcia wydanego moda C++ na PC. Nie twierdzimy też, że technicznie jest to zawsze niemożliwe. [Meta: Hand Tracking Overview](https://developers.meta.com/vr/documentation/unity/unity-handtracking-overview/). |
| Funkcje deweloperskie Link | Meta opisuje wybór runtime w Settings → General oraz Developer Runtime Features w Settings → Developer. | Mogą być potrzebne przy eksperymentach; nie zmieniamy automatycznie runtime systemowego. [Meta: Use Link for App Development](https://developers.meta.com/vr/documentation/unity/unity-link/). |
| Virtual Desktop | Autor VDXR opisuje przekazywanie danych dłoni przez własny runtime OpenXR oraz oddzielne kanały danych wprowadzone w 2024. | Możliwa druga ścieżka, jeśli użytkownik już ją posiada. Wymaga własnego sprawdzianu; nie zakładamy identycznego zachowania przy uruchomieniu przez inne runtime. [VDXR: Oculus Runtimes](https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki/Oculus-%22Runtimes%22). |
| Komponenty Stormworks | Oficjalne komponenty pakują XML, siatki, dźwięk i Lua; `onRender` pozwala renderować siatki komponentu, światło i laser. | Nie znaleziono tutaj API do ustawienia dwóch kamer gracza, powtórzenia renderowania świata ani podłączenia natywnego modułu OpenXR. To ograniczenie zbadanej dokumentacji, nie dowód nieistnienia innych mechanizmów. [Geometa: Component Modding](https://geometa.co.uk/wiki/stormworks/view/component_modding). |
| Shadery Stormworks | Patch notes wspominają naprawy nadpisywania shaderów i dopuszczenie plików `glslh` do moddingu. | Jest podstawa do inspekcji shaderów w instalacji. Samo nadpisanie shaderów nie zapewnia dwóch pełnych widoków ani sterowania kamerą. [Geometa: patch notes](https://store.steampowered.com/news/posts/?appids=573090&enddate=1739522413&feed=steam_community_announcements). |

## Kontrakt OpenXR dla implementacji dłoni

Zgodnie ze [specyfikacją Khronos `XR_EXT_hand_tracking`](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/ext/ext_hand_tracking.adoc):

1. Enumerować rozszerzenia i włączyć `XR_EXT_hand_tracking` tylko wtedy, gdy runtime je udostępnia.
2. Do `xrGetSystemProperties` dołączyć `XrSystemHandTrackingPropertiesEXT`; sprawdzić `supportsHandTracking`.
3. Po utworzeniu sesji utworzyć oddzielny tracker lewej i prawej dłoni przez `xrCreateHandTrackerEXT`.
4. Pobierać `xrLocateHandJointsEXT` w ustalonej przestrzeni odniesienia i dla czasu klatki. Domyślny szkielet zawiera 26 stawów.
5. Sprawdzać `isActive` i flagi ważności/śledzenia każdej używanej pozycji i orientacji. Nie traktować samego wyniku `XR_SUCCESS` jako dowodu widoczności dłoni.
6. Zwalniać trackery przed sesją. Obsługiwać przejścia stanu sesji oraz utratę danych.

Opcjonalny `XR_FB_hand_tracking_aim` może dostarczać dodatkowe dane gestów, ale nie jest zamiennikiem podstawowego rozszerzenia. Nie istnieje zarejestrowany zamiennik nazwany `XR_FB_hand_tracking`. Źródło zależności: [specyfikacja Khronos dla aim](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/fb/fb_hand_tracking_aim.adoc).

## Rekomendacje implementacyjne — wnioski projektu

- Najpierw zbudować niezależny program diagnostyczny C++: runtime/version, rozszerzenia, HMD/system, stereo view configuration, możliwości dłoni i obsługiwane API grafiki. Raportować osobno brak runtime oraz podłączonego HMD.
- Kolejny test ma uruchomić realną sesję, wyświetlać prostą scenę stereo i szkielety dłoni. Dopiero ten test potwierdzi przepływ danych; enumeracja rozszerzeń sama tego nie potwierdza.
- Gest szczypnięcia można prototypować na odległości końców kciuka i wskazującego, z histerezą i anulowaniem chwytu po utracie śledzenia. To nasza heurystyka, wymagająca kalibracji i testów, a nie gwarantowany gest runtime.
- Nie utożsamiać syntetycznych szkieletów kontrolerów z optycznym śledzeniem gołych dłoni. Test akceptacyjny wykonywać z odłożonymi kontrolerami, sprawdzając niezależne ruchy palców.
- Przed wyborem hooków gry ustalić lokalnie architekturę EXE, załadowane biblioteki, API grafiki, układ shaderów i przepływ kamery. Nie ustalać stałych adresów ani nie zakładać kompatybilności z przyszłymi aktualizacjami.
- Test stereoskopii musi objąć dwa widoki tej samej chwili symulacji, odsłanianie geometrii przy wychyleniu, wodę, przezroczystość, cienie i ekrany kokpitu. Samo przechwycenie końcowego obrazu to jeszcze nie VR 6DoF.

## Nierozstrzygnięte lokalnie

Model GPU/CPU, instalacja i wersja gry, runtime aktywny na komputerze, transport gogli, rzeczywista dostępność dłoni oraz punkty integracji renderera i interakcji w grze. Potrzebne są pomiary sprzętowe; dokumentacja nie pozwala ich rozstrzygnąć.
