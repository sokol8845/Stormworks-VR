# Binding helperów natywnego requestu przycisku

Aktualizacja 2026-11-04: dodano wybór lewej/prawej ręki oraz natywne Q/E.
Certyfikat zawiera akcję; pola zapisanego hitu, trzymania i zwolnienia są
wybierane dla danej ręki. [Aktualny opis i dowody](left-hand-q.md).
Poniższy opis zachowuje również historyczny etap prawej dłoni.


Aktualizacja 2026-11-02: adresy uzywane przez implementacje przeniesiono na
Steam build 26294447. [Zakres portu i dowody](game-profile.md). Powiazane
starsze zrzuty analizy zachowuja adresy i hash swojego oryginalnego EXE.

`NativeButtonRequestBindings` wiąże rzeczywiste ABI Windows x64 z
`NativeButtonRequestApi`. Wymaga wyniku porównania **pełnego** SHA-256 bieżącego
EXE z profilem `c38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d`.
Nie instaluje hooków, nie uruchamia gry i nie aktywuje produkcyjnego wejścia.
Konstruktor oraz destruktor nie wywołują żadnego helpera Stormworks.

## Powiązane funkcje i walidacja

| RVA | ABI używane przez binding |
| --- | --- |
| `95C6F8` | `void* allocate(size_t bytes)`, RCX=72 |
| `17CA10` | `void reserve(NativeButtonString*, uint32_t capacity_bytes)`, RCX/EDX |
| `9B6750` | `void aligned_free(void* buffer)`, RCX=payload.data |
| `95C2EC` | `void sized_delete(void*, size_t)`, RCX=request, RDX=72 |
| `236ED0` | `void push(void* live_queue, void* request)`, RCX/RDX |
| `231BD0` | serializer wskazywany przez vtable `B0FEC0[0]`; tylko weryfikowany |
| `32B470` | `NativeButtonString* getter(button*, NativeButtonString* out, const float* saved_hit)` |

Przed zwróceniem API konstruktor sprawdza czy helpery są w czytelnych,
wykonywalnych regionach pamięci, porównuje ich prefiksy oraz wymaga
`pointer_at(base+B0FEC0)==base+231BD0` oraz
`pointer_at(base+B75658+2A8)==base+32B470`. Kontrola przepełnienia bazowego adresu
obejmuje także wyższy slot vtable Button. `95C2EC` jest 5-bajtowym jump thunk;
pozostałe wpisy mają kontrolę 24 bajtów. False SHA jest odrzucane przed
jakimkolwiek sprawdzeniem pamięci pod przekazaną bazą. Niepoprawna baza,
prefiks lub vtable powodują wyjątek bez wywoływania gry.

Dowód ABI reserve: `17CA10..17CA77` bierze string w RCX i liczbę bajtów w EDX,
zaokrągla do potęgi dwójki co najmniej 32. Gdy nie potrzebuje realokacji, RAX
nie jest definiowany; binding ma więc zwrot **void**. Capacity obejmuje NUL.
`9B6750..9B676B` przyjmuje wyrównany pointer w RCX, odczytuje oryginalny adres
alokacji sprzed wyrównanego bufora i wywołuje free. Nie zastępować go `free`
z modułu. Potwierdzenia są również w `button-request-lifecycle-evidence.json`
oraz `button-release-request-evidence.json`.

## Payload i własność przed handoff

API dopuszcza alokację dokładnie 72 B i zapisuje jej adres w lokalnym rejestrze
pre-handoff. `assign_payload` wymaga własnego requestu i całkowicie zerowego
stringu. Pusty payload pozostawia zero bez native reserve. Dla niepustego
rezerwuje `length+1`, sprawdza uzyskany pointer/capacity, kopiuje dokładne bajty
źródła (także osadzone NUL), dopisuje terminator i ustawia długość. Maksymalna
długość 65535 odpowiada długości wire u16; rezerwacja graniczna ma 65536 B.
Payload należy niezależnie do requestu.

`destroy_unsubmitted` usuwa własny adres z rejestru **przed** cleanup, następnie
wywołuje aligned-free dla niezerowego payloadu oraz sized-delete(request,72).
Nie wywołuje slotu zero vtable: jest to serializer, a nie destruktor. Częściowo
przygotowany string może zostać jawnie sprzątnięty. Jeżeli helper rzuci podczas
częściowego cleanup, API nie pozostawia adresu w rejestrze jako własności
pozwalającej ponowić cleanup; builder zapisuje `discard_uncertain`. Brak ponowienia może pozostawić
niezwolnioną resztę po nieokreślonym błędzie, ale nie zgaduje czy wolno zrobić
drugie free.

`enqueue` wymaga niezerowej żywej kolejki i własnego requestu. Usuwa lokalny
adres przed native push, nie odczytuje requestu po wywołaniu i nie zwalnia go.
Push może synchronicznie usunąć alokację lub rzucić po nieokreślonym handoff.
Retry/discard po próbie enqueue jest zabronione przez builder; binding nie
przejmuje zarządzania kolejką gry.

Jeden właściciel serializuje wszystkie operacje konkretnego requestu, w tym
assign, discard i submit. Różne builders mogą współdzielić binding; mutex chroni
tylko rejestr metadanych, a żaden helper gry nie jest pod nim wywoływany.
`owns()` nie jest blokadą żywotności przeciw równoczesnemu discard tego samego
obiektu. Nie wolno zachowywać surowego adresu do odłożonego callbacku po handoff,
zwłaszcza po ponownym użyciu adresu przez natywny allocator.

Binding musi przeżyć wszystkie korzystające z niego builders. Destruktor usuwa
wyłącznie metadane C++; nie sprząta automatycznie pozostawionych requestów i nie
wywołuje gry spod Windows loader lock. Caller ma jawnie discardować własny
pre-handoff obiekt albo przekazać go grze przed zniszczeniem bindingu.

## Jawne capture aktualnego własnego held-right

`capture_release(current_scope,accepted_press)` zbiera realne dane do buildera,
ale nie jest zezwoleniem na release. Certyfikat musi pochodzić z press faktycznie
przyjętego przez grę. Scope zawiera bieżącą generację sceny, owner-token,
aktualny żywy C oraz flagę rozpoznanego native caller scope. Flaga ma być ustawiana
w rozpoznanym hooku logiki; API nie tworzy żywotności przez samo porównanie
adresów ani nie uznaje dowolnych pointerów za żywy kontekst.

Przed pierwszym odczytem C metoda wymaga rozpoznanego scope i zgodności
scene/owner/controller z certyfikatem. Dopiero potem odczytuje **aktualne** C+558
i wymaga równości z target-token przyjętego press. Nigdy nie dereferencjonuje
zachowanej historycznej kopii targetu. Weryfikuje dokładną Button-vtable B75658,
niezerową definicję +58 oraz button_type pod definicja+DA4 należący do {0,1}.

Dalej od aktualnego held pobiera section+28, vehicle ze section+250 i vehicle-id
pod vehicle+178. Natywny ring sekcji ma array+0, capacity-u32+8, begin-u32+C,
count-u32+10. Szukanie używa dokładnie `uint32(begin+i)%capacity`; określone
zawijanie dodawania u32 jest zachowane, natomiast przepełnienie adresu/bajtowego
zakresu tablicy jest odrzucane. Null, capacity=0, count>capacity, niedostępna
tablica i brak sekcji są błędem. **Brak sekcji nie daje domyślnego index=0.**
Section-index wyniku to logiczne i, a nie fizyczny indeks slotu.

Grid-int3 jest kopiowany bitowo z held+18. Lokalny hit-float3 jest kopiowany z
C+568, zapisany podczas press; C+528 opisuje nowe wskazanie i nie jest źródłem
release. Zachowano m.in. bity -0. Niepoprawne liczby są odrzucane przed getterem.

Payload-getter jest wywoływany z RCX=aktualny held, RDX=&zerowy temporary,
R8=&skopiowany saved-hit. Wymagany jest return==&temporary. Metoda sprawdza
length<=65535, capacity>length i NUL pod data[length], a potem kopiuje dokładne
bajty do własnego `std::string`. Całkowicie zerowy pusty string również jest
poprawny. Po getterze ponownie sprawdza aktualne C+558, aby nie oddać danych
po zmianie held podczas wywołania.

Każdy niezerowy temporary.data przechodzi dokładnie jedną próbę native
aligned-free, również po wyjątku gettera, błędnym return pointerze lub wadliwym
stringu. Pointer temporary jest usuwany z lokalnego stanu przed wywołaniem free;
wyjątek z free nie powoduje ponowienia. Wynik błędny nie udostępnia częściowo
skopiowanych danych.

Wynik zawiera wyłącznie status, skopiowane scalars i `std::string`. Nie zachowuje
natywnego section/vehicle/held ani kontekstu do przyszłego odczytu. Metoda nie
zapisuje C+558/C+4BF, nie woła request-allocate/push i nie wykonuje zmian świata.
`result.data.release_data(skip)` daje jedynie borrowed view do natychmiastowego
przekazania builderowi; sam wynik musi wtedy nadal żyć.

## Wykonane testy bez gry i pozostałe granice

`native_button_bindings_tests.cpp` buduje własny obraz VirtualAlloc z dokładnymi
prefiksami. Po przejściu walidacji podmienia wpisy na tail-jump do własnych
funkcji C++, zachowując rejestry i argumenty stosowe. Test wykonuje przez API
rzeczywiste wywołania po przypisanych adresach, potwierdzając RCX/EDX/RDX i
kolejność reserve/free/delete/push; serializer ma pułapkę i nigdy nie jest wołany.

Sprawdzono fałszywy SHA z nieprawidłową bazą, każdy prefiks, vtable i ochronę
strony, null/obce/occupied argumenty, niezależny payload z NUL, pusty oraz
graniczny string, częściowe wyjątki reserve i cleanup, enqueue o niepewnym
wyniku oraz synchroniczne zwolnienie całej mapy requestu wewnątrz fake push.
Konstruktor/destruktor bindingu i destruktor buildera nie wykonują callbacków.
Samodzielny MSVC `/std:c++20 /W4 /WX /permissive-` i test fixture przeszły.

Próby capture obejmują odmowę niezgodnych kontekstów/held bez dereferencji
historycznych adresów, ring wrap (także u32), brak sekcji, błędny count, null,
przepełnienie tablicy, klasę i typ przycisku. Getter dostaje bitowo dokładny
saved-hit zamiast odmiennego hover; próby sprawdzają niezależny payload z NUL,
pusty i 65535-bajtowy, zły return pointer, częściowy wyjątek, jednokrotny free,
zmianę held podczas getter oraz niezmienność całego bufora C.

Łączona próba wykonuje `capture_release -> release_data(skip=true) ->
NativeButtonReleaseRequest.prepare -> submit` przez ten sam binding. Fake push
sprawdza surowe 72 B: header, vehicle, logiczny section-index, grid, saved-hit,
pressed=0, skip=1, padding i własną kopię payloadu po zmianie obu źródeł stringu.
Nie wykonuje zewnętrznego network retirement. Dwie normalne drogi jego końca
rozpoznano osobno w `button-request-last-reader.md`; nie są jeszcze podłączone.

Nie ładowano Stormworks. Test nie dowodzi zewnętrznej destrukcji requestu/stringu
po rzeczywistym enqueue, znaczenia wszystkich pól broadcastu ani kompletności
cleanup przy śmierci i zmianie świata. Produkcyjny backend nie jest instalowany;
zachowuje się wymagania z `button-release-request.md`.
