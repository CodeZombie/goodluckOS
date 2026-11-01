export GALLIUM_HUD_TOGGLE_SIGNAL=34
export GALLIUM_HUD_VISIBLE=false
export GALLIUM_HUD=fps,cpu
# System Settings -> Overlay
grep -qs '^HUD_STATUS_BATTERY=true' /home/player/.config/gallium_hud.conf && export GALLIUM_HUD_STATUS=battery
grep -qs '^HUD_FEEDBACK=false' /home/player/.config/gallium_hud.conf && export GALLIUM_HUD_FEEDBACK=false
