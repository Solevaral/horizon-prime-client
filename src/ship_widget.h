#pragma once

// Pixel-voxel ship widget — a low-res 3D model of the player's ship rendered
// with a real perspective camera (immediate-mode GL), then deliberately
// down-sampled so it reads as chunky pixel-art rather than smooth 3D. It slowly
// auto-rotates and animates per the current ship activity (idle drift, mining
// drill flicker, warp thruster pulse) sourced from g_ship in state.h.
//
// Drawn into an arbitrary screen rectangle so it can live as a docked HUD
// widget in the top-right corner, on top of the terminal.

// Render the ship into the rectangle (x,y,w,h) in framebuffer pixels.
// `dt` advances the local animation clock.
void ship_widget_render(float x, float y, float w, float h, float dt);
