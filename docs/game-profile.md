# Profil gry — Steam build 26294447

Aktualny profil produkcyjny dotyczy wyłącznie SHA-256
`c38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d`.
Zachowano wymóg zgodnego pełnego SHA oraz dokładnych sygnatur instrukcji.
Nie dodano wyszukiwania przybliżonych adresów podczas uruchamiania moda.

Port z 2026-11-02 porównuje zachowany build 26275574
(`f7d605e3…e652968a0`) z zainstalowanym EXE. Stare raporty, zrzuty i analizy
w dokumentacji zachowują swoje historyczne adresy. Nie należy kopiować ich
bezpośrednio do nowej wersji. Bieżące adresy są w kodzie oraz
[raporcie migracji](../artifacts/profile-port-20261102.json).

## Najważniejsze różnice

Wszystkie adresy funkcji i vtable w tabeli to RVA; pola ownera to offsety obiektu.

| Element | Build 26275574 | Build 26294447 |
| --- | --- | --- |
| Renderer | `6D4DB0` | `6D4DE0` |
| Aktualizacja ownera kamery | `71A140` | `71A170` |
| Pętla input | `86A0D0` / `86CBB0` | `86A150` / `86CC30` |
| Dispatch input | `790820` | `790850` |
| Selekcja celu | `74F230` | `74F260` |
| Akcja kontrolera | `78C770` | `78C7A0` |
| Deaktywacja symulacji | `7ACA40` | `7ACA70` |
| Zmiana lokalnej postaci | `5F2920` | `5F2950` |
| Dispatch requestów | `855A30` | `855AB0` |
| Raw clear / typed clear | `8D3AF0` / `89CF70` | `8D3BB0` / `89D030` |
| Sized delete | `95C22C` | `95C2EC` |
| Globalny licznik renderera | `CFD3B4` | `CFD3C4` |
| Vtable przycisku | `B75678` | `B75658` |
| Vtable requestu type10 | `B0FFF0` | `B0FEC0` |
| Kolejka ownera | `CE6B0` | `CE6B8` |
| Konteksty ownera | `CE5C8` / `CE5F8` | `CE5D0` / `CE600` |

Przesunięcia funkcji są różne, vtable mają odrębne przemieszczenia, a pola
ownera zmieniły się o 8 bajtów. Port obejmuje też sygnaturę LEA w destruktorze
eventu, displacement skoku sized delete, sześć sygnatur CALL sondy wejścia,
filtry adresów powrotu i odpowiadające im atrapy testowe.

Próba menu ujawniła dodatkowo przesunięcie globalnego licznika renderera o 16 B.
Pierwsza próba zakończyła się timeoutem: odczyt starego zerowego miejsca BSS
stale wybierał pominięcie stereo dla klatki maintenance. Nowy adres potwierdzają
zarówno native load, jak i store w przeniesionym zakresie `6DDA76→6DDAA6`.
Audyt odrzuca teraz globalne dane bez referencji dowodzącej ich tożsamości;
same jednakowe zera nie wystarczają. Osobny test regresji sprawdza tę odmowę.

## Weryfikacja statyczna

```powershell
python tools/audit_profile_port.py
python tools/verify_game_profile.py --output artifacts/profile-guards-20261102.json
python -m unittest discover -s tests -p 'test_*.py'
```

Audyt wymaga obu dokładnych EXE i lokalnie zbudowanego
`build/capture/Release/swvr_profile_decoder.dll`. Używa dekodera HDE z vendored
MinHook; nie ładuje ani nie wykonuje gry. Specyfikacja wejściowa jest utrwalona
w `tools/profile_port_26275574_to_26294447.json`, niezależnie od aktualnych
adresów w kodzie moda.

Porównano 85 adresów profilu, 46 zakresów funkcji i 24710 operandów instrukcji
oraz wpisy tablic skoków. Relatywne adresy porównuje się przez ich cele,
offsety obiektów pozostają jawne, a vtable identyfikuje RTTI i sprawdza używane
sloty. To ograniczone porównanie kodu objętego profilem oraz jego bezpośrednich
celów, nie dowód równoważności całego grafu wywołań gry.

Helper `589390` rzeczywiście się zmienił: przed dopisaniem rekordu do bufora
odrzuca identyczną trójkę `(uint16, int, int)`. Zachowuje cztery argumenty,
układ bufora i operację append. Nie jest celem hooka moda. Wyjątek opisano
w specyfikacji audytu i przypięto do dokładnych hashy obu wersji funkcji;
[stary zrzut](../artifacts/profile-helper-old-20261102.json) i
[nowy zrzut](../artifacts/profile-helper-new-20261102.json) zawierają instrukcje
i dekompilację. Narzędzie nie oznacza go jako niezmienionego.

Pełny Release build, 23/23 CTest i 20/20 testów Pythona przeszły.
Wszystkie 46 kontroli sygnatur i referencji względem zainstalowanego EXE przeszło.
[CTest](../artifacts/profile-port-tests-20261102.xml),
[sygnatury](../artifacts/profile-guards-20261102.json).
Wyniki próby gry i gotowość gogli są w [statusie](status.md).

To nie potwierdza jeszcze poprawnego Gameplay 6DoF ani interakcji HandButtons
na nowym buildzie. Te wymagają rzeczywistego kokpitu i śledzenia dłoni.
