# Disable motion blur (optional, off by default)

`[patches] disable_motion_blur = true` removes the game's full-screen motion
blur (the blur scripts set through Camera.SetBlur / SetBlurCut /
SetFullScreenMotionBlurCut). Default is `false`, so behavior is unchanged
unless the user turns it on.

## How

`sub_822A47C0` is the camera's per-frame update. It eases the camera's current
values toward their targets and then hands the current blur amount (camera
`+0xD4`) to the renderer: `lfs f12, 0xd4(r31)` at `0x822A49E8`, then
`stfs f12, 0x7c(r11)`. The mid-asm hook `fable2_hook_disable_motion_blur`
(after the load) zeroes `f12`, so the renderer always receives 0. The camera's
own value, and scripts reading it, are untouched. `f12` is not read again
before it is overwritten.

The first non-zero request is logged once
(`[motion-blur] game requested full-screen motion blur 0.300 (forced to 0)`),
which shows whether the game actually asked for blur.

Verified in game that the hook fires and the blur request is zeroed.
