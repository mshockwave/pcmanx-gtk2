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
  #pragma implementation "mainframe.h"
#endif


#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <stdio.h>
#include <string.h>

#include "mainframe.h"

#include "telnetview.h"
#include "notebook.h"
#include "telnetcon.h"

#include "inputdialog.h"
#include "editfavdlg.h"
#include "sitedlg.h"
#include "prefdlg.h"
#include "appconfig.h"
#include "sitelistdlg.h"
#include "emoticondlg.h"

#include "debug.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#ifdef USE_NOTIFIER
#include "notifier/api.h"
#endif

#ifdef USE_SCRIPT
#include "script/api.h"
#endif

#ifdef USE_DOCKLET
#include "docklet/api.h"

void CMainFrame::OnTrayButton_Toggled(
	GtkToggleButton *button,
	CMainFrame *_this)
{
	static bool hide_next_time = TRUE;
	if (hide_next_time) {
		_this->Hide();
		hide_next_time = FALSE;
	}
	else {
		_this->Show();
		hide_next_time = TRUE;
	}
}

/*
void CMainFrame::OnTrayButton_Changed(GtkWidget* widget, GtkAllocation *allocation, CMainFrame* _this)
{
	if (! _this->m_MainIcon)
		return;
	_this->set_tray_icon();
}
*/

void CMainFrame::set_tray_icon()
{
        int panel_w;
        int panel_h;
        gtk_window_get_size (GTK_WINDOW(gtk_widget_get_toplevel(m_TrayButton)),
                        &panel_w, &panel_h);

        int icon_size = (panel_h < 30) ? 16 : 24;
        GdkPixbuf *tray_icon_pixbuf = gdk_pixbuf_copy (m_MainIcon);

        /*
         * Scale the icon rather than clip it if our allocation just isn't
         * what we want it to be.
         */
        int ori_icon_size = gdk_pixbuf_get_height(tray_icon_pixbuf);
		if( ori_icon_size > icon_size )
        {
                int new_size = icon_size;
                GdkPixbuf *new_pixbuf;

                new_pixbuf = gdk_pixbuf_scale_simple (tray_icon_pixbuf,
                                new_size, new_size,
                                GDK_INTERP_BILINEAR);

                g_object_unref (tray_icon_pixbuf);
                tray_icon_pixbuf = new_pixbuf;
        }

        gtk_image_set_from_pixbuf(GTK_IMAGE (m_TrayIcon), tray_icon_pixbuf);
        g_object_unref (tray_icon_pixbuf);
}
#endif

bool CMainFrame::g_bIsUpateHandlerExisted = false;
bool CMainFrame::g_bUpdateingBBSList = false;
CMainFrame* CMainFrame::g_pMyself = NULL;

gboolean CMainFrame::OnSize( GtkWidget* widget, GdkEventConfigure* evt, CMainFrame* _this )
{
	gtk_window_get_position( GTK_WINDOW(widget), &AppConfig.MainWndX, & AppConfig.MainWndY );
	AppConfig.MainWndW = evt->width;
	AppConfig.MainWndH = evt->height;
	INFO("x=%d, y=%d, w=%d, h=%d", evt->x, evt->y, evt->width, evt->height );
	INFO("get_pos: x=%d, y=%d",AppConfig.MainWndX, AppConfig.MainWndY );
	return false;
}

CMainFrame::CMainFrame()
{
	m_pView = NULL;
	m_FavoritesMenuItem = NULL;
	m_FavoritesMenu = NULL;
	m_IsFlashing = false;

	m_Widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	PostCreate();

#if defined(USE_DOCKLET) || defined(USE_NOTIFIER)
	/* We need to make sure m_MainIcon is null at startup. */
	m_MainIcon = (GdkPixbuf *) NULL;
#endif

	LoadIcons();

#ifdef USE_DOCKLET
	m_TrayIcon_Instance = egg_tray_icon_new ("applet");

	m_TrayButton = gtk_toggle_button_new ();
	gtk_button_set_relief (GTK_BUTTON (m_TrayButton), GTK_RELIEF_NONE);
	gtk_container_add (GTK_CONTAINER (m_TrayIcon_Instance), m_TrayButton);
	gtk_widget_show (m_TrayButton);

	g_signal_connect (G_OBJECT (m_TrayButton), "toggled",
			G_CALLBACK (CMainFrame::OnTrayButton_Toggled), this);

/*
	g_signal_connect (G_OBJECT (m_TrayButton), "size-allocate",
			G_CALLBACK (CMainFrame::OnTrayButton_Changed), this);
*/

	m_TrayIcon = gtk_image_new ();
	gtk_container_add (GTK_CONTAINER (m_TrayButton), m_TrayIcon);
	gtk_widget_show (m_TrayIcon);

	set_tray_icon();
#endif


	gtk_window_set_title (GTK_WINDOW (m_Widget), "PCMan X "VERSION );
	
	m_pNotebook = new CNotebook();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(m_pNotebook->m_Widget), TRUE);
	g_signal_connect( G_OBJECT(m_pNotebook->m_Widget), "button_press_event", 
			G_CALLBACK(CMainFrame::OnNotebookPopupMenu), this );

	MakeUI();

	gtk_window_set_icon((GtkWindow*)m_Widget, m_MainIcon);

	GtkWidget* vbox = gtk_vbox_new(false, 0);
	gtk_widget_show (vbox);

	GtkWidget* status_bar = gtk_statusbar_new ();

	gtk_container_add(GTK_CONTAINER(m_Widget), vbox);

	gtk_box_pack_start (GTK_BOX (vbox), m_Menubar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), m_Toolbar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), m_pNotebook->m_Widget, TRUE, TRUE, 0);
	gtk_widget_set_size_request(m_pNotebook->m_Widget, 300, 200);
	gtk_box_pack_start (GTK_BOX (vbox), status_bar, FALSE, FALSE, 0);

	gtk_widget_grab_focus(m_pNotebook->m_Widget);

//	GTK_WIDGET_UNSET_FLAGS(m_pNotebook->m_Widget, GTK_CAN_FOCUS);

	g_signal_connect(G_OBJECT(m_Widget), "delete-event", G_CALLBACK(CMainFrame::OnClose), this);

	g_signal_connect(m_pNotebook->m_Widget, "switch-page", G_CALLBACK(CMainFrame::OnNotebookChangeCurPage), this);

	g_signal_connect(m_Widget, "configure-event", G_CALLBACK(CMainFrame::OnSize), this);

	g_signal_connect(G_OBJECT(m_Widget), "focus-in-event", G_CALLBACK(CMainFrame::OnActivated), this);

//	g_signal_connect(G_OBJECT(m_Widget), "focus-out-event", G_CALLBACK(CMainFrame::OnDeactivated), this);

	gtk_box_set_spacing( GTK_BOX (status_bar), 4 );
#ifdef USE_NANCY
	m_StatusBarBotState = (GtkLabel*)gtk_label_new("");
	gtk_box_pack_start (GTK_BOX (status_bar), (GtkWidget*)m_StatusBarBotState, FALSE, FALSE, 2);
	GtkWidget* vsep = gtk_vseparator_new ();
 	gtk_box_pack_start (GTK_BOX (status_bar), vsep, FALSE, FALSE, 2);
#endif
	m_StatusBarTime = (GtkLabel*)gtk_label_new("");
	gtk_box_pack_start (GTK_BOX (status_bar), (GtkWidget*)m_StatusBarTime, FALSE, FALSE, 2);
	if (AppConfig.ShowStatusBar) {
		gtk_widget_show_all(status_bar);
	}
	
	m_BlinkTimer = g_timeout_add(600, (GSourceFunc)CMainFrame::OnBlinkTimer, this );
	m_EverySecondTimer = g_timeout_add(1000, (GSourceFunc)CMainFrame::OnEverySecondTimer, this );

	CTelnetView::SetParentFrame(this);
	CTelnetView::SetWebBrowser(AppConfig.WebBrowser);
	CTelnetView::SetMailClient(AppConfig.MailClient);
	CTelnetView::setWgetFiles(AppConfig.UseWgetFiles);
}


CTelnetCon* CMainFrame::NewCon(string title, string url, CSite* site )
{
	if( !site )
		site = &AppConfig.m_DefaultSite;

	m_pView = new CTelnetView;
	m_Views.push_back(m_pView);

	CTelnetCon* pCon;

	pCon = new CTelnetCon( m_pView, *site );

	m_pView->m_pTermData = pCon;
	m_pView->SetContextMenu(m_EditMenu);
	m_pView->SetFont(AppConfig.FontFamily, AppConfig.FontSize, AppConfig.CompactLayout, AppConfig.AntiAliasFont);
	m_pView->SetHyperLinkColor( &AppConfig.HyperLinkColor );
	m_pView->SetHorizontalCenterAlign( AppConfig.HCenterAlign );
	m_pView->SetVerticalCenterAlign( AppConfig.VCenterAlign );
	m_pView->m_CharPaddingX = AppConfig.CharPaddingX;
	m_pView->m_CharPaddingY = AppConfig.CharPaddingY;

	pCon->m_Site.m_Name = title;
	pCon->m_Site.m_URL = url;
	pCon->m_Encoding = pCon->m_Site.m_Encoding;

	pCon->AllocScreenBuf( site->m_RowsPerPage, site->m_RowsPerPage, site->m_ColsPerPage );
	
	int idx = m_pNotebook->AddPage( m_pView, title, m_ConnIcon );
	m_pNotebook->SetCurPage(idx);
	m_pView->SetFocus();

	pCon->Connect();

	return pCon;
}

GtkActionEntry CMainFrame::entries[] =
  {
    {"connect_menu", NULL, _("_Connect")},
    {"site_list", GTK_STOCK_OPEN, _("_Site List"), "<Alt>S", NULL, G_CALLBACK (CMainFrame::OnSiteList)},
    {"update_bbs_list", GTK_STOCK_REFRESH, _("Update BBS List"), NULL, NULL, G_CALLBACK (CMainFrame::updateBBSList)},
    {"new_con", GTK_STOCK_NETWORK, _("_New Connection"), "<Alt>Q", NULL, G_CALLBACK (CMainFrame::OnNewCon)},
    {"reconnect", GTK_STOCK_UNDO, _("_Reconnect"), "<Alt>R", NULL, G_CALLBACK (CMainFrame::OnReconnect)},
    {"close", GTK_STOCK_CLOSE, _("_Close Connection"), "<Alt>W", NULL, G_CALLBACK (CMainFrame::OnCloseCon)},
    {"next_con", GTK_STOCK_GO_DOWN, _("Next Page"), "<Alt>X", NULL, G_CALLBACK (CMainFrame::OnNextCon)},
    {"previous_con", GTK_STOCK_GO_UP, _("Previous Page"), "<Alt>Z", NULL, G_CALLBACK (CMainFrame::OnPrevCon)},
    {"jump", GTK_STOCK_JUMP_TO, _("_Jump to")},
    {"quit", GTK_STOCK_QUIT, _("_Quit"), "", NULL, G_CALLBACK (CMainFrame::OnQuit)},
    {"edit_menu", NULL, _("_Edit")},
    {"copy", GTK_STOCK_COPY, _("_Copy"), "<Alt>O", NULL, G_CALLBACK (CMainFrame::OnCopy)},
    {"copy_with_ansi", GTK_STOCK_SELECT_COLOR, _("Copy with A_NSI Color"), NULL, NULL, G_CALLBACK (CMainFrame::OnCopyWithColor)},
    {"paste", GTK_STOCK_PASTE, _("_Paste"), "<Alt>P", NULL, G_CALLBACK (CMainFrame::OnPaste)},
    {"paste_from_clipboard", GTK_STOCK_PASTE, _("Paste from Clipboard"), "<Shift>Insert", NULL, G_CALLBACK (CMainFrame::pasteFromClipboard)},
    {"select_all", NULL, _("Select A_ll"), NULL, NULL, G_CALLBACK (CMainFrame::OnSelectAll)},
    {"emoticon", NULL, _("_Emoticons"), "<Ctrl>Return", NULL, G_CALLBACK (CMainFrame::OnEmoticons)},
    {"preference", GTK_STOCK_PREFERENCES, _("_Preference"), NULL, NULL, G_CALLBACK (CMainFrame::OnPreference)},
    {"favorites_menu", NULL, _("F_avorites")},
    {"add_to_fav", GTK_STOCK_ADD, _("_Add to Favorites"), NULL, NULL, G_CALLBACK (CMainFrame::OnAddToFavorites)},
    {"edit_fav", GTK_STOCK_EDIT, _("_Edit Favorites"), NULL, NULL, G_CALLBACK (CMainFrame::OnEditFavorites)},
    {"view_menu", NULL, _("_View")},
    {"font", GTK_STOCK_SELECT_FONT, NULL, NULL, NULL, G_CALLBACK (CMainFrame::OnFont)},
#ifdef USE_NANCY
    {"cur_bot_menu", GTK_STOCK_EXECUTE, _("Bot (Current Connection)")},
    {"all_bot_menu", GTK_STOCK_EXECUTE, _("Bot (All Opened Connections)")},
#endif
    {"help_menu", NULL, _("_Help")},
    {"about", GTK_STOCK_ABOUT, NULL, NULL, NULL, G_CALLBACK (CMainFrame::OnAbout)}
  };

#ifdef USE_MOUSE
GtkToggleActionEntry CMainFrame::mouse_toggle_entries[] =
  {
    {"mouse_support", NULL, _("Mouse Support"), NULL, NULL, G_CALLBACK (CMainFrame::OnToggleMouse), false}
  };
#endif

#ifdef USE_NANCY
GtkRadioActionEntry CMainFrame::cur_bot_entries[] =
  {
    {"disable_cur_bot", NULL, _("Disable Bot"), NULL, NULL, 0},
    {"nancy_bot_current", NULL, _("Nancy Bot"), NULL, NULL, 1}
  };

GtkRadioActionEntry CMainFrame::all_bot_entries[] =
  {
    {"disable_all_bot", NULL, _("Disable Bot"), NULL, NULL, 0},
    {"nancy_bot_all", NULL, _("Nancy Bot"), NULL, NULL, 1}
  };
#endif

static const char *ui_info = 
  "<ui>"
  "  <menubar>"
  "    <menu action='connect_menu'>"
  "      <menuitem action='site_list'/>"
  "      <menuitem action='update_bbs_list'/>"
  "      <menuitem action='new_con'/>"
  "      <menuitem action='reconnect'/>"
  "      <menuitem action='close'/>"
  "      <separator/>"
  "      <menuitem action='next_con'/>"
  "      <menuitem action='previous_con'/>"
  "      <menuitem action='jump'/>"
  "      <separator/>"
  "      <menuitem action='quit'/>"
  "    </menu>"
  "    <menu action='edit_menu'>"
  "      <menuitem action='copy'/>"
  "      <menuitem action='copy_with_ansi'/>"
  "      <menuitem action='paste'/>"
  "      <menuitem action='paste_from_clipboard'/>"
  "      <menuitem action='select_all'/>"
  "      <separator/>"
  "      <menuitem action='emoticon'/>"
  "      <menuitem action='preference'/>"
  "    </menu>"
  "    <menu action='favorites_menu'>"
  "      <separator/>"
  "      <menuitem action='add_to_fav'/>"
  "      <menuitem action='edit_fav'/>"
  "    </menu>"
  "    <menu action='view_menu'>"
  "      <menuitem action='font'/>"
#ifdef USE_NANCY
  "      <separator/>"
  "      <menu action='cur_bot_menu'>"
  "        <menuitem action='disable_cur_bot'/>"
  "        <menuitem action='nancy_bot_current'/>"
  "      </menu>"
  "      <menu action='all_bot_menu'>"
  "        <menuitem action='disable_all_bot'/>"
  "        <menuitem action='nancy_bot_all'/>"
  "      </menu>"
#endif
#ifdef USE_MOUSE
  "      <menuitem action='mouse_support'/>"
#endif
  "    </menu>"
  "    <menu action='help_menu'>"
  "      <menuitem action='about'/>"
  "    </menu>"
  "  </menubar>"
  "  <toolbar>"
  "    <separator/>"
  "    <toolitem action='site_list'/>"
  "    <toolitem action='new_con'/>"
  "    <toolitem action='reconnect'/>"
  "    <toolitem action='close'/>"
  "    <separator/>"
  "    <toolitem action='copy'/>"
  "    <toolitem action='copy_with_ansi'/>"
  "    <toolitem action='paste'/>"
  "    <separator/>"
  "    <toolitem action='add_to_fav'/>"
  "    <toolitem action='preference'/>"
  "    <toolitem action='about'/>"
  "    <toolitem action='update_bbs_list'/>"
  "    <separator/>"
  "  </toolbar>"
  "</ui>";

void CMainFrame::MakeUI()
{
  GtkActionGroup * action_group = gtk_action_group_new("GlobalActions");

  gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);

  gtk_action_group_add_actions(action_group, entries, G_N_ELEMENTS(entries), this);

#ifdef USE_MOUSE
  gtk_action_group_add_toggle_actions(action_group, mouse_toggle_entries,
				      G_N_ELEMENTS(mouse_toggle_entries), this);
#endif

#ifdef USE_NANCY
  gtk_action_group_add_radio_actions(action_group,
				     cur_bot_entries,
				     G_N_ELEMENTS(cur_bot_entries),
				     0,
				     G_CALLBACK (CMainFrame::OnChangeCurrentBot),
				     this);
  gtk_action_group_add_radio_actions(action_group,
				     all_bot_entries,
				     G_N_ELEMENTS(all_bot_entries),
				     0,
				     G_CALLBACK (CMainFrame::OnChangeAllBot),
				     this);
#endif

  m_UIManager = gtk_ui_manager_new();
  gtk_ui_manager_insert_action_group(m_UIManager, action_group, 0);

  GtkAccelGroup* accel_group = gtk_ui_manager_get_accel_group ( m_UIManager );
  gtk_window_add_accel_group (GTK_WINDOW (m_Widget), accel_group);
	
  GError * error = NULL;
  if (!gtk_ui_manager_add_ui_from_string(m_UIManager, ui_info, -1, & error))
    {
      g_message("Building menu failed : %s", error->message);
      g_error_free(error); exit(EXIT_FAILURE);
    }

  m_Menubar = gtk_ui_manager_get_widget (m_UIManager, "/ui/menubar");
  m_Toolbar = gtk_ui_manager_get_widget (m_UIManager, "/ui/toolbar");

  m_EditMenu = gtk_ui_manager_get_widget (m_UIManager, "/ui/menubar/edit_menu");
  m_FavoritesMenuItem = gtk_ui_manager_get_widget (m_UIManager, "/ui/menubar/favorites_menu");

#ifdef USE_NANCY

  m_DisableCurBotRadio = (GtkRadioMenuItem*) gtk_ui_manager_get_widget (m_UIManager,
			 "/ui/menubar/view_menu/cur_bot_menu/disable_cur_bot");
  m_CurBotNancyRadio = (GtkRadioMenuItem*) gtk_ui_manager_get_widget (m_UIManager,
		       "/ui/menubar/view_menu/cur_bot_menu/nancy_bot_current");
  
  m_DisableAllBotRadio = (GtkRadioMenuItem*) gtk_ui_manager_get_widget (m_UIManager,
       			 "/ui/menubar/view_menu/all_bot_menu/disable_all_bot");
  m_AllBotNancyRadio = (GtkRadioMenuItem*) (gtk_ui_manager_get_widget (m_UIManager,
		       "/ui/menubar/view_menu/all_bot_menu/nancy_bot_all"));

#endif

  GtkWidget* jump = gtk_ui_manager_get_widget (m_UIManager, "/ui/menubar/connect_menu/jump");
  
  GtkWidget* jump_menu = gtk_menu_new ();
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (jump), jump_menu);
  
  const char* page_str = _("Page");
  for(int i = 1; i < 11; i++)
    {
      long keyval = (i == 10 ? GDK_0 : GDK_0 + +i);
      char title[32];
      sprintf(title, "%s _%d", page_str, i);
      
      GtkWidget* jump_item = m_JumpMenuItems[i-1] = gtk_menu_item_new_with_mnemonic ( title );
      gtk_widget_show (jump_item);
      gtk_container_add (GTK_CONTAINER (jump_menu), jump_item);
      gtk_widget_add_accelerator (jump_item, "activate", accel_group,
				  keyval, (GdkModifierType) GDK_MOD1_MASK,
				  GTK_ACCEL_VISIBLE);
      g_signal_connect( G_OBJECT(jump_item), "activate",
			G_CALLBACK (CMainFrame::OnJumpToPage),
			this);
    }

  GtkWidget* sep = (GtkWidget*)gtk_separator_tool_item_new();
  gtk_widget_show(sep);
  gtk_container_add (GTK_CONTAINER (m_Toolbar), sep);
  // Create the URL address bar
  GtkWidget* url_bar = gtk_hbox_new (FALSE, 0);
  GtkWidget* url_label = (GtkWidget*) gtk_label_new_with_mnemonic(_("A_ddress:"));
  m_URLEntry = (GtkWidget*) gtk_entry_new();
  gtk_widget_set_size_request(m_URLEntry, 0, -1);
  //gtk_tooltips_set_tip(tooltips, m_URLEntry, _("Type URL here, then hit \"Enter\""), NULL);
  gtk_label_set_mnemonic_widget(GTK_LABEL(url_label), m_URLEntry);
  gtk_box_pack_start( GTK_BOX(url_bar), url_label, FALSE, FALSE, 4);
  gtk_box_pack_start( GTK_BOX(url_bar), m_URLEntry, TRUE, TRUE, 4);
  
  GtkToolItem* url_bar_item = gtk_tool_item_new();
  gtk_tool_item_set_expand(url_bar_item, true);
  gtk_container_add (GTK_CONTAINER (url_bar_item), url_bar);
  gtk_widget_show_all ( (GtkWidget*)url_bar_item);
  gtk_toolbar_insert(GTK_TOOLBAR(m_Toolbar), url_bar_item, -1);
  
  g_signal_connect ((gpointer) m_URLEntry, "key-press-event",
		    G_CALLBACK (CMainFrame::OnURLEntryKeyDown),
		    this);
  g_signal_connect ((gpointer) m_URLEntry, "focus-out-event",
		    G_CALLBACK (CMainFrame::OnURLEntryKillFocus),
		    this);
  
  CreateFavoritesMenu();
  
  //popupmenu = gtk_ui_manager_get_widget(m_UIManager, "/PopMenu");
	
}

void CMainFrame::OnNewCon(GtkMenuItem* mitem, CMainFrame* _this)
{
	CInputDialog* dlg = new CInputDialog( _this, _("Connect"), _("Host IP Address:\nAppend port number to IP with a separating colon if it's not 23."), NULL, true );
	if( dlg->ShowModal() == GTK_RESPONSE_OK && !dlg->GetText().empty() )
	{
		_this->NewCon( dlg->GetText(), dlg->GetText() );
	}
	dlg->Destroy();
}

void CMainFrame::OnQuit(GtkMenuItem* mitem, CMainFrame* _this)
{
	if( _this->CanClose() )
	{
		_this->Hide();
		_this->Destroy();
	}
}


#include "pcmanx_xpm.xpm"
#include "pcmanx_inverse_xpm.xpm"
#include "conn_xpm.xpm"

void CMainFrame::LoadIcons()
{
	m_MainIcon = gdk_pixbuf_new_from_xpm_data((const char**)pcmanx_xpm);
	m_InverseMainIcon = gdk_pixbuf_new_from_xpm_data((const char**)pcmanx_inverse_xpm);
	m_ConnIcon = gdk_pixbuf_new_from_xpm_data((const char**)conn_xpm);
}


void CMainFrame::OnFont(GtkMenuItem* mitem, CMainFrame* _this)
{
	GtkWidget* dlg = gtk_font_selection_dialog_new(_("Font"));
	gtk_window_set_modal( (GtkWindow*)dlg, true);
	gtk_window_set_transient_for( (GtkWindow*)dlg, (GtkWindow*)_this->m_Widget);

	GtkFontSelectionDialog* fsdlg = (GtkFontSelectionDialog*)dlg;
	GtkWidget* apply_to_all = gtk_check_button_new_with_label( _("Apply to all opened pages") );
	gtk_widget_show(apply_to_all);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(apply_to_all), true);
	gtk_box_pack_start( GTK_BOX(fsdlg->action_area), apply_to_all, true, true, 4);
	gtk_box_reorder_child( GTK_BOX(fsdlg->action_area), apply_to_all, 0 );
	gtk_box_set_homogeneous(GTK_BOX(fsdlg->action_area), false);

	// This is not a good method because fontsel is a private member of GtkFontSelectionDialog.
	// But we need this functionality.
	GtkFontSelection* fontsel = GTK_FONT_SELECTION(fsdlg->fontsel);
	gtk_widget_set_sensitive(fontsel->face_list, false);

	char pango_font_name[32];
	sprintf( pango_font_name, "%s %d", AppConfig.FontFamily.c_str(), (AppConfig.FontSize > 6 && AppConfig.FontSize <= 72) ? AppConfig.FontSize : 12 );
	gtk_font_selection_dialog_set_font_name(fsdlg, pango_font_name);

	if( gtk_dialog_run((GtkDialog*)dlg) == GTK_RESPONSE_OK )
	{
		gchar* name = gtk_font_selection_dialog_get_font_name( fsdlg );
		PangoFontDescription* desc = pango_font_description_from_string( name );
		g_free( name );
		const char* family = pango_font_description_get_family(desc);
		AppConfig.FontFamily = family;
		AppConfig.FontSize = pango_font_description_get_size(desc);
		pango_font_description_free(desc);

		if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(apply_to_all) ) )
		{
			vector<CTelnetView*>::iterator it;
			for( it = _this->m_Views.begin(); it != _this->m_Views.end(); ++it )
				(*it)->SetFontFamily(AppConfig.FontFamily);
			/// FIXME: Poor design! Different connection must be allowed to use different fonts in the future.
		}
		else if( _this->GetCurView() )
			_this->GetCurView()->SetFontFamily(AppConfig.FontFamily);

		gtk_widget_destroy(dlg);

		if( _this->GetCurView() )
			_this->GetCurView()->Refresh();
	}
	else
		gtk_widget_destroy(dlg);
}


void CMainFrame::OnAbout(GtkMenuItem* mitem, CMainFrame* _this)
{
	char* authors = _(
			"Hong Jen Yee (Main developer) <hzysoft@sina.com.tw>\n"
			"Jim Huang (Developer) <jserv@kaffe.org>\n"
			"Kanru Chen (Developer) <koster@debian.org.tw>\n"
			"Chia I Wu (Developer) <b90201047@ntu.edu.tw>\n"
			"Shih-yuan Lee (Developer) <fourdollars@gmail.com>\n"
			"Youchen Lee (Developer) <youchen.lee@gmail.com>\n"
			"Emfox Zhou (Developer) <emfoxzhou@gmail.com>"
			);
	char* translators = _( "Chinese Simplified (zh_CN): Haifeng Chen <optical.dlz@gmail.com>" );

	GtkWidget* dlg = gtk_message_dialog_new_with_markup( (GtkWindow*)_this->m_Widget,
						GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
						_("<big>PCMan X  %s\nA free BBS client developed with GTK+ 2.x</big>\n\n"
						"Copyright (C) 2005\n"
						"License: GNU Genral Public License\n"
						"Project Homepage: http://pcmanx.csie.net/\n\n"
						"Authors:\n%s\n\n"
						"Translators:\n%s\n\n"), VERSION, authors, translators );

// GTK+ supports this API since ver 2.6.
/*	gtk_message_dialog_format_secondary_text((GtkMessageDialog*)dlg,
						_("Copyright (C) 2005\n"
						"License: GNU Genral Public License\n"
						"Project Homepage: http://pcmanx.csie.net/\n\n"
						"Authors:\n%s\n")
						, authors	);
*/
	gtk_image_set_from_pixbuf((GtkImage*)((GtkMessageDialog*)dlg)->image, _this->m_MainIcon);
	gtk_dialog_run((GtkDialog*)dlg) == GTK_RESPONSE_OK;
	gtk_widget_destroy(dlg);

}

void CMainFrame::updateBBSList(GtkMenuItem* pMenuItem, CMainFrame* pThis)
{
	if (g_bIsUpateHandlerExisted == false) {
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = &CMainFrame::updateBBSListHandler;
		g_pMyself = pThis;
		sigaction(SIGUSR1, &sa, NULL);
		sigaction(SIGUSR2, &sa, NULL);
		g_bIsUpateHandlerExisted = true;
	}
	
	if (g_bUpdateingBBSList == false) {
		pid_t child_pid = 0, parent_pid = getpid();

		g_bUpdateingBBSList = true;

		child_pid = fork();
		if (child_pid == 0)
		{
			int t_nRet = system("wget -O ~/.pcmanx/sitelist http://free.ym.edu.tw/pcman/site_list.utf8");
			if (t_nRet == 0)
				kill(parent_pid, SIGUSR1);
			else
				kill(parent_pid, SIGUSR2);
			exit(0);
		}
		wait(NULL);
	}
}

void CMainFrame::updateBBSListHandler(int nSignalNumber)
{
	if (nSignalNumber == SIGUSR1 || nSignalNumber == SIGUSR2)
	{
		char* t_pcSuccess = _( "Update BBS List Success!");
		char* t_pcFault = _( "Update BBS List Fault.");
		char* t_pcMessage = NULL;

		if (nSignalNumber == SIGUSR1)
			t_pcMessage = t_pcSuccess;
		else
			t_pcMessage = t_pcFault;

		GtkWidget* t_pDialog = gtk_message_dialog_new_with_markup(
			(GtkWindow*) g_pMyself->m_Widget, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, _("%s"), t_pcMessage);

		gtk_image_set_from_pixbuf((GtkImage*) ((GtkMessageDialog*) t_pDialog)->image, g_pMyself->m_MainIcon);
		gtk_dialog_run((GtkDialog*) t_pDialog) == GTK_RESPONSE_OK;
		gtk_widget_destroy(t_pDialog);
		g_bUpdateingBBSList = false;
	}
}

void CMainFrame::pasteFromClipboard(GtkMenuItem* pMenuItem, CMainFrame* pMainFrame)
{
	CTelnetView* t_pView = pMainFrame->GetCurView();
	if (t_pView != NULL)
		t_pView->PasteFromClipboard(true);
}

void CMainFrame::OnCloseCon(GtkMenuItem* mitem, CMainFrame* _this)
{
	CTelnetCon* con = _this->GetCurCon();
	if( !con )
		return;
	if( AppConfig.QueryOnCloseCon && !con->IsClosed() )
	{
		GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(_this->m_Widget), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, _("Close Connection?"));
		bool can_close = ( gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK );
		gtk_widget_destroy(dlg);
		if( !can_close )
			return;
	}

	GtkNotebook* nb = GTK_NOTEBOOK(_this->m_pNotebook->m_Widget);
	_this->CloseCon(gtk_notebook_get_current_page(nb), true);
}


void CMainFrame::OnCopy(GtkMenuItem* mitem, CMainFrame* _this)
{
	if( _this->GetCurView() )
	{
		_this->GetCurView()->CopyToClipboard(false, false, AppConfig.CopyTrimTail);
		if( AppConfig.CancelSelAfterCopy )
		{
			_this->GetCurCon()->m_Sel->NewStart(0, 0);
			_this->GetCurView()->Refresh();
		}
	}
}


void CMainFrame::OnCopyWithColor(GtkMenuItem* mitem, CMainFrame* _this)
{
	if( _this->GetCurView() )
	{
		_this->GetCurView()->CopyToClipboard(false, true, AppConfig.CopyTrimTail);
		if( AppConfig.CancelSelAfterCopy )
		{
			_this->GetCurCon()->m_Sel->NewStart(0, 0);
			_this->GetCurView()->Refresh();
		}
	}
}


void CMainFrame::OnNextCon(GtkMenuItem* mitem, CMainFrame* _this)
{
	int i = _this->GetNotebook()->GetCurPage() + 1;
	int n = _this->GetNotebook()->GetPageCount();
	_this->GetNotebook()->SetCurPage( i < n ? i : 0 );
}


void CMainFrame::OnPaste(GtkMenuItem* mitem, CMainFrame* _this)
{
	if(_this->GetCurView())
		_this->GetCurView()->PasteFromClipboard(false);
}


void CMainFrame::OnPreference(GtkMenuItem* mitem, CMainFrame* _this)
{
//	bool show_tray_icon = AppConfig.ShowTrayIcon;
	CPrefDlg* dlg = new CPrefDlg(_this);
	dlg->ShowModal();
	dlg->Destroy();

	CTelnetView::SetWebBrowser(AppConfig.WebBrowser);
	CTelnetView::SetMailClient(AppConfig.MailClient);
	CTelnetView::setWgetFiles(AppConfig.UseWgetFiles);

#ifdef USE_NOTIFIER
	popup_notifier_set_timeout( AppConfig.PopupTimeout );
#endif
//	FIXME: Currently we cannot freely hide or show tray icon.
/*	if( AppConfig.ShowTrayIcon != show_tray_icon )
		if(AppConfig.ShowTrayIcon)
			_this->ShowTrayIcon();
		else
			_this->HideTrayIcon();
*/
}


void CMainFrame::OnPrevCon(GtkMenuItem* mitem, CMainFrame* _this)
{
	int i = _this->GetNotebook()->GetCurPage() - 1;
	int n = _this->GetNotebook()->GetPageCount();
	_this->GetNotebook()->SetCurPage( i >= 0 ? i : n-1 );

}

void CMainFrame::OnSiteList(GtkMenuItem* mitem, CMainFrame* _this)
{
	CSiteListDlg* dlg = new CSiteListDlg(_this);
	dlg->ShowModal();
	dlg->Destroy();
}


void CMainFrame::OnJumpToPage(GtkWidget* widget, CMainFrame* _this)
{
	INFO("On jump to, widget=%x, _this->m_JumpMenuItems[0]=%x", widget, _this->m_JumpMenuItems[0]);
	for( int i = 0; i < 10; ++i )
		if( widget == _this->m_JumpMenuItems[i] )
		{
			_this->GetNotebook()->SetCurPage(i);
			break;
		}
}

void CMainFrame::OnTelnetConBell(CTelnetView* con)
{
	if( !IsActivated() )
		FlashWindow(true);

	if( AppConfig.BeepOnBell )
		gdk_display_beep(gdk_display_get_default());
	if( GetCurView() == con )
		return;
	string markup = "<span foreground=\"red\">";
	markup += con->GetCon()->m_Site.m_Name;
	markup += "</span>";
	m_pNotebook->SetPageTitle( con, markup);
}

void CMainFrame::OnTelnetConClose(CTelnetView* con)
{
	if( !con )
		return;
	string markup = "<span foreground=\"#808080\">";
	markup += con->GetCon()->m_Site.m_Name;
	markup += "</span>";
	m_pNotebook->SetPageTitle( con, markup);
}

void CMainFrame::OnTelnetConConnect(CTelnetView* con)
{
	if( !con )
		return;
	m_pNotebook->SetPageTitle( con, con->GetCon()->m_Site.m_Name );
}


void CMainFrame::OnTelnetConRecv(CTelnetView* con)
{
	if( !con )
		return;

	// If the text color of the label is red which indicate 
	// there is new incoming message, we should return.
//	if(  )
//		return;
	string markup = "<span foreground=\"green\">";
	markup += con->GetCon()->m_Site.m_Name;
	markup += "</span>";
	m_pNotebook->SetPageTitle( con, markup);
}


void CMainFrame::OnNotebookChangeCurPage(GtkNotebook* widget, GtkNotebookPage* page,  gint page_num, CMainFrame* _this)
{
	_this->SetCurView( _this->m_Views[page_num] );
}

gboolean CMainFrame::OnNotebookPopupMenu(GtkWidget *widget, GdkEventButton *event, gpointer p_mainframe)
{
       static GtkWidget *menu = NULL;

       // set menu items
       GtkWidget *menu_item_close = gtk_image_menu_item_new_with_label( _("Close") );
       GtkWidget *menu_item_reconnect = gtk_image_menu_item_new_with_label( _("Reconnect") );
       GtkWidget *menu_item_add2fav = gtk_image_menu_item_new_with_label( _("Add to Favorites") );
       // set images
       GtkWidget *image_close = gtk_image_new_from_stock ("gtk-close", GTK_ICON_SIZE_MENU);
       GtkWidget *image_reconnect = gtk_image_new_from_stock ("gtk-undo", GTK_ICON_SIZE_MENU);
       GtkWidget *image_add2fav = gtk_image_new_from_stock ("gtk-add", GTK_ICON_SIZE_MENU);
       gtk_image_menu_item_set_image ( (GtkImageMenuItem *)menu_item_close, image_close);
       gtk_image_menu_item_set_image ( (GtkImageMenuItem *)menu_item_reconnect, image_reconnect);
       gtk_image_menu_item_set_image ( (GtkImageMenuItem *)menu_item_add2fav, image_add2fav);

       // if not right check the mouse
       if (event->type != GDK_BUTTON_PRESS || event->button != 3)
               return FALSE;

       // if menu exists
       if (menu != NULL)
               gtk_widget_destroy(menu);
       menu = gtk_menu_new();

       // widgets show
       gtk_widget_show (menu_item_reconnect);
       gtk_container_add (GTK_CONTAINER (menu), menu_item_reconnect);

       gtk_widget_show (menu_item_close);
       gtk_container_add (GTK_CONTAINER (menu), menu_item_close);

       gtk_widget_show (menu_item_add2fav);
       gtk_container_add (GTK_CONTAINER (menu), menu_item_add2fav);

       // signals
       g_signal_connect ( G_OBJECT(menu_item_reconnect), "activate",
                       G_CALLBACK (CMainFrame::OnReconnect),
                       p_mainframe);
       g_signal_connect ( G_OBJECT(menu_item_close), "activate",
                       G_CALLBACK (CMainFrame::OnCloseCon),
                       p_mainframe);
       g_signal_connect ( G_OBJECT(menu_item_add2fav), "activate",
                       G_CALLBACK (CMainFrame::OnAddToFavorites),
                       p_mainframe);

       // popup
       gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, event->button, event->time);
       return TRUE;

}


void CMainFrame::CloseCon(int idx, bool confirm)
{
	m_pNotebook->RemovePage(idx);
	m_Views.erase( m_Views.begin() + idx );

	int n = m_pNotebook->GetCurPage();
	SetCurView( n >= 0 ? m_Views[n] : NULL );
}

gboolean CMainFrame::OnBlinkTimer(CMainFrame* _this)
{
	if( _this->m_IsFlashing )
	{
		if( gtk_window_get_icon( GTK_WINDOW(_this->m_Widget) ) == _this->m_MainIcon )
			gtk_window_set_icon( GTK_WINDOW(_this->m_Widget), _this->m_InverseMainIcon );
		else
			gtk_window_set_icon( GTK_WINDOW(_this->m_Widget), _this->m_MainIcon );
	}

	if(_this->GetCurView() && _this->GetCurView()->IsVisible() )
		_this->GetCurView()->OnBlinkTimer();
	return true;
}


gboolean CMainFrame::OnClose( GtkWidget* widget, GdkEvent* evt, CMainFrame* _this )
{
	return !_this->CanClose();
}


void CMainFrame::OnDestroy()
{
	g_source_remove( m_BlinkTimer );
	g_source_remove( m_EverySecondTimer );

	Hide();
	gtk_widget_destroy( GTK_WIDGET(m_TrayIcon_Instance) );

	while( g_main_context_iteration(NULL, FALSE) );

	CWidget::OnDestroy();

#ifdef USE_SCRIPT
	FinalizeScriptInterface();
#endif

	gtk_main_quit();
}


void CMainFrame::OnCreate()
{
	CWidget::OnCreate();
	LoadStartupSites();
}

bool CMainFrame::CanClose()
{
	if( GetNotebook()->GetPageCount() == 0 || !AppConfig.QueryOnExit )
		return true;

	GtkWidget* dlg = gtk_message_dialog_new( (GtkWindow*)m_Widget,
						GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
						_("Quit PCMan X ?"));
	bool close = gtk_dialog_run((GtkDialog*)dlg) == GTK_RESPONSE_OK;
	gtk_widget_destroy(dlg);
	return close;
}


void CMainFrame::NotImpl(const char* str)
{
	GtkWidget* dlg = gtk_message_dialog_new( (GtkWindow*)m_Widget,
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
					_("Function not implemented yet!\nDebug: %s"), str);
	gtk_dialog_run((GtkDialog*)dlg);
	gtk_widget_destroy(dlg);
}


void CMainFrame::OnAddToFavorites(GtkMenuItem* widget, CMainFrame* _this)
{
	if(_this->m_Views.empty() )
		return;

	CSite &site = _this->GetCurCon()->m_Site;
	CSiteDlg* dlg = new CSiteDlg(_this, _("Add To Favorites"), site );
	if(dlg->ShowModal() == GTK_RESPONSE_OK)
	{
		AppConfig.Favorites.push_back(dlg->m_Site);
		CSite& newsite = AppConfig.Favorites.back();

		GtkWidget* fav_item = gtk_image_menu_item_new_with_label( newsite.m_Name.c_str() );
		gtk_widget_show (fav_item);
		gtk_menu_shell_insert( GTK_MENU_SHELL(_this->m_FavoritesMenu), fav_item, 
			AppConfig.Favorites.size()>0 ? (AppConfig.Favorites.size()-1) : 0 );

		GtkWidget* image = gtk_image_new_from_stock ("gtk-network", GTK_ICON_SIZE_MENU);

		gtk_widget_show (image);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (fav_item), image);

		newsite.m_MenuItem = fav_item;

		g_signal_connect( G_OBJECT(fav_item), "activate",
						G_CALLBACK(CMainFrame::OnFavorite),
						_this);
	}
	dlg->Destroy();
}

void CMainFrame::OnEditFavorites(GtkMenuItem* widget, CMainFrame* _this)
{
	CEditFavDlg* dlg = new CEditFavDlg(_this, AppConfig.Favorites);
	dlg->ShowModal();
	AppConfig.Favorites.swap(dlg->m_Sites);
	AppConfig.SaveFavorites();
	dlg->Destroy();

	_this->CreateFavoritesMenu();
}


void CMainFrame::CreateFavoritesMenu()
{
	GtkWidget* favorites_menu = gtk_menu_new();
	vector<CSite>::iterator it;
	for( it = AppConfig.Favorites.begin(); it != AppConfig.Favorites.end(); ++it )
	{
		CSite& site = *it;

		GtkWidget* fav_item = gtk_image_menu_item_new_with_label( site.m_Name.c_str() );
		gtk_widget_show (fav_item);
		gtk_container_add (GTK_CONTAINER (favorites_menu), fav_item);

		GtkWidget* image = gtk_image_new_from_stock ("gtk-network", GTK_ICON_SIZE_MENU);
//		GtkWidget* image = gtk_image_new_from_pixbuf(m_ConnIcon);
		gtk_widget_show (image);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (fav_item), image);

		site.m_MenuItem = fav_item;

		g_signal_connect( G_OBJECT(fav_item), "activate",
						G_CALLBACK(CMainFrame::OnFavorite),
						this);
	}

	GtkWidget* separator3 = gtk_separator_menu_item_new ();
	gtk_widget_show (separator3);
	gtk_container_add (GTK_CONTAINER (favorites_menu), separator3);
	gtk_widget_set_sensitive (separator3, FALSE);

	GtkWidget* add_to_fav_menu = gtk_image_menu_item_new_with_mnemonic (_("_Add to Favorites"));
	gtk_widget_show (add_to_fav_menu);
	gtk_container_add (GTK_CONTAINER (favorites_menu), add_to_fav_menu);

	GtkWidget* image347 = gtk_image_new_from_stock ("gtk-add", GTK_ICON_SIZE_MENU);
	gtk_widget_show (image347);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (add_to_fav_menu), image347);

	GtkWidget* edit_fav_menu = gtk_image_menu_item_new_with_mnemonic (_("_Edit Favorites"));
	
	gtk_widget_show (edit_fav_menu);
	gtk_container_add (GTK_CONTAINER (favorites_menu), edit_fav_menu);

	GtkWidget* image348 = gtk_image_new_from_stock ("gtk-edit", GTK_ICON_SIZE_MENU);
	gtk_widget_show (image348);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (edit_fav_menu), image348);


	g_signal_connect ((gpointer) add_to_fav_menu, "activate",
					G_CALLBACK (CMainFrame::OnAddToFavorites),
					this);
	g_signal_connect ((gpointer) edit_fav_menu, "activate",
					G_CALLBACK (CMainFrame::OnEditFavorites),
					this);

	m_FavoritesMenu = favorites_menu;
	gtk_menu_item_set_submenu( GTK_MENU_ITEM(m_FavoritesMenuItem), favorites_menu);
}


void CMainFrame::OnFavorite(GtkMenuItem* item, CMainFrame* _this)
{
	vector<CSite>::iterator it;
	for( it = AppConfig.Favorites.begin(); it != AppConfig.Favorites.end(); ++it )
	{
		CSite& site = *it;
		if( site.m_MenuItem == (GtkWidget*)item )
		{
			_this->NewCon( site.m_Name, site.m_URL, &site );
			break;
		}
	}
}


static void format_elapsed_time_str( char* time_str, unsigned int sec )
{
	static const char* time_format = _("Time Elapsed : %02d : %02d : %02d");
	unsigned int min = sec / 60;
	sec %= 60;
	unsigned int hr = min / 60;
	min %= 60;
	sprintf( time_str, time_format, hr, min, sec );
}


void CMainFrame::SetCurView(CTelnetView* view)
{
	m_pView = view;
	if( !m_pView || !m_pView->GetCon() )
	{
		gtk_window_set_title (GTK_WINDOW (m_Widget), "PCMan X "VERSION );
		gtk_entry_set_text( GTK_ENTRY(m_URLEntry), "");
		return;
	}

	CTelnetCon* con = GetCurCon();
	gtk_entry_set_text( GTK_ENTRY(m_URLEntry), con->m_Site.m_URL.c_str());

	string title = con->m_Site.m_Name;

	if( ! con->IsClosed() )
		m_pNotebook->SetPageTitle( m_pView, title );

	title += " - PCMan X "VERSION;
	gtk_window_set_title (GTK_WINDOW (m_Widget), title.c_str() );

	char time_str[100];
	format_elapsed_time_str(time_str, con->m_Duration);
	gtk_label_set_text( m_StatusBarTime, time_str );

#ifdef USE_NANCY
	UpdateBotStatus();
#endif

}

void CMainFrame::OnSelectAll(GtkMenuItem* mitem, CMainFrame* _this)
{
	CTelnetCon* con = _this->GetCurCon();
	if( con )
	{
		con->m_Sel->SelectPage();
		_this->GetCurView()->Refresh();
	}
}

void CMainFrame::LoadStartupSites()
{
	vector<CSite>::iterator it=AppConfig.Favorites.begin();
	for( it = AppConfig.Favorites.begin(); it != AppConfig.Favorites.end(); ++it )
	{
		CSite& site = *it;
		if( site.m_Startup )
			NewCon( site.m_Name, site.m_URL, &site);
	}
	m_pNotebook->SetCurPage(0);
}

gboolean CMainFrame::OnEverySecondTimer(CMainFrame* _this)
{
	vector<CTelnetView*>::iterator it;
	for( it = _this->m_Views.begin(); it != _this->m_Views.end(); ++it )
	{
		CTelnetView* view = *it;
		if( view->GetCon() )
			view->GetCon()->OnTimer();
	}
	CTelnetCon* con = _this->GetCurCon();
	if( con )
	{
		char time_str[100];
		format_elapsed_time_str(time_str, con->m_Duration);
		gtk_label_set_text( _this->m_StatusBarTime, time_str );
	}
	return true;
}

void CMainFrame::OnEmoticons(GtkMenuItem* mitem, CMainFrame* _this)
{
	CEmoticonDlg* dlg = new CEmoticonDlg(_this);
	if( dlg->ShowModal() == GTK_RESPONSE_OK )
	{
		CTelnetView* view = _this->GetCurView();
		if( view )
			view->OnTextInput( dlg->GetSelectedStr().c_str() );
	}
	dlg->Destroy();
}

void CMainFrame::OnReconnect(GtkMenuItem* mitem, CMainFrame* _this)
{
	CTelnetCon* con = _this->GetCurCon();
	if( !con )
		return;
	if( con->IsClosed() )
		con->Reconnect();
	else
		_this->NewCon( con->m_Site.m_Name, con->m_Site.m_URL, &con->m_Site);
}

void CMainFrame::FlashWindow( bool flash )
{
	GdkPixbuf* icon = (m_IsFlashing = flash) ? m_InverseMainIcon : m_MainIcon;
	gtk_window_set_icon((GtkWindow*)m_Widget, icon);
}


gboolean CMainFrame::OnActivated( GtkWidget* widget, GdkEventFocus* evt, CMainFrame* _this )
{
	if( _this->m_IsFlashing )
		_this->FlashWindow(false);
	return false;
}

gboolean CMainFrame::OnURLEntryKeyDown(GtkWidget *widget, GdkEventKey *evt, CMainFrame* _this)
{
	switch(evt->keyval)
	{
	case GDK_Return:
	{
		string url = gtk_entry_get_text( GTK_ENTRY(widget) );
		if( !url.empty() )
		{
			_this->NewCon( url, url );
			return true;
		}
		//	else goto case GDK_Escape
	}
	case GDK_Escape:
		if( _this->GetCurView() )
			_this->GetCurView()->SetFocus();
		else
			gtk_entry_set_text( GTK_ENTRY(widget), "" );
		return true;
	}
	return false;
}

gboolean CMainFrame::OnURLEntryKillFocus(GtkWidget* entry, GdkEventFocus* evt, CMainFrame* _this)
{
	if( _this && _this->GetCurView() )
	{
		gtk_entry_set_text( GTK_ENTRY(entry), _this->GetCurCon()->m_Site.m_URL.c_str());
		gtk_editable_select_region(GTK_EDITABLE(entry), 0, 0);
	}
	return FALSE;
}


int CMainFrame::GetViewIndex(CTermView* view)
{
	DEBUG( "get view index, view = %x", view );
	if( !view )
		return -1;
	DEBUG( "view->m_Widget = %x", view->m_Widget );
	return gtk_notebook_page_num( GTK_NOTEBOOK(m_pNotebook->m_Widget), view->m_Widget );
}


void CMainFrame::SwitchToCon(CTelnetCon* con)
{
	int idx = GetViewIndex( con->GetView() );
	DEBUG( "switch to con %d", idx );
	if( idx >= 0 )
		m_pNotebook->SetCurPage(idx);
}

#ifdef USE_MOUSE
void CMainFrame::OnToggleMouse(GtkToggleAction* action, CMainFrame* _this)
{
  _this->m_MouseSupport = gtk_toggle_action_get_active(action);
}
#endif

#ifdef USE_NANCY

void CMainFrame::OnChangeCurrentBot(GtkRadioAction *action, GtkRadioAction *current, CMainFrame* _this)
{
  CTelnetCon* con = _this->GetCurCon();
  if( !con ) return;
  if( gtk_radio_action_get_current_value(current) == 0 )
    con->set__UseNancy(false);
  else
    con->set__UseNancy(true);

  _this->UpdateBotStatus();
}

void CMainFrame::OnChangeAllBot(GtkRadioAction *action, GtkRadioAction *all, CMainFrame* _this)
{
  if( _this->m_Views.empty() ) return;
  gboolean use_nancy = ( gtk_radio_action_get_current_value(all) != 0 );
  CTelnetCon::set__OpenConnectionWithNancySupport(use_nancy);

  vector<CTelnetView*>::iterator it = _this->m_Views.begin();
  for( ; it != _this->m_Views.end() ; ++it )
    (*it)->GetCon()->set__UseNancy(use_nancy);
  
  if( use_nancy )
    gtk_check_menu_item_set_active( (GtkCheckMenuItem*)_this->m_CurBotNancyRadio, true );
  else
    gtk_check_menu_item_set_active( (GtkCheckMenuItem*)_this->m_DisableCurBotRadio, true );
  
  _this->UpdateBotStatus();
}

void CMainFrame::UpdateBotStatus()
{
	if( GetCurCon()->get__UseNancy() )
	{
		gtk_check_menu_item_set_active( (GtkCheckMenuItem*)m_CurBotNancyRadio, true );
		gchar* bot_state = g_markup_printf_escaped( "<span foreground=\"#ff0000\">%s</span>", _("Nancy Bot is enabled") );
		gtk_label_set_markup( m_StatusBarBotState, bot_state );
		g_free(bot_state);
	}
	else
	{
		gtk_check_menu_item_set_active( (GtkCheckMenuItem*)m_DisableCurBotRadio, true );
		gchar* bot_state = g_markup_printf_escaped( "<span foreground=\"gray\">%s</span>", _("Nancy Bot is disabled") );
		gtk_label_set_markup( m_StatusBarBotState, bot_state );
		g_free(bot_state);
	}
}

#endif	//	#ifdef USE_NANCY

