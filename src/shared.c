#include "shared.h"

WAShared *wa_shm_open(HANDLE *phMap)
{
	HANDLE h;
	WAShared *p;
	int created;

	h = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(WAShared), WA_SHM_NAME);
	if (!h)
		return NULL;
	created = GetLastError() != ERROR_ALREADY_EXISTS;

	p = (WAShared*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WAShared));
	if (!p)
	{
		CloseHandle(h);
		return NULL;
	}
	if (created || p->version != WA_SHM_VERSION)
	{
		ZeroMemory(p, sizeof(*p));
		p->version = WA_SHM_VERSION;
	}
	*phMap = h;
	return p;
}

void wa_shm_close(WAShared *p, HANDLE hMap)
{
	if (p)
		UnmapViewOfFile(p);
	if (hMap)
		CloseHandle(hMap);
}
