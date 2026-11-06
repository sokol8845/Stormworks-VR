# Rejestr własnych zdarzeń dłoni

`HandEventRegistry` jest wdrożonym rejestrem metadanych, niezależnym od natywnej
emisji. Nie czyta ani nie zapisuje pamięci pod adresem eventu, nie alokuje go,
nie wywołuje gry i sam nie dopuszcza naciśnięcia. Działa także poza Windows.

Konstruktor rezerwuje stałą liczbę slotów (domyślnie 128). Późniejsze operacje
nie rozrastają magazynu. Pełny rejestr odrzuca nową rejestrację; nigdy nie usuwa
zapisów oczekujących, anulowanych lub już obsłużonych. Zapis istnieje do
`erase_before_destroy`, również po `reset`.

## Kontrakt użycia

1. Po utworzeniu kompletnego natywnego eventu wywołać `register_event` z jego
   adresem, fazą i kopią certyfikatu selekcji. Zachować zwrócony token.
2. Bezpośrednio przed native push wywołać `mark_submitted(token)`. Native push
   wykonać po powrocie z API. Nieznany wynik handoff oznaczyć przez
   `mark_uncertain(token)`; nie ponawiać native push i nie zgadywać własności.
3. Przy odbiorze wywołać `claim_for_dispatch(address,known_caller)`. Nieznany
   rejestrowi event daje `foreign_passthrough`. Własny daje `owned_evaluate`
   najwyżej raz i tylko po submission, bez anulowania/niepewności, w znanej
   ścieżce. `owned_evaluate` oznacza obowiązek sprawdzenia końcowych bramek,
   świeżości, zgodności kontekstu i celu; nie jest zezwoleniem na kliknięcie.
4. Po własnym dopuszczonym claim wywołać `finish(token,executed)`, po ewentualnym
   native call. Reset w trakcie nie ukrywa rzeczywistego wykonania: pozostaje
   jednocześnie informacja cancelled oraz wynik executed/not_executed.
5. W natywnym destruktorze usunąć rekord przez `erase_before_destroy(address)`
   **przed** oryginalnym zwolnieniem pamięci. Dla odłożonych callbacków używać
   wariantu przyjmującego token. Wariant address jest przeznaczony wyłącznie dla
   aktualnie wykonywanego destruktora żywego obiektu.

Każdy własny event z nieznanego callera, z niepewną własnością, nieoddany jeszcze
do kolejki, anulowany lub ponownie dispatchowany daje `owned_suppress`. Nie wolno
takiego wyniku potraktować jako wejścia fizycznego. Pierwszy odrzucony claim
pozostaje odrzucony i nie może być później ponowiony jako dopuszczony.

Certyfikat i token zwracane przez API są kopiami. Certyfikat wewnątrz istniejącego
rekordu pozostaje niezmienny. Pointer-sized tokeny owner/controller/target są
identyfikatorami równości; nie dają prawa dereferencji poza właściwym kontekstem.

## Reset, pointer reuse i synchronizacja

`reset()` podnosi generację rejestru i oznacza wszystkie istniejące rekordy jako
anulowane, zachowując je do destruktora. Własny queued press po reset nadal jest
własny i zostanie stłumiony. Generacja zapisu oraz monotoniczne `event_id` są
częścią tokenu. Rejestracja tego samego adresu bez zniszczenia poprzedniego
obiektu jest odrzucana. Ponowne użycie adresu po destruktorze dostaje nowe id;
stary token nie może wykonać submit/uncertain/finish ani usunięcia nowego obiektu.
Przepełnienie 64-bitowych liczników blokuje kolejne rejestracje zamiast je zawijać.

Wszystkie operacje na metadanych są serializowane mutexem. API nie przyjmuje
callbacków i nigdy nie wywołuje natywnego kodu pod tym mutexem. Caller ma wykonać
native push/dispatch/destructor pomiędzy zakończonymi operacjami rejestru. Dwóch
konsumentów może otrzymać kopie rekordu, ale tylko jeden otrzyma `owned_evaluate`.

Rejestr musi przeżyć natywne eventy i hook ich destruktora. Samo zniszczenie
obiektu C++ rejestru nie czyści kolejki gry. Skończona pojemność jest celowym
warunkiem odmowy nowych zdarzeń, nie mechanizmem odzyskiwania eventów bez dowodu
ich destrukcji.

## Weryfikacja i ograniczenia

`hand_event_registry_tests.cpp` sprawdza FIFO z fizycznymi i własnymi eventami,
cancel przed odbiorem, zachowanie ostatniego slotu przy pustym pop, destrukcję
oczekujących eventów przy teardown, pojemność zachowującą tombstones, pointer
reuse ze starym callbackiem, reset podczas claim, rozróżnienie obcych eventów
od własnych z nieznanego callera, wyścig dwóch claim i niezmienność 16 bajtów
payloadu. Fixture modeluje udokumentowane granice własności `694200`; nie ładuje
ani nie uruchamia Stormworks.

Samodzielna kompilacja MSVC 19.44 z `/std:c++20 /W4 /WX /permissive-` i uruchomienie
`build/registry-standalone/swvr_hand_event_registry_tests.exe` przeszły. Obejmuje
to niepewny wynik handoff zgłoszony po reset oraz po synchronicznym finish;
żaden z tych raportów nie usuwa dowodu faktycznego wykonania.

Nie podłączono rejestru do produkcyjnego native push ani dispatchera. Nie
rozstrzyga on semantyki action20/phase0, cleanup po śmierci/zmianie świata,
kompletności UI ani kolizji z fizycznym action20. Te wymagania pozostają opisane
w `hand-action-integration.md` oraz `held-button-cleanup.md`.
