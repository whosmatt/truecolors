// snare.c
// Snare/clap/rim criteria: a sharp mid+treble attack that clearly outweighs
// the kick-band content. Applied by kick.c's state machine only when the
// kick classification fails, so cross-fire on kicks is ~0.4% (snare corpus
// in kick context, 2026-07-09; ~80% of snare-class hits detected). Snare
// hits are pattern anchors, not effect triggers.

// TODO: until coil whine is physically reduced, snare detection is going to remain pretty bad

#include "snare.h"

#ifndef SN_THRESH_ABS
#define SN_THRESH_ABS 0.35f  // mid+treble flux floor
#endif
#ifndef SN_RATIO
#define SN_RATIO      1.5f  // ...and it must outweigh the kick-band strength
#endif
#define W_FUND     1.4f
#define W_BODY     1.0f
#define W_CLICK    0.6f

bool snare_classify(const kick_in_t *a)
{
    float ks = W_FUND * a->flux[0] + W_BODY * a->flux[1] + W_CLICK * a->flux[2];
    float mt = a->mid_flux + a->treble_flux;
    return mt > SN_THRESH_ABS && mt > SN_RATIO * ks;
}
