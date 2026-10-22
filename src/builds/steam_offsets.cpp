// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_profile.h"

// Every Steam build profile lives here, append-only. Never edit an existing
// profile's numbers to "fix" a patch and never delete one: a user who has held
// back on an older build must keep matching their old profile from the same
// mod binary. Adding a profile is the only correct response to a patch.

namespace wf2_ht::builds {

// Wreckfest 2, Steam app 1203190, Wreckfest2.exe built 2026-10-22 12:51:54 UTC.
// Code and statics moved; the view manager grew 0x10 bytes ahead of its
// embedded cameras, so the render camera sits at +0x1350. GameplayState, the
// pause chain and the network state fields kept their layout. Derived by the
// same route as the profiles below, recorded in .lab/NOTES.md.
extern const BuildProfile kSteamProfile_20261022 = {
    "steam-win64-20261022",
    { 0x6ADA06EA, 0x04F75000, 0x00000000 },
    {
        /* view_manager_update_rva         */ 0x000D1AF0,
        /* camera_view_matrix_rva          */ 0x00B0C480,
        /* camera_world_transform          */ 0x10,
        /* camera_world_transform_floats   */ 16,
        /* view_manager_ptr_rva            */ 0x015C9408,
        /* view_manager_render_camera      */ 0x1350,
        /* view_manager_active_controller  */ 0x28,
        /* garage_camera_vtable_rva        */ 0x00D42BF0,
        /* car_paint_camera_vtable_rva     */ 0x00D3E7D0,
        /* car_camera_vtable_rva           */ 0x00D3E6C0,
        /* free_camera_vtable_rva          */ 0x00D41808,
        /* track_side_camera_vtable_rva    */ 0x00D42830,
        /* animated_camera_vtable_rva      */ 0x00D42BC0,
        /* photo_mode_camera_vtable_rva    */ 0x00D42C68,
        /* gameplay_state_ptr_rva          */ 0x015C9418,
        /* race_phase_offset               */ 0x15D0,
        /* race_phase_countdown            */ 2,
        /* race_phase_racing               */ 3,
        /* countdown_ms_offset             */ 0x1574,
        /* pause_state_ptr_rva             */ 0x015C90C8,
        /* pause_state_object              */ 0x70,
        /* pause_state_paused              */ 0x59,
        /* net_client_ptr_rva              */ 0x015C9428,
        /* net_client_state                */ 0x370,
        /* net_client_session_lo           */ 3,
        /* net_client_session_hi           */ 6,
        /* net_server_ptr_rva              */ 0x015C9430,
        /* net_server_state                */ 0x108,
    },
};


// Wreckfest 2, Steam app 1203190, Wreckfest2.exe built 2026-10-15 09:45:59 UTC.
// The patch moved more than code: the view manager grew 0x130 bytes ahead of its
// embedded cameras, so the render camera sits at +0x1340; GameplayState grew,
// moving the race phase and the countdown; and the pause chain's object and flag
// moved by 0x10. The BCORE::Camera layout, the race phase values and the network
// state fields are unchanged. Derived by the same route as the profile below,
// recorded in .lab/NOTES.md.
extern const BuildProfile kSteamProfile_20261015 = {
    "steam-win64-20261015",
    { 0x6AD0A0D7, 0x04F67000, 0x00000000 },
    {
        /* view_manager_update_rva         */ 0x000D1670,
        /* camera_view_matrix_rva          */ 0x00B02020,
        /* camera_world_transform          */ 0x10,
        /* camera_world_transform_floats   */ 16,
        /* view_manager_ptr_rva            */ 0x015BBE88,
        /* view_manager_render_camera      */ 0x1340,
        /* view_manager_active_controller  */ 0x28,
        /* garage_camera_vtable_rva        */ 0x00D37598,
        /* car_paint_camera_vtable_rva     */ 0x00D33458,
        /* car_camera_vtable_rva           */ 0x00D33340,
        /* free_camera_vtable_rva          */ 0x00D36310,
        /* track_side_camera_vtable_rva    */ 0x00D37238,
        /* animated_camera_vtable_rva      */ 0x00D37568,
        /* photo_mode_camera_vtable_rva    */ 0x00D37608,
        /* gameplay_state_ptr_rva          */ 0x015BBE98,
        /* race_phase_offset               */ 0x15D0,
        /* race_phase_countdown            */ 2,
        /* race_phase_racing               */ 3,
        /* countdown_ms_offset             */ 0x1574,
        /* pause_state_ptr_rva             */ 0x015BBB48,
        /* pause_state_object              */ 0x70,
        /* pause_state_paused              */ 0x59,
        /* net_client_ptr_rva              */ 0x015BBEA8,
        /* net_client_state                */ 0x370,
        /* net_client_session_lo           */ 3,
        /* net_client_session_hi           */ 6,
        /* net_server_ptr_rva              */ 0x015BBEB0,
        /* net_server_state                */ 0x108,
    },
};


// Wreckfest 2, Steam app 1203190, Wreckfest2.exe built 2026-07-09 12:50:52 UTC.
// CheckSum is 0 in the shipped EXE - the linker never stamped one - so the
// fingerprint leans on TimeDateStamp + SizeOfImage, exactly as Wreckfest 1's
// does.
//
// The gate fields changed shape on 2026-09-05 without a game patch: the race
// phase enum replaced the green-light byte and the frontend flag the gate used
// to AND together, so that head tracking follows through the grid countdown and
// through a multiplayer race the player is driving in.
// Nothing that was already here moved - the numbers this profile had before are
// either unchanged or gone, and no RVA was rewritten. That distinction is the
// whole of the append-only rule: adding a field the mod has learned to read is
// not the thing it forbids, which is answering a patch by editing a shipped
// profile's numbers and stranding everyone still on that build.
//
// Every RVA here was read out of the shipped EXE rather than guessed: the
// controller vtables come from the PE's own RTTI (type descriptor -> complete
// object locator -> the vtable slot that points back at it), the two hooked
// functions from the view-matrix history roll that is unique in the image and
// the .pdata entry that bounds it, and the statics from the RIP-relative store
// in the singleton factory that fills each one.
// The CheckSum is 0 because Bugbear's linker never stamped one, the same as
// Wreckfest 1, so only two of the three fingerprint fields carry information
// here. That is enough to route a build - no two shipped builds share a
// TimeDateStamp and a SizeOfImage - but it is not enough to notice an EXE
// patched in place, which keeps both. Nothing in the mod claims otherwise.
extern const BuildProfile kSteamProfile_20260709 = {
    "steam-win64-20260709",
    { 0x6A4F992C, 0x04C9B000, 0x00000000 },
    {
        /* view_manager_update_rva         */ 0x000C7BD0,
        /* camera_view_matrix_rva          */ 0x00A22C90,
        /* camera_world_transform          */ 0x10,
        /* camera_world_transform_floats   */ 16,
        /* view_manager_ptr_rva            */ 0x013FEE48,
        /* view_manager_render_camera      */ 0x1210,
        /* view_manager_active_controller  */ 0x28,
        /* garage_camera_vtable_rva        */ 0x00C46778,
        /* car_paint_camera_vtable_rva     */ 0x00C43050,
        /* car_camera_vtable_rva           */ 0x00C42EC0,
        /* free_camera_vtable_rva          */ 0x00C455C0,
        /* track_side_camera_vtable_rva    */ 0x00C46438,
        /* animated_camera_vtable_rva      */ 0x00C46748,
        /* photo_mode_camera_vtable_rva    */ 0x00C467E8,
        /* gameplay_state_ptr_rva          */ 0x013FEE58,
        /* race_phase_offset               */ 0x13F8,
        /* race_phase_countdown            */ 2,
        /* race_phase_racing               */ 3,
        /* countdown_ms_offset             */ 0x139C,
        /* pause_state_ptr_rva             */ 0x013FEB08,
        /* pause_state_object              */ 0x60,
        /* pause_state_paused              */ 0x49,
        /* net_client_ptr_rva              */ 0x013FEE68,
        /* net_client_state                */ 0x370,
        /* net_client_session_lo           */ 3,
        /* net_client_session_hi           */ 6,
        /* net_server_ptr_rva              */ 0x013FEE70,
        /* net_server_state                */ 0x108,
    },
};

}  // namespace wf2_ht::builds
