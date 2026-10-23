/*
MX Bikes output plugin (.dlo) structures.
Copied from PiBoSo's official example: https://www.mx-bikes.com/downloads/mxb_example.c
(mod data version 8, interface version 9). Default MSVC packing must be kept.
*/
#pragma once

typedef struct
{
	char m_szRiderName[100];
	char m_szBikeID[100];
	char m_szBikeName[100];
	int m_iNumberOfGears;
	int m_iMaxRPM;
	int m_iLimiter;
	int m_iShiftRPM;
	float m_fEngineOptTemperature;
	float m_afEngineTemperatureAlarm[2];
	float m_fMaxFuel;
	float m_afSuspMaxTravel[2];
	float m_fSteerLock;
	char m_szCategory[100];
	char m_szTrackID[100];
	char m_szTrackName[100];
	float m_fTrackLength;
	int m_iType;
	/* newer game builds append these (seen in current community plugins); check the data size before reading */
	char m_szServerName[64];
	int m_iServerType;
	char m_szGUID[100];
} SPluginsBikeEvent_t;

typedef struct
{
	int m_iRPM;														/* engine rpm */
	float m_fEngineTemperature;										/* degrees Celsius */
	float m_fWaterTemperature;										/* degrees Celsius */
	int m_iGear;													/* 0 = Neutral */
	float m_fFuel;													/* liters */
	float m_fSpeedometer;											/* meters/second */
	float m_fPosX,m_fPosY,m_fPosZ;									/* world position of a reference point attached to chassis ( not CG ) */
	float m_fVelocityX,m_fVelocityY,m_fVelocityZ;					/* velocity of CG in world coordinates. meters/second */
	float m_fAccelerationX,m_fAccelerationY,m_fAccelerationZ;		/* local acceleration of CG, G, averaged over 10ms */
	float m_aafRot[3][3];											/* rotation matrix of the chassis. It incorporates lean and wheeling */
	float m_fYaw,m_fPitch,m_fRoll;									/* degrees, -180 to 180 */
	float m_fYawVelocity,m_fPitchVelocity,m_fRollVelocity;			/* degress / second */
	float m_afSuspLength[2];										/* meters. 0 = front; 1 = rear */
	float m_afSuspVelocity[2];										/* meters/second. 0 = front; 1 = rear */
	int m_iCrashed;													/* 1 = rider is detached from bike */
	float m_fSteer;													/* degrees. Negative = right  */
	float m_fThrottle;												/* 0 to 1 */
	float m_fFrontBrake;											/* 0 to 1 */
	float m_fRearBrake;												/* 0 to 1 */
	float m_fClutch;												/* 0 to 1. 0 = Fully engaged */
	float m_afWheelSpeed[2];										/* meters/second. 0 = front; 1 = rear */
	int m_aiWheelMaterial[2];										/* material index. 0 = not in contact */
	float m_afBrakePressure[2];										/* kPa */
	float m_fSteerTorque;											/* Nm */
} SPluginsBikeData_t;

typedef struct
{
	float m_aafPos[4][2];			/* 0,0 -> top left. 1,1 -> bottom right. counter-clockwise */
	int m_iSprite;					/* 1 based index in SpriteName buffer. 0 = fill with m_ulColor */
	unsigned long m_ulColor;		/* ABGR */
} SPluginQuad_t;

typedef struct
{
	char m_szString[100];
	float m_afPos[2];
	int m_iFont;
	float m_fSize;
	int m_iJustify;
	unsigned long m_ulColor;
} SPluginString_t;
