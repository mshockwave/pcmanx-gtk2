/**
 * Copyright (c) 2005 PCMan <hzysoft@sina.com.tw>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef TERMVIEW_H
#define TERMVIEW_H

#ifdef __GNUG__
  #pragma interface "termview.h"
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "view.h"
#include "caret.h"

#include <string>

#include <X11/Xft/Xft.h>

using namespace std;

/**
@author PCMan
*/
class CTermData;
class CHyperLink;
class CFont;

class CTermView : public CView
{
friend class CTermData;
public:
    CTermView();

    virtual bool PreKeyDown(GdkEventKey *evt);
    virtual bool OnKeyDown(GdkEventKey* evt);
    virtual void OnTextInput(const gchar* string);
    int DrawChar(int row, int col);
    void PointToLineCol(int *x, int *y, bool *left = NULL);

    GtkIMContext* m_IMContext;

    virtual void OnLButtonDown(GdkEventButton* evt);
    virtual void OnRButtonDown(GdkEventButton* evt);
    virtual void OnLButtonUp(GdkEventButton* evt);
    virtual void OnRButtonUp(GdkEventButton* evt);
    virtual void OnMouseMove(GdkEventMotion* evt);
    void OnBlinkTimer();
    virtual void OnMButtonDown(GdkEventButton* evt);
    void PasteFromClipboard(bool primary);
    virtual void DoPasteFromClipboard(string text, bool contain_ansi_color);
    void CopyToClipboard(bool primary, bool with_color, bool trim);
    void SetFont( string name, int pt_size, bool compact, bool anti_alias);
    void SetFontFamily(string name);
    void SetFont(CFont* font);
	void SetHyperLinkColor( GdkColor* clr ){	m_pHyperLinkColor = clr;	}
	static void SetWebBrowser(string browser){	m_WebBrowser = browser;	}
    CFont* GetFont(){	return m_Font;	}
    void SetHorizontalCenterAlign( bool is_hcenter );
    void SetVerticalCenterAlign( bool is_vcenter );
    void SetTermData(CTermData* data){	m_pTermData = data;	}

protected:
    void OnPaint(GdkEventExpose* evt);
    void OnSetFocus(GdkEventFocus* evt);
    void OnCreate();
    void OnSize(GdkEventConfigure* evt);
    void OnKillFocus(GdkEventFocus *evt);
	static void OnBeforeDestroy( GtkWidget* widget, CTermView* _this);
    void UpdateCaretPos();
    bool HyperLinkHitTest(int x, int y, int* start, int* end);
    void OnDestroy();
    void RecalcCharDimension();
    void GetCellSize( int &w, int &h );
protected:
	CTermData* m_pTermData;

    XftDraw* m_XftDraw;
    CFont* m_Font;

	int m_CharW;
	int m_CharH;
	int m_LeftMargin;
	int m_TopMargin;
	bool m_IsHCenterAlign;
	bool m_IsVCenterAlign;

	CCaret m_Caret;
	CHyperLink* m_pHyperLink;

	bool m_ShowBlink;
	int m_CharPaddingX;
	int m_CharPaddingY;
    GdkColor* m_pColorTable;
	GdkColor* m_pHyperLinkColor;
    GdkGC* m_GC;
    bool m_AutoFontSize;

    static string m_s_ANSIColorStr;
    int m_FontSize;
    string m_FontFamily;

	static GdkCursor* m_HandCursor;
    static string m_WebBrowser;
};

#endif
