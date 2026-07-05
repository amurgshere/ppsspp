# Debug visualization pattern for rendering fixes

Two practices for porting/adapting upstream rendering logic:

## 1. Live visual toggle, not just logs

When a rendering fix's effect is hard to see, add a live visual debug toggle (e.g. tint touched geometry a solid color) rather than only logging. Avoid env vars or log-level checks as the trigger — they force running at a costly verbosity/require a relaunch, and can't be flipped on/off live while playing.

**Preferred pattern:** tie the toggle to the *active* state of an existing user-facing mappable control (e.g. a `VIRTKEY_*` binding the user already has mapped) via a small global bool, set/cleared in the screen/input handler, independent of what that control's own setting/level is configured to. Zero runtime cost when inactive, decoupled from unrelated systems (e.g. log verbosity), and the user can leave the control bound and just press it to toggle live.

**How to apply:** before inventing a new mechanism (env var, ini setting, etc.) for a rendering-path fix that needs live on-screen confirmation, look for an existing user-facing toggle to piggyback on — ask what mechanism the user would want to drive it rather than assuming.

## 2. Verify hand-ports against upstream's own evidence

Don't trust a hand-ported algorithm is behaviorally equivalent to upstream just because it compiles and "does something" — verify against upstream's own evidence (PR screenshots, before/after) and test the exact scenario upstream validated, before generalizing to other games/cases speculatively.

A common failure mode: testing on cases that were never in upstream's validated list for a feature, and mistaking their unrelated visual artifacts (caused by a different, unrelated mechanism) for the bug the feature actually targets. Find and test the specific case from upstream's own PR/issue evidence first.
