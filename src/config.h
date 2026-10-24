#pragma once
#include "assist.h"

/* pad hotkeys: 1..10 = XInput buttons (Start, Back, LS, RS, LB, RB, A, B, X, Y), then D-pad directions */
#define WA_BTN_NONE			0
#define WA_BTN_DPAD_UP		101
#define WA_BTN_DPAD_RIGHT	102
#define WA_BTN_DPAD_DOWN	103
#define WA_BTN_DPAD_LEFT	104

/* assist presets cycled in game. The last one ("custom") uses the [assist] values of the ini */
typedef struct
{
	const char *name;
	float target, maxAngle, engageAngle, lookahead, kp, brakeStart, brakeGain, leanStart, leanGain;
} WAPreset;

#define WA_NUM_PRESETS 5
extern const WAPreset wa_presets[WA_NUM_PRESETS - 1];

/* copies preset `index` over the matching fields of `p` (index WA_NUM_PRESETS - 1 = keep ini values) */
void wa_preset_apply(int index, WAParams *p);

typedef struct
{
	WAParams assist;
	int startEnabled;
	int startPreset;			/* 0-based */
	int allowOnline;			/* 0 = assist forced off on online servers */
	float pitchSign;
	float groundTau;			/* seconds, ground pitch low-pass while both wheels touch */

	int leanInvert;				/* flip the assumed forward direction of the CTRL_FBLEAN binding */
	int toggleButton;			/* pad hotkeys, WA_BTN_* or 1..10 */
	int presetUpButton;
	int presetDownButton;
	int toggleKey;				/* keyboard hotkeys, Windows virtual-key codes, 0 = none */
	int presetUpKey;
	int presetDownKey;

	int log;
	int hud;
	float hudX, hudY, hudH;
} WAConfig;

/* loads wheelie_assist.ini next to the module containing this code. Missing keys get defaults */
void wa_config_load(WAConfig *c, char *iniPathOut, int iniPathSize);
