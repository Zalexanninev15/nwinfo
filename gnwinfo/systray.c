// SPDX-License-Identifier: Unlicense

#include <windows.h>
#include <shellapi.h>
#include <limits.h>
#include "gnwinfo.h"
#include "ioctl.h"
#include "gettext.h"
#include "utils.h"
#include "version.h"

static nk_bool m_systray = nk_false;

void gnwinfo_add_systray(HWND wnd, HICON icon)
{
	if (m_systray == nk_true)
		return;
	NOTIFYICONDATAW nid = { 0 };
	nid.cbSize = sizeof(NOTIFYICONDATAW);
	nid.hWnd = wnd;
	nid.uID = 1; // Unique ID for the icon
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = icon;
	wcscpy_s(nid.szTip, ARRAYSIZE(nid.szTip), g_window_title);
	Shell_NotifyIconW(NIM_ADD, &nid);
	m_systray = nk_true;
}

void gnwinfo_remove_systray(HWND wnd)
{
	if (m_systray == nk_false)
		return;
	NOTIFYICONDATAW nid = { 0 };
	nid.cbSize = sizeof(NOTIFYICONDATAW);
	nid.hWnd = wnd;
	nid.uID = 1;
	Shell_NotifyIconW(NIM_DELETE, &nid);
	m_systray = nk_false;
}

HICON
gnwinfo_create_tray_number_icon(int value, COLORREF color)
{
	int cx = GetSystemMetrics(SM_CXSMICON);
	int cy = GetSystemMetrics(SM_CYSMICON);
	if (cx < 8) cx = 16;
	if (cy < 8) cy = 16;

	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = cx;
	bmi.bmiHeader.biHeight = -cy; // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* bits = NULL;
	HDC screen = GetDC(NULL);
	HDC dc = CreateCompatibleDC(screen);
	HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	ReleaseDC(NULL, screen);
	if (!bmp)
	{
		DeleteDC(dc);
		return NULL;
	}
	HBITMAP old_bmp = SelectObject(dc, bmp);

	// Clear to transparent
	memset(bits, 0, (size_t)cx * cy * 4);

	WCHAR text[8];
	int len;
	if (value == INT_MIN)
	{
		text[0] = L'-'; text[1] = L'-'; text[2] = 0;
		len = 2;
	}
	else
	{
		len = _snwprintf_s(text, _countof(text), _TRUNCATE, L"%d", value);
	}

	// Select font size based on character count, shrunk to avoid clipping
	int font_height;
	if (len <= 1)
		font_height = cy - 2;
	else if (len <= 2)
		font_height = cy - 3;
	else
		font_height = cy / 2;
	HFONT font = CreateFontW(-font_height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
	HFONT old_font = SelectObject(dc, font);

	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, color);

	RECT rc = { 0, 0, cx, cy };
	DrawTextW(dc, text, len, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

	// Fix alpha channel: set alpha to 255 for any pixel that was touched
	BYTE* p = (BYTE*)bits;
	for (int i = 0; i < cx * cy; i++)
	{
		if (p[0] || p[1] || p[2])
			p[3] = 255;
		p += 4;
	}

	SelectObject(dc, old_font);
	DeleteObject(font);
	SelectObject(dc, old_bmp);
	DeleteDC(dc);

	// Create mask bitmap (all zero = opaque where color bitmap has alpha)
	HBITMAP mask = CreateBitmap(cx, cy, 1, 1, NULL);
	HDC mask_dc = CreateCompatibleDC(NULL);
	HBITMAP old_mask = SelectObject(mask_dc, mask);
	PatBlt(mask_dc, 0, 0, cx, cy, BLACKNESS);
	SelectObject(mask_dc, old_mask);
	DeleteDC(mask_dc);

	ICONINFO ii = { 0 };
	ii.fIcon = TRUE;
	ii.hbmMask = mask;
	ii.hbmColor = bmp;
	HICON icon = CreateIconIndirect(&ii);

	DeleteObject(bmp);
	DeleteObject(mask);
	return icon;
}

static int
gnwinfo_get_tray_value(void)
{
	switch (g_ctx.tray_data_source)
	{
	case TRAY_SRC_CPU_USAGE:
	{
		int v = (int)(g_ctx.cpu_usage + 0.5);
		if (v < 0) v = 0;
		if (v > 100) v = 100;
		return v;
	}
	case TRAY_SRC_CPU_TEMP:
		if (g_ctx.cpu_info && g_ctx.cpu_info[0].MsrTemp > 0)
		{
			int v = (int)NWL_GetTemperature((float)g_ctx.cpu_info[0].MsrTemp);
			if (v < -99) v = -99;
			if (v > 999) v = 999;
			return v;
		}
		return INT_MIN;
	case TRAY_SRC_MEM_USAGE:
	{
		int v = (int)g_ctx.mem_status.PhysUsage;
		if (v < 0) v = 0;
		if (v > 100) v = 100;
		return v;
	}
	case TRAY_SRC_BATTERY:
	{
		LPCSTR attr = NWL_NodeAttrGet(g_ctx.battery, "Battery Life Percentage");
		if (!attr || attr[0] == '-' || attr[0] == '\0')
			return -1;
		unsigned int pct = 0;
		if (sscanf_s(attr, "%u%%", &pct) == 1 && pct <= 100)
			return (int)pct;
		return INT_MIN;
	}
	}
	return INT_MIN;
}

static COLORREF
gnwinfo_get_tray_color(int value)
{
	if (g_ctx.tray_color_mode == 1)
	{
		return RGB(g_ctx.tray_fixed_color.r, g_ctx.tray_fixed_color.g, g_ctx.tray_fixed_color.b);
	}

	// Dynamic color mode
	struct nk_color c;
	if (value == INT_MIN)
	{
		c = g_color_unknown;
	}
	else if (g_ctx.tray_data_source == TRAY_SRC_BATTERY)
	{
		// Battery: inverted - low is bad
		if (value <= 15)
			c = g_color_error;
		else if (value <= 30)
			c = g_color_warning;
		else
			c = g_color_good;
	}
	else if (g_ctx.tray_data_source == TRAY_SRC_CPU_TEMP)
	{
		c = gnwinfo_get_color((double)value,
			(double)NWL_GetTemperature(65.0f), (double)NWL_GetTemperature(85.0f));
	}
	else
	{
		// CPU% / Mem%: warn=70, err=90
		c = gnwinfo_get_color((double)value, 70.0, 90.0);
	}
	return RGB(c.r, c.g, c.b);
}

void gnwinfo_update_systray(HWND wnd, HICON icon)
{
	if (g_ctx.main_flag & MAIN_SYSTRAY)
		gnwinfo_add_systray(wnd, icon);
	else
	{
		gnwinfo_remove_systray(wnd);
		return;
	}

	HICON display_icon = icon;
	if (g_ctx.tray_data_source > TRAY_SRC_OFF)
	{
		int value = gnwinfo_get_tray_value();
		COLORREF color = gnwinfo_get_tray_color(value);
		HICON new_icon = gnwinfo_create_tray_number_icon(value, color);
		if (new_icon)
		{
			if (g_ctx.tray_icon)
				DestroyIcon(g_ctx.tray_icon);
			g_ctx.tray_icon = new_icon;
			display_icon = new_icon;
		}
	}
	else
	{
		if (g_ctx.tray_icon)
		{
			DestroyIcon(g_ctx.tray_icon);
			g_ctx.tray_icon = NULL;
		}
	}

	NOTIFYICONDATAW nid = { 0 };
	nid.cbSize = sizeof(NOTIFYICONDATAW);
	nid.hWnd = wnd;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_TIP;
	nid.hIcon = display_icon;
	swprintf(nid.szTip, ARRAYSIZE(nid.szTip),
		L"CPU: %.0f%%\nRAM: %u%%\n\u2191 %hs\n\u2193 %hs",
		g_ctx.cpu_usage,
		g_ctx.mem_status.PhysUsage,
		g_ctx.net_traffic.StrSend,
		g_ctx.net_traffic.StrRecv);
	Shell_NotifyIconW(NIM_MODIFY, &nid);
}

#define MAX_POWER_SCHEMES 64

static inline void
show_power_schemes_menu(HMENU menu)
{
	PNODE table = NWL_NodeGetChild(g_ctx.battery, "Power Schemes");
	if (!table)
		return;
	INT count = NWL_NodeChildCount(table);
	if (count <= 0)
		return;
	if (count > MAX_POWER_SCHEMES)
		count = MAX_POWER_SCHEMES;
	for (UINT i = 0; i < (UINT)count; i++)
	{
		PNODE scheme = NWL_NodeEnumChild(table, i);
		if (!scheme)
			continue;
		LPCSTR name = NWL_NodeAttrGet(scheme, "Name");
		UINT id = IDM_POWER_SCHEME_BASE + i;
		UINT flags = MF_STRING;
		LPCSTR active = NWL_NodeAttrGet(scheme, "Active");
		if (strcmp(active, NA_BOOL_TRUE) == 0)
			flags |= MF_CHECKED;
		AppendMenuW(menu, flags, id, NWL_Utf8ToUcs2(name));
	}
}

void gnwinfo_show_systray_menu(HWND wnd)
{
	POINT pt;
	GetCursorPos(&pt);

	HMENU menu = CreatePopupMenu();
	if (menu)
	{
		if (g_ctx.lib.NwDrv != NULL && g_ctx.lib.NwDrv->type == WR0_DRIVER_PAWNIO)
		{
			UINT flags = MF_STRING;
			if (g_ctx.lib.NwDrv->installed == FALSE)
				flags |= MF_DISABLED | MF_CHECKED;
			AppendMenuW(menu, flags, IDM_INSTALL_PAWNIO, NWL_Utf8ToUcs2(N_(N__INSTALL_PAWNIO)));
			AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
		}

		AppendMenuW(menu, MF_STRING, IDM_CLEAN_MEM, NWL_Utf8ToUcs2(N_(N__CLEAN_MEMORY)));

		AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

		HMENU power_menu = CreatePopupMenu();
		if (power_menu)
		{
			show_power_schemes_menu(power_menu);
			AppendMenuW(menu, MF_POPUP, (UINT_PTR)power_menu, NWL_Utf8ToUcs2(N_(N__POWER_OPTIONS)));
		}

		AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

		AppendMenuW(menu, MF_STRING, IDM_EXIT, NWL_Utf8ToUcs2(N_(N__CLOSE)));

		SetForegroundWindow(wnd);
		TrackPopupMenuEx(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, wnd, NULL);
		DestroyMenu(menu);
	}
}

void gnwinfo_handle_systray_cmd(HWND wnd, WORD wmid)
{
	switch (wmid)
	{
	case IDM_INSTALL_PAWNIO:
		if (g_ctx.lib.NwDrv)
			g_ctx.lib.NwDrv->installed = FALSE;
		return;
	case IDM_CLEAN_MEM:
		gnwinfo_clean_memory();
		return;
	case IDM_EXIT:
		InterlockedExchange(&g_ctx.exit_pending, 1);
		return;
	}

	if (wmid >= IDM_POWER_SCHEME_BASE && wmid < IDM_POWER_SCHEME_BASE + MAX_POWER_SCHEMES)
	{
		DWORD (WINAPI *set_active_scheme)(HKEY, const GUID *) = NULL;
		INT index = wmid - IDM_POWER_SCHEME_BASE;
		PNODE table = NWL_NodeGetChild(g_ctx.battery, "Power Schemes");
		if (!table)
			return;
		PNODE scheme = NWL_NodeEnumChild(table, index);
		if (!scheme)
			return;
		LPCSTR attr = NWL_NodeAttrGet(scheme, "GUID");
		GUID guid = { 0 };
		if (!NWL_StrToGuid(attr, &guid))
			return;
		HMODULE dll = LoadLibraryW(L"powrprof.dll");
		if (!dll)
			return;
		*(FARPROC*)&set_active_scheme = GetProcAddress(dll, "PowerSetActiveScheme");
		if (set_active_scheme)
			set_active_scheme(NULL, &guid);
		FreeLibrary(dll);
		PostMessageW(wnd, WM_TIMER, (WPARAM)IDT_TIMER_1M, 0);
	}
}
