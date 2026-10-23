/*
Wheelie assist control law. Pure code (no Windows / game dependencies) so it can be simulated offline.

It acts on three things only: rider lean front/back, throttle and rear brake.
- lean: shifted forward when the nose is predicted too high, back when it is predicted too low
- throttle: the rider's throttle capped by a limit computed from the predicted wheelie angle (never adds throttle)
- rear brake: added when the nose is predicted to overshoot the target
*/
#pragma once

typedef struct
{
	float target;			/* desired wheelie angle, degrees */
	float maxAngle;			/* above this: throttle cut, full forward lean and strong rear brake */
	float engageAngle;		/* below this the assist is idle */
	float hover;			/* throttle that roughly holds the target */
	float kp;				/* throttle reduction per degree of predicted error */
	float lookahead;		/* seconds. predicted angle = angle + lookahead * rate */
	float ki;				/* hover adaptation, throttle per degree-second */
	float brakeStart;		/* degrees of predicted overshoot before rear brake starts */
	float brakeGain;		/* rear brake per degree beyond brakeStart */
	float minSpeed;			/* m/s, below this the assist is idle */
	float leanGain;			/* lean added per degree of predicted error (+ = forward) */
} WAParams;

typedef struct
{
	float integ;
	int active;
	float cap;				/* last throttle cap, for logging */
} WAState;

typedef struct
{
	int valid;				/* telemetry is fresh */
	int inWheelie;
	float angle;			/* degrees, nose up positive, relative to ground */
	float rate;				/* deg/s, nose rising positive */
	float speed;			/* m/s */
} WABike;

void wa_assist_reset(WAState *s);
/* inputs 0..1, lean -1 (back) .. 1 (forward) */
typedef struct
{
	float throttle;
	float rearBrake;
	float lean;
} WAControls;

void wa_assist_step(const WAParams *p, WAState *s, const WABike *b, const WAControls *user, float dt, WAControls *out);
