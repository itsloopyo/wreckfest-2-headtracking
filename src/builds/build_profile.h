// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace wf2_ht::builds {

// Everything this mod pins to a specific Wreckfest2.exe build.
//
// Bugbear's engine ships full MSVC RTTI, so the camera CONTROLLER classes
// (romu::CarCamera and its siblings, romu::GarageCamera) have vtables that can
// be resolved from the PE's RTTI and pinned here by RVA. What RTTI cannot reach
// is the render camera itself: BCORE::Camera carries a vtable holding only its
// destructor, and neither the function that derives its view matrix nor the
// view manager update that drives it is virtual at all. Those are the pinned
// surface, along with the struct offsets the mod reads through.
struct OffsetTable {
    // The view manager's per-frame camera update. It interpolates the render
    // camera at view_manager_render_camera between the two camera states either
    // side of the current time, and then calls the view matrix derivation
    // below. The mod hooks it to take the previous frame's head pose back OUT
    // before the interpolation runs: on the branch where the target state is
    // still in the future the engine copies the render camera into its own
    // blend source, so a tracked rotation left in place is fed back in and
    // compounds frame on frame.
    //   void __fastcall Update(ViewManager* this, FrameContext* frame)
    unsigned int view_manager_update_rva;

    // BCORE::Camera's view matrix derivation, called once per rendered frame
    // from the view manager update above, and from nowhere else in the image.
    // It rolls the camera's eight-deep history of view matrices forward, writes
    // a fresh one by inverting the camera-to-world transform, and then rebuilds
    // the frustum from it. The head pose is composed into that transform
    // immediately before this runs, so the view matrix this frame is drawn
    // with, the history entry the next frame reprojects against, and the
    // frustum the frame is culled against are all built from the tracked eye.
    //   void __fastcall UpdateViewMatrix(Camera* this)
    unsigned int camera_view_matrix_rva;

    // Byte offset of the camera-to-world transform inside BCORE::Camera, and
    // the number of floats it occupies. Row-major: rows 0..2 are the camera's
    // right, up and forward axes, row 3 is its world position.
    unsigned int camera_world_transform;
    unsigned int camera_world_transform_floats;

    // Module-static pointer to the view manager (the object that owns the
    // camera controllers and the BCORE::Camera instances), and the byte offsets
    // of the fields the mod reads.
    unsigned int view_manager_ptr_rva;

    // The render camera is EMBEDDED in the view manager by value, not reached
    // through a pointer - the view manager's constructor runs BCORE::Camera's
    // constructor on four inline sub-objects, and this is the first of them.
    // So this offset is added to the view manager, never dereferenced. That is
    // the one structural difference from the Wreckfest 1 mod this is ported
    // from, where the same field was a pointer, and it is why the hook's
    // "is this still the camera I injected into" check compares against a fixed
    // address rather than re-reading a slot the engine can swap.
    unsigned int view_manager_render_camera;

    // Pointer to the camera controller currently driving the render camera, set
    // by the view manager's SetCameraType from a jump table over the camera
    // type enum. Null for the type values that drive no controller at all.
    unsigned int view_manager_active_controller;

    // Vtable RVAs of every camera controller class the view manager can make
    // active - the whole romu ingame camera family plus the two menu cameras.
    // The gameplay gate reads the active controller's vtable pointer and looks
    // it up here, which is one comparison per class on the render path and no
    // RTTI walk. GarageCamera and CarPaintCamera are the menus and the garage;
    // the rest are the race. A controller whose vtable is in neither list
    // leaves the gate reading unknown, so a class added by a patch cannot
    // quietly be treated as gameplay.
    unsigned int garage_camera_vtable_rva;
    unsigned int car_paint_camera_vtable_rva;
    unsigned int car_camera_vtable_rva;
    unsigned int free_camera_vtable_rva;
    unsigned int track_side_camera_vtable_rva;
    unsigned int animated_camera_vtable_rva;
    unsigned int photo_mode_camera_vtable_rva;

    // The gameplay gate reads the engine's RACE PHASE, and nothing else.
    //
    // The camera controller class cannot answer this at all, which is why any of
    // it is needed: Wreckfest 2's main menu renders through a romu::TrackSideCamera
    // and its car select, livery, pre-race paddock and post-race results screens
    // all render through a romu::CarCamera - the same two classes a race and a
    // replay use - so the gate would otherwise stand open over every menu.
    //
    // gameplay_state_ptr_rva is the module static holding the romu::GameplayState
    // singleton, and the two offsets below are fields inside it.
    //
    // race_phase_offset is a dword holding the engine's own race state machine.
    // Six values are written across the image and the transitions between them
    // are explicit in the code: the session begin path sets 1 and bumps the
    // session counter beside it, a loop over the participants then steps 1 -> 2,
    // the per-frame update steps 2 -> 3 the instant the countdown field below
    // reaches zero and calls the green-light setter in the same breath, and the
    // teardown path clears it to 0. race_phase_countdown and race_phase_racing
    // pin the two values head tracking follows through. Every menu observed on
    // this build sits at 0 or 1, including the ones a race camera renders.
    //
    // countdown_ms_offset is the signed millisecond counter the update adds the
    // frame delta to each tick. It is NEGATIVE for the length of the grid
    // countdown and reaches zero at the green light, which is what makes it the
    // proof that a phase read is landing on the right field rather than on
    // whatever a patch has moved there: a dword counting up at wall-clock rate
    // and stopping dead at zero is not something an unrelated field imitates.
    // Nothing gates on it - it is read for the log line.
    //
    // Two earlier terms were tried here and are gone, both because they answered
    // a different question than the one the gate asks:
    //
    // - A module-static green-light byte, ANDed with the frontend flag.
    //   That byte is the green light and nothing else, so head tracking could not
    //   come on until the lights went out.
    // - The frontend flag, a byte in this same singleton raised while the
    //   frontend owns the game. It is correct in single player and WRONG in
    //   multiplayer: it stays raised for the whole of an online race a player
    //   participates in, so ANDing it shut the gate through every multiplayer
    //   race while leaving it open for the same player spectating one. The phase
    //   already covers every menu it was there to catch.
    //
    // A phase that cannot be read reads as no gameplay, so an offset a patch has
    // moved leaves the gate SHUT. That is the right way round: a stale offset
    // costs head tracking, where the opposite would put it in the menus.
    unsigned int gameplay_state_ptr_rva;
    unsigned int race_phase_offset;
    unsigned int race_phase_countdown;
    unsigned int race_phase_racing;
    unsigned int countdown_ms_offset;

    // The engine's global pause byte, reached as
    // *(*(module + pause_state_ptr_rva) + pause_state_object) + pause_state_paused.
    // Written by the engine's SetPaused(paused, silent), which raises or clears
    // it and then fires the GameState_PauseEnter / GameState_PauseExit events
    // either side. The pause menu, and a window that loses focus, both reach it
    // through SetPaused.
    //
    // Deliberately NOT part of IsProfileComplete: a build whose pause chain has
    // not been derived leaves pause_state_ptr_rva zero, and the mod skips the
    // read and tracks as it did before rather than going dormant. Every failure
    // to read this reads as NOT paused - see IsGamePaused.
    unsigned int pause_state_ptr_rva;
    unsigned int pause_state_object;
    unsigned int pause_state_paused;

    // The multiplayer gate. Module-static pointers to the engine's romu::NetClient
    // and romu::NetServer singletons, and the offset of the connection-state
    // enum inside each. Both objects are constructed at startup and live for the
    // whole process, so their existence says nothing; only the state field does.
    //
    // net_client_session_lo/hi bound the inclusive range of client states the
    // engine's own "am I in a network session" predicate accepts. That predicate
    // is a four-instruction leaf function in the image - null check, load the
    // state, `(unsigned)(state - lo) <= (hi - lo)` - and this reproduces it by
    // reading the field rather than calling it, so a stale offset after a patch
    // cannot end with the mod jumping into whatever the address then holds.
    unsigned int net_client_ptr_rva;
    unsigned int net_client_state;
    unsigned int net_client_session_lo;
    unsigned int net_client_session_hi;
    unsigned int net_server_ptr_rva;
    unsigned int net_server_state;
};

struct BuildProfile {
    const char* Name;
    cameraunlock::memory::PeFingerprint Fingerprint;
    OffsetTable Offsets;
};

// A profile with no camera addresses is a placeholder landed ahead of the
// rederive: the fingerprint routes, but the mod must stay dormant.
inline bool IsProfileComplete(const BuildProfile& p) {
    return p.Offsets.view_manager_update_rva != 0
        && p.Offsets.camera_view_matrix_rva != 0
        && p.Offsets.camera_world_transform != 0
        && p.Offsets.camera_world_transform_floats != 0
        && p.Offsets.view_manager_ptr_rva != 0
        && p.Offsets.view_manager_render_camera != 0
        && p.Offsets.garage_camera_vtable_rva != 0
        && p.Offsets.gameplay_state_ptr_rva != 0
        && p.Offsets.race_phase_offset != 0
        && p.Offsets.net_client_ptr_rva != 0;
}

}  // namespace wf2_ht::builds
