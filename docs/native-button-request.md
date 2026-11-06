# Natywny obiekt zwolnienia przycisku

`NativeButtonReleaseRequest` buduje rzeczywisty 72-bajtowy format
`c_client_to_server_request_use_vehicle_button` rozpoznanej wersji gry.
Nie uruchamia Stormworks i nie ma jeszcze aktywnego wywołującego w module input.
Przygotowuje tylko release (`pressed=0`), pozostawiając natywną ścieżkę press
z jej warunkami postaci, odległości i wyboru celu.

## Dane i przypięte operacje

`NativeButtonRequestLayout` ma statycznie sprawdzone offsety i rozmiar 0x48.
Nagłówek zawiera vtable, type=10 i metadata=0. Kopiowane są vehicle ID, section
index, int3 grid i **zapisany lokalny hit rozpoczętego trzymania**. Nie używa się
nowego hover. `skip_on_button_press` jest kopiowany jako byte 0/1; pełny wybór
polityki normalnego release lub anulowania należy do adaptera interakcji.
Zapis hit zachowuje bity, w tym ujemne zero. Niepoprawne wartości niefinitywne
są odrzucane przed alokacją.

`NativeButtonRequestApi` dostarcza natywną alokację, niezależną kopię stringu,
usunięcie własnego obiektu przed oddaniem oraz enqueue. Sam builder nie
wyprowadza adresów ani nie sprawdza SHA. Warstwa wiążąca te operacje musi
zweryfikować profil i ABI, a jej kontekst musi żyć przez wywołania requestu.
Nie wolno wskazać `request.vtable[0]` jako destruktora: to serializator.

NativeString zajmuje 16 bajtów: pointer, uint32 length, uint32 capacity.
Capacity oznacza liczbę zaalokowanych **bajtów wraz z terminatorem**.
Niepusty string wymaga pointer!=0 i capacity>=length+1; stan pusty
`{0,0,0}` jest dozwolony. Builder wymaga niezależnej, zakończonej NUL kopii
dokładnych bajtów payloadu, z zachowaniem jawnej długości i wewnętrznych NUL.
Limit 65535 bajtów wynika z uint16 używanego przez wire serializer.

## Własność i błędy

1. `prepare(data)` najpierw waliduje dane, następnie alokuje 0x48 i zeruje pola
   oraz padding. Dopiero wtedy wywołuje assign payload. Cały obiekt jest
   przeznaczony do jednej próby przygotowania.
2. Przed enqueue własny kompletny lub częściowo przygotowany obiekt można
   usunąć przez jawne `discard()`. Callback musi obsłużyć także payload częściowy.
3. `submit(live_queue)` wymaga stanu prepared i niezerowego aktualnego argumentu
   kolejki. Queue nie jest zapamiętywana. Właściwą kolejkę musi dostarczyć adapter
   w rozpoznanym żywym zakresie, nie z historycznej kopii wskaźnika.
4. **Przed** wywołaniem enqueue stan staje się ownership_uncertain i znika
   dereferencjonowany wskaźnik requestu. Po powrocie jest transferred. Callback
   może już synchronicznie zniszczyć obiekt; builder nie czyta go po wywołaniu.
5. Wyjątek enqueue zostawia ownership_uncertain. Nie wolno ponawiać push ani
   samodzielnie zwalniać obiektu: nie wiadomo, czy został już przyjęty.
6. Wyjątek discard zostawia discard_uncertain, także bez ponowienia.

Destructor C++ nie wykonuje **żadnego** natywnego callbacku. Jest to istotne
przy końcu procesu i pod Windows DLL loader lock. Brak explicit discard przed
handoff pozostawia alokację bez automatycznego sprzątania; adapter ma zamknąć
ten cykl w swoim żywym zakresie. Nie jest to ogólny smart pointer.

`address_token()` pozostaje wyłącznie identyfikatorem. Po submit alokacja może
już nie istnieć. Token nie daje prawa późniejszego odczytu pamięci.

## Co potwierdzają testy

`native_button_request_tests.cpp` używa osobnych atrap natywnych alokatorów,
payloadu i kolejki. Sprawdza surowe offsety nagłówka/siatki/hitu/stringu,
zerowy padding, niezależną kopię z wewnętrznym NUL i limit wire, niepoprawne
wyniki assign, wyjątek przed/po alokacji payloadu, null allocation, pustą kolejkę,
jednorazowość, synchroniczne zniszczenie podczas enqueue oraz wyjątek przed i
po przekazaniu obiektu. Odrzuca retry i free po każdym podjętym handoff.
Osobna próba potwierdza brak natywnych callbacków w destructorze i brak retry
po częściowo wykonanym discard. CMake buduje core i ten test z MSVC
`/W4 /WX /permissive-`; target przeszedł bez uruchamiania gry.

To jest test rzeczywistego buildera z atrapami granic natywnych. Nie dowodzi
poprawnego serwerowego przycisku ani finalnego disposal po oddaniu requestu.
Pełny cykl request queue → network message → odbiorcy/clear jest osobną analizą
w `button-request-lifecycle.md`. Dwie normalne drogi ostatniego odbiorcy są już
rozpoznane w `button-request-last-reader.md`, lecz nie wdrożono jeszcze ich
hooków, bookkeeping i polityki pending reset/teardown. Pozostają również lokalny
held slot i powiązanie release z faktycznie przejętym press. Samo transferred nie potwierdza zwolnienia
kontrolki ani zwolnienia jej pamięci.

Źródło układu i kolejek: `button-release-request.md` oraz odpowiadający mu profil
`artifacts/button-release-request-evidence.json`.
