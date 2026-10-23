/*
State shared between wheelie_assist64.dlo (telemetry side) and wheelie_assist64.dli (input side).
Both DLLs live in the game process but are separate modules, so they talk through a named file mapping.
*/
#pragma once
#include <windows.h>

#define WA_SHM_NAME "Local\\MXBWheelieAssist"
#define WA_SHM_VERSION 5

typedef struct
{
	int version;

	/* written by the .dlo at telemetry rate */
	volatile LONG telemetrySeq;		/* incremented on every RunTelemetry */
	ULONGLONG telemetryTick;		/* GetTickCount64() of last RunTelemetry */
	int onTrack;					/* between RunStart and RunStop */
	int online;						/* current event is on a server */
	float time;
	float pitch;					/* raw m_fPitch, degrees */
	float pitchVel;					/* raw m_fPitchVelocity, deg/s */
	float groundPitch;				/* low-passed pitch while both wheels touch the ground */
	float wheelieAngle;				/* (pitch - groundPitch) * pitch_sign, degrees, nose up positive */
	float wheelieRate;				/* pitchVel * pitch_sign, deg/s, nose rising positive */
	int frontContact, rearContact;
	int inWheelie;					/* rear on ground, front in the air, in gear, not crashed */
	float speed;
	int gear, rpm, crashed;
	float gameThrottle, gameRearBrake, gameFrontBrake, gameClutch;	/* as applied by the game */

	/* written by the .dli every input poll */
	volatile LONG inputSeq;
	int deviceConnected;
	int assistEnabled;
	int assistActive;				/* law is currently modifying inputs */
	float target;
	int preset;						/* 0-based assist preset selected with the D-pad */
	float userThrottle, outThrottle;
	float userRearBrake, outRearBrake;
	float userLean, outLean;		/* -1 back .. 1 forward */
	char deviceName[64];
} WAShared;

/* opens (creating if needed) the shared block. Returns NULL on failure */
WAShared *wa_shm_open(HANDLE *phMap);
void wa_shm_close(WAShared *p, HANDLE hMap);
