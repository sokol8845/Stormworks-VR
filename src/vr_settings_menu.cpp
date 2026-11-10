#include "swvr/vr_settings_menu.hpp"
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace swvr {
namespace {
std::string decimal(float value){std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(1)<<value;return out.str();}
std::vector<PanelButton> with_help(std::vector<PanelButton> buttons){buttons.push_back({static_cast<int>(VrMenuAction::help),"?",.1775f,.29f,.055f,.055f,true});return buttons;}
std::vector<PanelButton> buttons(VrMenuCapabilities c){
    const auto id=[](VrMenuAction a){return static_cast<int>(a);};
    return {{id(VrMenuAction::joystick),"JOYSTICK",-.105f,.14f,.19f,.065f,c.locomotion},{id(VrMenuAction::arm_swing),"DWIE PIESCI",.105f,.14f,.19f,.065f,c.locomotion},
        {id(VrMenuAction::reload),"WCZYTAJ",-.105f,.06f,.19f,.065f,true},{id(VrMenuAction::controls),"OBROT / MENU",.105f,.06f,.19f,.065f,true},
        {id(VrMenuAction::calibrate),"KALIBRUJ",-.105f,-.02f,.19f,.065f,c.avatar},{id(VrMenuAction::clear_profile),"USUN PROFIL",.105f,-.02f,.19f,.065f,c.avatar},
        {id(VrMenuAction::movement_page),"CHODZENIE",-.105f,-.10f,.19f,.065f,c.locomotion},{id(VrMenuAction::tools_page),"NARZEDZIA",.105f,-.10f,.19f,.065f,true},
        {id(VrMenuAction::priority_left),"CEL LEWA",-.105f,-.18f,.19f,.065f,true},{id(VrMenuAction::priority_right),"CEL PRAWA",.105f,-.18f,.19f,.065f,true},
        {id(VrMenuAction::keypad),"KLAWIATURA",-.105f,-.26f,.19f,.06f,c.keypad},{id(VrMenuAction::close),"ZAMKNIJ",.105f,-.26f,.19f,.06f,true}};
}
std::string_view help_description(VrMenuAction action)noexcept{switch(action){
    case VrMenuAction::joystick:return "CHWYC JEDNA PIESCIA, ABY CHODZIC. DRUGA DLON OBRACA POSTAC.";
    case VrMenuAction::arm_swing:return "DWIE ZAMKNIETE PIESCI = BIEG. OTWORZ JEDNA DLON, ABY STANAC.";
    case VrMenuAction::reload:return "WCZYTUJE USTAWIENIA Z PLIKU. ZATRZYMUJE RUCH NA CZAS ZMIANY.";
    case VrMenuAction::calibrate:return "DOPASOWUJE GLOWE I RAMIONA. PO WYBORZE ROZLOZ RECE W T-POSE.";
    case VrMenuAction::clear_profile:return "USUWA ZAPISANA KALIBRACJE CIALA. WYMAGA OSOBNEGO POTWIERDZENIA.";
    case VrMenuAction::keypad:return "WSKAZ KLAWIATURE POJAZDU I AKTYWUJ JAK PRZYCISK. PANEL VR WPISUJE LICZBY DO GRY.";
    case VrMenuAction::joystick_left:return "LEWA DLON STERUJE CHODZENIEM, PRAWA OBRACA W TRYBIE JOYSTICK.";
    case VrMenuAction::joystick_right:return "PRAWA DLON STERUJE CHODZENIEM, LEWA OBRACA W TRYBIE JOYSTICK.";
    case VrMenuAction::movement_page:return "WYBOR DLONI JOYSTICKA I PRZELACZNIK CHODZENIA TYLKO DO PRZODU.";
    case VrMenuAction::forward_only_toggle:return "JOYSTICK CHODZI TYLKO DO PRZODU. COFNIECIE DLONI ZATRZYMUJE. DRUGA DLON OBRACA.";
    case VrMenuAction::priority_left:return "LEWA DLON MA PIERWSZENSTWO, GDY OBIE WSKAZUJA PRZYCISKI.";
    case VrMenuAction::priority_right:return "PRAWA DLON MA PIERWSZENSTWO, GDY OBIE WSKAZUJA PRZYCISKI.";
    case VrMenuAction::close:return "ZAMYKA MENU. WYBRANY TRYB RUCHU ZNOW MOZE DZIALAC.";
    case VrMenuAction::controls:return "OTWIERA REGULACJE OBROTU, ODLEGLOSCI MENU I DALSZE OPCJE.";
    case VrMenuAction::back:return "WRACA DO GLOWNEJ STRONY MENU.";
    case VrMenuAction::turn_slower:case VrMenuAction::turn_faster:return "PREDKOSC PLYNNEGO OBROTU PRZY PELNYM WYCHYLENIU DRUGIEJ DLONI.";
    case VrMenuAction::turn_dead_less:case VrMenuAction::turn_dead_more:return "MARTWA STREFA: MALY RUCH PIESCI NIE OBRACA POSTACI. MNIEJ = CZULEJ.";
    case VrMenuAction::turn_range_less:case VrMenuAction::turn_range_more:return "WYCHYLENIE PIESCI DO PELNEJ PREDKOSCI OBROTU. MNIEJ = MNIEJSZY RUCH.";
    case VrMenuAction::menu_nearer:case VrMenuAction::menu_farther:return "PRZYSUWA LUB ODSUWA OTWARTY PANEL O 10 CM. ZMIANE WIDAC OD RAZU.";
    case VrMenuAction::defaults:return "PRZYWRACA WSZYSTKIE DOMYSLNE USTAWIENIA STEROWANIA I KOMFORTU.";
    case VrMenuAction::interface_page:return "OTWIERA SKALE MENU, CZAS WSKAZANIA I DZWIEK KLIKNIEC.";
    case VrMenuAction::comfort_page:return "OTWIERA OBROT SKOKOWY, WINIETE, RUCH GLOWY I PROFIL SIEDZACY.";
    case VrMenuAction::dwell_less:case VrMenuAction::dwell_more:return "JAK DLUGO WSKAZYWAC, ABY KLIKNAC. MNIEJ = SZYBCIEJ. DOTYK BEZ CZEKANIA.";
    case VrMenuAction::scale_less:case VrMenuAction::scale_more:return "ZMIENIA WIELKOSC PANELU, NAPISOW I POL TRAFIENIA. NIE ZMIENIA ODLEGLOSCI.";
    case VrMenuAction::sound_toggle:return "WLACZA KROTKI DZWIEK PO KLIKNIECIU. UZYWA WYJSCIA DZWIEKU WINDOWS.";
    case VrMenuAction::turn_toggle:return "DRUGA PIESC OBRACA PLYNNIE ALBO SKOKAMI. DZIALA W TRYBIE JOYSTICK.";
    case VrMenuAction::snap_30:case VrMenuAction::snap_45:return "KAT JEDNEGO SKOKU OBROTU. NAJPIERW WLACZ OBROT SKOKOWY.";
    case VrMenuAction::vignette_toggle:return "PRZYCIEMNIA BRZEGI PODCZAS RUCHU. KOLEJNO: WYLACZONA, 45%, 75%.";
    case VrMenuAction::head_toggle:return "LIMIT OGRANICZA WYCHYLENIE. 1:1 ZACHOWUJE RUCH I WYGASZA OBRAZ PRZY GRANICY.";
    case VrMenuAction::seated_toggle:return "USTAWIA NOWY SRODEK GLOWY. PROFIL SIEDZACY OBNIZA PANEL O 7 CM.";
    case VrMenuAction::recenter:return "BIEZACA POZYCJA GLOWY STAJE SIE SRODKIEM. KIERUNEK SWIATA BEZ ZMIAN.";
    case VrMenuAction::tools_page:return "KIEROWNICA, ZAPISANE USTAWIENIA I DIAGNOSTYKA STEROWANIA.";
    case VrMenuAction::steering_page:return "REGULUJE KAT PELNEGO SKRETU, MARTWA STREFE I WYGLADZANIE KIEROWNICY.";
    case VrMenuAction::profiles_page:return "TRZY ZESTAWY USTAWIEN. WYBOR NAZWY NIE ZMIENIA STEROWANIA. UZYJ WCZYTAJ.";
    case VrMenuAction::wheel_lock_less:case VrMenuAction::wheel_lock_more:return "KAT OD SRODKA DO PELNEGO SKRETU KOL. MNIEJ = CZULSZA KIEROWNICA.";
    case VrMenuAction::wheel_dead_less:case VrMenuAction::wheel_dead_more:return "MALY OBROT PRZY SRODKU NIE SKRECA KOL. WARTOSC W STOPNIACH.";
    case VrMenuAction::wheel_smooth_less:case VrMenuAction::wheel_smooth_more:return "WYGLADZANIE DRZEN DLONI W MS. WIECEJ = SPOKOJNIEJ, ALE WOLNIEJSZA REAKCJA.";
    case VrMenuAction::profile_walking:case VrMenuAction::profile_driving:case VrMenuAction::profile_second_user:return "WYBIERA MIEJSCE ZAPISU. WCZYTAJ PRZYWRACA JEGO USTAWIENIA; ZAPISZ ZAPISUJE BIEZACE.";
    case VrMenuAction::profile_save:return "ZAPISUJE BIEZACE STEROWANIE I KOMFORT. POPRZEDNI ZAPIS TRAFIA DO KOPII.";
    case VrMenuAction::profile_load:return "WCZYTUJE WYBRANY ZESTAW PO ZWOLNIENIU PRZYCISKOW. NIE ZMIENIA KALIBRACJI CIALA.";
    case VrMenuAction::profile_restore:return "WCZYTUJE POPRZEDNIA WERSJE WYBRANEGO ZESTAWU. ZAPISZ UTRWALA JA W PROFILU.";
    case VrMenuAction::diagnostics_toggle:return "POKAZUJE OSIE RUCHU, CHWYTY I POWOD BLOKADY KLIKANIA. PANEL TYLKO DO ODCZYTU.";
    case VrMenuAction::mark_problem:return "ZAPISUJE OSTATNIE MINUTY NA KOMPUTERZE. ZACHOWUJE 3 NAJNOWSZE ZGLOSZENIA. NICZEGO NIE WYSYLA.";
    case VrMenuAction::movement_tuning_page:return "REGULUJE MARTWA STREFE, WYCHYLENIE JOYSTICKA I PROGI SPRINTU W MILIMETRACH.";
    case VrMenuAction::joy_dead_less:case VrMenuAction::joy_dead_more:return "MARTWA STREFA CHODZENIA. MNIEJSZE WYCHYLENIA DLONI NIE RUSZAJA POSTACI.";
    case VrMenuAction::joy_range_less:case VrMenuAction::joy_range_more:return "WYCHYLENIE DLONI DO PELNEJ OSI RUCHU. MNIEJ = CZULSZY JOYSTICK. GRA UZYWA KLAWISZY RUCHU.";
    case VrMenuAction::sprint_on_less:case VrMenuAction::sprint_on_more:return "WYCHYLENIE DO PRZODU WLACZAJACE SPRINT. MUSI BYC WIEKSZE OD PROGU STOP.";
    case VrMenuAction::sprint_off_less:case VrMenuAction::sprint_off_more:return "PRZY COFANIU DLONI PONIZEJ TEGO PROGU SPRINT WYLACZA SIE. CHODZENIE MOZE TRWAC.";
    case VrMenuAction::joystick_visual_toggle:return "DWA OKREGI POKAZUJA SRODEK I WYCHYLENIE JOYSTICKA RUCHU ORAZ OBROTU.";
    case VrMenuAction::wheel_grip_reset:return "W FOTELU USTAWIA CHWYT OD NOWA. OTWORZ OBIE DLONIE, POTEM ZLAP KIEROWNICE.";
    case VrMenuAction::wheel_visual_toggle:return "POKAZUJE UMOWNA OBRECZ CHWYTU. TO STREFA DETEKTORA, NIE PODSWIETLENIE MODELU POJAZDU.";
    case VrMenuAction::practice:return "TRENING RUCHU, OBROTU, CHWYTU I KLIKANIA. GESTY NIE STERUJA WTEDY GRA. WYJSCIE W PANELU.";
    case VrMenuAction::help:return "PRZELACZA POMOC. WSKAZANIE OPCJI POKAZUJE OPIS I NIC NIE ZMIENIA.";
    }return "";}
std::vector<PanelButton> controls(){const auto id=[](VrMenuAction a){return static_cast<int>(a);};return {
    {id(VrMenuAction::turn_slower),"WOLNIEJ",-.105f,.14f,.19f,.065f},{id(VrMenuAction::turn_faster),"SZYBCIEJ",.105f,.14f,.19f,.065f},
    {id(VrMenuAction::turn_dead_less),"STREFA -",-.105f,.06f,.19f,.065f},{id(VrMenuAction::turn_dead_more),"STREFA +",.105f,.06f,.19f,.065f},
    {id(VrMenuAction::turn_range_less),"ZAKRES -",-.105f,-.02f,.19f,.065f},{id(VrMenuAction::turn_range_more),"ZAKRES +",.105f,-.02f,.19f,.065f},
    {id(VrMenuAction::menu_nearer),"MENU BLIZEJ",-.105f,-.10f,.19f,.065f},{id(VrMenuAction::menu_farther),"MENU DALEJ",.105f,-.10f,.19f,.065f},
    {id(VrMenuAction::interface_page),"INTERFEJS",-.105f,-.18f,.19f,.065f},{id(VrMenuAction::comfort_page),"KOMFORT",.105f,-.18f,.19f,.065f},
    {id(VrMenuAction::defaults),"DOMYSLNE",-.105f,-.26f,.19f,.06f},{id(VrMenuAction::back),"WROC",.105f,-.26f,.19f,.06f}};}
std::vector<PanelButton> interface_buttons(){const auto id=[](VrMenuAction a){return static_cast<int>(a);};return {
    {id(VrMenuAction::dwell_less),"CZAS -",-.105f,.14f,.19f,.065f},{id(VrMenuAction::dwell_more),"CZAS +",.105f,.14f,.19f,.065f},
    {id(VrMenuAction::scale_less),"ROZMIAR -",-.105f,.06f,.19f,.065f},{id(VrMenuAction::scale_more),"ROZMIAR +",.105f,.06f,.19f,.065f},
    {id(VrMenuAction::sound_toggle),"DZWIEK KLIKNIEC WL / WYL",0,-.02f,.40f,.065f},
    {id(VrMenuAction::controls),"WROC",0,-.26f,.40f,.06f}};}
std::vector<PanelButton> comfort_buttons(){const auto id=[](VrMenuAction a){return static_cast<int>(a);};return {
    {id(VrMenuAction::turn_toggle),"OBROT PLYNNY / SKOKOWY",0,.14f,.40f,.065f},
    {id(VrMenuAction::snap_30),"SKOK 30",-.105f,.06f,.19f,.065f},{id(VrMenuAction::snap_45),"SKOK 45",.105f,.06f,.19f,.065f},
    {id(VrMenuAction::vignette_toggle),"WINIETA 0 / 45 / 75",0,-.02f,.40f,.065f},
    {id(VrMenuAction::head_toggle),"GLOWA LIMIT / 1:1",0,-.10f,.40f,.065f},
    {id(VrMenuAction::seated_toggle),"SIEDZE / STOJE",-.105f,-.18f,.19f,.065f},{id(VrMenuAction::recenter),"USTAW SRODEK",.105f,-.18f,.19f,.065f},
    {id(VrMenuAction::controls),"WROC",0,-.26f,.40f,.06f}};}
std::vector<PanelButton> movement_buttons(){const auto id=[](VrMenuAction a){return static_cast<int>(a);};return {
    {id(VrMenuAction::joystick_left),"JOY LEWA",-.105f,.14f,.19f,.065f},{id(VrMenuAction::joystick_right),"JOY PRAWA",.105f,.14f,.19f,.065f},
    {id(VrMenuAction::forward_only_toggle),"TYLKO PRZOD: WYL",0,.06f,.40f,.065f},
    {id(VrMenuAction::movement_tuning_page),"REGULACJA CHODZENIA",0,-.02f,.40f,.065f},
    {id(VrMenuAction::joystick_visual_toggle),"PODGLAD JOYSTICKOW: WYL",0,-.10f,.40f,.065f},
    {id(VrMenuAction::back),"WROC",0,-.26f,.40f,.06f}};}
std::vector<PanelButton> tools_buttons(){const auto id=[](VrMenuAction a){return static_cast<int>(a);};return {
    {id(VrMenuAction::steering_page),"KIEROWNICA",0,.14f,.40f,.065f},
    {id(VrMenuAction::profiles_page),"PROFILE STEROWANIA",0,.06f,.40f,.065f},
    {id(VrMenuAction::diagnostics_toggle),"DIAGNOSTYKA: WYL",0,-.02f,.40f,.065f},
    {id(VrMenuAction::mark_problem),"ZGLOS PROBLEM - ZAPIS",0,-.10f,.40f,.065f},
    {id(VrMenuAction::practice),"CWICZENIA STEROWANIA",0,-.18f,.40f,.065f},
    {id(VrMenuAction::back),"WROC",0,-.26f,.40f,.06f}};}
std::vector<PanelButton> steering_buttons(){const auto id=[](VrMenuAction a){return static_cast<int>(a);};return {
    {id(VrMenuAction::wheel_lock_less),"KAT -",-.105f,.14f,.19f,.065f},{id(VrMenuAction::wheel_lock_more),"KAT +",.105f,.14f,.19f,.065f},
    {id(VrMenuAction::wheel_dead_less),"STREFA -",-.105f,.06f,.19f,.065f},{id(VrMenuAction::wheel_dead_more),"STREFA +",.105f,.06f,.19f,.065f},
    {id(VrMenuAction::wheel_smooth_less),"FILTR -",-.105f,-.02f,.19f,.065f},{id(VrMenuAction::wheel_smooth_more),"FILTR +",.105f,-.02f,.19f,.065f},
    {id(VrMenuAction::wheel_grip_reset),"USTAW CHWYT",0,-.10f,.40f,.065f},
    {id(VrMenuAction::wheel_visual_toggle),"POKAZ CHWYT: WYL",0,-.18f,.40f,.065f},
    {id(VrMenuAction::tools_page),"WROC",0,-.26f,.40f,.06f}};}
std::vector<PanelButton> profile_buttons(){const auto id=[](VrMenuAction a){return static_cast<int>(a);};return {
    {id(VrMenuAction::profile_walking),"CHODZENIE",-.105f,.14f,.19f,.065f},{id(VrMenuAction::profile_driving),"JAZDA",.105f,.14f,.19f,.065f},
    {id(VrMenuAction::profile_second_user),"UZYTKOWNIK 2",0,.06f,.40f,.065f},
    {id(VrMenuAction::profile_save),"ZAPISZ",-.105f,-.02f,.19f,.065f},{id(VrMenuAction::profile_load),"WCZYTAJ",.105f,-.02f,.19f,.065f},
    {id(VrMenuAction::profile_restore),"WCZYTAJ KOPIE",0,-.10f,.40f,.065f},
    {id(VrMenuAction::tools_page),"WROC",0,-.26f,.40f,.06f}};}

std::vector<PanelButton> movement_tuning_buttons(){const auto id=[](VrMenuAction a){return static_cast<int>(a);};return {
    {id(VrMenuAction::joy_dead_less),"STREFA -",-.105f,.14f,.19f,.065f},{id(VrMenuAction::joy_dead_more),"STREFA +",.105f,.14f,.19f,.065f},
    {id(VrMenuAction::joy_range_less),"ZAKRES -",-.105f,.06f,.19f,.065f},{id(VrMenuAction::joy_range_more),"ZAKRES +",.105f,.06f,.19f,.065f},
    {id(VrMenuAction::sprint_on_less),"BIEG START -",-.105f,-.02f,.19f,.065f},{id(VrMenuAction::sprint_on_more),"BIEG START +",.105f,-.02f,.19f,.065f},
    {id(VrMenuAction::sprint_off_less),"BIEG STOP -",-.105f,-.10f,.19f,.065f},{id(VrMenuAction::sprint_off_more),"BIEG STOP +",.105f,-.10f,.19f,.065f},
    {id(VrMenuAction::movement_page),"WROC",0,-.26f,.40f,.06f}};}
void increase(float& value,float step,float limit){if(limit>value)value=std::min(limit,value+step);}
void decrease(float& value,float step,float limit){if(limit<value)value=std::max(limit,value-step);}

}
std::string_view vr_menu_help(VrMenuAction action)noexcept{return help_description(action);}
bool adjust_vr_preferences(VrMenuAction action,GestureSettings& s){auto& j=s.joystick;switch(action){
    case VrMenuAction::joy_dead_less:decrease(j.dead_zone_m,.005f,.005f);break;
    case VrMenuAction::joy_dead_more:increase(j.dead_zone_m,.005f,std::min({.1f,j.full_deflection_m-.001f,j.sprint_off_m-.001f}));break;
    case VrMenuAction::joy_range_less:decrease(j.full_deflection_m,.01f,std::max(.02f,j.dead_zone_m+.001f));break;
    case VrMenuAction::joy_range_more:increase(j.full_deflection_m,.01f,.4f);break;
    case VrMenuAction::sprint_on_less:decrease(j.sprint_on_m,.01f,std::max(.02f,j.sprint_off_m+.001f));break;
    case VrMenuAction::sprint_on_more:increase(j.sprint_on_m,.01f,.4f);break;
    case VrMenuAction::sprint_off_less:decrease(j.sprint_off_m,.01f,std::max(.01f,j.dead_zone_m+.001f));break;
    case VrMenuAction::sprint_off_more:increase(j.sprint_off_m,.01f,std::min(.4f,j.sprint_on_m-.001f));break;
    case VrMenuAction::joystick_visual_toggle:s.input.joystick_visual=!s.input.joystick_visual;break;
    case VrMenuAction::wheel_visual_toggle:s.steering.show_grip=!s.steering.show_grip;break;
    case VrMenuAction::wheel_lock_less:s.steering.full_lock_degrees=std::max(30.f,s.steering.full_lock_degrees-15);break;
    case VrMenuAction::wheel_lock_more:s.steering.full_lock_degrees=std::min(180.f,s.steering.full_lock_degrees+15);break;
    case VrMenuAction::wheel_dead_less:s.steering.dead_zone_degrees=std::max(0.f,s.steering.dead_zone_degrees-.5f);break;
    case VrMenuAction::wheel_dead_more:s.steering.dead_zone_degrees=std::min(15.f,s.steering.dead_zone_degrees+.5f);break;
    case VrMenuAction::wheel_smooth_less:s.steering.smoothing_ms=std::max(0.f,s.steering.smoothing_ms-10);break;
    case VrMenuAction::wheel_smooth_more:s.steering.smoothing_ms=std::min(200.f,s.steering.smoothing_ms+10);break;
    case VrMenuAction::forward_only_toggle:s.joystick.forward_only=!s.joystick.forward_only;break;
    case VrMenuAction::turn_slower:s.turn.speed_degrees=std::max(15.f,s.turn.speed_degrees-15);break;
    case VrMenuAction::turn_faster:s.turn.speed_degrees=std::min(120.f,s.turn.speed_degrees+15);break;
    case VrMenuAction::turn_dead_less:s.turn.dead_zone_m=std::max(.005f,s.turn.dead_zone_m-.005f);break;
    case VrMenuAction::turn_dead_more:s.turn.dead_zone_m=std::min({.1f,s.turn.full_deflection_m-.005f,s.turn.dead_zone_m+.005f});break;
    case VrMenuAction::turn_range_less:s.turn.full_deflection_m=std::max({.02f,s.turn.dead_zone_m+.005f,s.turn.full_deflection_m-.01f});break;
    case VrMenuAction::turn_range_more:s.turn.full_deflection_m=std::min(.4f,s.turn.full_deflection_m+.01f);break;
    case VrMenuAction::menu_nearer:s.input.menu_distance_m=std::max(.6f,s.input.menu_distance_m-.1f);break;
    case VrMenuAction::menu_farther:s.input.menu_distance_m=std::min(1.2f,s.input.menu_distance_m+.1f);break;
    case VrMenuAction::dwell_less:s.input.dwell_ms=std::max(300u,s.input.dwell_ms-100);break;
    case VrMenuAction::dwell_more:s.input.dwell_ms=std::min(2000u,s.input.dwell_ms+100);break;
    case VrMenuAction::scale_less:s.input.menu_scale=std::max(1.f,s.input.menu_scale-.1f);break;
    case VrMenuAction::scale_more:s.input.menu_scale=std::min(1.4f,s.input.menu_scale+.1f);break;
    case VrMenuAction::sound_toggle:s.input.click_sound=!s.input.click_sound;break;
    case VrMenuAction::turn_toggle:s.turn.snap=!s.turn.snap;break;
    case VrMenuAction::snap_30:s.turn.snap_degrees=30;break;
    case VrMenuAction::snap_45:s.turn.snap_degrees=45;break;
    case VrMenuAction::vignette_toggle:s.comfort.vignette_strength=s.comfort.vignette_strength<.4f?.45f:s.comfort.vignette_strength<.7f?.75f:0;break;
    case VrMenuAction::head_toggle:s.comfort.head_fade=!s.comfort.head_fade;break;
    case VrMenuAction::seated_toggle:s.comfort.seated=!s.comfort.seated;break;
    case VrMenuAction::defaults:s={};break;
    default:return false;
    }validate_gesture_settings(s);return true;}
bool VrSettingsMenu::open(const XrRuntimeFrame& f,VrMenuCapabilities capabilities,const GestureSettings& settings,bool arms){
    capabilities_=capabilities;confirm_clear_=clicked_=help_mode_=false;page_=0;status(settings,arms);if(!panel_.open(f,with_help(buttons(capabilities)),"USTAWIENIA VR",options_))return false;status(settings,arms);return true;
}
void VrSettingsMenu::status(const GestureSettings& settings,bool arms){settings_=settings;arms_=arms;options_.offset.z=-settings.input.menu_distance_m;options_.offset.y=settings.comfort.seated?-.12f:-.05f;options_.dwell_ms=settings.input.dwell_ms;options_.scale=settings.input.menu_scale;
    if(panel_.snapshot().visible)panel_.reconfigure(options_);
    // Each switch on these pages names its own state; the value line has no room for all of them.
    if(page_==4){panel_.set_label(static_cast<int>(VrMenuAction::forward_only_toggle),settings.joystick.forward_only?"TYLKO PRZOD: WL":"TYLKO PRZOD: WYL");
        panel_.set_label(static_cast<int>(VrMenuAction::joystick_visual_toggle),settings.input.joystick_visual?"PODGLAD JOYSTICKOW: WL":"PODGLAD JOYSTICKOW: WYL");}
    if(page_==5)panel_.set_label(static_cast<int>(VrMenuAction::diagnostics_toggle),diagnostics_?"DIAGNOSTYKA: WL":"DIAGNOSTYKA: WYL");
    if(page_==6)panel_.set_label(static_cast<int>(VrMenuAction::wheel_visual_toggle),settings.steering.show_grip?"POKAZ CHWYT: WL":"POKAZ CHWYT: WYL");
    if(page_==8){const auto millimetres="STREFA "+std::to_string(static_cast<int>(std::lround(settings.joystick.dead_zone_m*1000)))+" / ZAKRES "+std::to_string(static_cast<int>(std::lround(settings.joystick.full_deflection_m*1000)))+" / BIEG "+std::to_string(static_cast<int>(std::lround(settings.joystick.sprint_on_m*1000)))+"/"+std::to_string(static_cast<int>(std::lround(settings.joystick.sprint_off_m*1000)));
        // Four three-digit values leave no room for the space before the unit.
        panel_.set_value(millimetres+(millimetres.size()>37?"MM":" MM"));return;}
    if(page_==5){panel_.set_value(diagnostics_?"DIAGNOSTYKA: WL":"DIAGNOSTYKA: WYL");return;}
    if(page_==6){panel_.set_value("KAT "+std::to_string(static_cast<int>(std::lround(settings.steering.full_lock_degrees)))+" / STREFA "+decimal(settings.steering.dead_zone_degrees)+" DEG / FILTR "+std::to_string(static_cast<int>(std::lround(settings.steering.smoothing_ms)))+" MS");return;}
    if(page_==7){panel_.set_value("WYBRANY: "+std::string(control_profile_name(profile_)));return;}
    if(page_==4){panel_.set_value(std::string(settings.joystick.forward_only?"TYLKO PRZOD: WL":"TYLKO PRZOD: WYL")+" / JOY "+(settings.input.joystick_hand==GestureHand::left?"L / OBROT P":"P / OBROT L"));return;}
    if(page_==2){panel_.set_value("CZAS "+std::to_string(settings.input.dwell_ms)+" MS / SKALA "+std::to_string(static_cast<int>(std::lround(settings.input.menu_scale*100)))+"% / "+(settings.input.click_sound?"DZW WL":"DZW WYL"));return;}
    if(page_==3){panel_.set_value(std::string(settings.turn.snap?"SKOK ":"PLYNNY ")+(settings.turn.snap?std::to_string(settings.turn.snap_degrees):"")+" / W"+std::to_string(static_cast<int>(std::lround(settings.comfort.vignette_strength*100)))+" / "+(settings.comfort.head_fade?"1:1":"LIMIT")+" / "+(settings.comfort.seated?"SIEDZE":"STOJE"));return;}
    panel_.set_value(page_==1?std::to_string(static_cast<int>(std::lround(settings.turn.speed_degrees)))+" DEG/S  "+std::to_string(static_cast<int>(std::lround(settings.turn.dead_zone_m*1000)))+"/"+std::to_string(static_cast<int>(std::lround(settings.turn.full_deflection_m*1000)))+" MM  "+std::to_string(static_cast<int>(std::lround(settings.input.menu_distance_m*100)))+" CM":std::string(arms?"PIESCI":"JOYSTICK")+" / JOY "+(settings.input.joystick_hand==GestureHand::left?"L":"P")+" / CEL "+(settings.input.pointing_priority==GestureHand::left?"L":"P"));}
std::optional<VrMenuAction> VrSettingsMenu::update(const XrRuntimeFrame& f){
    const auto hit=panel_.update(f);refresh_help();if(!hit)return {};
    clicked_=true;
    const auto action=static_cast<VrMenuAction>(*hit);
    if(action==VrMenuAction::help){help_mode_=!help_mode_;panel_.inspect_only(help_mode_,static_cast<int>(VrMenuAction::help));refresh_help();return {};}
    std::optional<unsigned> next_page;
    switch(action){
    case VrMenuAction::back:next_page=0;break;
    case VrMenuAction::controls:next_page=1;break;
    case VrMenuAction::interface_page:next_page=2;break;
    case VrMenuAction::comfort_page:next_page=3;break;
    case VrMenuAction::movement_page:next_page=4;break;
    case VrMenuAction::tools_page:next_page=5;break;
    case VrMenuAction::steering_page:next_page=6;break;
    case VrMenuAction::profiles_page:next_page=7;break;
    case VrMenuAction::movement_tuning_page:next_page=8;break;
    default:break;
    }
    if(next_page){page_=*next_page;
        const std::array<const char*,9> titles{"USTAWIENIA VR","OBROT I MENU","INTERFEJS","KOMFORT","CHODZENIE","NARZEDZIA","KIEROWNICA","PROFILE STEROWANIA","REGULACJA CHODZENIA"};
        auto list=page_==0?buttons(capabilities_):page_==1?controls():page_==2?interface_buttons():page_==3?comfort_buttons():page_==4?movement_buttons():page_==5?tools_buttons():page_==6?steering_buttons():page_==7?profile_buttons():movement_tuning_buttons();
        panel_.open(f,with_help(std::move(list)),titles[page_],options_);status(settings_,arms_);return {};
    }
    if(action==VrMenuAction::profile_walking||action==VrMenuAction::profile_driving||action==VrMenuAction::profile_second_user){
        profile_=action==VrMenuAction::profile_walking?ControlProfile::walking:action==VrMenuAction::profile_driving?ControlProfile::driving:ControlProfile::second_user;
        panel_.set_notice({});status(settings_,arms_);return {};
    }
    if(confirm_clear_){
        if(action==VrMenuAction::clear_profile){cancel();return action;}
        panel_.open(f,with_help(buttons(capabilities_)),"USTAWIENIA VR",options_);confirm_clear_=false;return {};
    }
    if(action==VrMenuAction::clear_profile){
        const std::array<PanelButton,2> confirm{{{static_cast<int>(VrMenuAction::clear_profile),"TAK - USUN",0,.06f,.40f,.08f},{static_cast<int>(VrMenuAction::close),"WROC",0,-.06f,.40f,.08f}}};
        panel_.open(f,with_help({confirm.begin(),confirm.end()}),"USUNAC KALIBRACJE?",options_);confirm_clear_=true;return {};
    }
    if(action==VrMenuAction::close||action==VrMenuAction::calibrate||action==VrMenuAction::keypad)cancel();
    return action;
}
void VrSettingsMenu::refresh_help(){if(!help_mode_){panel_.set_description({});return;}const auto& p=panel_.snapshot();
    if(p.hover<0){panel_.set_description("WSKAZ OPCJE, ABY ZOBACZYC OPIS. ? WYLACZA POMOC.");return;}
    const auto& button=p.buttons[static_cast<unsigned>(p.hover)];const auto action=static_cast<VrMenuAction>(button.id);
    if(confirm_clear_&&action==VrMenuAction::close){panel_.set_description("WRACA DO MENU BEZ USUWANIA ZAPISANEJ KALIBRACJI.");return;}
    if(confirm_clear_&&action==VrMenuAction::clear_profile){panel_.set_description("POTWIERDZA USUNIECIE ZAPISANEJ KALIBRACJI. MOZNA POTEM SKALIBROWAC OD NOWA.");return;}
    panel_.set_description(std::string(button.enabled?"":"NIEDOSTEPNE. ")+std::string(vr_menu_help(action)));
}
bool VrMenuSummon::update(const XrRuntimeFrame& f,unsigned dwell_ms){
    if(!button_.snapshot().visible){const std::array<PanelButton,1> button{{{0,"MENU",0,0,.14f,.065f,true}}};
        if(!button_.open(f,button,"",{{0,.18f,-.80f},true,true,true,dwell_ms}))return false;}
    return button_.update(f).has_value();
}
}
