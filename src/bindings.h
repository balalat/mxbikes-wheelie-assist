/*
Reads the player's own bindings for the assisted controls from the active profile's controls.txt, so the assist
works with whatever device and layout the player already uses.

controls.txt line formats (written by the game):
	NAME C_AXIS <uuid> <index> <sign> <deadzone> <linearity> ...
	NAME C_SLIDER <uuid> <index> <deadzone> <linearity> ...
	NAME C_BUTTON <uuid> <index> ...
	NAME C_POV <uuid> <direction 0=N 1=E 2=S 3=W> ...
	NAME KEY <scancode> [<scancode2>] <floats...>
Half-axis convention (inferred from the stock xinput.cfg / dualshock4.cfg, not yet verified in game):
	sign 0 = positive half of the axis, sign 1 = negative half.
*/
#pragma once
#include <windows.h>

typedef enum
{
	WA_BIND_NONE = 0,
	WA_BIND_AXIS,
	WA_BIND_SLIDER,
	WA_BIND_BUTTON,
	WA_BIND_POV,
	WA_BIND_KEY
} WABindKind;

typedef struct
{
	WABindKind kind;
	char uuid[64];			/* device id as written by the game, lower case */
	int index;				/* axis / slider / button index, POV direction, or first scancode */
	int sign;				/* C_AXIS only */
	int key2;				/* KEY only: second scancode, 0 if none */
} WABinding;

typedef struct
{
	WABinding throttle;
	WABinding rearBrake;
	WABinding lean;			/* CTRL_FBLEAN */
	char path[MAX_PATH];
	FILETIME mtime;
} WABindings;

/* (re)loads the bindings if controls.txt changed. Returns 1 if they were (re)loaded */
int wa_bindings_refresh(WABindings *b);

/* "C_AXIS 47f2... #3 sign 1" style description for the diag log */
void wa_binding_describe(const WABinding *b, char *out, int size);

/* 1 if both bindings read the same physical channel */
int wa_binding_same_channel(const WABinding *a, const WABinding *b);
