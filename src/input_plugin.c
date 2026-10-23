/*
wheelie_assist64.dli - MX Bikes input plugin (interface version 4), "transparent" mode.

It exposes no controller. Instead it reads the player's own bindings for CTRL_THROTTLE, CTRL_REARBRAKE and
CTRL_FBLEAN from controls.txt and edits those channels where the game reads them:
	- XInput pads: XInputGetState as imported by the stock xinput64.dli plugin (import table patch)
	- DirectInput devices (keyboard, DS4 without Steam Input, wheels...): IDirectInputDevice8::GetDeviceState
	  (vtable patch, shared by every device the game creates)
When the assist is idle, data passes through untouched.
*/
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <xinput.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "input_plugin.h"
#include "shared.h"
#include "config.h"
#include "assist.h"
#include "bindings.h"
#include "diag.h"

#define EXPORT __declspec(dllexport)

#define COMPUTE_INTERVAL_MS	2.0
#define BINDINGS_CHECK_MS	1000
#define MAX_DI_DEVICES		16

/* UUIDs the stock xinput64.dli gives to pads 1..4 */
static const char *g_xinputUUID[4] =
{
	"47f219dc-2cb8-4afc-b670-56c637f64a5a", "9ade649d-3e55-4b84-a401-8df5d643b5af",
	"77149f68-773e-42d1-b31d-d74ad60ea477", "2eb88625-66b7-4c43-bf58-00b98906c06e"
};
/* stock xinput64.dli button order */
static const WORD g_xinputButtons[10] =
{
	XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
	XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
	XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y
};
static const WORD g_dpadBits[4] = { XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_DOWN,
	XINPUT_GAMEPAD_DPAD_LEFT };

static WAConfig g_cfg;
static WAShared *g_shm;
static HANDLE g_hMap;
static WABindings g_bind;
static ULONGLONG g_bindCheckTick;

static WAState g_assist;
static int g_enabled;
static int g_preset;
static LARGE_INTEGER g_qpf, g_lastCompute;

/******************************************************************************
controls: user values read from the bound channels, assisted values written back
******************************************************************************/

enum { C_THROTTLE, C_REARBRAKE, C_LEAN, C_COUNT };

typedef struct
{
	const WABinding *bind;
	int bipolar;			/* lean: -1 back .. 1 forward */
	float user;				/* last value read from the device */
	ULONGLONG seenTick;		/* when it was last read */
	float out;
	float sigma;			/* sigma-delta accumulator for digital channels */
	int restKnown;			/* DirectInput axis rest position detection */
	LONG rest;
} Control;

static Control g_ctl[C_COUNT];

static void controls_bind(void)
{
	char desc[128];
	static const char *names[C_COUNT] = { "throttle", "rear brake", "lean F/B" };
	int i;

	g_ctl[C_THROTTLE].bind = &g_bind.throttle;
	g_ctl[C_REARBRAKE].bind = &g_bind.rearBrake;
	g_ctl[C_LEAN].bind = &g_bind.lean;
	g_ctl[C_LEAN].bipolar = 1;
	for (i = 0; i < C_COUNT; i++)
	{
		g_ctl[i].user = g_ctl[i].out = g_ctl[i].sigma = 0.0f;
		g_ctl[i].seenTick = 0;
		g_ctl[i].restKnown = 0;
		wa_binding_describe(g_ctl[i].bind, desc, sizeof(desc));
		wa_diag("binding %s: %s", names[i], desc);
	}
	if (wa_binding_same_channel(&g_bind.lean, &g_bind.rearBrake))
		wa_diag("note: lean F/B and rear brake share a channel; lean wins unless above max angle");
}

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* runs the assist at most every COMPUTE_INTERVAL_MS using the latest user values */
static void compute(void)
{
	LARGE_INTEGER now;
	WAShared *s = g_shm;
	WABike bike;
	WAParams params;
	WAControls user, out;
	ULONGLONG tick = GetTickCount64();
	int i, online, active;
	float dt;

	QueryPerformanceCounter(&now);
	if (g_lastCompute.QuadPart && (now.QuadPart - g_lastCompute.QuadPart) * 1000.0 / g_qpf.QuadPart < COMPUTE_INTERVAL_MS)
		return;
	dt = g_lastCompute.QuadPart ? (float)((now.QuadPart - g_lastCompute.QuadPart) / (double)g_qpf.QuadPart) : 0.01f;
	dt = clampf(dt, 0.001f, 0.05f);
	g_lastCompute = now;

	user.throttle = g_ctl[C_THROTTLE].user;
	user.rearBrake = g_ctl[C_REARBRAKE].user;
	user.lean = g_ctl[C_LEAN].user;
	out = user;

	ZeroMemory(&bike, sizeof(bike));
	online = 0;
	if (s)
	{
		bike.valid = s->onTrack && tick - s->telemetryTick < 150;
		bike.inWheelie = s->inWheelie;
		bike.angle = s->wheelieAngle;
		bike.rate = s->wheelieRate;
		bike.speed = s->speed;
		online = s->online && !g_cfg.allowOnline;
	}
	params = g_cfg.assist;
	wa_preset_apply(g_preset, &params);
	active = g_enabled && !online;
	if (active)
		wa_assist_step(&params, &g_assist, &bike, &user, dt, &out);
	else
		g_assist.active = 0;

	g_ctl[C_THROTTLE].out = out.throttle;
	g_ctl[C_REARBRAKE].out = out.rearBrake;
	g_ctl[C_LEAN].out = out.lean;

	if (s)
	{
		int seen = 0;

		for (i = 0; i < C_COUNT; i++)
			if (g_ctl[i].bind->kind != WA_BIND_NONE && tick - g_ctl[i].seenTick < 1000)
				seen++;
		s->deviceConnected = seen > 0;
		s->assistEnabled = active;
		s->assistActive = g_assist.active;
		s->target = params.target;
		s->preset = g_preset;
		s->userThrottle = user.throttle;
		s->outThrottle = out.throttle;
		s->userRearBrake = user.rearBrake;
		s->outRearBrake = out.rearBrake;
		s->userLean = user.lean;
		s->outLean = out.lean;
		InterlockedIncrement(&s->inputSeq);
	}
}

/* digital output for an analog request: on/off pattern whose average is v */
static int sigma_delta(Control *c, float v)
{
	c->sigma += clampf(v, 0.0f, 1.0f);
	if (c->sigma >= 1.0f)
	{
		c->sigma -= 1.0f;
		return 1;
	}
	return 0;
}

/* lean F/B: positive raw = forward for sign 1 (what the game writes for a normal binding), flipped otherwise */
static float lean_dir(const WABinding *b)
{
	float d = (b->kind == WA_BIND_AXIS && b->sign != 1) ? -1.0f : 1.0f;
	return g_cfg.leanInvert ? -d : d;
}

/******************************************************************************
hotkeys
******************************************************************************/

static void hotkey_toggle(void)
{
	g_enabled = !g_enabled;
	wa_assist_reset(&g_assist);
	wa_diag("assist %s", g_enabled ? "enabled" : "disabled");
}

static void hotkey_preset(int delta)
{
	int p = g_preset + delta;

	if (p < 0 || p >= WA_NUM_PRESETS)
		return;
	g_preset = p;
	wa_assist_reset(&g_assist);
	wa_diag("preset %d", g_preset);
}

static int pad_pressed(WORD buttons, int hotkey)
{
	if (hotkey >= 1 && hotkey <= 10)
		return (buttons & g_xinputButtons[hotkey - 1]) != 0;
	if (hotkey >= WA_BTN_DPAD_UP && hotkey <= WA_BTN_DPAD_LEFT)
		return (buttons & g_dpadBits[hotkey - WA_BTN_DPAD_UP]) != 0;
	return 0;
}

static void pad_hotkeys(WORD buttons)
{
	static WORD prev;
	WORD down = buttons & ~prev;

	prev = buttons;
	if (!down)
		return;
	if (pad_pressed(down, g_cfg.toggleButton))
		hotkey_toggle();
	if (pad_pressed(down, g_cfg.presetUpButton))
		hotkey_preset(1);
	if (pad_pressed(down, g_cfg.presetDownButton))
		hotkey_preset(-1);
}

static int game_focused(void)
{
	DWORD pid = 0;

	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	return pid == GetCurrentProcessId();
}

static void key_hotkeys(void)
{
	static int prev[3];
	int keys[3], now[3], i;

	keys[0] = g_cfg.toggleKey;
	keys[1] = g_cfg.presetUpKey;
	keys[2] = g_cfg.presetDownKey;
	for (i = 0; i < 3; i++)
		now[i] = keys[i] && game_focused() && (GetAsyncKeyState(keys[i]) & 0x8000);
	if (now[0] && !prev[0])
		hotkey_toggle();
	if (now[1] && !prev[1])
		hotkey_preset(1);
	if (now[2] && !prev[2])
		hotkey_preset(-1);
	memcpy(prev, now, sizeof(prev));
}

/******************************************************************************
XInput channels
******************************************************************************/

static int xinput_pad_of(const WABinding *b)
{
	int i;

	if (b->kind == WA_BIND_NONE || b->kind == WA_BIND_KEY)
		return -1;
	for (i = 0; i < 4; i++)
		if (!strcmp(b->uuid, g_xinputUUID[i]))
			return i;
	return -1;
}

static float xi_axis(const XINPUT_GAMEPAD *g, int idx)
{
	SHORT v = idx == 0 ? g->sThumbLX : idx == 1 ? g->sThumbLY : idx == 2 ? g->sThumbRX : g->sThumbRY;
	return clampf(v / 32767.0f, -1.0f, 1.0f);
}

static void xi_set_axis(XINPUT_GAMEPAD *g, int idx, float v)
{
	SHORT s = (SHORT)(clampf(v, -1.0f, 1.0f) * 32767.0f);

	if (idx == 0) g->sThumbLX = s;
	else if (idx == 1) g->sThumbLY = s;
	else if (idx == 2) g->sThumbRX = s;
	else g->sThumbRY = s;
}

static WORD xi_bit(const WABinding *b)
{
	if (b->kind == WA_BIND_BUTTON && b->index >= 0 && b->index < 10)
		return g_xinputButtons[b->index];
	if (b->kind == WA_BIND_POV && b->index >= 0 && b->index < 4)
		return g_dpadBits[b->index];
	return 0;
}

/* raw value of the control: 0..1 (unipolar) or -1..1 (lean) */
static int xi_read(const Control *c, const XINPUT_GAMEPAD *g, float *v)
{
	const WABinding *b = c->bind;
	float a;

	switch (b->kind)
	{
	case WA_BIND_AXIS:
		if (b->index < 0 || b->index > 3)
			return 0;
		a = xi_axis(g, b->index);
		*v = c->bipolar ? a * lean_dir(b) : b->sign == 1 ? clampf(-a, 0.0f, 1.0f) : clampf(a, 0.0f, 1.0f);
		return 1;
	case WA_BIND_SLIDER:
		if (b->index < 0 || b->index > 1)
			return 0;
		*v = (b->index ? g->bRightTrigger : g->bLeftTrigger) / 255.0f;
		return 1;
	case WA_BIND_BUTTON:
	case WA_BIND_POV:
		if (!xi_bit(b))
			return 0;
		*v = (g->wButtons & xi_bit(b)) ? 1.0f : 0.0f;
		return 1;
	default:
		return 0;
	}
}

static void xi_write(Control *c, XINPUT_GAMEPAD *g)
{
	const WABinding *b = c->bind;
	float v = c->out;

	switch (b->kind)
	{
	case WA_BIND_AXIS:
		if (c->bipolar)
			xi_set_axis(g, b->index, v * lean_dir(b));
		else
			xi_set_axis(g, b->index, b->sign == 1 ? -v : v);
		break;
	case WA_BIND_SLIDER:
		if (b->index)
			g->bRightTrigger = (BYTE)(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f);
		else
			g->bLeftTrigger = (BYTE)(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f);
		break;
	case WA_BIND_BUTTON:
	case WA_BIND_POV:
		if (sigma_delta(c, c->bipolar ? fabsf(v) : v))
			g->wButtons |= xi_bit(b);
		else
			g->wButtons &= ~xi_bit(b);
		break;
	default:
		break;
	}
}

/* write order: when two controls share a channel the last one wins */
static void write_order(int order[C_COUNT])
{
	int braking = g_shm && g_shm->wheelieAngle > g_cfg.assist.maxAngle;

	order[0] = C_THROTTLE;
	order[1] = braking ? C_LEAN : C_REARBRAKE;
	order[2] = braking ? C_REARBRAKE : C_LEAN;
}

/* 1 if the control must be written. Idle assist = untouched data; the output is re-clamped against the
   freshest user value so a stale compute can never add throttle or remove the rider's own braking */
static int prepare(int which)
{
	Control *c = &g_ctl[which];
	float out = c->out;

	if (!g_assist.active)
		return 0;
	if (which == C_THROTTLE && out > c->user)
		out = c->user;
	if (which == C_REARBRAKE && out < c->user)
		out = c->user;
	c->out = out;
	return fabsf(out - c->user) > 0.002f;
}

static void process_xinput(DWORD pad, XINPUT_STATE *st)
{
	ULONGLONG tick = GetTickCount64();
	int i, order[C_COUNT], hotkeyPad;
	float v;

	/* hotkeys come from the pad driving the throttle, or pad 1 */
	hotkeyPad = xinput_pad_of(&g_bind.throttle);
	if ((int)pad == (hotkeyPad >= 0 ? hotkeyPad : 0))
		pad_hotkeys(st->Gamepad.wButtons);

	for (i = 0; i < C_COUNT; i++)
		if (xinput_pad_of(g_ctl[i].bind) == (int)pad && xi_read(&g_ctl[i], &st->Gamepad, &v))
		{
			g_ctl[i].user = v;
			g_ctl[i].seenTick = tick;
		}
	compute();
	write_order(order);
	for (i = 0; i < C_COUNT; i++)
	{
		Control *c = &g_ctl[order[i]];
		if (xinput_pad_of(c->bind) == (int)pad && prepare(order[i]))
			xi_write(c, &st->Gamepad);
	}
}

typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE*);
static XInputGetState_t g_realXInputGetState;
static void **g_xinputIATSlot;

static DWORD WINAPI hook_XInputGetState(DWORD pad, XINPUT_STATE *st)
{
	DWORD r = g_realXInputGetState(pad, st);

	if (r == ERROR_SUCCESS && st && pad < 4)
		process_xinput(pad, st);
	return r;
}

/* patches XInputGetState in the import table of the stock xinput64.dli */
static void hook_xinput_plugin(void)
{
	HMODULE mod;
	BYTE *base;
	IMAGE_NT_HEADERS *nt;
	IMAGE_IMPORT_DESCRIPTOR *imp;
	DWORD old;

	if (g_xinputIATSlot)
		return;
	mod = GetModuleHandleA("xinput64.dli");
	if (!mod)
		return;
	base = (BYTE*)mod;
	nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
	imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
	for (; imp->Name; imp++)
	{
		IMAGE_THUNK_DATA *names, *slots;

		if (_strnicmp((char*)(base + imp->Name), "xinput", 6))
			continue;
		names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
		slots = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
		for (; names->u1.AddressOfData; names++, slots++)
		{
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
				continue;
			if (strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name, "XInputGetState"))
				continue;
			g_xinputIATSlot = (void**)&slots->u1.Function;
			g_realXInputGetState = (XInputGetState_t)*g_xinputIATSlot;
			VirtualProtect(g_xinputIATSlot, sizeof(void*), PAGE_READWRITE, &old);
			*g_xinputIATSlot = (void*)hook_XInputGetState;
			VirtualProtect(g_xinputIATSlot, sizeof(void*), old, &old);
			wa_diag("hooked XInputGetState of xinput64.dli");
			return;
		}
	}
	wa_diag("xinput64.dli found but XInputGetState import not found");
	g_xinputIATSlot = (void**)-1;
}

static void unhook_xinput_plugin(void)
{
	DWORD old;

	if (g_xinputIATSlot && g_xinputIATSlot != (void**)-1)
	{
		VirtualProtect(g_xinputIATSlot, sizeof(void*), PAGE_READWRITE, &old);
		*g_xinputIATSlot = (void*)g_realXInputGetState;
		VirtualProtect(g_xinputIATSlot, sizeof(void*), old, &old);
	}
	g_xinputIATSlot = NULL;
}

/******************************************************************************
DirectInput channels
******************************************************************************/

typedef struct
{
	void *self;
	int wide;
	int keyboard;
	char instance[40], product[40];
	LONG min[8], max[8];		/* x y z rx ry rz slider0 slider1 */
} DIDev;

static DIDev g_diDevs[MAX_DI_DEVICES];
static int g_numDiDevs;
static int g_inHook;

static void guid_str(const GUID *g, char *out, int size)
{
	sprintf_s(out, size, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", g->Data1, g->Data2, g->Data3,
		g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static DIDev *di_dev(void *self, int wide)
{
	static const DWORD offs[8] = { DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ,
		DIJOFS_SLIDER(0), DIJOFS_SLIDER(1) };
	DIDev *d;
	DIPROPRANGE range;
	GUID inst, prod;
	DWORD type;
	char name[MAX_PATH];
	int i;

	for (i = 0; i < g_numDiDevs; i++)
		if (g_diDevs[i].self == self)
			return &g_diDevs[i];
	if (g_numDiDevs >= MAX_DI_DEVICES)
		return NULL;

	d = &g_diDevs[g_numDiDevs++];
	ZeroMemory(d, sizeof(*d));
	d->self = self;
	d->wide = wide;
	if (wide)
	{
		DIDEVICEINSTANCEW di;
		ZeroMemory(&di, sizeof(di));
		di.dwSize = sizeof(di);
		((LPDIRECTINPUTDEVICE8W)self)->lpVtbl->GetDeviceInfo((LPDIRECTINPUTDEVICE8W)self, &di);
		inst = di.guidInstance; prod = di.guidProduct; type = di.dwDevType;
		WideCharToMultiByte(CP_ACP, 0, di.tszProductName, -1, name, sizeof(name), NULL, NULL);
	}
	else
	{
		DIDEVICEINSTANCEA di;
		ZeroMemory(&di, sizeof(di));
		di.dwSize = sizeof(di);
		((LPDIRECTINPUTDEVICE8A)self)->lpVtbl->GetDeviceInfo((LPDIRECTINPUTDEVICE8A)self, &di);
		inst = di.guidInstance; prod = di.guidProduct; type = di.dwDevType;
		strcpy_s(name, sizeof(name), di.tszProductName);
	}
	guid_str(&inst, d->instance, sizeof(d->instance));
	guid_str(&prod, d->product, sizeof(d->product));
	d->keyboard = GET_DIDEVICE_TYPE(type) == DI8DEVTYPE_KEYBOARD;

	for (i = 0; i < 8; i++)
	{
		ZeroMemory(&range, sizeof(range));
		range.diph.dwSize = sizeof(range);
		range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
		range.diph.dwObj = offs[i];
		range.diph.dwHow = DIPH_BYOFFSET;
		if (d->keyboard || FAILED(((LPDIRECTINPUTDEVICE8A)self)->lpVtbl->GetProperty((LPDIRECTINPUTDEVICE8A)self,
			DIPROP_RANGE, &range.diph)) || range.lMax <= range.lMin)
		{
			range.lMin = 0;
			range.lMax = 65535;
		}
		d->min[i] = range.lMin;
		d->max[i] = range.lMax;
	}
	wa_diag("DirectInput device seen: '%s' instance=%s product=%s%s", name, d->instance, d->product,
		d->keyboard ? " (keyboard)" : "");
	return d;
}

static int di_matches(const DIDev *d, const WABinding *b)
{
	if (b->kind == WA_BIND_KEY)
		return d->keyboard;
	if (b->kind == WA_BIND_NONE || d->keyboard)
		return 0;
	return !strcmp(b->uuid, d->instance) || !strcmp(b->uuid, d->product);
}

static LONG *di_axis_ptr(BYTE *js, int idx)
{
	/* lX..lRz, same layout in DIJOYSTATE and DIJOYSTATE2 */
	return idx >= 0 && idx < 6 ? (LONG*)(js + idx * 4) : NULL;
}

/* normalized -1..1 around the middle of the range, or 0..1 from the rest position for pedals/triggers */
static int di_read(Control *c, const DIDev *d, BYTE *data, DWORD cb, float *v)
{
	const WABinding *b = c->bind;
	LONG *p, mid, half;
	DWORD pov;

	if (b->kind == WA_BIND_KEY)
	{
		float k1 = (data[b->index & 0xFF] & 0x80) ? 1.0f : 0.0f;
		float k2 = b->key2 && (data[b->key2 & 0xFF] & 0x80) ? 1.0f : 0.0f;
		*v = c->bipolar ? (k1 - k2) * (g_cfg.leanInvert ? -1.0f : 1.0f) : (k1 > k2 ? k1 : k2);
		return 1;
	}
	if (cb != sizeof(DIJOYSTATE) && cb != sizeof(DIJOYSTATE2))
		return 0;
	switch (b->kind)
	{
	case WA_BIND_AXIS:
		p = di_axis_ptr(data, b->index);
		if (!p)
			return 0;
		mid = (d->min[b->index] + d->max[b->index]) / 2;
		half = (d->max[b->index] - d->min[b->index]) / 2;
		if (c->bipolar)
		{
			*v = clampf((float)(*p - mid) / half, -1.0f, 1.0f) * lean_dir(b);
			return 1;
		}
		/* first sample decides: resting at an end of the range = pedal/trigger, otherwise a centred stick */
		if (!c->restKnown)
		{
			LONG r = *p;
			c->restKnown = 1;
			c->rest = (r - d->min[b->index] < half / 5) ? d->min[b->index] :
				(d->max[b->index] - r < half / 5) ? d->max[b->index] : mid;
		}
		if (c->rest != mid)
		{
			LONG farEnd = c->rest == d->min[b->index] ? d->max[b->index] : d->min[b->index];
			*v = clampf((float)(*p - c->rest) / (farEnd - c->rest), 0.0f, 1.0f);
		}
		else
			*v = clampf((float)(*p - mid) / half * (b->sign == 1 ? -1.0f : 1.0f), 0.0f, 1.0f);
		return 1;
	case WA_BIND_SLIDER:
		if (b->index < 0 || b->index > 1)
			return 0;
		p = (LONG*)(data + 24 + b->index * 4);
		*v = clampf((float)(*p - d->min[6 + b->index]) / (d->max[6 + b->index] - d->min[6 + b->index]), 0.0f, 1.0f);
		return 1;
	case WA_BIND_BUTTON:
		if (b->index < 0 || b->index >= (cb == sizeof(DIJOYSTATE2) ? 128 : 32))
			return 0;
		*v = (data[48 + b->index] & 0x80) ? 1.0f : 0.0f;
		return 1;
	case WA_BIND_POV:
		pov = *(DWORD*)(data + 32);
		*v = (LOWORD(pov) != 0xFFFF && ((pov / 9000 + 4) % 4 == (DWORD)b->index)) ? 1.0f : 0.0f;
		return 1;
	default:
		return 0;
	}
}

static void di_write(Control *c, const DIDev *d, BYTE *data)
{
	const WABinding *b = c->bind;
	float v = c->out;
	LONG *p, mid, half;

	switch (b->kind)
	{
	case WA_BIND_KEY:
		if (c->bipolar)
		{
			float dir = v * (g_cfg.leanInvert ? -1.0f : 1.0f);
			int on = sigma_delta(c, fabsf(dir));
			data[b->index & 0xFF] = (on && dir > 0) ? 0x80 : 0;
			if (b->key2)
				data[b->key2 & 0xFF] = (on && dir < 0) ? 0x80 : 0;
		}
		else
		{
			data[b->index & 0xFF] = sigma_delta(c, v) ? 0x80 : 0;
			if (b->key2)
				data[b->key2 & 0xFF] = 0;
		}
		break;
	case WA_BIND_AXIS:
		p = di_axis_ptr(data, b->index);
		mid = (d->min[b->index] + d->max[b->index]) / 2;
		half = (d->max[b->index] - d->min[b->index]) / 2;
		if (c->bipolar)
			*p = mid + (LONG)(clampf(v * lean_dir(b), -1.0f, 1.0f) * half);
		else if (c->restKnown && c->rest != mid)
		{
			LONG farEnd = c->rest == d->min[b->index] ? d->max[b->index] : d->min[b->index];
			*p = c->rest + (LONG)(clampf(v, 0.0f, 1.0f) * (farEnd - c->rest));
		}
		else
			*p = mid + (LONG)(clampf(v, 0.0f, 1.0f) * half * (b->sign == 1 ? -1.0f : 1.0f));
		break;
	case WA_BIND_SLIDER:
		p = (LONG*)(data + 24 + b->index * 4);
		*p = d->min[6 + b->index] + (LONG)(clampf(v, 0.0f, 1.0f) * (d->max[6 + b->index] - d->min[6 + b->index]));
		break;
	case WA_BIND_BUTTON:
		data[48 + b->index] = sigma_delta(c, c->bipolar ? fabsf(v) : v) ? 0x80 : 0;
		break;
	default:
		/* POV: leave it alone, a hat can't express an analog request */
		break;
	}
}

static void process_dinput(void *self, int wide, DWORD cb, void *data)
{
	ULONGLONG tick = GetTickCount64();
	DIDev *d;
	int i, any = 0, order[C_COUNT];
	float v;

	d = di_dev(self, wide);
	if (!d || (d->keyboard ? cb != 256 : (cb != sizeof(DIJOYSTATE) && cb != sizeof(DIJOYSTATE2))))
		return;
	for (i = 0; i < C_COUNT; i++)
		if (di_matches(d, g_ctl[i].bind) && di_read(&g_ctl[i], d, (BYTE*)data, cb, &v))
		{
			g_ctl[i].user = v;
			g_ctl[i].seenTick = tick;
			any = 1;
		}
	if (!any)
		return;
	compute();
	write_order(order);
	for (i = 0; i < C_COUNT; i++)
	{
		Control *c = &g_ctl[order[i]];
		if (di_matches(d, c->bind) && prepare(order[i]))
			di_write(c, d, (BYTE*)data);
	}
}

typedef HRESULT (STDMETHODCALLTYPE *GetDeviceState_t)(void*, DWORD, LPVOID);
static GetDeviceState_t g_realGDS[2];
static void **g_gdsSlot[2];

static HRESULT STDMETHODCALLTYPE hook_GDS_A(void *self, DWORD cb, LPVOID data)
{
	HRESULT hr = g_realGDS[0](self, cb, data);

	if (SUCCEEDED(hr) && data && !g_inHook)
	{
		g_inHook = 1;
		process_dinput(self, 0, cb, data);
		g_inHook = 0;
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE hook_GDS_W(void *self, DWORD cb, LPVOID data)
{
	HRESULT hr = g_realGDS[1](self, cb, data);

	if (SUCCEEDED(hr) && data && !g_inHook)
	{
		g_inHook = 1;
		process_dinput(self, 1, cb, data);
		g_inHook = 0;
	}
	return hr;
}

/* GetDeviceState is slot 9 of IDirectInputDevice8: IUnknown(3), GetCapabilities, EnumObjects, GetProperty,
   SetProperty, Acquire, Unacquire, GetDeviceState. All devices of one interface flavour share the vtable */
static void patch_slot(int wide, void *dev)
{
	void **vtbl = *(void***)dev;
	DWORD old;

	if (g_gdsSlot[wide] || vtbl[9] == (wide ? (void*)hook_GDS_W : (void*)hook_GDS_A))
		return;
	g_gdsSlot[wide] = &vtbl[9];
	g_realGDS[wide] = (GetDeviceState_t)vtbl[9];
	VirtualProtect(&vtbl[9], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
	vtbl[9] = wide ? (void*)hook_GDS_W : (void*)hook_GDS_A;
	VirtualProtect(&vtbl[9], sizeof(void*), old, &old);
}

static void hook_dinput(void)
{
	HMODULE self = NULL;
	LPDIRECTINPUT8A diA = NULL;
	LPDIRECTINPUT8W diW = NULL;
	LPDIRECTINPUTDEVICE8A devA = NULL;
	LPDIRECTINPUTDEVICE8W devW = NULL;

	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&hook_dinput, &self);
	if (SUCCEEDED(DirectInput8Create(self, DIRECTINPUT_VERSION, &IID_IDirectInput8A, (void**)&diA, NULL)) &&
		SUCCEEDED(diA->lpVtbl->CreateDevice(diA, &GUID_SysKeyboard, &devA, NULL)))
		patch_slot(0, devA);
	if (SUCCEEDED(DirectInput8Create(self, DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void**)&diW, NULL)) &&
		SUCCEEDED(diW->lpVtbl->CreateDevice(diW, &GUID_SysKeyboard, &devW, NULL)))
		patch_slot(1, devW);
	/* the vtable lives in dinput8.dll and stays valid after these objects are released */
	if (devA) devA->lpVtbl->Release(devA);
	if (devW) devW->lpVtbl->Release(devW);
	if (diA) diA->lpVtbl->Release(diA);
	if (diW) diW->lpVtbl->Release(diW);
	wa_diag("DirectInput GetDeviceState hook: A=%s W=%s", g_gdsSlot[0] ? "ok" : "FAILED", g_gdsSlot[1] ? "ok" : "FAILED");
}

static void unhook_dinput(void)
{
	DWORD old;
	int i;

	for (i = 0; i < 2; i++)
		if (g_gdsSlot[i])
		{
			VirtualProtect(g_gdsSlot[i], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
			*g_gdsSlot[i] = (void*)g_realGDS[i];
			VirtualProtect(g_gdsSlot[i], sizeof(void*), old, &old);
			g_gdsSlot[i] = NULL;
		}
}

/******************************************************************************
exports
******************************************************************************/

static void refresh_bindings(int force)
{
	ULONGLONG tick = GetTickCount64();

	if (!force && tick - g_bindCheckTick < BINDINGS_CHECK_MS)
		return;
	g_bindCheckTick = tick;
	if (wa_bindings_refresh(&g_bind))
	{
		wa_diag("bindings loaded from %s", g_bind.path);
		controls_bind();
	}
}

EXPORT int Version()
{
	return 4;
}

EXPORT int Startup()
{
	char ini[MAX_PATH];

	wa_config_load(&g_cfg, ini, sizeof(ini));
	g_enabled = g_cfg.startEnabled;
	g_preset = g_cfg.startPreset;
	wa_assist_reset(&g_assist);
	QueryPerformanceFrequency(&g_qpf);
	g_shm = wa_shm_open(&g_hMap);
	wa_diag("startup (transparent mode). ini=%s shm=%s", ini, g_shm ? "ok" : "FAILED");

	controls_bind();
	refresh_bindings(1);
	hook_dinput();
	hook_xinput_plugin();
	compute();
	return 0;
}

EXPORT void Shutdown()
{
	unhook_xinput_plugin();
	unhook_dinput();
	wa_shm_close(g_shm, g_hMap);
	g_shm = NULL;
	g_hMap = NULL;
}

EXPORT void Update()
{
	hook_xinput_plugin();
	refresh_bindings(0);
	key_hotkeys();
	compute();
}

EXPORT int GetNumControllers()
{
	hook_xinput_plugin();
	return 0;
}

EXPORT int GetControllerInfo(int _iIndex, SControllerInfo_t *_psInfo)
{
	return 1;
}

EXPORT int GetControllerData(int _iID, SControllerData_t *_psData)
{
	return 1;
}

EXPORT int SetControllerRumble(int _iID, float _fLow, float _fHigh)
{
	return 0;
}

/* test hook for tools/harness.c: runs a fake XInput read through the same path as the game */
EXPORT void WA_TestXInput(DWORD pad, XINPUT_STATE *st)
{
	process_xinput(pad, st);
}
