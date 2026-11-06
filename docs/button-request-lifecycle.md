# Cykl pamięci natywnego button requestu

Aktualizacja: `docs/button-request-last-reader.md` domyka dwie konkretne trasy
WRITE i local-host. Local-host ma dodatkowy typed clear `89CF70`, który usuwa
requesty przed rodzicem; nie stosować do niego reguły outer-only clear z WRITE.
Poniższy zapis zachowuje zakres wcześniejszego etapu analizy i jego ograniczenia.

Analiza tylko do odczytu, 2026-10-29. Profil EXE SHA-256:
`f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`.
Nie uruchamiano Stormworks, nie modyfikowano żywego procesu i nie aktywowano
emisji. Wszystkie adresy są RVA. Dokument dotyczy kolejki i pamięci z
`button-release-request.md`; odbiornik broadcastu `0x1A` jest rozpoznany osobno
w `button-release-receiver.md`, a dwie normalne drogi ostatniego clear opisuje
`button-request-last-reader.md`.

**Request nie ma 16-bajtowej otoczki.** Ten sam pointer do obiektu `0x48`
przechodzi z `context+340` do kontenera requestów w osobnym komunikacie sieciowym
`0xD0`. Argument `0x10` przy native delete nie ogranicza liczby zwalnianych
bajtów: prowadzi do `HeapFree` całego bloku. Jednak odczytane zwykłe clear
**nie niszczy zagnieżdżonego stringu ani requestów wewnątrz komunikatu**.
Własnego backendu nie można więc oprzeć na obietnicy automatycznej natywnej
destrukcji payloadu. **Nie jest to dowód wycieku całego silnika ani podstawa do
patchowania jego cleanup.** Ogólny punkt disposal po handoff pozostaje
nieustalony; builder po submit nadal nie powinien niczego automatycznie zwalniać.

## Transfer bez zmiany pointera

Niech `context` oznacza aktualny argument3 natywnej akcji `78C770`, a `Q` jego
zsynchronizowaną kolejkę pod `context+340`. Oryginalny producent alokuje przez
`95C638(0x48)`, zapisuje vtable `B0FFF0`, type10, pola i string, po czym wywołuje
`236ED0(Q,request)`. `236ED0` dopisuje dokładnie przekazany pointer, bez dodatkowej
alokacji otoczki dla elementu.

Normalny drain wykonuje pełna funkcja:

```cpp
void gather_requests(void* game_owner, void* live_context,
                     void* destination_synchronized_deque); // 868390..869131
```

Funkcja tworzy również inne requesty ze stanu logiki, więc **nie służy jako
helper do ponownego wywoływania przez mod**. Jej interesujący nas końcowy blok:

- `868F34`: sprawdza count spod `context+350`;
- `868F41`: ustawia źródłowy deque na `context+340`;
- `868F63`: ładuje pointer elementu do R14 z natywnej tablicy pointerów;
- `868F78..868FA2`: usuwa wpis z pierścienia i aktualizuje jego tablicę/liczniki;
- `868FA7`: `RDX=R14`, `868FAA`: `RCX=destination`;
- `868FAD`: wywołuje pełne `236ED0(destination,request)`.

Źródłowa kolejka zostaje opróżniona pod jej natywną synchronizacją. W tym
transferze request nie jest kopiowany, serializowany ani zwalniany. Samo
opróżnienie `Q` nie oznacza zakończenia żywotności payloadu.

## Rzeczywista otoczka: komunikat sieciowy 0xD0

W `86A0D0` oryginalna pętla tworzy tymczasowy zsynchronizowany kontener,
wywołuje `868390(owner,context,&temporary)` i — jeżeli count jest niezerowy —
tworzy komunikat przez:

```cpp
void* create_request_message(void* outbound_message_deque,
                             std::uint16_t local_peer_id); // 23D950..23DA22
```

To `c_network_message_client_to_server_request`, alokacja `0xD0`, type3,
vtable `B89568`; peer ID jest pod `+10`, kontener requestów zaczyna się pod `+18`.
Caller zamienia pierwsze pola pierścienia komunikatu (`+18..+2B`) z polami
tymczasowego kontenera. Pointery elementów pozostają te same. `23D950` sam nie
otrzymuje requestu jako argumentu i nie przejmuje go automatycznie; istotny jest
następujący po utworzeniu native swap w istniejącej pętli.

Dlatego rejestr moda musi potrafić opisać własność:

```text
own request0x48 + own string allocation
  -> Q(context+340)
  -> temporary deque
  -> request_message0xD0 + request_deque(+18)
  -> serialization or local host delivery
  -> final native clear
```

Samo śledzenie usunięcia z pierwotnej `Q` zgubiłoby własny request w tej zmianie
właściciela.

## Serializacja: pełne funkcje i ABI

W zwykłej gałęzi sieciowej `240640(context)` bierze outgoing deque spod
`pointer_at(context+188)+D8`, zapisuje go przez `23CC80`, kopiuje gotowe bajty do
bufora transportu i przekazuje bufor do `18D7C0`. Dopiero później wywołuje
`8D3AF0(outgoing_message_deque)`. Dla drugiej kolejki (`+F0`) używa tymczasowej
jednoelementowej listy i tego samego wzorca serialize/copy/clear.

```cpp
void serialize_network_messages(void* message_deque, void* native_buffer);
    // 23CC80..23D30C
void serialize_request_message(void* request_message, void* native_buffer);
    // 23CA90..23CACC, peer ID, potem tail-jump do 234DD0(message+18,buffer)
void serialize_requests(void* request_deque, void* native_buffer);
    // 234DD0..236EC8
void serialize_button_request(void* request, void* native_buffer);
    // 231BD0..231DF3, vtable B0FFF0 slot0
```

`native_buffer+08` to bieżący cursor, `+10` liczba pozostałych bajtów, `+14=1`
oznacza zapis. Serializatory zapisują count, typ i pola, przesuwają cursor;
nie niszczą requestu i nie zwalniają jego stringu. Końcowe kopiowanie na transport
tworzy niezależne bajty, ale sam powrót z dowolnego serializatora nie jest ogólną
granicą własności: komunikat może przejść gałąź lokalnego hosta zamiast zapisu.

Przy `CEE65F!=0` `240640` przenosi te same pointery komunikatów do `peer+C0`,
zamiast zapisywać je na tym etapie. Późniejsze consumer/clear musi nadal zachować
payload do zakończenia natywnego odbioru; nie zwalniać go wyłącznie na podstawie
flagi „pierwsza serializacja zakończona”.

## Co zwalnia zwykłe clear i teardown

Pełne funkcje:

```cpp
void clear_pointer_deque(void* deque); // 8D3AF0..8D3B70
void pop_delete_pointer(void* deque);  // 8D3B70..8D3BFA
```

`8D3AF0` iteruje po elementach, a przy `8D3B29` woła
`95C22C(pointer,0x10)`. Następnie zwalnia samą tablicę kontenera przez `9B6690`
i zeruje jego pola. `8D3B70` robi tę samą niewirtualną operację dla pojedynczego
pobieranego elementu.

Łańcuch delete w tej wersji EXE:

```text
95C22C -> JMP 15A780 -> JMP 9AA474 -> JMP 9C11D4
9C11D4: HeapFree(private_heap, 0, original_pointer)
```

Końcowa funkcja nie czyta request type, vtable, rozmiaru w RDX, stringu ani
zagnieżdżonego kontenera. Nie ma w tym łańcuchu dispatchu destruktora. To
rozstrzyga pozorny konflikt `delete(ptr,0x10)` vs alokacja `0x48`: jest to
niewirtualne usunięcie przez pointer bazowy, a rzeczywisty deallocator zwalnia
cały wskazany blok. **Nie dowodzi to wykonania destruktora klasy pochodnej.**

W odczytanym normalnym write branch clear widzi listę *komunikatów*; zwalnia
blok komunikatu `0xD0`, lecz nie przechodzi po `message+18`. W resetowaniu
nieprzesłanej `Q` clear widzi listę *requestów*; zwalnia blok requestu `0x48`, lecz
nie jego bufor stringu. Te konkretne ścieżki nie zapewniają automatycznego
disposal zagnieżdżonych alokacji. Jest to wniosek statyczny z odczytanego kodu,
nie pomiar zużycia pamięci całej gry ani twierdzenie o każdej innej ścieżce.

`23CAD0` jest pełnym helperem zniszczenia zsynchronizowanego kontenera: niszczy
condition/mutex i woła `8D3AF0(deque)`. Oczekuje pointera do *kontenera*, a nie do
początku `request_message0xD0`. Nie jest vtable-destruktorem komunikatu. Wyszukane
bezpośrednie odwołania do niego są tail-jumpami z funcletów; nie dodaje ukrytego
wywołania do normalnego clear.

Reset klienta `23FF80(context,...)` wykonuje `8D3AF0(context+340)`, potem niszczy
połączenie przez `8DEF20(context+188)`. To drugie czyści jego listy komunikatów
`+C0/+D8/+F0`. Podobne clear występują w `240640`, `240A70`, `85E530` i przy
destrukcji ownera `89AEA0`. Po teardown nie wolno zachować pointera do kolejki
do późniejszego użycia. Rejestr własnego requestu nie powinien wygasać przed
natychmiastowym disposal tylko dlatego, że zmieniła się kalibracja/generacja.

## NativeString: capacity i bezpieczny discard przed handoff

NativeString zajmuje 16 bajtów: pointer, int32 length, int32 capacity.
`17CA10(string*, requested_bytes)` wybiera potęgę2 nie mniejszą niż liczba
żądanych **bajtów**, minimum32, alokuje/reallokuje tyle i wpisuje tę liczbę pod
`string+0C`. To pojemność całego bufora, obejmująca miejsce na NUL.

Źródłowy copy w `78C770` kopiuje `length+1` bajtów. Deserializator stringu
`231BD0` rezerwuje `length+1` i zapisuje terminator pod `data[length]`. Dla
niepustego poprawnego payloadu wymagane są więc `data!=nullptr` i
**`capacity>length`**, a nie samo `capacity>=length`. Zerowy stan
`{nullptr,0,0}` trzeba rozpatrywać osobno; natywny builder może go zainicjalizować
przez `17B550`. Przy przyszłym kopiowaniu payloadu nie zakładać capacity==length
ani nie pomijać terminatora.

Przed próbą handoff własny builder nadal jest właścicielem obu alokacji:

```cpp
// Wyłącznie po znanym sukcesie budowy i przed rozpoczęciem enqueue.
if (own_request.native_string.data)
    native_aligned_free(own_request.native_string.data); // 9B6690
native_delete(own_request_pointer, 0x48);                // 95C22C
```

Bufor stringu z `17CA10` pochodzi z natywnej wyrównanej alokacji; nie zamieniać
`9B6690` na CRT `free` z DLL. Dla obiektu użyć odpowiedniego natywnego delete,
nie serializatora `231BD0`. Po rozpoczęciu `236ED0` własność może już być
przeniesiona; wyjątek daje stan nieustalonej własności, nie powód do automatycznego
discard/retry. Wrapper buildera nie powinien uruchamiać takich natywnych callbacków
w globalnym destruktorze lub w `DllMain`.

## Kontrakt dla dedykowanego release i dalszej implementacji

Można przygotować request0x48 z `pressed=0`, zapisanym lokalnym hitem poprzedniego
własnego press i danymi tego samego, nadal aktualnego `C+558`. Użycie pełnego
`236ED0(live_context+340,request)` omija cały branch short-click w `78C770`.
`skip_onButtonPress=1` jest zgodne z natywnym wzorcem zwolnienia po oddaleniu
i pomija udowodniony callback addonów; pełna semantyka odbiorcy broadcastu nadal
wymaga sprawdzenia. Nie zastępować go zwykłym phase0 akcji20.

Do podłączenia potrzebne jest najpierw niezależne śledzenie zmiany kontenera
**własnego** requestu i potwierdzenie jego ostatniego odbiorcy. Rejestr może
przechowywać wartości własności i identyfikatory generacji; sam fakt obecności
pointera w rejestrze nie uprawnia do późniejszego dereferencjonowania go.

Konkretny punkt zakończenia istnieje dla **resetu przed drain**: `23FF80` podaje
jeszcze aktualną `context+340` do `8D3AF0`, które bez dalszego odczytu payloadu
usuwa znajdujące się w niej obiekty. Wcześniejsze store/transfery trzeba powiązać
z tą samą własną alokacją; nie zakładać, że każdy element tej listy należy do moda.
To dobry kandydat do offline fixture, nie kontrakt wszystkich tras po submit.

Dla gałęzi normalnego WRITE udowodniono brak nested cleanup w odczytanym ciągu,
ale **nie wykazano jeszcze jednego poprawnego callbacku disposal obejmującego
zarówno WRITE, lokalnego hosta, jak i późniejszy teardown**. Nie instalować
freehooka na tej podstawie. Szczególnie nie usuwać własnego requestu drugi raz
po clear, które już uwolniło jego blok, ani nie zwalniać payloadu po pierwszej
serializacji, jeśli inna trasa nadal go odczytuje. Nie zmieniać cudzych requestów
i nie naprawiać hipotetycznego wycieku bazowego silnika.

Najwęższy następny krok: dla własnego requestu oznaczyć faktyczny transfer do
komunikatu oraz końcowy odbiór/clear, a potem wykazać, czy istnieje jakikolwiek
późniejszy reader lub osobne zwolnienie jego stringu. Offline fixture powinien
przejść przez: original queue → temp → message+18 →
write serialize / local delivery → final clear, a także reset przed drain,
zniszczenie ownera, nieustalony wynik enqueue i brak własnego requestu w danym
kontenerze. Wymagany sygnał to zero żywych własnych alokacji oraz brak
double-free/UAF po faktycznym końcu odbioru; liczba wyemitowanych release nie
jest testem cyklu pamięci.

Dowody: `artifacts/button-request-lifecycle-01.json` do `-03.json`, odpowiadające
`button-lifecycle-140*.c`, zwięzły `button-request-lifecycle-evidence.json` oraz
wcześniejsze `button-release-request-01.json` / `native-input-queue-14086a0d0.c`.
Eksport `-02` początkowo urwał granicę leaf23CA90 przed tail-jump; poprawny pełny
zakres `23CA90..23CACC` jest w `-03` i w profilu dowodowym. Nie używać pierwszej
niepełnej dekompilacji jako ABI innego helpera.
