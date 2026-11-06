# Dedykowane żądanie zwolnienia przycisku

Analiza lokalnej kopii Stormworks, 2026-10-29, SHA-256
`f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`.
Nie uruchamiano gry. Poniższy kontrakt jest wynikiem instrukcji i dekompilacji,
nie testem nowego backendu. Adresy są RVA tej jednej wersji.

**Można zbudować własny wąski helper, który tworzy natywne request(pressed=0),
następnie wywołuje pełne `236ED0` z aktualną kolejką żądań. Nie znaleziono jednej
natywnej funkcji „release held-right”, którą wolno wywołać bez pozostałej logiki
`78C770`.** Istnieją pełne funkcje alokacji, serializacji i enqueue; bloków
`79013x..7902F2` i `74794A..747BE2` nie należy traktować jako funkcji z własnym ABI.

## Dokładny obiekt requestu

`c_client_to_server_request_use_vehicle_button` zajmuje `0x48` bajtów.
RTTI potwierdza klasę dla vtable `B0FFF0`. Obiekt jest tworzony inline przez
`78C770`, `742570`, `76E1E0` i fabrykę/deserializator kontenera `234DD0`.

| Offset | Typ / znaczenie | Źródło wartości dla wcześniej przyjętego prawego przycisku |
| --- | --- | --- |
| `00` | vtable pointer, `base+B0FFF0` | stała profilu |
| `08` | uint32 request type, `10` | stała |
| `0C` | uint32 metadata długości po deserializacji; lokalni producenci inicjalizują zerem | 0 |
| `10` | uint32 identyfikator pojazdu | uint32 pod `pointer_at(pointer_at(held+28)+250)+178` |
| `14` | uint32 indeks sekcji pojazdu w jego natywnym kontenerze | wyszukanie `pointer_at(held+28)` w pierścieniu pod `pointer_at(pointer_at(held+28)+250)`; oryginał używa 0, jeśli nie znalazł |
| `18` | int32 × 3 współrzędne komponentu w siatce | dokładne bajty `held+18..23` |
| `24` | bool pressed | **0** dla release |
| `25..27` | padding | 0 |
| `28` | float × 3 lokalny punkt trafienia, kopiowany bitowo | `C+568..573`, zapisany podczas natywnego press; nie nowe wskazanie `C+528` |
| `34` | bool pomijający co najmniej callback `onButtonPress` | 0 normalny release, **1 natywne oddalenie** |
| `35..37` | padding | 0 |
| `38` | pointer danych natywnego stringu | wynik native `held.vtable+2A8` |
| `40` | int32 length | wynik tego samego native stringu |
| `44` | int32 capacity | wynik tego samego native stringu |

Współrzędne `+28` są **floatami**, mimo że dekompilator kopiuje je jako inty;
podobnie część dekompilacji `742570` błędnie interpretuje inty jako floaty.
Nie konwertować numerycznie — zachować dokładnie bity zapisanej lokalnej pozycji.

`B0FFF0[0]` wskazuje `231BD0`, a ta funkcja ma ABI
`void serialize(request*, buffer*)`: odczytuje/zapisuje pola powyżej według flagi
`buffer+14`. **To nie wirtualny destruktor.** Nie wywoływać
`request.vtable[0](request,1)`. Typ eventu `key_action` ma zupełnie inny model
własności i jego destruktor `695E40` nie pasuje do tego obiektu.

Wire pomija vtable i padding: kontener `234DD0` zapisuje count, potem dla każdego
request type i payload: vehicle ID, section index, int3 grid, bool pressed,
float3 hit, drugi bool oraz długość u16 i bajty stringu. Deserializator potrafi
alokować request0x48 i dopisać go do zwykłego natywnego pierścienia, ale nie jest
zsynchronizowanym helperem do wrzucania własnego requestu do kolejki `+340`.
Nie używać go jako zamiennika `236ED0`.

## Payload stringu i jego własność

Dla dokładnej vtable przycisku `B75678` slot `+2A8` to pełna funkcja `32B470`.
Oryginalna ścieżka woła ją z `RCX=held`, `RDX=&native_string`,
`R8=&saved_local_hit`; zwraca ten sam output pointer w RAX. Wrapper ustawia piąty
argument dla wywołania metody `+2A0` na zero, a następnie woła `32B950` dla tej
klasy. `32B950` pobiera dane z definicji/danych komponentu i przydziela string.
Nie jest poprawne założenie, że zwykły pushbutton ma zawsze pusty string.

Natywny kod inicjalizuje string requestu przez `17B550`, rezerwuje przez
`17CA10` według capacity źródła, kopiuje `length+1` bajtów i długość, a następnie
zwalnia bufor tymczasowego stringu przez `9B6690`, jeżeli niezerowy. To daje wzór
kopii z niezależną własnością; nie wkładać do requestu wskaźnika do stosu lub do
tymczasowego bufora, który zniknie przed obsługą kolejki.

## Pełne funkcje i właściwa kolejka

Potwierdzone operacje mają własny początek i koniec:

```cpp
// Windows x64; odpowiednie typedefy wymagają zgodności pełnego SHA i prefiksów.
void* allocate_native(std::size_t bytes);             // 95C638, bytes=0x48
void enqueue_request(void* live_queue, void* request); // 236ED0, bez wyniku przyjęcia
void serialize_request(void* request, void* buffer);  // 231BD0, NIE destructor
```

Poprawnym adresem `live_queue` jest **argument 3 aktualnego wywołania
`78C770` + `0x340`**, ten sam kontekst klienta, którego używa natywny press.
Zwykle jest to `c_application_data+280+340`, ale nie uprawnia to do zachowania
wskaźnika na później. To odrębna kolejka od `owner+CE6B0` (`key_action`) i
`owner+CE5F8` (logic→game). Nie wkładać requestu do kolejki `key_action`.

`236ED0` bierze argumenty w RCX/RDX, synchronizuje natywną kolejkę i dopisuje
pointer na końcu. Oryginalni producenci nie zwalniają obiektu po powrocie.
Nie ma osobnego potwierdzenia przyjęcia: po nieokreślonym błędzie podczas push
nie wolno ani ponowić enqueue, ani zgadywać, czy można uwolnić obiekt.

### Co rzeczywiście wiadomo o zniszczeniu

Reset klientowego kontekstu `23FF80` wywołuje `8D3AF0(context+340)`. Ta kompletna
funkcja iteruje pointery, wywołuje `95C22C(pointer,0x10)` przy `8D3B29`, następnie
zwalnia samą tablicę pierścienia przez `9B6690`. `95C22C` jest skokiem do
`15A780`. Nie wywołuje w tej pętli serializatora ani wirtualnego destruktora.

**Nie prześledzono jeszcze do końca zwykłego transferu request queue → pakiet
sieciowy oraz zwalniania stringu requestu w tej ścieżce.** Odczytany reset nie
stanowi dowodu, że własny bufor stringu zostanie automatycznie zwolniony. Przed
implementacją trzeba rozstrzygnąć ten cykl i wskazać jeden dokładny moment
zniszczenia. Nie używać `free/delete` z DLL do obiektu, który został już oddany
grze, ani nie nazywać `231BD0` destruktorem w warstwie rejestru.

## Znaczenie drugiego boola: skip-onButtonPress

Serwerowy dispatcher `855A30`, case request type10, dla każdego odbiorcy woła
pełny builder `22C430`. Ten tworzy `c_server_to_client_event_use_vehicle_button`
vtable `B8AD00`, type `0x1A`, size `0x40`. Przekazuje pressed i drugi bool bez
zmiany, a ponadto dodaje peer ID nadawcy.

```cpp
void enqueue_button_broadcast(
    void* outbound_deque, std::uint16_t peer,
    std::uint32_t vehicle, std::uint32_t section,
    const std::int32_t* component_grid, const float* local_hit,
    std::uint8_t pressed, std::uint8_t skip_on_button_press); // 22C430
```

To **kolejka pakietów konkretnego połączenia**, używana przez serwerowy relay,
bez synchronizacji `236ED0`. Nie jest właściwym helperem do emisji requestu
klienta z hooka logiki. Nie wywoływać jej z `action_arg3+340`.

Gdy request+34==0, dispatcher dodatkowo woła `5D5A20` z vehicle ID, peer ID,
stringiem payloadu i pressed. `5D5A20 → 5BC880` buduje argumenty i uruchamia
callback o dosłownej nazwie **`onButtonPress`** (string RVA `B9ECC0`). Gdy +34==1,
tego wywołania nie ma. Drugi bool trafia również do broadcastu pod `+38`.

Zatem natywne oddalenie (`pressed=0, +34=1`) jest wzorcem zwolnienia, które
pomija callback addonów. Sama analiza relay nie dowodzi, że pominięcie callbacku
jest jedyną rolą boola po odbiorze broadcastu. Nie nadawać mu nazwy ogólnego
„cancel” przed prześledzeniem konsumenta packet0x1A. Normalny release ma
`pressed=0,+34=0` i może wywołać callback Lua, więc nie zastępuje semantycznie
anulowania bez zatwierdzenia działania skryptu.

## Żywotność celu i śmierć postaci

Store `C+558=component.vtable+38()` nie zwiększa żadnego rozpoznanego refcountu;
dla `B75678` getter `CB690` zwraca po prostu self. Held jest surowym pointerem
żyjącym w natywnym świecie. Nie znaleziono operacji retain/managed pointer,
która uprawniałaby adapter do późniejszego dereferencjonowania kopii tego celu.

Właściwy zakres przyszłego helpera to aktualne, rozpoznane wywołanie logiki,
w którym równocześnie są żywe: kontroler, request context i jego kolejka, świat
oraz aktualny `C+558`. Należy ponownie wymagać zgodności własnego naciśnięcia,
generacji świata/postaci i aktualnego held. Dopiero tam wolno odczytać klasę,
sekcję, siatkę, zapisany hit i payload. Nie używać nowego hover jako release
starego trzymania. Zniknięcie lub zmiana held nie uprawnia do odczytu starego celu.

Dedykowane utworzenie requestu nie potrzebowałoby dojścia przez health gate w
`78C770`. W odczytanym serwerowym case10 również nie ma bezpośredniej kontroli
zdrowia postaci. **To jeszcze nie dowód działania po śmierci**: zewnętrzne
bramki połączenia, konsument broadcastu, żywotność celu/kolejki i zmiana świata
pozostają do potwierdzenia. Po zmianie świata nie można ponowić starego release
na nowej postaci ani odtworzyć adresu starej kolejki z historycznego logu.

## Najwęższy następny krok

Prześledzić `context+340` od normalnego opróżnienia do końcowego usunięcia
requestu/stringu, oraz case packet0x1A do faktycznego przycisku. Następnie zbudować
offline fixture adaptera `native-owned-request → 236ED0 → serialize → receiver
→ disposal`, uwzględniający `pressed=0/skip=1`, śmierć przy nadal żywym świecie,
zniknięcie held i zniszczenie pending requestu. Dopiero kompletny cykl własności
i poprawny wynik przycisku będą podstawą do podłączenia cleanup w grze.

Materiały: `artifacts/button-release-request-01.json` do `-04.json` i pliki
`button-release-140*.c`; zwięzły profil `button-release-request-evidence.json`.
`held-button-cleanup-01.json` i `cockpit-input-functions-02.json` zawierają
porównywane wzorce normalnego i dystansowego release. Ustalenia o dwóch
kolejkach nie zastępują pomiaru ich emisji; produkcyjnej aktywacji nie dodano.
