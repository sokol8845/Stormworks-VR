#include "swvr/pointing_panel.hpp"
#include "swvr/control_recording.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace swvr {
namespace {
bool usable(const XrTrackedJoint& j){const auto p=j.pose.position;return j.position_valid&&j.position_tracked&&std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool head(const XrRuntimeFrame& f){return f.id&&f.running&&f.focused&&!f.terminal&&f.should_render&&f.position_valid&&f.orientation_valid&&f.position_tracked&&f.orientation_tracked;}
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
int hit(const PanelSnapshot& s,Vec3 p){for(unsigned i=0;i<s.buttons.size();++i){const auto& b=s.buttons[i];if((b.enabled||s.help_mode)&&std::abs(p.x-b.x)<b.width*.5f&&std::abs(p.y-b.y)<b.height*.5f)return static_cast<int>(i);}return -1;}
bool valid(PanelOptions options){const auto p=options.offset;return options.dwell_ms>=300&&options.dwell_ms<=2000&&options.dwell_grace_ms<=150&&std::isfinite(options.scale)&&options.scale>=1&&options.scale<=1.4f&&std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<=.5f&&std::abs(p.y)<=.5f&&p.z<=-.2f&&p.z>=-1.2f;}
}
void PointingPanel::reset_input(){touch_={};dwell_={};need_clear_={};pressed_until_.reset();state_.pressed=state_.hover=-1;state_.dwell_progress=0;state_.rays={};state_.ray_targets={-1,-1};state_.touches={};state_.direct_touch=state_.release_required=false;}
void PointingPanel::set_preferred_hand(unsigned hand){if(hand>1)throw std::invalid_argument("Invalid panel hand");preferred_=hand;reset_input();if(recorder_)recorder_->panel_priority(recording_id_,hand);}
void PointingPanel::set_value(std::string value){state_.value=value.substr(0,40);}
bool PointingPanel::open(const XrRuntimeFrame& f,std::span<const PanelButton> buttons,std::string title,PanelOptions options){
    if(!head(f)||buttons.empty()||buttons.size()>32||title.size()>40)return false;
    if(!valid(options))return false;
    const auto offset=options.offset;
    std::set<int> ids;for(const auto& b:buttons)if(!ids.insert(b.id).second||b.label.size()>40||
        !std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.width)||!std::isfinite(b.height)||
        std::abs(b.x)>.25f||std::abs(b.y)>.32f||b.width<.02f||b.width>.45f||b.height<.02f||b.height>.6f)return false;
    try{
        const auto centre=gravity_aligned_origin({f.eyes[0].pose,f.eyes[1].pose});const auto panel=compose(centre,Pose{{},offset});inverse(panel);
        options_=options;state_={};inspection_toggle_=-1;state_.visible=true;state_.compact=options.compact;state_.panel=panel;state_.space_revision=f.space_revision;state_.title=std::move(title);unscaled_buttons_.assign(buttons.begin(),buttons.end());state_.buttons=unscaled_buttons_;
        state_.scale=options.scale;for(auto& b:state_.buttons){b.x*=options.scale;b.y*=options.scale;b.width*=options.scale;b.height*=options.scale;}
        reset_input();last_time_.reset();last_frame_=0;if(recorder_)recorder_->panel_open(recording_id_,f,state_,options_,preferred_,buttons);return true;
    }catch(...){return false;}
}
bool PointingPanel::reconfigure(PanelOptions next){
    if(!valid(next))return false;
    if(next.offset.x==options_.offset.x&&next.offset.y==options_.offset.y&&next.offset.z==options_.offset.z&&next.scale==options_.scale&&next.dwell_ms==options_.dwell_ms&&next.dwell_grace_ms==options_.dwell_grace_ms&&next.follow_head==options_.follow_head&&next.compact==options_.compact&&next.allow_dwell==options_.allow_dwell)return true;
    const std::array<bool,2> engaged{state_.ray_targets[0]>=0||state_.touches[0]||need_clear_[0],state_.ray_targets[1]>=0||state_.touches[1]||need_clear_[1]};
    state_.panel=compose(state_.panel,Pose{{},sub(next.offset,options_.offset)});options_=next;state_.scale=next.scale;state_.compact=next.compact;
    state_.buttons=unscaled_buttons_;for(auto& b:state_.buttons){b.x*=next.scale;b.y*=next.scale;b.width*=next.scale;b.height*=next.scale;}
    reset_input();need_clear_=engaged;if(recorder_&&state_.visible)recorder_->panel_options(recording_id_,next);return true;
}
void PointingPanel::inspect_only(bool enabled,int toggle_id){if(state_.help_mode==enabled&&inspection_toggle_==toggle_id)return;
    const std::array<bool,2> engaged{state_.ray_targets[0]>=0||state_.touches[0],state_.ray_targets[1]>=0||state_.touches[1]};
    state_.help_mode=enabled;inspection_toggle_=toggle_id;reset_input();need_clear_=engaged;if(recorder_&&state_.visible)recorder_->panel_inspection(recording_id_,enabled,toggle_id);
}
void PointingPanel::cancel(){if(recorder_&&state_.visible)recorder_->panel_cancel(recording_id_);state_.visible=false;reset_input();}
std::optional<int> PointingPanel::update(const XrRuntimeFrame& f){
    if(!state_.visible)return {};
    auto* recorder=recorder_;recorder_=nullptr;const auto result=update_impl(f);recorder_=recorder;
    if(recorder)recorder->panel_update(recording_id_,f,result,state_);return result;
}
std::optional<int> PointingPanel::update_impl(const XrRuntimeFrame& f){
    if(!state_.visible)return {};
    if(!head(f)||f.space_revision!=state_.space_revision){cancel();return {};}
    if(f.id==last_frame_)return {};
    if(f.id<last_frame_||f.predicted_display_time_ns<0||(last_time_&&f.predicted_display_time_ns<=*last_time_)){cancel();return {};}
    if(last_time_&&f.predicted_display_time_ns-*last_time_>100'000'000)reset_input();
    last_frame_=f.id;last_time_=f.predicted_display_time_ns;state_.rays={};state_.ray_targets={-1,-1};state_.touches={};
    if(pressed_until_&&f.predicted_display_time_ns>=*pressed_until_){state_.pressed=-1;pressed_until_.reset();}state_.dwell_progress=0;state_.direct_touch=state_.release_required=false;
    std::array<int,2> hovered{-1,-1},poked{-1,-1},dwelt{-1,-1};std::array<float,2> progress{};
    try{
        if(options_.follow_head)state_.panel=compose(gravity_aligned_origin({f.eyes[0].pose,f.eyes[1].pose}),Pose{{},options_.offset});
        const auto panel_inverse=inverse(state_.panel);
        for(unsigned h=0;h<2;++h){const auto& hand=f.hands[h];const auto& tip=hand.joints[xr_index_tip];const auto& proximal=hand.joints[xr_index_proximal];
            const bool tracked=hand.active&&usable(tip);
            if(!tracked){touch_[h]={};dwell_[h]={};need_clear_[h]=false;continue;}
            const auto b=compose(panel_inverse,Pose{{},tip.pose.position}).position;
            auto& touch=touch_[h];const int target=hit(state_,b);
            const auto previous_tip=touch.previous?std::optional<Vec3>(compose(panel_inverse,Pose{{},*touch.previous}).position):std::nullopt;
            if(b.z>=.035f){touch.armed=true;touch.latched=false;touch.approach=0;}
            else if(touch.previous){const auto delta=compose(Pose{panel_inverse.orientation,{}},Pose{{},sub(tip.pose.position,*touch.previous)}).position;
                if(std::hypot(delta.x,delta.y,delta.z)>.20f){touch.armed=false;touch.approach=0;}
                else touch.approach=std::max(0.f,touch.approach-delta.z);}
            touch.previous=tip.pose.position;
            const float radius=std::isfinite(tip.radius)&&tip.radius>0?std::clamp(tip.radius,.004f,.015f):.008f;
            // Sweep the fingertip segment through the contact plane. At a low
            // frame rate the finger can already be behind the thin panel.
            const float surface=radius+.002f;
            Vec3 contact=b;bool crossed=false;
            if(previous_tip&&previous_tip->z>surface&&b.z<=surface){
                const float fraction=(previous_tip->z-surface)/(previous_tip->z-b.z);
                contact={previous_tip->x+(b.x-previous_tip->x)*fraction,previous_tip->y+(b.y-previous_tip->y)*fraction,surface};crossed=true;
            }
            const int contact_target=hit(state_,contact);
            if(touch.armed&&!touch.latched&&touch.approach>=.012f&&contact_target>=0&&b.z<=surface&&(b.z>=-.035f||crossed)){
                poked[h]=contact_target;touch.latched=true;touch.armed=false;state_.touches[h]=true;state_.touch_local[h]={contact.x,contact.y,0};}
            if(target>=0&&b.z>=-.035f&&b.z<=.08f){hovered[h]=target;state_.touches[h]=true;state_.touch_local[h]={b.x,b.y,0};}
            if(!options_.allow_dwell||!usable(proximal)||b.z<=.08f)continue;
            const auto a=compose(panel_inverse,Pose{{},proximal.pose.position}).position;
            auto d=sub(b,a);const float length=std::hypot(d.x,d.y,d.z);if(!std::isfinite(length)||length<.01f)continue;
            d={d.x/length,d.y/length,d.z/length};if(d.z>=-.05f||b.z<=0)continue;
            const float distance=-b.z/d.z;if(distance>2.f)continue;
            Vec3 point{b.x+d.x*distance,b.y+d.y*distance,0};hovered[h]=hit(state_,point);
            // Keep the acquired button through millimetre-scale edge jitter.
            // A different actual hit always wins; no tolerance across rows.
            const int previous=dwell_[h].button;
            if(hovered[h]<0&&previous>=0){const auto& button=state_.buttons[static_cast<unsigned>(previous)];
                if((button.enabled||state_.help_mode)&&std::abs(point.x-button.x)<=button.width*.5f+.008f&&std::abs(point.y-button.y)<=button.height*.5f+.006f){
                    hovered[h]=previous;point.x=std::clamp(point.x,button.x-button.width*.5f,button.x+button.width*.5f);point.y=std::clamp(point.y,button.y-button.height*.5f,button.y+button.height*.5f);}}
            state_.ray_targets[h]=hovered[h];
            state_.rays[h]=true;state_.ray_start[h]=tip.pose.position;state_.ray_end[h]=compose(state_.panel,Pose{{},point}).position;
        }
        const auto hovered_hand=hovered[preferred_]>=0?preferred_:1u-preferred_;
        // Only the displayed progress owns dwell. A background pointer must
        // not activate another button while the user watches this one.
        const auto actionable=[&](int target){return target>=0&&state_.buttons[static_cast<unsigned>(target)].enabled&&(!state_.help_mode||state_.buttons[static_cast<unsigned>(target)].id==inspection_toggle_);};
        for(unsigned h=0;h<2;++h){auto& dwell=dwell_[h];const int target=h==hovered_hand&&state_.rays[h]?hovered[h]:-1;
            if(h!=hovered_hand)state_.ray_targets[h]=-1;
            if(hovered[h]<0)need_clear_[h]=false;
            if(need_clear_[h]||!actionable(poked[h]))poked[h]=-1;
            if(need_clear_[h]||(target>=0&&!actionable(target))){dwell={};continue;}
            if(target<0){
                // A short geometric miss pauses progress. Tracking loss, another
                // active pointer, a different target or a completed click resets it.
                if(options_.dwell_grace_ms&&state_.rays[h]&&hovered[0]<0&&hovered[1]<0&&dwell.last_hit&&!dwell.fired){
                    if(!dwell.missed)dwell.missed=f.predicted_display_time_ns;
                    if(f.predicted_display_time_ns-*dwell.missed<=static_cast<std::int64_t>(options_.dwell_grace_ms)*1'000'000)continue;
                }dwell={};continue;
            }
            if(target!=dwell.button||(dwell.missed&&f.predicted_display_time_ns-*dwell.missed>static_cast<std::int64_t>(options_.dwell_grace_ms)*1'000'000)){dwell={};dwell.button=target;dwell.began=f.predicted_display_time_ns;}
            else if(dwell.missed&&dwell.began&&dwell.last_hit)*dwell.began+=f.predicted_display_time_ns-*dwell.last_hit;
            dwell.missed.reset();dwell.last_hit=f.predicted_display_time_ns;
            if(target>=0&&dwell.began){progress[h]=std::clamp(static_cast<float>(f.predicted_display_time_ns-*dwell.began)/(static_cast<float>(options_.dwell_ms)*1'000'000.f),0.f,1.f);
                if(progress[h]>=1&&!dwell.fired){dwelt[h]=target;dwell.fired=true;}}
        }
        state_.hover=hovered[hovered_hand];state_.dwell_progress=progress[hovered_hand];
        state_.direct_touch=state_.hover>=0&&state_.touches[hovered_hand];
        state_.release_required=state_.hover>=0&&(need_clear_[hovered_hand]||dwell_[hovered_hand].fired||touch_[hovered_hand].latched);
        for(const auto* actions:{&poked,&dwelt}){const auto selected=(*actions)[preferred_]>=0?preferred_:1u-preferred_;const int target=(*actions)[selected];
            if(target>=0){state_.pressed=state_.hover=target;pressed_until_=f.predicted_display_time_ns+150'000'000;state_.direct_touch=actions==&poked;state_.release_required=true;state_.dwell_progress=progress[selected];return state_.buttons[static_cast<unsigned>(target)].id;}}
    }catch(...){cancel();}
    return {};
}
}
