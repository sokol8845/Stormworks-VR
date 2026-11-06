# Pending button request: reset i teardown źródłowej kolejki

Analiza statyczna z 2026-10-29, SHA-256
`f7d605e34fe3b8ebde3adbc8e2e90fe28821c6cb3ef67be79abe58ce652968a0`.
Pierwotna analiza nie uruchamiała gry ani nie zmieniała kodu produkcyjnego. Wszystkie adresy są
RVA tej wersji. Ten dokument dotyczy **wyłącznie własnego, zarejestrowanego
requestu, który nadal jest w pierwotnej kolejce i nie przeszedł do parentmessage**.

Aktualizacja 2026-11-02: opisany scope jest już wdrożony w
`src/native_button_retirement.cpp`. Wrapper zapisuje Q przy handoff, a raw-clear
i delete rozliczają własny payload dopiero po dopasowanym usunięciu requestu
przez oryginał. Zła Q, parented child, wykluczone callery, nieznany delete,
niepewny powrót i zagnieżdżony obcy clear pozostawiają quarantine. Pełny Release
build i 23/23 CTest przeszły bez gry:
[raport](../artifacts/offline-transitions-tests-20261102.xml).
Szczegóły bieżącego kodu: [native-button-retirement.md](native-button-retirement.md).

`context` oznacza aktualny argument3 natywnej akcji `78C770`.
`Q=context+340` jest zsynchronizowaną kolejką client→server requestów. Nie jest
to `owner+CE6B0` dla `key_action`, ani kontroler `C` z polami `C+558/C+560`.

**Dwa konkretne native scope kończą żywotność pending requestu:** reset
`23FF80` z raw-clear return `24005B` oraz destruktor kontekstu `23EA20` z
raw-clear return `23EB68`. W obu oryginał usuwa blok requestu przez `95C22C`,
ale nie odczytuje i nie zwalnia jego stringu. Po potwierdzonym native delete
mod może rozliczyć **tylko swój payload**, bez drugiego delete requestu.

## Reset `23FF80`

Pełna funkcja `23FF80..2401BB` ma ABI:

```cpp
void reset_context(void* context, std::uint32_t flags); // RCX, EDX
```

Funkcja zachowuje RCX w RDI, EDX w EBX. Może wcześniej uruchomić network flush
(`240A70`, `240640`) i aktualizację flag (`23F040`). Dlatego **wejście do resetu
nie jest momentem zwalniania wszystkich własnych rekordów**. Dopiero rzeczywisty
clear pokazuje, które requesty pozostały pending w źródle:

```text
24004F  LEA RCX,[RDI+340h]        ; rzeczywista Q
240056  CALL 8D3AF0
24005B  ...                      ; powrót z clear
```

Później reset niszczy listę peerów i połączenie pod `context+188`; nie wraca do
odczytu requestów usuniętych z Q. Ewentualny request przeniesiony wcześniej do
message+18 **nie należy już do tego scope**. Musi zostać rozliczony kontraktem
parenta opisanym w `button-request-last-reader.md`.

## Destruktor `23EA20`

Pełna funkcja `23EA20..23ED16` ma ABI `void destroy_context(void* context)`.
RCX jest zachowany w RDI. Funkcja niszczy szereg zasobów kontekstu. Dwa pobliskie
clear odnoszą się do różnych kolejek:

| Instrukcje | Rzeczywisty argument raw clear | Guard return | Znaczenie dla pending source-Q |
| --- | --- | --- | --- |
| `23EAF3`: RBX=context+3F8; `23EB0A`: call | context+3F8 | `23EB0F` | **wykluczony**, inna kolejka |
| `23EB28`: RSI=context+340; `23EB63`: call | context+340 | **`23EB68`** | właściwa źródłowa Q |

Przed właściwym clear destruktor niszczy condition variable pod `Q+68` i mutex
pod `Q+18`. Hook retirement nie może wtedy próbować korzystać z synchronizacji
kolejki ani wywoływać ponownie `push/pop`. Może korzystać z ważnego, przekazanego
argumentu `Q` w aktualnym natywnym wywołaniu — dokładnie jak oryginalny clear.
Po powrocie teardown zeruje pola Q i później usuwa pozostałe zasoby połączenia.

Bezpośredni znany caller `23EA20` jest pod `27D391` (return `27D396`) w
destrukcji nadrzędnego obiektu. Dla hooka raw-clear ważniejsze są jednak jego
bezpośredni return `23EB68` i rzeczywisty argument Q; nie wolno odtwarzać Q
z historycznego adresu obiektu nadrzędnego.

## Co dokładnie kończy native delete

W obu source scope `8D3AF0(Q)` przechodzi po aktualnych pointerach Q. Dla każdego:

```text
8D3B20  MOV EDX,10h
8D3B25  MOV RCX,[table+index*8]
8D3B29  CALL 95C22C
8D3B2E  ...                      ; return dla pojedynczego native delete
```

Ten kod nie wywołuje `request.vtable[0]`. `B0FFF0[0]=231BD0` jest serializatorem,
nie destruktorem. Łańcuch `95C22C→15A780→9AA474→9C11D4` kończy się
`HeapFree(private_heap,0,request)`, bez odczytu nested stringu. Po prawidłowym
powrocie tego delete blok requestu już nie żyje. Oryginalny clear odczytuje dalej
tylko tablicę pointerów i jej liczniki, po czym zwalnia tablicę przez `9B6690`.

Payload własnego requestu jest odrębną, wcześniej skopiowaną alokacją. Dla
zarejestrowanego pending requestu przed transferem nie został jeszcze przekazany
do serializeru lub dispatchera parenta. W odczytanych source-clear nie powstaje
nowy alias, nie ma callbacku Lua ani odroczonego czytania payloadu. Znane native
przeniesienie `868390` usuwa źródłowy wpis przed dopisaniem pointera do destination;
dlatego request zaobserwowany już jako child parenta trzeba z tej polityki
wykluczyć, zamiast zakładać jednoczesną własność Q i parenta.

## Konkretny scope dla hooków retirement

Nowy scope można otworzyć przy wejściu raw-clear `8D3AF0`, **tylko gdy wszystkie
warunki są spełnione**:

1. Pełny SHA i prefiksy funkcji są zgodne z profilem.
2. Bezpośredni caller return to `24005B` albo `23EB68`.
3. Argument raw-clear jest dokładnie Q z rejestracji danego własnego requestu.
4. W aktualnym żywym kontenerze istnieje dokładnie ten pointer własnej alokacji;
   rekord ma zgodny allocation ID/generację i nadal stan pending/unparented.
5. W tym samym zagnieżdżonym scope obserwowany native delete ma return `8D3B2E`
   i argument RCX równy temu własnemu requestowi. Inne elementy przechodzą
   transparentnie, bez przejmowania ich własności.

Przed delete należy zachować **wartości metadanych własności**: ID alokacji,
Q, request ID i pointer własnego payloadu. Nie wolno po delete czytać pól
requestu, aby dopiero wtedy poznać jego stringpointer.

Po prawidłowym powrocie dopasowanego native delete można oznaczyć obiekt jako
usunięty przez oryginał i po zakończeniu rozpoznanego clear rozliczyć własny
payload przez `9B6690`. Te dwa potwierdzenia zapobiegają potraktowaniu samej
obecności w snapshot jako dowodu destrukcji. **Nie wywoływać drugiego
`95C22C(request,0x48)`**. Własny rekord kończy się dokładnie raz.

Jeżeli nie zaobserwowano prawidłowego powrotu, stan własności pozostaje
nieustalony; bez automatycznego free/retry. Brak membership w Q nie oznacza
„można usunąć” — request mógł już przejść do parenta. Zmiana focus/kalibracji
nie może kasować metadanych o nadal żywej alokacji. Nie wybierać tej polityki na
podstawie samego słowa „reset” ani aktualnego ID wątku.

## ABI pozostałych hooków i guardy dla modułu retirement

Poniższe deklaracje zachowują wszystkie argumenty używane przez oryginał:

```cpp
void serialize_parent(void* message, void* native_buffer);             // 23CA90
void dispatch_parent(void* owner, void* graphics, void* live_context,
                     void* message, void* opaque_game_data);           // 855A30
void raw_clear(void* deque);                                          // 8D3AF0
void typed_parent_clear(void* deque);                                 // 89CF70
void native_delete(void* allocation, std::size_t size);                 // 95C22C
```

**Parent argument dispatchera to argument4/R9.** Caller `86A768` zapisuje
argument5 na `[RSP+20]`, a prologue `855AD7` czyta go z `[RBP+17F0]`, czyli
entry-RSP+28. Hook czteroargumentowy zgubiłby piąty argument. Oba są pointerami
lub nieinterpretowanymi wartościami 64-bit, nie danymi do ponownego wywołania
po opuszczeniu native scope. Zwroty tych funkcji nie niosą rozpoznanego wyniku;
deklaracje `void` są właściwe dla tych native callerów.

| Hook | Bezpośredni caller return | Dodatkowe ograniczenie |
| --- | --- | --- |
| 23CA90 | `23CD40` z virtual CALL pod `23CD3E` | WRITE buffer+14==1, własny child w parent+18 |
| 855A30 | `86A77F` | parent w arg4, własny child, żywy caller scope |
| 8D3AF0 | `240898`, `240A21` | końcowy WRITE clear outermessage |
| 89CF70 | `86A797` | typed clear owner+CE308 po dispatch |
| 8D3AF0 | `89CFE8` | inner clear message+18, tylko wewnątrz właściwego typed scope |
| 8D3AF0 | **`24005B`, `23EB68`** | pending source-Q reset/destruction z membership i rejestracją |
| 95C22C | `8D3B2E` | delete elementu w aktualnym, dopasowanym raw scope |
| 95C22C | `89D00F` | delete outermessage w aktualnym typed scope |

Return `23CD1C` jest drugim virtual call w pętli serializatora przy innym stanie
bufora; nie stanowi dowodu właściwej gałęzi WRITE. `23EB0F` oraz helper-wrapper
`242040→8D3AF0` (return `242057`) nie mają tu uprawnienia do source-Q retirement.
Nie rozszerzać allowlisty tylko dlatego, że te funkcje też wykonują clear.

Prefiksy24, pełne granice funkcji, bajty CALL/return oraz instrukcje są w
`artifacts/button-request-teardown-evidence.json`. Leaf `95C22C` ma właściwy kod
tylko `95C22C..95C231` (pięciobajtowy JMP); 24-bajtowy guard obejmuje również
padding i początek następnej funkcji, a nie 24-bajtowe ciało delete.

## Sprawdzenie offline wymagane przed aktywacją

Fixture powinien odróżnić oba dobre source guardy od `23EB0F`, obcego Q, braku
membership, childa już przeniesionego do parenta, obcego pointera pod tym samym
clear, niedopasowanego delete callera, zagnieżdżonego clear i niezaobserwowanego
powrotu. W dobrym przypadku oryginał usuwa request raz, a moduł zwalnia tylko
swój payload raz. Teardown wykonujący clear na pustej Q nie kończy arbitralnie
rekordów innych rodziców. Ten dokument nie uruchamia takiej integracji w grze.

Materiały: `button-request-teardown-01.json` i pliki `button-teardown-140*.c`,
wcześniejszy `button-release-request-04.json` (23FF80),
`native-input-queue-01.json` (caller855A30), `held-button-cleanup-03.json`
(prologue855A30), `button-request-lifecycle-01/02/03.json` i
`button-request-last-reader-01.json`.
