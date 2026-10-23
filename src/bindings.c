#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bindings.h"

static void controls_path(char *out, int size)
{
	char docs[MAX_PATH], ini[MAX_PATH], profile[128];

	if (FAILED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docs)))
		strcpy_s(docs, sizeof(docs), ".");
	sprintf_s(ini, sizeof(ini), "%s\\PiBoSo\\MX Bikes\\global.ini", docs);
	GetPrivateProfileStringA("profile", "lastprofile", "unnamedProfile", profile, sizeof(profile), ini);
	sprintf_s(out, size, "%s\\PiBoSo\\MX Bikes\\profiles\\%s\\controls.txt", docs, profile);
}

static int is_int(const char *s)
{
	if (!*s)
		return 0;
	for (; *s; s++)
		if (*s < '0' || *s > '9')
			return 0;
	return 1;
}

static void parse_line(char *line, WABinding *b)
{
	char *tok[8], *ctx = NULL, *t;
	int n = 0;

	ZeroMemory(b, sizeof(*b));
	for (t = strtok_s(line, " \t\r\n", &ctx); t && n < 8; t = strtok_s(NULL, " \t\r\n", &ctx))
		tok[n++] = t;
	if (n < 3)
		return;

	if (!strcmp(tok[1], "KEY"))
	{
		b->kind = WA_BIND_KEY;
		b->index = atoi(tok[2]);
		if (n > 3 && is_int(tok[3]))
			b->key2 = atoi(tok[3]);
		return;
	}
	if (n < 4)
		return;
	strncpy_s(b->uuid, sizeof(b->uuid), tok[2], _TRUNCATE);
	_strlwr_s(b->uuid, sizeof(b->uuid));
	b->index = atoi(tok[3]);
	if (!strcmp(tok[1], "C_AXIS"))
	{
		b->kind = WA_BIND_AXIS;
		b->sign = n > 4 ? atoi(tok[4]) : 0;
	}
	else if (!strcmp(tok[1], "C_SLIDER"))
		b->kind = WA_BIND_SLIDER;
	else if (!strcmp(tok[1], "C_BUTTON"))
		b->kind = WA_BIND_BUTTON;
	else if (!strcmp(tok[1], "C_POV"))
		b->kind = WA_BIND_POV;
	/* anything else (e.g. an unbound "NAME 0.000000 ...") stays WA_BIND_NONE */
}

int wa_bindings_refresh(WABindings *b)
{
	WIN32_FILE_ATTRIBUTE_DATA fa;
	char path[MAX_PATH], line[512], name[64];
	FILE *f;

	controls_path(path, sizeof(path));
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa))
		return 0;
	if (!strcmp(path, b->path) && !CompareFileTime(&fa.ftLastWriteTime, &b->mtime))
		return 0;
	if (fopen_s(&f, path, "r"))
		return 0;

	ZeroMemory(b, sizeof(*b));
	strcpy_s(b->path, sizeof(b->path), path);
	b->mtime = fa.ftLastWriteTime;
	while (fgets(line, sizeof(line), f))
	{
		if (sscanf_s(line, "%63s", name, (unsigned)sizeof(name)) != 1)
			continue;
		if (!strcmp(name, "CTRL_THROTTLE"))
			parse_line(line, &b->throttle);
		else if (!strcmp(name, "CTRL_REARBRAKE"))
			parse_line(line, &b->rearBrake);
		else if (!strcmp(name, "CTRL_FBLEAN"))
			parse_line(line, &b->lean);
	}
	fclose(f);
	return 1;
}

void wa_binding_describe(const WABinding *b, char *out, int size)
{
	static const char *kinds[] = { "none", "axis", "slider", "button", "pov", "key" };

	if (b->kind == WA_BIND_KEY)
		sprintf_s(out, size, "key %d%s%.0d", b->index, b->key2 ? "+" : "", b->key2);
	else if (b->kind == WA_BIND_AXIS)
		sprintf_s(out, size, "axis #%d sign %d on %s", b->index, b->sign, b->uuid);
	else if (b->kind != WA_BIND_NONE)
		sprintf_s(out, size, "%s #%d on %s", kinds[b->kind], b->index, b->uuid);
	else
		strcpy_s(out, size, "unbound");
}

int wa_binding_same_channel(const WABinding *a, const WABinding *b)
{
	if (a->kind == WA_BIND_NONE || a->kind != b->kind || a->index != b->index)
		return 0;
	return a->kind == WA_BIND_KEY || !strcmp(a->uuid, b->uuid);
}
