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

#ifdef __GNUG__
  #pragma implementation "termview.h"
#endif


#include "termview.h"
#include "termdata.h"

#include "debug.h"

#include <string>

#include <gdk/gdkx.h>
#include <pango/pangoxft.h>
#include "font.h"

using namespace std;

static gboolean on_key_pressed(GtkWidget* wnd, GdkEventKey *evt, CTermView* _this)
{
	bool ret = gtk_im_context_filter_keypress(_this->m_IMContext, evt );
	if( !_this->PreKeyDown(evt) && !ret )
		ret = _this->OnKeyDown(evt);
	return ret;
}

static void on_im_commit(GtkIMContext *im, gchar *arg, CTermView* _this)
{
	_this->OnTextInput(arg);
}

static gboolean on_mouse_down(GtkWidget* widget, GdkEventButton* evt, CTermView* _this)
{
	switch(evt->button)
	{
	case 1:
		_this->OnLButtonDown(evt);
		break;
	case 2:
		_this->OnMButtonDown(evt);
		break;
	case 3:
		_this->OnRButtonDown(evt);
	}
	return true;
}

static gboolean on_mouse_up(GtkWidget* widget, GdkEventButton* evt, CTermView* _this)
{
	switch(evt->button)
	{
	case 1:
		_this->OnLButtonUp(evt);
		break;
	case 3:
		_this->OnRButtonUp(evt);
	}
	return true;
}

static gboolean on_mouse_move(GtkWidget* widget, GdkEventMotion* evt, CTermView* _this)
{
	if (evt->is_hint)
	{
		int x, y;	GdkModifierType state;
    	gdk_window_get_pointer (evt->window, &x, &y, &state);
		evt->x = x;	evt->y = y;
		evt->state = state;
	}
	_this->OnMouseMove(evt);
	return true;
}

void CTermView::OnBeforeDestroy( GtkWidget* widget, CTermView* _this)
{
	XftDrawDestroy( _this->m_XftDraw);
	_this->m_XftDraw = NULL;
	DEBUG("unrealize, destroy XftDraw");
}

GdkCursor* CTermView::m_HandCursor = NULL;
string CTermView::m_WebBrowser;

CTermView::CTermView()
        : CView(), m_pColorTable(CTermCharAttr::GetDefaultColorTable())
{
	m_pTermData = NULL;
	m_GC = NULL;
	m_ShowBlink = false;
	m_Font = NULL;
	m_XftDraw = NULL;
	m_CharW = 18;
	m_CharH = 18;
	m_LeftMargin = 0;
	m_TopMargin = 0;
	m_IsHCenterAlign = false;
	m_IsVCenterAlign = false;

	gtk_widget_add_events(m_Widget, GDK_EXPOSURE_MASK
		 | GDK_KEY_PRESS_MASK
		 | GDK_BUTTON_PRESS_MASK
		 | GDK_BUTTON_MOTION_MASK
		 | GDK_BUTTON_RELEASE_MASK
		 | GDK_POINTER_MOTION_MASK
		 | GDK_POINTER_MOTION_HINT_MASK
		 | GDK_ALL_EVENTS_MASK);

	GTK_WIDGET_SET_FLAGS(m_Widget, GTK_CAN_FOCUS);
	gtk_widget_set_double_buffered(m_Widget, false);

	g_signal_connect(G_OBJECT(m_Widget), "unrealize", G_CALLBACK(CTermView::OnBeforeDestroy), this);

	g_signal_connect(G_OBJECT(m_Widget), "key_press_event", G_CALLBACK(on_key_pressed), this);

	g_signal_connect(G_OBJECT(m_Widget), "button_press_event", G_CALLBACK(on_mouse_down), this);

	g_signal_connect(G_OBJECT(m_Widget), "button_release_event", G_CALLBACK(on_mouse_up), this);

	g_signal_connect(G_OBJECT(m_Widget), "motion_notify_event", G_CALLBACK(on_mouse_move), this);

	m_CharPaddingX = m_CharPaddingY = 0;
	m_AutoFontSize = true;
	m_pHyperLinkColor = NULL;

	m_IMContext = gtk_im_multicontext_new();
	gtk_im_context_set_use_preedit( m_IMContext, FALSE );
	g_signal_connect( G_OBJECT(m_IMContext), "commit", G_CALLBACK(on_im_commit), this );

	if( m_HandCursor )
		gdk_cursor_ref(m_HandCursor);
	else
		m_HandCursor = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_HAND2);
}


void CTermView::OnPaint(GdkEventExpose* evt)
{
	// Hide the caret to prevent drawing problems.
	m_Caret.Hide();

	PrepareDC();

	GdkDrawable* dc = m_Widget->window;
	if(!GDK_IS_DRAWABLE(dc))
	{
		DEBUG("WARNNING! Draw on DELETED widget!");
		return;
	}

	int w = m_Widget->allocation.width, h = m_Widget->allocation.height;

	if( m_pTermData )
	{
		// Only redraw the invalid area to greatly enhance performance.
		int top = evt->area.y;		int bottom = top + evt->area.height;
		int left = evt->area.x;		int right = left + evt->area.width;
		this->PointToLineCol( &left, &top );
		this->PointToLineCol( &right, &bottom );

		if(right < m_pTermData->m_ColsPerPage)	right++;
		if(bottom < m_pTermData->m_RowsPerPage-1)	bottom++;
		if(top > 0)	top-=top>1?2:1;
		int iline = m_pTermData->m_FirstLine + top;
		int ilast = m_pTermData->m_FirstLine + bottom;
		top *= m_CharH;

		for( ; iline <= ilast; iline++ , top += m_CharH )
		{
			for( int col = left; col < right; )
				col += DrawChar( iline, col, top );
		}

		gdk_gc_set_rgb_fg_color(m_GC, CTermCharAttr::GetDefaultColorTable(0));
		left = m_pTermData->m_ColsPerPage*m_CharW-2;

		/* repaint some region that should be repainted */
		gdk_draw_rectangle(dc, m_GC, true, 0, 0, m_LeftMargin, h ); // fill left margin
		gdk_draw_rectangle(dc, m_GC, true, left + m_LeftMargin, 0, w-left, h ); // fill right marin

		top = m_pTermData->m_RowsPerPage*m_CharH;
		gdk_draw_rectangle(dc, m_GC, true, 0, 0, w, m_TopMargin ); // fill top margin
		gdk_draw_rectangle(dc, m_GC, true, 0, top + m_TopMargin, w, h - top ); // fill bottom margin

		m_Caret.Show();
	}
	else
	{
		gdk_gc_set_rgb_bg_color(m_GC, CTermCharAttr::GetDefaultColorTable(0));
		gdk_draw_rectangle(dc, m_GC, true, 0, 0, w, h );
	}

}

void CTermView::OnSetFocus(GdkEventFocus* evt)
{
	gtk_im_context_focus_in(m_IMContext);
}


bool CTermView::OnKeyDown(GdkEventKey* evt)
{
	return true;
}


void CTermView::OnTextInput(const gchar* string)
{
    // Override this function to process text input.
}

void CTermView::OnCreate()
{
	CWidget::OnCreate();
	gtk_im_context_set_client_window(m_IMContext, m_Widget->window);

	m_XftDraw = XftDrawCreate(
		GDK_WINDOW_XDISPLAY(m_Widget->window),
		GDK_WINDOW_XWINDOW(m_Widget->window),
		GDK_VISUAL_XVISUAL(gdk_drawable_get_visual(m_Widget->window)),
		GDK_COLORMAP_XCOLORMAP (gdk_drawable_get_colormap(m_Widget->window)));
	XftDrawSetSubwindowMode(m_XftDraw, IncludeInferiors);

	if( !m_Font )
		m_Font = new CFont("Sans", 16);

	m_GC = gdk_gc_new(m_Widget->window);
	gdk_gc_copy(m_GC, m_Widget->style->black_gc);

	m_Caret.Create(this, m_GC);
	m_Caret.Show();
}


int CTermView::DrawChar(int line, int col, int top)
{
	GdkDrawable* dc = m_Widget->window;
	if(!GDK_IS_DRAWABLE(dc) && m_XftDraw == NULL)
	{
//		g_warning("Draw on DELETED widget!\n");
		return 1;
	}

	const char* pLine = m_pTermData->m_Screen[line];
	CTermCharAttr* pAttr = m_pTermData->GetLineAttr(pLine);
	int w = 2;
	bool is_mbcs2 = false;
	switch( pAttr[col].GetCharSet() )
	{
	case CTermCharAttr::CS_MBCS1:
		break;
	case CTermCharAttr::CS_MBCS2:
		col--;
		is_mbcs2 = true;
//		This will not cause any problem at any time because 'col' always > 0.
//		In CTermData_this->DetectCharSets() I've done some checks to ensure that the first
//		character of every lines cannot be marked as second bytes of MBCS characters.
		break;
	default:
//	case CTermCharAttr::CS_ASCII:
		w = 1;
	}

	const char* pChar = pLine + col;
	pAttr += col;
	int left = m_CharW*col + m_LeftMargin;
	top += m_TopMargin;

	bool bSel[2];	bSel[0]= IsPosInSel( col, line );
	if( w > 1 )		bSel[1] = IsPosInSel( col+1, line );

	GdkColor iFg, iBg;

	for( int i=0; i < w; i++ )	//	do the drawing
	{
		GdkColor* Fg = pAttr[i].GetFgColor( m_pColorTable );
		GdkColor* Bg = pAttr[i].GetBgColor( m_pColorTable );
		// if it's property is inverse, GetFgColor & GetBgColor will swap Bg & Fg for us.

		if( bSel[i] )	// if it's selected, reverse two colors.
		{
			iFg.red = ~Fg->red;
			iFg.green = ~Fg->green;
			iFg.blue = ~Fg->blue;
			Fg = &iFg;
			iBg.red = ~Bg->red;
			iBg.green = ~Bg->green;
			iBg.blue = ~Bg->blue;
			Bg = &iBg;

		}

		gdk_gc_set_rgb_fg_color( m_GC, Bg );

		XftColor xftclr;
		xftclr.pixel = 0;
		xftclr.color.red = Fg->red;
		xftclr.color.green = Fg->green;
		xftclr.color.blue = Fg->blue;
		xftclr.color.alpha = 0xffff;

		int bgw = m_CharW*w;

		gdk_draw_rectangle(dc, m_GC, true, left , top, bgw, m_CharH );

		gdk_gc_set_rgb_fg_color( m_GC, Fg );

		if( !pAttr[i].IsBlink() || m_ShowBlink )	// If text shold be draw.
		{
			if( ' ' != *pChar && '\0' != *pChar )
			{
				gsize wl;
				gchar *utf8_ch;
				if( w > 0 && (utf8_ch = g_convert(pChar, w, "UTF-8", m_pTermData->m_Encoding.c_str(), NULL, &wl, NULL)))
				{
					XftFont* font = m_Font->GetXftFont();
					XftDrawStringUtf8(m_XftDraw, &xftclr, font, left, top + font->ascent, (FcChar8*)utf8_ch, strlen(utf8_ch));
					g_free(utf8_ch);
				}
			}
			if( pAttr[i].IsUnderLine() )
			{
				int bottom = top + m_CharH - 1;
				gdk_draw_line(dc, m_GC, left, bottom, left + bgw, bottom);
			}
		}
		// 2004.08.07 Added by PCMan: Draw the underline of hyperlinks.
		if( pAttr[i].IsHyperLink() ) 
		{
//			dc.SetPen(wxPen(m_HyperLinkColor, 1, wxSOLID));
//			int bottom = top + m_CharH - 1;
//			dc.DrawLine(left, bottom, left+bgw, bottom);

			if(m_pHyperLinkColor)
	 			gdk_gc_set_rgb_fg_color( m_GC, m_pHyperLinkColor );
			int bottom = top + m_CharH - 1;
			gdk_draw_line(dc, m_GC, left, bottom, left + bgw, bottom);
		}

		if( w == 1 || i>=1 || (pAttr[i].IsSameAttr(pAttr[i+1].AsShort()) && bSel[0] == bSel[1]) )
			break;

		gdk_gc_set_clip_rectangle( m_GC, NULL );
		GdkRectangle rect;	rect.x=(left + m_CharW); rect.y=top; rect.width=m_CharW; rect.height=m_CharH;
		gdk_gc_set_clip_origin( m_GC, rect.x, rect.y );
		gdk_gc_set_clip_rectangle( m_GC, &rect );

		Region xregion = XCreateRegion();
		XRectangle xrect;	xrect.x=rect.x;	xrect.y=rect.y;	xrect.width=rect.width;	xrect.height=rect.height;
		XUnionRectWithRegion (&xrect, xregion, xregion);
		XftDrawSetClip(m_XftDraw, xregion );
		XDestroyRegion(xregion);
	}
	gdk_gc_set_clip_rectangle( m_GC, NULL );
	XftDrawSetClip(m_XftDraw, NULL);

	return is_mbcs2 ? 1 : w;
}


bool CTermView::IsPosInSel(int x, int y)
{
//  If the user selected from bottom to top or from right to left, we should swap the
//	coordinates to make them correct.
	int selstartx = m_pTermData->m_SelStart.x, selstarty = m_pTermData->m_SelStart.y;
	int selendx = m_pTermData->m_SelEnd.x, selendy = m_pTermData->m_SelEnd.y;
	bool one_line_selected = m_pTermData->m_SelStart.y == selendy;
/*	if( selstarty > selendy )
	{
		int tmp = selstarty; selstarty = selendy; selendy = tmp;
		if( selstartx > selendx )
		{	int tmp = selstartx; selstartx = selendx; selendx = tmp;	}
	}
	else if( (one_line_selected||m_pTermData->m_SelBlock) && selstartx > selendx )
	{	int tmp = selstartx; selstartx = selendx; selendx = tmp;	}	*/
//-------------------------------------------------------------------
	CorrectSelPos(selstartx, selstarty, selendx, selendy );
//			Check if specified position is in selected area;
	if( y < selstarty || y > selendy )
		return false;

	if( m_pTermData->m_SelBlock )	// If selected area is a block
		return (x >= selstartx && x < selendx );
	else	// normal selection with multiple lines selected
	{
		bool x_larger_than_startx = (x >= selstartx);
		bool x_smaller_than_endx = (x < selendx);
		if( one_line_selected )	// if only one line is selected
			return ( x_larger_than_startx && x_smaller_than_endx );
		if( y == selstarty )	// If in first selected line
			return x_larger_than_startx;
		else if( y == selendy )	// If in last selected line
			return x_smaller_than_endx;
	}
	return true;
}


void CTermView::CorrectSelPos(int &selstartx, int &selstarty, int &selendx, int &selendy)
{
//  If the user selected from bottom to top or from right to left, we should swap the
//	coordinates to make them correct.
	bool one_line_selected = m_pTermData->m_SelStart.y == selendy;
	if( selstarty > selendy )
	{
		int tmp = selstarty; selstarty = selendy; selendy = tmp;
		tmp = selstartx; selstartx = selendx; selendx = tmp;
	}
	if( (one_line_selected ||m_pTermData->m_SelBlock) && selstartx > selendx )
	{	int tmp = selstartx; selstartx = selendx; selendx = tmp;	}
}

void CTermView::PrepareDC()
{
	if( GDK_IS_GC(m_GC) )
	{
		gdk_gc_set_clip_origin( m_GC, 0, 0 );
		gdk_gc_set_clip_rectangle( m_GC, NULL);
	}
}


void CTermView::PointToLineCol(int *x, int *y)
{
	*x -= m_LeftMargin;
	*x /= m_CharW;
	if (*x < 0)
		*x = 0;
	else if( *x > m_pTermData->m_ColsPerPage )
		*x = m_pTermData->m_ColsPerPage;

	*y -= m_TopMargin;
	*y /= m_CharH;
	if(*y <0 )
		*y = 0;
	else if( (*y+1) > m_pTermData->m_RowsPerPage )
		*y = m_pTermData->m_RowsPerPage-1;
}

void CTermView::OnSize(GdkEventConfigure* evt)
{
	if( !m_AutoFontSize || !m_pTermData )
		return;

	int w, h;
	GetCellSize( w, h );
	m_Font->SetFont( m_Font->GetName(), w, h, m_Font->GetCompact(), m_Font->GetAntiAlias() );

	RecalcCharDimension();
}


void CTermView::OnLButtonDown(GdkEventButton* evt)
{
	SetFocus();

	if( !m_pTermData )
		return;
	SetCapture();

	int x = (int)evt->x;
	int y = (int)evt->y;

	INFO("x=%d, y=%d, grab=%d", x, y, HasCapture());

	PointToLineCol( &x, &y );
	bool need_refresh = ( m_pTermData->m_SelStart != m_pTermData->m_SelEnd );
	m_pTermData->m_SelStart.x = m_pTermData->m_SelEnd.x = x;
	m_pTermData->m_SelStart.y = m_pTermData->m_SelEnd.y = y;
	if( need_refresh )
		Refresh();
	m_pTermData->m_SelBlock = (evt->state &
		(GDK_SHIFT_MASK|GDK_MOD1_MASK|GDK_CONTROL_MASK) );
}


void CTermView::OnRButtonDown(GdkEventButton* evt)
{

}

void CTermView::OnLButtonUp(GdkEventButton* evt)
{
	if( !m_pTermData )
		return;
	ReleaseCapture();

//	Correct the selected area when users stop selecting.
//  If the user selected from bottom to top or from right to left, we should swap the
//	coordinates to make them correct.
	CorrectSelPos(m_pTermData->m_SelStart.x, m_pTermData->m_SelStart.y, 
		m_pTermData->m_SelEnd.x, m_pTermData->m_SelEnd.y);

	//	2004.08.07 Added by PCMan.  Hyperlink detection.
	//	If no text is selected, consider hyperlink.
	if( m_pTermData->m_SelStart == m_pTermData->m_SelEnd && !m_WebBrowser.empty() )
	{
		int x = (int)evt->x;
		int y = (int)evt->y;
		PointToLineCol( &x, &y );
		int start, end;
		if( HyperLinkHitTest( x, y, &start, &end ) )
		{
			char* pline = m_pTermData->m_Screen[y];
			string URL( (pline+start), (int)(end-start) );

			char *cmdline = new char[ m_WebBrowser.length() + URL.length() + 10 ];
			if( strstr(m_WebBrowser.c_str(), "%s") )
				sprintf( cmdline, m_WebBrowser.c_str(), URL.c_str() );
			else
			{
				memcpy(cmdline, m_WebBrowser.c_str(), m_WebBrowser.length());
				cmdline[m_WebBrowser.length()] = ' ';
				memcpy( &cmdline[m_WebBrowser.length() + 1], URL.c_str(), URL.length() + 1);
			}
			strcat(cmdline, " &");	// launch the browser in background.
			system(cmdline);	// Is this portable?
			delete []cmdline;
		}
	}
	else	// if there is a selected area
	{
		CopyToClipboard(true, false, false);
	}

}

void CTermView::OnRButtonUp(GdkEventButton* evt)
{
}


void CTermView::OnMouseMove(GdkEventMotion* evt)
{
	if( !m_pTermData )
		return;

	int x = (int)evt->x;
	int y = (int)evt->y;

	INFO("x=%d, y=%d, grab=%d", x, y, HasCapture());

	this->PointToLineCol( &x, &y );
	if( HasCapture() )	//	Selecting text.
	{
		int oldx = m_pTermData->m_SelEnd.x;
		int oldy = m_pTermData->m_SelEnd.y;
		//	Store new SelPos
		m_pTermData->m_SelEnd.x = x;
		m_pTermData->m_SelEnd.y = y;
		//	Update Display
		RedrawSel( oldx, oldy, x, y );
	}
	else	//	Consider hyperlink detection.
	{
		/* Todo: hyperlink detection. */
		CTermCharAttr* pattr = m_pTermData->GetLineAttr(m_pTermData->m_Screen[ y ]);
		if( x > 0 && x < m_pTermData->m_ColsPerPage && pattr[x].IsHyperLink() )
			gdk_window_set_cursor(m_Widget->window, m_HandCursor);
		else
			gdk_window_set_cursor(m_Widget->window, NULL);
	}
}


//	This function is used to update selected region.
//	However, this piece of code is not only inefficient but also ugly.
//	Although it works, by no means can this be a good solution.
//	Leaving it temporarily because I don't have much time to do optimization.
//	Maybe someday there'll be someone who can fix it .
#define iMAX(x,y) ((x)>(y)?(x):(y))
#define iMIN(x,y) ((x)>(y)?(y):(x))
#define MAX3(x,y,z) iMAX(iMAX(x,y),z)
#define MIN3(x,y,z) iMIN(iMIN(x,y),z)
void CTermView::RedrawSel(int oldx, int oldy, int newx, int newy)
{
	//	Always remember to hide the caret before drawing.
	m_Caret.Hide();

	GdkDrawable* dc = m_Widget->window;
	PrepareDC();

	int starty;	int	endy;
	if( m_pTermData->m_SelBlock )
	{
		starty = MIN3( m_pTermData->m_SelStart.y, oldy, m_pTermData->m_SelEnd.y );
		endy = MAX3(m_pTermData->m_SelStart.y, oldy, m_pTermData->m_SelEnd.y );
	}
	else
	{
		if( oldy < newy )	// select downward
		{	starty = oldy;	endy = newy;	}
		else	//	upward
		{	starty = newy;	endy = oldy;	}
	}

	// This is not efficient at all.  Apparaently it needs some optimization.
	int top = starty*m_CharH;
	for( int y= starty; y <= endy; y++, top+=m_CharH )
	{
//		CTermCharAttr* pAttr = m_pTermData->GetLineAttr(m_pTermData->m_Screen[starty]);
		for( int x=0; x< m_pTermData->m_ColsPerPage;  )
			x += DrawChar( y, x, top );
	}

	//	Show the caret again but only set its visibility without display it immediatly.
	m_Caret.Show( false );
}
#undef MAX3
#undef MIN3
#undef iMIN
#undef iMAX

void CTermView::OnKillFocus(GdkEventFocus *evt)
{
	gtk_im_context_focus_out(m_IMContext);
}

// Preview the GdkEventKey before it's sent to OnKeyDown
// regardless of the returned value of im_context_filter_key_press.
bool CTermView::PreKeyDown(GdkEventKey *evt)
{
	return false;
}

void CTermView::OnBlinkTimer()
{
	m_ShowBlink = !m_ShowBlink;
	if(m_pTermData)
	{
		PrepareDC();
		int top = 0;
		int iline = m_pTermData->m_FirstLine;
		int ilast = m_pTermData->m_FirstLine+m_pTermData->m_RowsPerPage-1;
//		const int left = 0;	const int right = m_pTermData->m_ColsPerPage;
		for( ; iline <= ilast; iline++ , top += m_CharH )
		{
//			DrawLine( dc, i, y, true , left, right );
			CTermCharAttr* attr = m_pTermData->GetLineAttr( m_pTermData->m_Screen[iline] );
			for( int col = 0; col < m_pTermData->m_ColsPerPage; )
			{
				if( attr[col].IsBlink())
					col += DrawChar( iline, col, top );
				else
					col++;
			}
		}
	}
	m_Caret.Blink();
}

void CTermView::OnMButtonDown(GdkEventButton* evt)
{
	PasteFromClipboard(true);
}


string CTermView::m_s_ANSIColorStr;

void CTermView::PasteFromClipboard(bool primary)
{
	string text;
	if(m_s_ANSIColorStr.empty())
	{
		GtkClipboard* clipboard = gtk_clipboard_get( primary ? GDK_SELECTION_PRIMARY : GDK_NONE);
		INFO("paste");
		gchar* utext = gtk_clipboard_wait_for_text(clipboard);
		if( !utext )
			return;
		INFO("%s", utext);
		gsize wl;

		DoPasteFromClipboard( string(utext), false);
		g_free(utext);
	}
	else
		DoPasteFromClipboard(m_s_ANSIColorStr, true);
}

void CTermView::DoPasteFromClipboard(string text, bool contain_ansi_color)
{

}

void CTermView::CopyToClipboard(bool primary, bool with_color, bool trim)
{
	string text;
	if(!m_pTermData)
		return;
	if( with_color )
		text = m_pTermData->GetSelectedTextWithColor(trim);
	else
	{
		m_s_ANSIColorStr = "";
		text = m_pTermData->GetSelectedText(trim);
	}

	gsize wl = 0;
	const gchar* utext = g_convert_with_fallback( text.c_str(), text.length(),
			"utf-8", m_pTermData->m_Encoding.c_str(), "?", NULL, &wl, NULL);
	if(!utext)
		return;

	if( with_color )
		m_s_ANSIColorStr = string(utext);
	else
	{
		GtkClipboard* clipboard = gtk_clipboard_get(  primary ? GDK_SELECTION_PRIMARY : GDK_NONE );
		gtk_clipboard_set_text(clipboard, utext, wl );
	}
	INFO("select: %s", utext);
	g_free((void*)utext);
}

void CTermView::GetCellSize( int &w, int &h )
{
	if( !m_pTermData->m_ColsPerPage || !m_pTermData->m_RowsPerPage )
	{
		w = 0;
		h = 0;
		return;
	}
			
	w = ( m_Widget->allocation.width / m_pTermData->m_ColsPerPage ) - m_CharPaddingX;
	h = ( m_Widget->allocation.height / m_pTermData->m_RowsPerPage ) - m_CharPaddingY;
}

void CTermView::SetFont(CFont* font)
{
	if( !font )
		return;

	if( m_Font )
		delete m_Font;

	if( m_AutoFontSize )
	{
		int w, h;
		GetCellSize( w, h );
		m_Font = new CFont( font->GetName(), w, h, font->GetCompact(), font->GetAntiAlias() );
		delete font;
	}
	else
		m_Font = font;
	RecalcCharDimension();
}

void CTermView::SetFont( string name, int pt_size, bool compact, bool anti_alias )
{
	if( m_Font )
		delete m_Font;
	if( m_AutoFontSize )
	{
		int w, h;
		GetCellSize( w, h );
		m_Font = new CFont( name, w, h, compact, anti_alias );
	}
	else
		m_Font = new CFont( name, pt_size, compact, anti_alias );
	RecalcCharDimension();
}

void CTermView::SetFontFamily( string name )
{
	if( m_AutoFontSize )
	{
		int w, h;
		GetCellSize( w, h );
		m_Font->SetFont( name, w, h, m_Font->GetCompact(), m_Font->GetAntiAlias() );
	}
	else
		m_Font->SetFontFamily( name );
	RecalcCharDimension();
}

void CTermView::SetHorizontalCenterAlign( bool is_hcenter )
{
	if( m_IsHCenterAlign == is_hcenter || !m_pTermData )
		return;

	if( (m_IsHCenterAlign = is_hcenter) && GTK_WIDGET_REALIZED(m_Widget) )
		m_LeftMargin = (m_Widget->allocation.width - m_CharW * m_pTermData->m_ColsPerPage ) / 2 ;
	else
		m_LeftMargin = 0;

	if( IsVisible() )
		Refresh();
	UpdateCaretPos();
}

void CTermView::SetVerticalCenterAlign( bool is_vcenter )
{
	if( m_IsVCenterAlign == is_vcenter || !m_pTermData )
		return;

	if( (m_IsVCenterAlign = is_vcenter) && GTK_WIDGET_REALIZED(m_Widget) )
		m_TopMargin = (m_Widget->allocation.height - m_CharH * m_pTermData->m_RowsPerPage ) / 2 ;
	else
		m_TopMargin = 0;

	if( IsVisible() )
		Refresh();
	UpdateCaretPos();
}

void CTermView::UpdateCaretPos()
{
	if( !m_pTermData )
		return;

	int x = m_pTermData->m_CaretPos.x * m_CharW + m_LeftMargin;
	int y = (m_pTermData->m_CaretPos.y + 1) * m_CharH - 2 + m_TopMargin;
	m_Caret.Move( x, y );

	GdkRectangle rc;
	rc.x = x;	rc.y = y; 	rc.width = 0;	rc.height = 0;
	gtk_im_context_set_cursor_location(m_IMContext, &rc);
}


bool CTermView::HyperLinkHitTest(int x, int y, int* start, int* end)
{
	char* pline = m_pTermData->m_Screen[y];
	CTermCharAttr* pattr = m_pTermData->GetLineAttr(pline);
	if( x > 0 && x < m_pTermData->m_ColsPerPage && pattr[x].IsHyperLink() )
	{
		int _start, _end;
		for( _start = x-1; _start > 0 && pattr[_start].IsHyperLink(); _start-- );
		if( !pattr[_start].IsHyperLink() )
			_start++;
		for( _end = x+1; _end < m_pTermData->m_ColsPerPage && pattr[_end].IsHyperLink(); _end++ );
		*start = _start;
		*end = _end;
		INFO("%d, %d : %d, %d", x, y, _start, _end);
		return true;
	}
	return false;
}


void CTermView::OnDestroy()
{
	if( m_Font )
		delete m_Font;
	if( m_pTermData )
		m_pTermData->m_pView = NULL;

	if( m_HandCursor )
		gdk_cursor_unref(m_HandCursor);
	if( m_HandCursor->ref_count <= 0 )
		m_HandCursor = NULL;

	CView::OnDestroy();	// Remember to destruct parent
}

void CTermView::RecalcCharDimension()
{
	m_CharW = m_Font->GetWidth();
	m_CharH = m_Font->GetHeight();

	if( m_IsHCenterAlign )
		m_LeftMargin = (m_Widget->allocation.width - m_CharW * m_pTermData->m_ColsPerPage ) / 2;
	else
		m_LeftMargin = 0;

	if( m_IsVCenterAlign )
		m_TopMargin = (m_Widget->allocation.height - m_CharH * m_pTermData->m_RowsPerPage ) / 2;
	else
		m_TopMargin = 0;

	m_Caret.SetSize(m_CharW, 2);
	UpdateCaretPos();
	m_Caret.Show();
}

