/*
wheelie_assist64.dlo - MX Bikes output plugin.
Estimates the wheelie angle from telemetry, publishes it to shared memory for the input plugin,
logs CSV files for tuning and draws a small HUD gauge.
*/
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "mxb_plugin.h"
#include "shared.h"
#include "config.h"
#include "diag.h"

#define EXPORT __declspec(dllexport)

static WAConfig g_cfg;
static WAShared *g_shm;
static HANDLE g_hMap;
static char g_savePath[MAX_PATH];
static char g_bikeName[100];
static FILE *g_csv;
static int g_csvRows;
static int g_haveGround;
static float g_lastTime;
static int g_sizeWarned;

static float wrap180(float a)
{
	while (a > 180.0f)
		a -= 360.0f;
	while (a < -180.0f)
		a += 360.0f;
	return a;
}

static void csv_open(void)
{
	char dir[MAX_PATH], path[MAX_PATH];
	SYSTEMTIME t;
	size_t len;

	if (!g_cfg.log || g_csv)
		return;
	strcpy_s(dir, sizeof(dir), g_savePath);
	len = strlen(dir);
	if (len && dir[len - 1] != '\\' && dir[len - 1] != '/')
		strcat_s(dir, sizeof(dir), "\\");
	strcat_s(dir, sizeof(dir), "wheelie_logs");
	CreateDirectoryA(dir, NULL);

	GetLocalTime(&t);
	sprintf_s(path, sizeof(path), "%s\\%04d%02d%02d_%02d%02d%02d.csv", dir,
		t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	if (fopen_s(&g_csv, path, "w"))
	{
		g_csv = NULL;
		wa_diag("cannot create log %s", path);
		return;
	}
	g_csvRows = 0;
	fprintf(g_csv, "# bike=%s pitch_sign=%g preset=%d target=%g\n", g_bikeName, g_cfg.pitchSign,
		g_shm ? g_shm->preset : -1, g_shm ? g_shm->target : 0.0f);
	fprintf(g_csv, "time,pitch,pitch_vel,roll,ground_pitch,angle,rate,front_contact,rear_contact,in_wheelie,"
		"speed,wheel_f,wheel_r,gear,rpm,crashed,game_throttle,game_rear_brake,game_front_brake,game_clutch,"
		"user_throttle,out_throttle,user_rear_brake,out_rear_brake,user_lean,out_lean,assist_enabled,assist_active,device\n");
	wa_diag("logging to %s", path);
}

static void csv_close(void)
{
	if (g_csv)
	{
		fclose(g_csv);
		g_csv = NULL;
	}
}

EXPORT char *GetModID()
{
	return "mxbikes";
}

EXPORT int GetModDataVersion()
{
	return 8;
}

EXPORT int GetInterfaceVersion()
{
	return 9;
}

EXPORT int Startup(char *_szSavePath)
{
	char ini[MAX_PATH];

	wa_config_load(&g_cfg, ini, sizeof(ini));
	strcpy_s(g_savePath, sizeof(g_savePath), _szSavePath ? _szSavePath : ".");
	g_shm = wa_shm_open(&g_hMap);
	/* the input plugin overwrites this with the live target */
	if (g_shm && g_shm->target == 0.0f)
		g_shm->target = g_cfg.assist.target;
	wa_diag("startup. ini=%s save=%s shm=%s", ini, g_savePath, g_shm ? "ok" : "FAILED");
	/* 0 = 100hz */
	return g_shm ? 0 : -1;
}

EXPORT void Shutdown()
{
	csv_close();
	wa_shm_close(g_shm, g_hMap);
	g_shm = NULL;
	g_hMap = NULL;
}

EXPORT void EventInit(void *_pData, int _iDataSize)
{
	SPluginsBikeEvent_t *ev = (SPluginsBikeEvent_t*)_pData;

	if (ev && _iDataSize >= (int)offsetof(SPluginsBikeEvent_t, m_iNumberOfGears))
	{
		int online = _iDataSize >= (int)offsetof(SPluginsBikeEvent_t, m_iServerType) && ev->m_szServerName[0];

		strncpy_s(g_bikeName, sizeof(g_bikeName), ev->m_szBikeName, _TRUNCATE);
		if (g_shm)
			g_shm->online = online;
		wa_diag("event: bike=%s track=%s size=%d server='%s'", ev->m_szBikeName, ev->m_szTrackName, _iDataSize,
			online ? ev->m_szServerName : "");
	}
}

EXPORT void EventDeinit()
{
}

EXPORT void RunInit(void *_pData, int _iDataSize)
{
	/* reload so ini edits apply without restarting the game */
	wa_config_load(&g_cfg, NULL, 0);
	g_haveGround = 0;
	g_lastTime = 0.0f;
	csv_open();
}

EXPORT void RunDeinit()
{
	csv_close();
	if (g_shm)
		g_shm->onTrack = 0;
}

EXPORT void RunStart()
{
	if (g_shm)
		g_shm->onTrack = 1;
}

EXPORT void RunStop()
{
	if (g_shm)
		g_shm->onTrack = 0;
	if (g_csv)
		fflush(g_csv);
}

EXPORT void RunLap(void *_pData, int _iDataSize)
{
}

EXPORT void RunSplit(void *_pData, int _iDataSize)
{
}

EXPORT void RunTelemetry(void *_pData, int _iDataSize, float _fTime, float _fPos)
{
	SPluginsBikeData_t *d = (SPluginsBikeData_t*)_pData;
	WAShared *s = g_shm;
	float dt, k;
	int front, rear;

	if (!s || !d)
		return;
	if (_iDataSize < (int)sizeof(SPluginsBikeData_t))
	{
		if (!g_sizeWarned)
			wa_diag("telemetry size %d < expected %d, ignoring", _iDataSize, (int)sizeof(SPluginsBikeData_t));
		g_sizeWarned = 1;
		return;
	}

	dt = _fTime - g_lastTime;
	if (dt <= 0.0f || dt > 0.1f)
		dt = 0.01f;
	g_lastTime = _fTime;

	front = d->m_aiWheelMaterial[0] != 0;
	rear = d->m_aiWheelMaterial[1] != 0;

	if (front && rear)
	{
		if (!g_haveGround)
		{
			s->groundPitch = d->m_fPitch;
			g_haveGround = 1;
		}
		k = dt / (g_cfg.groundTau + dt);
		s->groundPitch = wrap180(s->groundPitch + wrap180(d->m_fPitch - s->groundPitch) * k);
	}
	else if (!g_haveGround)
	{
		s->groundPitch = d->m_fPitch;
		g_haveGround = 1;
	}

	s->time = _fTime;
	s->pitch = d->m_fPitch;
	s->pitchVel = d->m_fPitchVelocity;
	s->wheelieAngle = wrap180(d->m_fPitch - s->groundPitch) * g_cfg.pitchSign;
	s->wheelieRate = d->m_fPitchVelocity * g_cfg.pitchSign;
	s->frontContact = front;
	s->rearContact = rear;
	s->inWheelie = rear && !front && d->m_iGear > 0 && !d->m_iCrashed;
	s->speed = d->m_fSpeedometer;
	s->gear = d->m_iGear;
	s->rpm = d->m_iRPM;
	s->crashed = d->m_iCrashed;
	s->gameThrottle = d->m_fThrottle;
	s->gameRearBrake = d->m_fRearBrake;
	s->gameFrontBrake = d->m_fFrontBrake;
	s->gameClutch = d->m_fClutch;
	s->telemetryTick = GetTickCount64();
	InterlockedIncrement(&s->telemetrySeq);

	if (g_csv)
	{
		fprintf(g_csv, "%.3f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%.2f,%.2f,%.2f,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,"
			"%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d\n",
			_fTime, d->m_fPitch, d->m_fPitchVelocity, d->m_fRoll, s->groundPitch, s->wheelieAngle, s->wheelieRate,
			front, rear, s->inWheelie, d->m_fSpeedometer, d->m_afWheelSpeed[0], d->m_afWheelSpeed[1],
			d->m_iGear, d->m_iRPM, d->m_iCrashed, d->m_fThrottle, d->m_fRearBrake, d->m_fFrontBrake, d->m_fClutch,
			s->userThrottle, s->outThrottle, s->userRearBrake, s->outRearBrake, s->userLean, s->outLean,
			s->assistEnabled, s->assistActive,
			s->deviceConnected);
		if (++g_csvRows % 200 == 0)
			fflush(g_csv);
	}
}

/******************************************************************************
HUD: vertical gauge showing the wheelie angle (0..90 degrees), target and cut-off markers
******************************************************************************/

#define COL_BG		0x90000000
#define COL_OFF		0xFF707070
#define COL_IDLE	0xFF30D030
#define COL_ACTIVE	0xFF0090FF
#define COL_TARGET	0xFF00FFFF
#define COL_MAX		0xFF2020FF
#define COL_NODEV	0xFF2020FF

static SPluginQuad_t g_quads[16];
static char g_noNames[2];

static void quad(int i, float x0, float y0, float x1, float y1, unsigned long color)
{
	SPluginQuad_t *q = &g_quads[i];

	/* top-left, bottom-left, bottom-right, top-right */
	q->m_aafPos[0][0] = x0; q->m_aafPos[0][1] = y0;
	q->m_aafPos[1][0] = x0; q->m_aafPos[1][1] = y1;
	q->m_aafPos[2][0] = x1; q->m_aafPos[2][1] = y1;
	q->m_aafPos[3][0] = x1; q->m_aafPos[3][1] = y0;
	q->m_iSprite = 0;
	q->m_ulColor = color;
}

static float gauge_y(float angle)
{
	float f = angle / 90.0f;

	f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
	return g_cfg.hudY + g_cfg.hudH * (1.0f - f);
}

EXPORT int DrawInit(int *_piNumSprites, char **_pszSpriteName, int *_piNumFonts, char **_pszFontName)
{
	*_piNumSprites = 0;
	*_pszSpriteName = g_noNames;
	*_piNumFonts = 0;
	*_pszFontName = g_noNames;
	return 0;
}

EXPORT void Draw(int _iState, int *_piNumQuads, void **_ppQuad, int *_piNumString, void **_ppString)
{
	WAShared *s = g_shm;
	WAParams params;
	float w = 0.010f, x0, x1, bottom, top, y;
	unsigned long col;
	int i, n = 0;

	*_piNumQuads = 0;
	*_piNumString = 0;
	if (!s || !g_cfg.hud || _iState != 0)
		return;

	x0 = g_cfg.hudX - w * 0.5f;
	x1 = g_cfg.hudX + w * 0.5f;
	bottom = g_cfg.hudY + g_cfg.hudH;

	col = !s->deviceConnected ? COL_NODEV : !s->assistEnabled ? COL_OFF : s->assistActive ? COL_ACTIVE : COL_IDLE;

	quad(n++, x0 - 0.002f, g_cfg.hudY - 0.002f, x1 + 0.002f, bottom + 0.002f, COL_BG);
	top = gauge_y(s->wheelieAngle);
	if (top < bottom)
		quad(n++, x0, top, x1, bottom, col);
	top = gauge_y(s->target);
	quad(n++, x0 - 0.006f, top - 0.002f, x1 + 0.006f, top + 0.002f, COL_TARGET);
	params = g_cfg.assist;
	wa_preset_apply(s->preset, &params);
	top = gauge_y(params.maxAngle);
	quad(n++, x0 - 0.003f, top - 0.0015f, x1 + 0.003f, top + 0.0015f, COL_MAX);
	/* status square above the gauge */
	quad(n++, x0, g_cfg.hudY - 0.025f, x1, g_cfg.hudY - 0.010f, col);
	/* one small square per preset step below the gauge: 1 = gentle ... 5 = custom */
	for (i = 0; i <= s->preset && i < WA_NUM_PRESETS; i++)
	{
		y = bottom + 0.010f + i * 0.012f;
		quad(n++, x0 + 0.002f, y, x1 - 0.002f, y + 0.008f, COL_TARGET);
	}

	*_piNumQuads = n;
	*_ppQuad = g_quads;
}
