#include "xr_policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurora::xr::policy {

RoomPref parse_room_pref(const char* env) noexcept {
  if (env == nullptr || *env == '\0')
    return RoomPref::Auto;
  const std::string_view v{env};
  if (v == "fb")
    return RoomPref::Fb;
  if (v == "blend" || v == "alpha")
    return RoomPref::Blend;
  if (v == "none" || v == "0")
    return RoomPref::None;
  return RoomPref::Auto;
}

bool try_fb_layer(RoomPref pref) noexcept { return pref == RoomPref::Auto || pref == RoomPref::Fb; }

Room room_mode(bool fbLayer, bool alphaOffered, RoomPref pref) noexcept {
  switch (pref) {
  case RoomPref::None:
    return Room::None;
  case RoomPref::Fb:
    return fbLayer ? Room::Fb : Room::None;
  case RoomPref::Blend:
    return alphaOffered ? Room::Alpha : Room::None;
  case RoomPref::Auto:
    break;
  }
  if (fbLayer)
    return Room::Fb;
  return alphaOffered ? Room::Alpha : Room::None;
}

float display_rate(float reportedHz, int64_t periodNs) noexcept {
  const float fromPeriod = periodNs > 0 ? static_cast<float>(1e9 / static_cast<double>(periodNs)) : 0.f;
  if (reportedHz > 0.f && (fromPeriod <= 0.f || std::abs(reportedHz - fromPeriod) <= reportedHz * 0.1f))
    return reportedHz;
  if (fromPeriod > 0.f)
    return fromPeriod;
  return 60.f;
}

int per_game_frame(float rateHz) noexcept {
  const float ratio = rateHz / 60.f;
  return ratio >= 0.98f && std::abs(ratio - std::round(ratio)) < 0.02f ? static_cast<int>(std::round(ratio)) : 0;
}

float pick_refresh(const std::vector<float>& rates, float wantedHz) noexcept {
  if (rates.size() < 2)
    return 0.f;
  const auto offered = [&](float want) {
    return std::any_of(rates.begin(), rates.end(), [&](float r) { return std::abs(r - want) < 0.5f; });
  };
  if (wantedHz > 0.f && offered(wantedHz))
    return wantedHz;
  if (offered(60.f))
    return 60.f;
  if (offered(120.f))
    return 120.f;
  return 0.f;
}

QueueMode queue_mode(uint32_t extraQueues, bool forceShared) noexcept {
  return extraQueues == 0 || forceShared ? QueueMode::Shared : QueueMode::Separate;
}

Profile classify_profile(std::string_view path) noexcept {
  if (path.empty())
    return Profile::None;
  if (path == "/interaction_profiles/oculus/touch_controller" ||
      path == "/interaction_profiles/meta/touch_controller_plus" ||
      path == "/interaction_profiles/meta/touch_pro_controller")
    return Profile::Touch;
  if (path == "/interaction_profiles/valve/frame_controller_valve")
    return Profile::Frame;
  if (path == "/interaction_profiles/khr/simple_controller")
    return Profile::Simple;
  if (path == "/interaction_profiles/ext/hand_interaction_ext")
    return Profile::HandInteraction;
  return Profile::OtherController;
}

bool joints_are_hand(Profile current) noexcept {
  return current != Profile::Frame && current != Profile::OtherController;
}

std::vector<Binding> touch_bindings(bool haptics) {
  std::vector<Binding> b{
      {Act::Stick, "/user/hand/left/input/thumbstick"},
      {Act::CStick, "/user/hand/right/input/thumbstick"},
      {Act::A, "/user/hand/right/input/a/click"},
      {Act::B, "/user/hand/right/input/b/click"},
      {Act::X, "/user/hand/left/input/x/click"},
      {Act::Y, "/user/hand/left/input/y/click"},
      {Act::Z, "/user/hand/left/input/squeeze/value"},
      {Act::Z, "/user/hand/right/input/squeeze/value"},
      {Act::Start, "/user/hand/left/input/menu/click"},
      {Act::L, "/user/hand/left/input/trigger/value"},
      {Act::R, "/user/hand/right/input/trigger/value"},
      {Act::AimL, "/user/hand/left/input/aim/pose"},
      {Act::AimR, "/user/hand/right/input/aim/pose"},
      {Act::GrabL, "/user/hand/left/input/squeeze/value"},
      {Act::GrabR, "/user/hand/right/input/squeeze/value"},
  };
  if (haptics) {
    b.push_back({Act::RumbleL, "/user/hand/left/output/haptic"});
    b.push_back({Act::RumbleR, "/user/hand/right/output/haptic"});
  }
  return b;
}

std::vector<std::vector<Binding>> frame_binding_variants() {
  const auto variant = [](const char* shoulder, bool dpad, bool haptics) {
    std::vector<Binding> b{
        {Act::Stick, "/user/hand/left/input/thumbstick"},     {Act::CStick, "/user/hand/right/input/thumbstick"},
        {Act::A, "/user/hand/right/input/a/click"},           {Act::B, "/user/hand/right/input/b/click"},
        {Act::X, "/user/hand/right/input/x/click"},           {Act::Y, "/user/hand/right/input/y/click"},
        {Act::Start, "/user/hand/right/input/menu/click"},    {Act::L, "/user/hand/left/input/trigger/value"},
        {Act::R, "/user/hand/right/input/trigger/value"},     {Act::AimL, "/user/hand/left/input/aim/pose"},
        {Act::AimR, "/user/hand/right/input/aim/pose"},       {Act::GrabL, "/user/hand/left/input/squeeze/value"},
        {Act::GrabR, "/user/hand/right/input/squeeze/value"},
    };
    // Both hands' shoulder buttons press Z (the GameCube's is a shoulder button).
    if (shoulder != nullptr) {
      const bool bumper = std::strcmp(shoulder, "bumper") == 0;
      b.push_back({Act::Z, bumper ? "/user/hand/left/input/bumper/click" : "/user/hand/left/input/shoulder/click"});
      b.push_back({Act::Z, bumper ? "/user/hand/right/input/bumper/click" : "/user/hand/right/input/shoulder/click"});
    }
    if (dpad) {
      b.push_back({Act::DUp, "/user/hand/left/input/dpad_up/click"});
      b.push_back({Act::DDown, "/user/hand/left/input/dpad_down/click"});
      b.push_back({Act::DLeft, "/user/hand/left/input/dpad_left/click"});
      b.push_back({Act::DRight, "/user/hand/left/input/dpad_right/click"});
    }
    if (haptics) {
      b.push_back({Act::RumbleL, "/user/hand/left/output/haptic"});
      b.push_back({Act::RumbleR, "/user/hand/right/output/haptic"});
    }
    return b;
  };
  // Valve's documentation names the shoulder buttons bumper; SteamVR 2.17.10
  // calls them shoulder and still accepts bumper.
  return {
      variant("bumper", true, true),    variant("shoulder", true, true), variant("bumper", false, true),
      variant("shoulder", false, true), variant(nullptr, false, true),   variant("bumper", true, false),
      variant("shoulder", true, false), variant(nullptr, false, false),
  };
}

bool swapchain_admit(int live, int budget, bool required) noexcept { return required || budget <= 0 || live < budget; }

} // namespace aurora::xr::policy
