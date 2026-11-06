# Ostatni odbiorca własnego button requestu

Analiza statyczna, 2026-10-29, profil SHA-256
`f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`.
Nie uruchamiano gry i nie wdrożono freehooka ani emisji. Dokument rozdziela
dwie konkretne trasy tego samego obiektu request0x48, umieszczonego w natywnym
`c_network_message_client_to_server_request` (message0xD0, type3, vtableB89568).

**Trasy mają różne zasady zwalniania.** Normalny WRITE kończy się usunięciem
samego outermessage przez `8D3AF0`. Local-host przenosi message do `owner+CE308`,
wykonuje `855A30`, a potem używa **typed clear `89CF70`**, które zwalnia również
requesty w `message+18`. Tych dwóch zakończeń nie można obsłużyć identycznym
„free request po usunięciu rodzica”: w trasie local-host byłby to double-free.

## Warunek zakresu dowodu

Rozpatrywany request musi należeć do moda: znana własna alokacja, własny payload,
unikalny identyfikator alokacji/generacji oraz zaobserwowane przekazanie tego
samego pointera do message+18. Żaden poniższy wniosek nie uprawnia do sprzątania
cudzych requestów ani naprawiania hipotetycznego wycieku silnika.

Źródłowy `236ED0(context+340,request)` zapisuje dokładnie przekazany pointer.
`868390` wyjmuje go przy `868F63` i podaje niezmieniony do `236ED0(destination)`
przy `868FAD`. Następujący po `23D950` native swap przekazuje kontener do
message+18. Kopiowane są pola kontenera, nie request ani string. Rejestr moda
musi obserwować faktyczny transfer; sam adres parenta lub aktualna flaga hosta
nie jest wystarczającym tokenem własności.

## 1. Normalny WRITE: po skopiowaniu bajtów na transport

`240640(context)` w gałęzi bez local-host bierze outgoing message deque spod
`pointer_at(context+188)+D8`. Kolejność istotnych instrukcji:

| RVA | Operacja |
| --- | --- |
| `240800` | `23CC80(message_deque,write_buffer)` zapisuje komunikaty |
| `23CAC7` | message serializer `23CA90` robi tail-jump do `234DD0(message+18,buffer)` |
| `234DD0` WRITE | zapisuje request type, następnie woła jego serializer; dla type10 jest to `231BD0` |
| `231BD0` WRITE | kopiuje pola i bajty stringu do bufora, nie przechowuje aliasu requestu/stringu i niczego nie usuwa |
| `240805..240879` | powstaje niezależny bufor bajtów, kopiowany przez `9A0CF0`; `18D7C0` otrzymuje dane transportu, nie pointer requestu |
| `240893` | `8D3AF0(outgoing_message_deque)` usuwa outermessage |
| `240898` | powrót z tego clear; rodzic został rozliczony przez oryginał |

Dla osobnej kolejki `+F0` stosowany jest kontener tymczasowy: serializer przy
`24095B`, przekazanie transportowi przy `2409EA`, clear przy `240A1C`, powrót
`240A21`. To drugie zakończenie należy rozpoznawać osobno, a nie przez timer.

W WRITE `234DD0` po virtual serialize wykonuje tylko przejście do następnego
elementu/powrót. `231BD0` odczytuje string i kopiuje go do bufora; nie zapisuje
źródłowego pointera do żadnego innego obiektu. `23CC80` robi analogicznie dla
outermessage. Po skopiowaniu gotowych bajtów transport zachowuje bajty, a nie
alias do oryginalnego nativeString. W odczytanym ciągu `240640` pomiędzy
serializerem a `8D3AF0` nie ma innego typed cleanup requestu/stringu.

`8D3AF0` na tej liście wykonuje niewirtualny delete każdego *komunikatu*, a nie
destrukcję jego `+18`. Łańcuch `95C22C→15A780→9AA474→9C11D4` kończy się
`HeapFree` outerpointera i nie odczytuje zagnieżdżonych pól. Zatem w tej konkretnej
trasie oryginał nie zwalnia znanego własnego requestu ani jego stringu.

**Ograniczony kontrakt retirement WRITE:** przed wywołaniem tego dokładnie
rozpoznanego końcowego clear zebrać wartości własności własnych dzieci z żywego
parenta i rejestru. Po prawidłowym powrocie z clear można rozliczyć własny string
i własny request, korzystając wyłącznie z zapisanych wartości własności.
Nie odczytywać już zwolnionej message. Nie usuwać jej drugi raz. Reguła dotyczy
wyłącznie potwierdzonej trasy WRITE z zaobserwowanym transferem i zakończeniem;
nie może działać dla dowolnego `8D3AF0` lub pierwszego wywołania serializeru.

## 2. Local-host: ten sam pointer aż do dispatch i typed clear

`240640` w gałęzi `CEE65F!=0` znajduje lokalnego peera przez peer ID i przenosi
te same messagepointery z `local_connection+D8/F0` do `server_peer+C0`. Nie
serializuje ich w tym miejscu. Nie wolno wtedy uruchamiać kontraktu WRITE.

Dalszy, teraz zamknięty graf:

```text
message0xD0 w server_peer+C0
  -> 8519F0 case network-message type3
  -> 8BAE30(peer+C0, current_index) zwraca i usuwa TEN SAM pointer
  -> owner+CE308
  -> 86A0D0: 855A30(owner, ..., message)
  -> po obsłużeniu wszystkich message: 89CF70(owner+CE308)
  -> 8D3AF0(message+18): delete każdego requestu
  -> delete message0xD0
```

Instrukcje rozstrzygające:

- Jump table `852A84`, indeks `type-1`, wskazuje dla type3 blok `8525D2`.
- `8525D2`: RDX=current index, RCX=peer deque; `8525D7` woła `8BAE30`.
- `8525DC`: wynik RAX trafia do RDI; `8525DF` wybiera `owner+CE308`.
- `852624`: do docelowego pierścienia trafia dokładnie RDI, bez clone/allocation.
- `852628`: zmniejsza indeks, ponieważ wpis usunięto; dalsza pętla nie pomija
  kolejnego elementu. Końcowy clear `peer+C0` nie widzi już przeniesionego parenta.
- `86A77A`: oryginalna pętla wywołuje `855A30`; return `86A77F` oznacza powrót
  z obsługi konkretnego rodzica.
- `89CFE3`: typed clear woła `8D3AF0(message+18)`, co usuwa requesty.
- `89D002..89D00A`: `EDX=0xD0`, `RCX=message`, native delete rodzica.
- `86A792` / return `86A797`: caller wykonuje i kończy typed clear listy
  `owner+CE308` dopiero po pętli wywołań `855A30`.

`8BAE30` jest pełną funkcją `uint64 remove_return(deque*,uint32 index)`.
Zwraca wartość usuwanego elementu, przesuwa resztę wpisów, zmniejsza count,
ewentualnie zwalnia/zmienia tablicę pointerów. Nie usuwa zwracanego obiektu.
To przeniesienie własności, a nie dodatkowy alias pozostawiony w źródłowej liście.

**Ograniczony kontrakt retirement local-host:** po powrocie `89CF70` oryginał
zwolnił zarówno parentmessage, jak i own request. Nie wolno wykonywać drugiego
delete requestu. Można rozliczyć wyłącznie własny bufor stringu, korzystając z
wartości własności zebranej przed typed clear; clear requestów nie czyta ani nie
zwalnia nativeString. Nie dereferencjonować requestu po tym powrocie.

Obserwacja powrotu samego `855A30` wystarcza do oznaczenia „dispatch zakończony”,
ale nie do skasowania requestu: typed clear ma jeszcze odczytać jego pointer
i usunąć blok. Rejestr musi przetrwać do tego rzeczywistego końca.

## Alias payloadu w dispatcherze type10

Dla własnego release jest to ścieżka case10 w `855A30`:

1. Dla każdego peera `22C430` alokuje nowy broadcast0x40 i kopiuje peer ID,
   vehicle ID, section, int3 grid, float3 hit, pressed i drugi bool. Do broadcastu
   nie trafia pointer requestu, jego string ani adres wejściowych tablic.
2. Przy `request+34==1` callback addonów jest pomijany; string nie jest w tej
   gałęzi przekazywany dalej. To najwęższy przypadek własnego cancel-release.
3. Przy `request+34==0` `5D5A20` wywołuje dla addonów `5BC880`. Ta funkcja
   tworzy osobny argument typu string, rezerwuje jego osobny bufor przez
   `17CA10`, kopiuje `length+1` bajtów i długość, a dopiero potem wywołuje
   `onButtonPress` przez `1E6070`. Przekazuje kopię wartości stringu, nie oryginalny
   pointer payloadu requestu. Sprzątanie argumentów następuje przez `8D51C0`.

W tych konkretnych pomocnikach nie znaleziono store źródłowego requestpointera
lub jego stringpointera do trwałego stanu. Utrwalane broadcasty i argumenty Lua
zawierają kopie. Nie oznacza to audytu każdego typu requestu; kontrakt jest
ograniczony do własnego type10 i opisanej, rozpoznanej ścieżki.

## Wymagania dla własnego bookkeeping

Rejestr powinien odróżniać co najmniej:

| Zaobserwowany rzeczywisty koniec | Co oryginał usuwa | Co może rozliczyć mod po prawidłowym powrocie |
| --- | --- | --- |
| Normalny WRITE, clear outermessage w `240640` | parentmessage | własny request i własny string |
| Local-host, typed `89CF70` po dispatch | requesty oraz parentmessage | wyłącznie własny string |
| Reset pierwotnej Q przed transferem | requesty w Q | wymaga osobnej polityki; nie stosować automatycznie reguły WRITE |
| Nieustalony błąd enqueue albo niezaobserwowany parent | własność nieustalona | żadnego zgadywanego free/retry |

Trybu disposal nie wybierać z aktualnego `CEE65F`, tylko z faktycznie
zaobserwowanego native końca. Flaga może się zmienić między enqueue i cleanup.
Nie opierać go na stałym ID wątku. Zmiana XR focus/kalibracji unieważnia nowe
press, ale nie może usunąć zapisów o żywych własnych alokacjach.

Przed clear trzeba mieć snapshot wartości własności (allocation ID, parent ID,
request ID, pointer własnego bufora). Po clear nie odczytywać zwolnionych obiektów.
Po nieprawidłowym/niezaobserwowanym powrocie callbacku zachować stan nieustalonej
własności; nie kończyć go arbitralnie na podstawie czasu. Nie zwalniać natywnej
tablicy message+18 ani cudzych elementów jako „naprawy wycieku”.

To wystarcza do zbudowania ograniczonego offline ledgeru z dwiema politykami
retirement. Nie jest jeszcze dowodem działania hooków w grze: potrzebne pozostają
fixture rzeczywistego kolejnego transferu i clear, walidacja wszystkich aktywowanych
callerów oraz osobne pokrycie resetu/teardown z pending requestem. Trasy, których
nie da się dopasować do tego kontraktu, mają pozostać nieaktywne.

Dowody: `artifacts/button-request-last-reader-01.json`, `-02.json`, pliki
`button-last-reader-140*.c` i profil `button-request-last-reader-evidence.json`.
Eksport `8525D2..85262F` jest **basic blockiem wewnątrz `8519F0`**, nie funkcją
do wywoływania z moda. Pozostałe helpery mają rzeczywiste granice z PE unwind.
Wcześniejsze `button-request-lifecycle-02/03.json`, `button-release-request-01/03.json`
i `native-input-queue-01.json` zawierają porównywane WRITE/dispatch/serializery.
