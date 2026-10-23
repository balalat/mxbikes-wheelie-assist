#include "assist.h"

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

void wa_assist_reset(WAState *s)
{
	s->integ = 0.0f;
	s->active = 0;
	s->cap = 1.0f;
}

void wa_assist_step(const WAParams *p, WAState *s, const WABike *b, const WAControls *user, float dt, WAControls *out)
{
	float err, cap, thr, brake, lean;

	*out = *user;
	s->active = 0;
	s->cap = 1.0f;

	if (!b->valid || !b->inWheelie || b->speed < p->minSpeed || b->angle < p->engageAngle)
	{
		/* bleed the adaptation off between wheelies */
		s->integ *= 0.98f;
		return;
	}

	/* predicted error: positive means the nose will be too high */
	err = (b->angle - p->target) + p->lookahead * b->rate;

	/* lean: forward when too high, back when too low */
	lean = clampf(user->lean + p->leanGain * err, -1.0f, 1.0f);

	cap = p->hover + s->integ - p->kp * err;
	/* only adapt while the cap is what limits the throttle (anti-windup) */
	if (user->throttle > cap)
		s->integ = clampf(s->integ - p->ki * err * dt, -0.4f, 0.4f);
	cap = clampf(cap, 0.0f, 1.0f);

	thr = user->throttle < cap ? user->throttle : cap;

	brake = 0.0f;
	if (err > p->brakeStart)
		brake = clampf(p->brakeGain * (err - p->brakeStart), 0.0f, 1.0f);

	if (b->angle > p->maxAngle)
	{
		thr = 0.0f;
		lean = 1.0f;
		if (brake < 0.8f)
			brake = 0.8f;
	}
	if (brake < user->rearBrake)
		brake = user->rearBrake;

	s->cap = cap;
	s->active = thr < user->throttle - 0.01f || brake > user->rearBrake + 0.01f || lean - user->lean > 0.05f || user->lean - lean > 0.05f;
	out->throttle = thr;
	out->rearBrake = brake;
	out->lean = lean;
}
