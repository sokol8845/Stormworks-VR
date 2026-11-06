# Lewa dłoń i natywne Q — offline 2026-11-04

`-HandButtons` obsługuje teraz szczypnięcie kciuk–wskazujący obu rąk:
lewa wykonuje natywne Q (19), prawa E (20). Najpierw otwórz dłoń, wskaż
element palcem, następnie złącz kciuk ze wskazującym. Otwarcie kończy
przytrzymanie. Dźwignia nadal korzysta z oryginalnej logiki gry.

Gra ma jeden bieżący cel. Pierwszy rozpoznany pinch wybiera rękę do końca
gestu; druga nie przejmuje trzymanego elementu. Jednoczesne gesty zachowują
priorytet aktualnie wybranej ręki (prawa na początku). Zamknięta wcześniej
druga dłoń nie tworzy nowego naciśnięcia po puszczeniu pierwszej.
Promień diagnostyczny pokazuje aktualnie wybraną rękę. Przy braku promienia
pozostaje zaznaczanie wzrokiem i fizyczne Q/E.

| Akcja | Native id | Trzymany element C+ | Flaga wciśnięcia C+ | Zapisany hit C+ |
|---|---|---|---|---|
| Lewa / Q | 19 | 0x560 | 0x4c0 | 0x574 |
| Prawa / E | 20 | 0x558 | 0x4bf | 0x568 |

Pola potwierdzono w dyspozytorze `78C7A0` dla pełnego SHA obecnego profilu.
[Dowód statyczny](../artifacts/native-q-e-evidence-20261104.json).
Certyfikat naciśnięcia zawiera akcję; przechwycenie danych zwolnienia oraz
sprzątanie lokalnego stanu używają pól właściwej ręki. Zwolnienie nadal
wysyła dedykowany request typu 10, bez wywoływania natywnej fazy 0/short-click.
Konflikt z fizycznym Q/E albo innym trzymanym obiektem blokuje nowy gest.

W trybie joysticka lewy kciuk–środkowy palec rezerwuje lewą rękę, prawa
pozostaje dostępna. W `-ArmSwingRun` rozpoznana pięść przy ciele rezerwuje
obie ręce; także gdy śledzenie drugiej dłoni zniknie. Po powrocie do
interakcji trzeba ponownie otworzyć dłoń przed pinch.

Test integracyjny obejmuje oba naciśnięcia, zapisane hity, normalne
zwolnienie, utratę fokusu, zmianę postaci/dezaktywację, konflikt z fizycznym Q
oraz rozliczenie requestu i payloadu. To potwierdzenie offline.
Kierunek dźwigni i wygodę wyboru ręki trzeba jeszcze sprawdzić w goglach.
