#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

static float ini_float(const char *ini, const char *sec, const char *key, float def)
{
	char buf[64], defbuf[64];

	sprintf_s(defbuf, sizeof(defbuf), "%g", def);
	GetPrivateProfileStringA(sec, key, defbuf, buf, sizeof(buf), ini);
	return (float)atof(buf);
}

static int ini_int(const char *ini, const char *sec, const char *key, int def)
{
	return (int)GetPrivateProfileIntA(sec, key, def, ini);
}

static void ini_str(const char *ini, const char *sec, const char *key, const char *def, char *out, int size)
{
	char *p;

	GetPrivateProfileStringA(sec, key, def, out, size, ini);
	/* strip trailing comments and spaces */
	p = strchr(out, ';');
	if (p)
		*p = 0;
	for (p = out + strlen(out); p > out && (p[-1] == ' ' || p[-1] == '\t'); p--)
		p[-1] = 0;
}

const WAPreset wa_presets[WA_NUM_PRESETS - 1] =
{
	/* name			target	max		engage	look	kp		bstart	bgain	lean */
	{ "gentle",		25.0f,	50.0f,	5.0f,	0.40f,	0.045f,	3.0f,	0.08f,	0.08f },
	{ "normal",		35.0f,	65.0f,	6.0f,	0.30f,	0.035f,	6.0f,	0.06f,	0.06f },
	{ "high",		50.0f,	72.0f,	8.0f,	0.30f,	0.030f,	6.0f,	0.06f,	0.05f },
	{ "protection",	60.0f,	70.0f,	45.0f,	0.30f,	0.030f,	4.0f,	0.08f,	0.06f },
};

void wa_preset_apply(int index, WAParams *p)
{
	const WAPreset *s;

	if (index < 0 || index >= WA_NUM_PRESETS - 1)
		return;
	s = &wa_presets[index];
	p->target = s->target;
	p->maxAngle = s->maxAngle;
	p->engageAngle = s->engageAngle;
	p->lookahead = s->lookahead;
	p->kp = s->kp;
	p->brakeStart = s->brakeStart;
	p->brakeGain = s->brakeGain;
	p->leanGain = s->leanGain;
}

static int parse_preset(const char *s, int def)
{
	int i;

	for (i = 0; i < WA_NUM_PRESETS - 1; i++)
		if (!_stricmp(s, wa_presets[i].name))
			return i;
	if (!_stricmp(s, "custom"))
		return WA_NUM_PRESETS - 1;
	return def;
}

static int parse_button(const char *s, int def)
{
	static const struct { const char *name; int v; } map[] =
	{
		{ "none", WA_BTN_NONE },
		{ "start", 1 }, { "back", 2 }, { "ls", 3 }, { "rs", 4 }, { "lb", 5 }, { "rb", 6 },
		{ "a", 7 }, { "b", 8 }, { "x", 9 }, { "y", 10 },
		{ "dpad_up", WA_BTN_DPAD_UP }, { "dpad_right", WA_BTN_DPAD_RIGHT },
		{ "dpad_down", WA_BTN_DPAD_DOWN }, { "dpad_left", WA_BTN_DPAD_LEFT },
	};
	int i, n;

	for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
		if (!_stricmp(s, map[i].name))
			return map[i].v;
	n = atoi(s);
	return n >= 1 && n <= 10 ? n : !strcmp(s, "0") ? WA_BTN_NONE : def;
}

/* "f6", "k", "none" or a number (virtual-key code) */
static int parse_key(const char *s, int def)
{
	if (!_stricmp(s, "none") || !strcmp(s, "0"))
		return 0;
	if ((s[0] == 'f' || s[0] == 'F') && atoi(s + 1) >= 1 && atoi(s + 1) <= 24)
		return VK_F1 + atoi(s + 1) - 1;
	if (s[0] && !s[1] && ((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= '0' && s[0] <= '9')))
		return s[0] >= 'a' && s[0] <= 'z' ? s[0] - 'a' + 'A' : s[0];
	return atoi(s) > 0 ? atoi(s) : def;
}

void wa_config_load(WAConfig *c, char *iniPathOut, int iniPathSize)
{
	char ini[MAX_PATH], buf[64], *slash;
	HMODULE self = NULL;

	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&wa_config_load, &self);
	GetModuleFileNameA(self, ini, sizeof(ini));
	slash = strrchr(ini, '\\');
	strcpy_s(slash ? slash + 1 : ini, sizeof(ini) - (slash ? (slash + 1 - ini) : 0), "wheelie_assist.ini");
	if (iniPathOut)
		strcpy_s(iniPathOut, iniPathSize, ini);

	c->startEnabled = ini_int(ini, "assist", "enabled", 1);
	ini_str(ini, "assist", "start_preset", "normal", buf, sizeof(buf));
	c->startPreset = parse_preset(buf, 1);
	c->allowOnline = ini_int(ini, "assist", "allow_online", 0);
	c->assist.target = ini_float(ini, "assist", "target_angle", 35.0f);
	c->assist.maxAngle = ini_float(ini, "assist", "max_angle", 65.0f);
	c->assist.engageAngle = ini_float(ini, "assist", "engage_angle", 6.0f);
	c->assist.hover = ini_float(ini, "assist", "throttle_hover", 0.55f);
	c->assist.kp = ini_float(ini, "assist", "kp", 0.035f);
	c->assist.lookahead = ini_float(ini, "assist", "lookahead", 0.30f);
	c->assist.ki = ini_float(ini, "assist", "ki", 0.03f);
	c->assist.brakeStart = ini_float(ini, "assist", "brake_start", 6.0f);
	c->assist.brakeGain = ini_float(ini, "assist", "brake_gain", 0.06f);
	c->assist.minSpeed = ini_float(ini, "assist", "min_speed", 1.5f);
	c->assist.leanGain = ini_float(ini, "assist", "lean_gain", 0.06f);
	c->pitchSign = ini_float(ini, "assist", "pitch_sign", -1.0f) < 0 ? -1.0f : 1.0f;
	c->groundTau = ini_float(ini, "assist", "ground_tau", 0.3f);

	c->leanInvert = ini_int(ini, "input", "lean_invert", 0);
	ini_str(ini, "input", "toggle_button", "ls", buf, sizeof(buf));
	c->toggleButton = parse_button(buf, 3);
	ini_str(ini, "input", "preset_up_button", "dpad_up", buf, sizeof(buf));
	c->presetUpButton = parse_button(buf, WA_BTN_DPAD_UP);
	ini_str(ini, "input", "preset_down_button", "dpad_down", buf, sizeof(buf));
	c->presetDownButton = parse_button(buf, WA_BTN_DPAD_DOWN);
	ini_str(ini, "input", "toggle_key", "f6", buf, sizeof(buf));
	c->toggleKey = parse_key(buf, VK_F6);
	ini_str(ini, "input", "preset_down_key", "f7", buf, sizeof(buf));
	c->presetDownKey = parse_key(buf, VK_F7);
	ini_str(ini, "input", "preset_up_key", "f8", buf, sizeof(buf));
	c->presetUpKey = parse_key(buf, VK_F8);

	c->log = ini_int(ini, "log", "enabled", 1);
	c->hud = ini_int(ini, "hud", "enabled", 1);
	c->hudX = ini_float(ini, "hud", "x", 0.965f);
	c->hudY = ini_float(ini, "hud", "y", 0.30f);
	c->hudH = ini_float(ini, "hud", "height", 0.35f);
}
