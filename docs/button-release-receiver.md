# Odbiornik dedykowanego zwolnienia przycisku

Analiza statyczna lokalnego EXE, 2026-10-29, SHA-256
`f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`.
Wszystkie adresy poniżej są RVA tego profilu. Nie uruchamiano Stormworks,
nie odczytywano procesu gry i nie zmieniano `src`, testów ani CMake.

**Packet0x1A z `pressed=0, skip=1` wybiera osobną metodę resetu przycisku. Dla
dokładnej klasy Button zeruje tylko przejściowe bajty `278/279`; nie wykonuje
natywnej fazy0 action20 ani jej ścieżek short-click. Nie cofa wcześniej
zatrzaśniętego Toggle Button. Sam odbiór nie czyści lokalnego held-right.** To
konkretny wzorzec do dedykowanego cleanup, a nie ogólne „cofnij całą interakcję”.

## Dispatcher i rzeczywiste argumenty

Klientowy dispatcher pakietów zaczyna się w `85F2B0`. Instrukcje `85F3F7..85F41D`
czytają uint32 typu z `packet+8`, odejmują 1 i wybierają RVA z tablicy `867ECC`.
Wpis `(0x1A-1)*4` pod `867F30` zawiera `86054B`. Dokładna gałąź kończy się
skokiem `8605CE → 85F588` do następnego pakietu. Pełna dekompilacja dużego
dispatchera przekroczyła limit; poniższy przepływ jest potwierdzony jego
instrukcjami, tablicą skoków i małym, celowanym eksportem gałęzi.
[Dowód](../artifacts/button-release-receiver-evidence.json),
[wejście dispatchera](../artifacts/button-release-receiver-01.json),
[gałąź](../artifacts/button-release-receiver-02.json).

`860586..8605C9` przekazuje do **pełnej** funkcji `77AE30`:

```cpp
void receive_button(
    void* controller, uint64_t opaque_context, uint16_t peer,
    uint32_t vehicle, uint32_t section, const int32_t* grid3,
    const float* local_hit3, uint8_t pressed, uint8_t skip);
```

| Źródło w pakiecie | Argument odbiornika |
| --- | --- |
| `10` uint16 | peer, arg3 |
| `14` uint32 | vehicle ID, arg4 |
| `18` uint32 | section index, arg5 |
| `1C..27` int3 | kopia grid, arg6 |
| `28` byte | pressed, arg8 |
| `2C..37` float3 | kopia lokalnego hit, arg7 |
| `38` byte | skip, arg9 |

Arg1 to `pointer_at(owner+CE190)+38`. Arg2 pochodzi z trzeciego argumentu
bieżącego dispatchera, trzymanego w R15; sam `77AE30` go nie używa. Peer w R8
również jest nadpisywany przed użyciem. Nie opierać typedefu na automatycznej
deklaracji fragmentu `86054B`, która pomija część stack arguments.
[Instrukcje gałęzi](../artifacts/button-release-receiver-02.json).

`77AE30..77AF99` jest jedną logiczną funkcją z kilkoma wpisami chained-unwind:
pierwszy wpis PE kończy się w `77AEE7`, lecz dalsze fragmenty należą do tego
samego stosu. Eksport tylko pierwszego wpisu nie wystarcza do dowodu końcowej
gałęzi skip. [Pełny zakres](../artifacts/button-release-receiver-04.json).

## Wyszukanie celu nie wymaga zdrowia postaci

Odbiornik `77AE34..77AEDE` zaczyna od `world=*controller`, szuka w pierścieniu
`world+4CB0` deskryptora z ID pod `node+4`, wymaga `node+10==5`, niezerowego
kontenera sekcji pod `node+8`, dodatniego count, bajtu `node+0==0` oraz
`section<count`. Wybiera sekcję i wywołuje `4ACA40(section, grid3)`, następnie
odczytuje komponent przez **wskaźnik slotu zwrócony przez lookup**. Wymaga
niezerowego komponentu i wyniku jego metody identity `vtable+38`.
[Odbiornik](../artifacts/button-release-receiver-04.json),
[lookup grid](../artifacts/button-release-receiver-05.json).

W tej ścieżce nie ma wyszukiwania aktywnej postaci typu4, odczytu health,
kontroli dystansu od głowy ani wywołania `78C770`. Zatem odbiorca istniejącego
pakietu potrafi zaktualizować nadal istniejący przycisk niezależnie od health
lokalnej postaci. To nie dowód dostarczenia pakietu po zniszczeniu świata,
zerwania połączenia ani przejścia sceny. Brak ID/sekcji/grid powoduje wyjście bez
zmiany; funkcja zwraca `void`, bez potwierdzenia skutecznego release.
[Pełne instrukcje odbiornika](../artifacts/button-release-receiver-04.json).

## Skip wybiera inny wirtualny handler

Po wyszukaniu identity instrukcja `77AF0A` sprawdza arg9, czyli packet+38:

| Wartość | Dalsza ścieżka |
| --- | --- |
| `skip==0` | `identity.vtable+440(identity, hit)`; gdy true, `+448(identity, pressed, hit_copy)` przy `77AF83` |
| `skip!=0` | bez `+440` i bez `+448`; od razu `+450(identity)` przy `77AF26` |

To dodatkowa rola boola poza pominięciem serwerowego Lua `onButtonPress`
opisanym w [button-release-request.md](button-release-request.md).
[Gałąź odbiornika](../artifacts/button-release-receiver-04.json).

Dla dokładnej vtable Button `B75678` odczytane sloty to:

| Slot | Funkcja | Potwierdzony efekt |
| --- | --- | --- |
| `38` | `CB690` | return self |
| `440` | `1413B0` | return true |
| `448` | `35D4F0..35D571` | normalne press/release według `definition+DA4` |
| `450` | `35D4E0..35D4EA` | `mov word ptr [RCX+278],0; ret` |
| `458` | `35D4D0..35D4D8` | odczyt logicznego bajtu `27D` |
| `358` | `35D930..35E254` | aktualizacja stanu według typu |

Reset `+450` nie sprawdza pressed, nie czyta hit, nie wywołuje żadnej funkcji
i nie zapisuje `27D`. Ustawienie `pressed=0` pozostaje właściwym kontraktem
requestu, choć w gałęzi skip jego wartość nie jest używana.
[Vtable i bajty](../artifacts/button-release-receiver-evidence.json),
[metody](../artifacts/button-release-receiver-04.json),
[getter](../artifacts/button-release-receiver-05.json).

## Push i Toggle: stan wejścia a wynik logiczny

Normalna metoda `35D4F0` dla typu0 (Push Button) kopiuje pressed do `278`.
Dla typu1 (Toggle Button) ustawia `278=1` tylko przy pressed!=0; **normalny
release pressed=0 nic w niej nie zapisuje**. Typy2/3/4 mają kolejne odmienne
gałęzie; nie rozszerzać wstępnej whitelisty na podstawie wspólnej vtable.
[Instrukcje `35D4F0`](../artifacts/button-release-receiver-04.json).

Aktualizacja `35D930` konsumuje te flagi później:

- Typ0: `35DA4A..35DA66` porównuje `278` z poprzednim wejściem `27A`; zmiana
  aktualizuje `27A` i kandydat nowego stanu jest odwrotnością `27D`.
- Typ1: `35DA93..35DAA3` przy `278!=0` zeruje ten impuls i przechodzi do tej
  samej inwersji `27D`. To press impulsowy, nie przełączenie przy release.
- `35E06A..35E077` zapisuje zmieniony wynik do `27D`. Istnieje też osobny wpływ
  wejścia obwodu (`198/27B`), więc „usunąć nasze wciśnięcie” nie oznacza
  bezwarunkowo wymusić całe wyjście logiczne na zero.

[Aktualizacja](../artifacts/button-release-receiver-06.json),
[typ0](../artifacts/button-release-receiver-07.json),
[typ1](../artifacts/button-release-receiver-08.json).

W konsekwencji skip1 usuwa niezużyty impuls Toggle, jeśli dotrze przed jego
aktualizacją; po zatrzaśnięciu `27D` pozostawia ten stan. Dla Push usuwa flagę
naszego wejścia, a zwykła aktualizacja realizuje zbocze zwolnienia. Może to
wywołać normalną zmianę wyjścia/animację/dźwięk przycisku; nie jest to
short-click z dużej funkcji obsługi action20 phase0. Nie należy deklarować
rollbacku ani braku wszystkich skutków ubocznych samego zbocza release.
[Metody resetu i aktualizacji](../artifacts/button-release-receiver-04.json),
[aktualizacja logiczna](../artifacts/button-release-receiver-06.json).

## Lokalny held-slot pozostaje oddzielny

`77AE30` nie zapisuje `C+558` ani `C+4BF`. Także `35D4E0` otrzymuje już Button,
a nie C, i czyści wyłącznie jego dwa bajty. Natywna ścieżka oddalenia `742570`
tworzy request pressed0/skip1, oddaje go kolejce i dopiero w swoim kodzie
`747BE2` zeruje `C+558`. Normalna faza0 action20 osobno zeruje `C+4BF` przy
`78FB14`, a held-right przy `7902F2`; po drodze ma health gate i inne działania.
[Wzorzec dystansowy](../artifacts/held-button-cleanup-01.json),
[zwykła akcja](../artifacts/cockpit-input-functions-02.json).

Przyszły dedykowany cancel musi więc rozdzielić:

1. własny request pressed0/skip1, z identyfikatorami **aktualnego własnego held**
   oraz zapisanym hit, przez żywą kolejkę klienta;
2. lokalne zakończenie własnego trzymania i uzgodnienie współdzielonego action
   state w tym samym żywym zakresie, bez kasowania fizycznej interakcji;
3. późniejsze potwierdzenie stanu/odbioru, zamiast uznawać samo enqueue za efekt.

Nie trzeba przechodzić przez `78C770(...,20,0)`, aby odbiornik wykonał reset
Button. Nie uprawnia to do zachowania surowego pointera held na później,
odczytu starego świata ani ręcznego poprawiania `27D`. Odrębny cykl własności
requestu i stringu pozostaje wymaganiem przed wdrożeniem. Backend cleanup nie
został włączony przez tę analizę.
