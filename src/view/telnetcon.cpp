/////////////////////////////////////////////////////////////////////////////
// Name:        telnetcon.cpp
// Purpose:     Class dealing with telnet connections, parsing telnet commands.
// Author:      PCMan (HZY)   http://pcman.ptt.cc/
// E-mail:      hzysoft@sina.com.tw
// Created:     2004.7.16
// Copyright:   (C) 2004 PCMan
// Licence:     GPL : http://www.gnu.org/licenses/gpl.html
// Modified by:
/////////////////////////////////////////////////////////////////////////////

#ifdef __GNUG__
  #pragma implementation "telnetcon.h"
#endif

#include "telnetcon.h"
#include "telnetview.h"

#if !defined(MOZ_PLUGIN)
#include "mainframe.h"

#ifdef USE_NOTIFIER
#include "notifier/api.h"
#endif

#ifdef USE_SCRIPT
#include "script/api.h"
#endif

#endif /* !defined(MOZ_PLUGIN) */

#include <string.h>
#include <stdio.h>
#include <glib/gi18n.h>

#include "stringutil.h"

#if !defined(MOZ_PLUGIN)
#include "appconfig.h"
#endif /* !defined(MOZ_PLUGIN) */

// socket related headers
#include <sys/select.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/time.h>

// pseudo tty headers
#ifdef USING_LINUX
#include <pty.h>
#endif
#include <utmp.h>

#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#if defined(USING_FREEBSD) || defined(CSRG_BASED)
#include <sys/ioctl.h>
#include <termios.h>
#include <libutil.h>
#endif

#define RECV_BUF_SIZE (4097)

#include "debug.h"

// class constructor
CTelnetCon::CTelnetCon(CTermView* pView, CSite& SiteInfo)
	: CTermData(pView), m_Site(SiteInfo)
{
#ifdef USE_NANCY
	use_nancy = true;  // Dynamic open or close it.
	if(use_nancy)
        	bot = new NancyBot("default", "./"); // FIXME get PATH from env?
#endif
	m_pBuf = m_pLastByte = m_pRecvBuf = NULL;
    m_pCmdLine = m_CmdLine;
	m_pCmdLine[0] = '\0';

	m_State = TS_CONNECTING;
	m_Duration = 0;
	m_IdleTime = 0;
	m_AutoLoginStage = ALS_OFF;

	m_SockFD = -1;
	m_IOChannel = 0;
	m_IOChannelID = 0;
	m_Pid = 0;

	m_BellTimeout = 0;
	m_IsLastLineModified = false;

	// Cache the sockaddr_in which can be used to reconnect.
	m_InAddr.s_addr = INADDR_NONE;
	m_Port = 0;
	
	gchar* locale_str;
	gsize l;
	if( !m_Site.GetPreLoginPrompt().empty() )
	{
		if( (locale_str = g_convert(m_Site.GetPreLoginPrompt().c_str(), 
									m_Site.GetPreLoginPrompt().length(), 
									m_Site.m_Encoding.c_str(), 
									"UTF-8", NULL, &l, NULL)) )
		{
			m_PreLoginPrompt = locale_str;
			g_free(locale_str);
		}
	}
	if( !m_Site.GetLoginPrompt().empty() )
	{
		if( (locale_str = g_convert(m_Site.GetLoginPrompt().c_str(), 
									m_Site.GetLoginPrompt().length(), 
									m_Site.m_Encoding.c_str(), 
									"UTF-8", NULL, &l, NULL)) )
		{
			m_LoginPrompt = locale_str;
			g_free(locale_str);
		}
	}
	if( !m_Site.GetPasswdPrompt().empty() )
	{
		if( (locale_str = g_convert(m_Site.GetPasswdPrompt().c_str(), 
									m_Site.GetPasswdPrompt().length(), 
									m_Site.m_Encoding.c_str(), 
									"UTF-8", NULL, &l, NULL)) )
		{
			m_PasswdPrompt = locale_str;
			g_free(locale_str);
		}
	}
}

GThread* CTelnetCon::m_DNSThread = NULL;
int CTelnetCon::m_SocketTimeout = 30;
GMutex* CTelnetCon::m_DNSMutex = NULL;

// class destructor
CTelnetCon::~CTelnetCon()
{
#ifdef USE_NANCY
	        delete bot;
#endif
	Close();
	INFO("CTelnetCon::~CTelnetCon\n");
	list<CDNSRequest*>::iterator it;
	if(m_DNSMutex)
		g_mutex_lock(m_DNSMutex);
	for( it = m_DNSQueue.begin(); it != m_DNSQueue.end(); ++it)
	{
		CDNSRequest* thread = *it;
		if( thread->m_pCon == this )
		{
			if( thread->m_Running )
				thread->m_pCon = NULL;
			else
			{
				delete thread;
				m_DNSQueue.erase(it);
				INFO("thread obj deleted in CTelnet::~CTelnet()\n");
			}
			break;
		}
	}
	if(m_DNSMutex)
		g_mutex_unlock(m_DNSMutex);

	if( m_BellTimeout )
		g_source_remove( m_BellTimeout );
}

gboolean CTelnetCon::OnSocket(GIOChannel *channel, GIOCondition type, CTelnetCon* _this)
{
	bool ret = false;
	if( type & G_IO_IN )
		ret = _this->OnRecv();
	if( type & G_IO_HUP )
	{
		_this->OnClose();
		ret = false;
	}
	return ret;
}



// No description
bool CTelnetCon::Connect()
{
	m_State = TS_CONNECTING;

	string address;
	m_Port = 23;
	PreConnect( address, m_Port );

	// If this site has auto-login settings, activate auto-login
	// and set it to stage 1, waiting for prelogin prompt or stage 2,
	// waiting for login prompt.
	INFO("login = %s\n", m_Site.GetLogin().c_str());
	if( !m_Site.GetLogin().empty() /*&& AppConfig.IsLoggedIn()*/ )
		m_AutoLoginStage = m_Site.GetPreLogin().empty() ? ALS_LOGIN : ALS_PRELOGIN ;
	else
		m_AutoLoginStage = ALS_OFF;

#ifdef USE_EXTERNAL
	// Run external program to handle connection.
	if( m_Site.m_UseExternalTelnet || m_Site.m_UseExternalSSH )
	{
		// Suggestion from kyl <kylinx@gmail.com>
		// Call forkpty() to use pseudo terminal and run an external program.
		const char* prog = m_Site.m_UseExternalSSH ? "ssh" : "telnet";
		setenv("TERM", m_Site.m_TermType.c_str() , 1);
		// Current terminal emulation is buggy and only suitable for BBS browsing.
		// Both xterm or vt??? terminal emulation has not been fully implemented.
		m_Pid = forkpty (& m_SockFD, NULL, NULL, NULL );
		if ( m_Pid == 0 )
		{
			// Child Process;
			if( m_Site.m_UseExternalSSH )
				execlp ( prog, prog, address.c_str(), NULL ) ;
			else
				execlp ( prog, prog, "-8", address.c_str(), NULL ) ;
		}
		else
		{
			// Parent process
		}
		OnConnect(0);
	}
	else	// Use built-in telnet command handler
#endif
	{
		if( m_InAddr.s_addr != INADDR_NONE || inet_aton(address.c_str(), &m_InAddr) )
			ConnectAsync();
		else	// It's a domain name, DNS lookup needed.
		{
			g_mutex_lock(m_DNSMutex);
			CDNSRequest* dns_request = new CDNSRequest(this, address, m_Port);
			m_DNSQueue.push_back( dns_request );
			if( !m_DNSThread ) // There isn't any runnung thread.
				m_DNSThread = g_thread_create( (GThreadFunc)&CTelnetCon::ProcessDNSQueue, NULL, true, NULL);
			g_mutex_unlock(m_DNSMutex);
		}
	}

    return true;
}

// No description
bool CTelnetCon::OnRecv()
{
	static unsigned char recv_buf[RECV_BUF_SIZE];
	m_pRecvBuf = recv_buf;

	if( !m_IOChannel || m_SockFD == -1 )
		return false;

	gsize rlen = 0;
	g_io_channel_read(m_IOChannel, (char*)m_pRecvBuf, (RECV_BUF_SIZE - 1), &rlen);

	if(rlen == 0 && !(m_State & TS_CLOSED) )
	{
		OnClose();
		return false;
	}

    m_pRecvBuf[rlen] = '\0';
    m_pBuf = m_pRecvBuf;
    m_pLastByte = m_pRecvBuf + rlen;
//printf("recv: %s", m_pRecvBuf);
    ParseReceivedData();

	UpdateDisplay();

//	((CTelnetView*)m_pView)->GetParentFrame()->OnTelnetConRecv((CTelnetView*)m_pView);
	return true;
}

void CTelnetCon::OnConnect(int code)
{
	if( 0 == code )
	{
		m_State = TS_CONNECTED;
#if !defined(MOZ_PLUGIN)
		((CTelnetView*)m_pView)->GetParentFrame()->OnTelnetConConnect((CTelnetView*)m_pView);
#endif
		m_IOChannel = g_io_channel_unix_new(m_SockFD);
		m_IOChannelID = g_io_add_watch( m_IOChannel, 
				GIOCondition(G_IO_ERR|G_IO_HUP|G_IO_IN), (GIOFunc)CTelnetCon::OnSocket, this );
		g_io_channel_set_encoding(m_IOChannel, NULL, NULL);
		g_io_channel_set_buffered(m_IOChannel, false);
	}
	else
	{
		m_State = TS_CLOSED;
		Close();
#if !defined(MOZ_PLUGIN)
		((CTelnetView*)m_pView)->GetParentFrame()->OnTelnetConClose((CTelnetView*)m_pView);
#endif
		const char failed_msg[] = "Unable to connect.";
		memcpy( m_Screen[0], failed_msg, sizeof(failed_msg) );
#if !defined(MOZ_PLUGIN)
		if( GetView()->GetParentFrame()->GetCurView() == m_pView )
		{
			for( int col = 0; col < sizeof(failed_msg); )
				col += m_pView->DrawChar( 0, col );
		}
#endif
	}
}

void CTelnetCon::OnClose()
{
	m_State = TS_CLOSED;
	Close();
#if !defined(MOZ_PLUGIN)
	((CTelnetView*)m_pView)->GetParentFrame()->OnTelnetConClose((CTelnetView*)m_pView);
#endif
	//	if disconnected by the server too soon, reconnect automatically.
	if( m_Site.m_AutoReconnect > 0 && m_Duration < m_Site.m_AutoReconnect )
		Reconnect();
}

/*
 * Parse received data, process telnet command 
 * and ANSI escape sequence.
 */
void CTelnetCon::ParseReceivedData()
{
    for( m_pBuf = m_pRecvBuf; m_pBuf < m_pLastByte; m_pBuf++ )
    {
		if( 0 == m_Pid ) // No external program.  Handle telnet commands ourselves.
		{
			if( m_CmdLine[0] == TC_IAC )	// IAC, in telnet command mode.
			{
				ParseTelnetCommand();
				continue;
			}
	
			if( *m_pBuf == TC_IAC )    // IAC, in telnet command mode.
			{
				m_CmdLine[0] = TC_IAC;
				m_pCmdLine = &m_CmdLine[1];
				continue;
			}
		}
		// *m_pBuf is not a telnet command, let genic terminal process it.
		CTermData::PutChar( *m_pBuf );
    }
}

// Process telnet command.
void CTelnetCon::ParseTelnetCommand()
{
    *m_pCmdLine = *m_pBuf;
    m_pCmdLine++;
	switch( m_CmdLine[1] )
	{
	case TC_WILL:
		{
			if( 3 > (m_pCmdLine-m_CmdLine) )
				return;
			char ret[]={TC_IAC,TC_DONT,*m_pBuf};
			switch(*m_pBuf)
			{
			case TO_ECHO:
			case TO_SUPRESS_GO_AHEAD:
				ret[1] = TC_DO;
				break;
			}
			SendRawString(ret, 3);
			break;
		}
	case TC_DO:
		{
			if( 3 > (m_pCmdLine-m_CmdLine) )
				return;
			char ret[]={TC_IAC,TC_WILL,*m_pBuf};
			switch(*m_pBuf)
			{
			case TO_TERMINAL_TYPE:
			case TO_NAWS:
				break;
			default:
				ret[1] = TC_WONT;
			}
			SendRawString(ret,3);
			if( TO_NAWS == *m_pBuf )	// Send NAWS
			{
				unsigned char naws[]={TC_IAC,TC_SB,TO_NAWS,0,80,0,24,TC_IAC,TC_SE};
				naws[3] = m_ColsPerPage >>8;	// higher byte
				naws[4] = m_ColsPerPage & 0xff; // lower byte
				naws[5] = m_RowsPerPage >> 8;	// higher byte
				naws[6] = m_RowsPerPage & 0xff; // lower byte
				SendRawString( (const char*)naws,sizeof(naws));
			}
			break;
		}
	case TC_WONT:
	case TC_DONT:
		if( 3 > (m_pCmdLine-m_CmdLine) )
			return;
		break;
	case TC_SB:	// sub negotiation
		if( *m_pBuf == TC_SE )	// end of sub negotiation
		{
			switch( m_CmdLine[2] )
			{
			case TO_TERMINAL_TYPE:
				{
					// Return terminal type.  2004.08.05 modified by PCMan.
					unsigned char ret_head[] = { TC_IAC, TC_SB, TO_TERMINAL_TYPE, TO_IS };
					unsigned char ret_tail[] = { TC_IAC, TC_SE };
					int ret_len = 4 + 2 + m_Site.m_TermType.length();
					unsigned char *ret = new unsigned char[ret_len];
					memcpy( ret, ret_head, 4);
					memcpy( ret + 4, m_Site.m_TermType.c_str(), m_Site.m_TermType.length() );
					memcpy( ret + 4 + m_Site.m_TermType.length() , ret_tail, 2);
					SendRawString( (const char*)ret, ret_len);
					delete []ret;
				}
			}
		}
		else
			return;	// prevent m_CmdLine from being cleard.
	}
	m_CmdLine[0] = '\0';
	m_pCmdLine = m_CmdLine;
}

void CTelnetCon::Disconnect()
{
	if( m_State != TS_CONNECTED )
		return;
	Close();
}

void CTelnetCon::OnTimer()
{
	if( m_State == TS_CLOSED )
		return;
	m_Duration++;
	m_IdleTime++;
//	Note by PCMan:
//	Here is a little trick.
//	Since we have increased m_IdleTime by 1, it's impossible for 
//	m_IdleTime to equal zero.
//	When 'Anti Idle' is disabled, m_Site.m_AntiIdle must = 0.
//	So m_Site.m_AntiIdle != m_IdleTimeand, and the following SendRawString() won't be called.
//	Hence we don't need to check if 'Anti Idle' is enabled or not.
	if( m_Site.m_AntiIdle == m_IdleTime )
	{
		//	2004.8.5 Added by PCMan.	Convert non-printable control characters.
		INFO("AntiIdle: %s\n", m_Site.m_AntiIdleStr.c_str() );
		string aistr = UnEscapeStr( m_Site.m_AntiIdleStr.c_str() );
		SendRawString( aistr.c_str(), aistr.length() );
	}
	//	When SendSRawtring() is called, m_IdleTime is set to 0 automatically.
}


//	Virtual function called from parent class to let us determine
//	whether to beep or show a visual indication instead.
void CTelnetCon::Bell()
{
#if !defined(MOZ_PLUGIN)
	((CTelnetView*)m_pView)->GetParentFrame()->OnTelnetConBell((CTelnetView*)m_pView);
#endif
	if( m_BellTimeout )
		g_source_remove( m_BellTimeout );

	m_BellTimeout = g_timeout_add( 500, (GSourceFunc)CTelnetCon::OnBellTimeout, this );
}


bool CTelnetCon::OnBellTimeout( CTelnetCon* _this )
{
	INFO("on bell timer\n");
	if( _this->m_IsLastLineModified )
	{
		char* line = _this->m_Screen[ _this->m_RowsPerPage-1 ];
		_this->OnNewIncomingMessage( line );
		_this->m_IsLastLineModified = false;
	}
	_this->m_BellTimeout = 0;
	return false;
}


void CTelnetCon::CheckAutoLogin(int row)
{
	if( m_AutoLoginStage > ALS_PASSWD )	// This shouldn't happen, but just in case.
		return;
	INFO("check auto login: %d\n", row);

	const char* prompts[] = {
		NULL,	//	Just used to increase array indices by one.
		m_PreLoginPrompt.c_str(),	//	m_AutoLoginStage = 1 = ALS_PROMPT
		m_LoginPrompt.c_str(),	//	m_AutoLoginStage = 2 = ALS_LOGIN
		m_PasswdPrompt.c_str() };	//	m_AutoLoginStage = 3 = ALS_PASSWD

	if( strstr(m_Screen[row], prompts[m_AutoLoginStage] ) )
	{
		const char* responds[] = {
			NULL,	//	Just used to increase array indices by one.
			m_Site.GetPreLogin().c_str(),	//	m_AutoLoginStage = 1
			m_Site.GetLogin().c_str(),	//	m_AutoLoginStage = 2
			m_Site.GetPasswd().c_str(),	//	m_AutoLoginStage = 3
			""	//	m_AutoLoginStage = 4, turn off auto-login
			};

		string respond = responds[m_AutoLoginStage];
		UnEscapeStr(respond);
		respond += '\n';
		SendString(respond);	// '\n' will be converted to m_Site.GetCRLF() here.

		if( (++m_AutoLoginStage) >= ALS_END )	// Go to next stage.
		{
			m_AutoLoginStage = ALS_OFF;	// turn off auto-login after all stages end.
			respond = m_Site.GetPostLogin();	// Send post-login string
			if( respond.length() > 0 )
			{
				// Unescape all control characters.
				UnEscapeStr(respond);
				SendString(respond);
			}
		}
	}
}

void CTelnetCon::SendUnEscapedString(string str)
{
	UnEscapeStr(str);
	SendString(str);
}

void CTelnetCon::SendString(string str)
{
//	str.Replace( "\n", m_Site.GetCRLF(), true);
	string str2;
	const char* crlf = m_Site.GetCRLF();
	for( const char* pstr = str.c_str(); *pstr; ++pstr )
		if( *pstr == '\n' )
			str2 += crlf;
		else
			str2 += *pstr;
	gsize l;
	gchar* _text = g_convert(str2.c_str(), str2.length(), m_Site.m_Encoding.c_str(), "UTF-8", NULL, &l, NULL);
	if( _text )
	{
		SendRawString(_text, strlen(_text));
		g_free(_text);
	}
}


void CTelnetCon::PreConnect(string& address, unsigned short& port)
{
	m_Duration = 0;
	m_IdleTime = 0;
	m_State = TS_CONNECTING;

	int p = m_Site.m_URL.find(':',true);
	if( p >=0 )		// use port other then 23;
	{
		port = (unsigned short)atoi(m_Site.m_URL.c_str()+p+1);
		address = m_Site.m_URL.substr(0, p);
	}
	else
		address = m_Site.m_URL;
}

int CTelnetCon::Send(void *buf, int len)
{
	if( m_IOChannel && m_State & TS_CONNECTED )
	{
		gsize wlen = 0;
		g_io_channel_write(m_IOChannel, (char*)buf, len, &wlen);

		if( wlen > 0 )
			m_IdleTime = 0;	// Since data has been sent, we are not idle.

		return wlen;
	}
	return 0;
}

list<CDNSRequest*> CTelnetCon::m_DNSQueue;

void CTelnetCon::DoDNSLookup( CDNSRequest* data )
{
	in_addr addr;
	addr.s_addr = INADDR_NONE;

//  Because of the usage of thread pool, all DNS requests are queued
//  and be executed one by one.  So no mutex lock is needed anymore.
	if( ! inet_aton(data->m_Address.c_str(), &addr) )
	{
//  gethostbyname is not a thread-safe socket API.
		hostent* host = gethostbyname(data->m_Address.c_str());
		if( host )
			addr = *(in_addr*)host->h_addr_list[0];
	}

	g_mutex_lock(m_DNSMutex);
	if( data && data->m_pCon)
	{
		data->m_pCon->m_InAddr = addr;
		g_idle_add((GSourceFunc)OnDNSLookupEnd, data->m_pCon);
	}
	g_mutex_unlock(m_DNSMutex);
}

void CTelnetCon::Close()
{
	m_State = TS_CLOSED;

	if( m_IOChannel )
	{
		g_source_remove(m_IOChannelID);
		m_IOChannelID = 0;
		g_io_channel_shutdown(m_IOChannel, true, NULL);
		g_io_channel_unref(m_IOChannel);
		m_IOChannel = NULL;
	}

	if( m_SockFD != -1 )
	{
		if( m_Pid )
		{
			int kill_ret = kill( m_Pid, 1 );	// SIG_HUP Is this correct?
			int status = 0;
			pid_t wait_ret = waitpid(m_Pid, &status, 0);
			INFO("pid=%d, kill=%d, wait=%d\n", m_Pid, kill_ret, wait_ret);
			m_Pid = 0;
		}
		close( m_SockFD );
		m_SockFD = -1;
	}
}

void CTelnetCon::Cleanup()
{
	if( m_DNSThread )
		g_thread_join(m_DNSThread);

	if(m_DNSMutex)
	{
		g_mutex_free(m_DNSMutex);
		m_DNSMutex = NULL;
	}
	
}

void CTelnetCon::Reconnect()
{
	ClearScreen(2);
	m_CaretPos.x = m_CaretPos.y = 0;
	Connect();
}

void CTelnetCon::OnLineModified(int row)
{
    /// @todo implement me
	if( m_AutoLoginStage > ALS_OFF )
		CheckAutoLogin(row);
	INFO("line %d is modified\n", row);
	if( row == (m_RowsPerPage-1) )	// If last line is modified
		m_IsLastLineModified = true;
}

#if !defined(MOZ_PLUGIN)

#ifdef USE_NOTIFIER

void popup_win_clicked(GtkWidget* widget, CTelnetCon* con)
{
	INFO("popup clicked\n");
	CMainFrame* mainfrm = con->GetView()->GetParentFrame();
	mainfrm->SwitchToCon(con);
	gtk_widget_destroy( gtk_widget_get_parent(widget) );
	gtk_window_present(GTK_WINDOW(mainfrm->m_Widget));
	return;
}

#endif

#endif /* !defined(MOZ_PLUGIN) */

// When new incoming message is detected, this function gets called.
void CTelnetCon::OnNewIncomingMessage(char* line)
{
#if !defined(MOZ_PLUGIN)
#ifdef USE_NANCY
	if( use_nancy )
	{
		if ( !*line )
			return;
	
		// FIXME ugly ugly ugly...
		string sub;
		string sub2;
		string str(line);
	
		int n = str.find_first_of("  ");  // cut userid and spaces at head
		if( n != string::npos) // found
			sub = str.substr(n+2);
	
		int m = sub.find_last_not_of(" ");  // cut spaces at tail
		if( n != string::npos)
			sub2 = sub.erase(m+1);
	
		SendString("\022");            // ^R
		SendString(bot->askNancy(sub2));
		SendString("\015");            // Enter
		SendString("y");
		SendString("\015");
		SendString("\033[A");          // up
		SendString("\033[B");          // down
	} // end if
#endif  // USE_NANCY

#ifdef USE_NOTIFIER
	if ( !AppConfig.PopupNotifier || !*line )
		return;

	/* We need to convert the incoming message into UTF-8 encoding from
	 * the original one.
	 */
	gsize l;
	gchar *utf8_text = g_convert(
		line, strlen(line), 
		"UTF-8", m_Site.m_Encoding.c_str(), 
		NULL, &l, NULL);
#ifdef USE_SCRIPT
	ScriptOnNewIncomingMessage(this, utf8_text);
#endif

	CMainFrame* mainfrm = ((CTelnetView*)m_pView)->GetParentFrame();
	if( mainfrm->IsActivated() && mainfrm->GetCurCon() == this )
		return;


	gchar **column = g_strsplit(utf8_text, " ", 2);
	GtkWidget* popup_win = popup_notifier_notify(
		g_strdup_printf("%s - %s",
			m_Site.m_Name.c_str(),
			g_strchomp(column[0])),
		g_strchomp(column[1]),
		m_pView->m_Widget, 
		G_CALLBACK(popup_win_clicked), 
		this);
	g_strfreev(column);
	g_free(utf8_text);
#endif

#endif /* !defined(MOZ_PLUGIN) */
}

gboolean CTelnetCon::OnDNSLookupEnd(CTelnetCon* _this)
{
	INFO("CTelnetCon::OnDNSLookupEnd\n");
	g_mutex_lock(m_DNSMutex);
	if( _this->m_InAddr.s_addr != INADDR_NONE )
		_this->ConnectAsync();
	g_mutex_unlock(m_DNSMutex);
	return false;
}


gboolean CTelnetCon::OnConnectCB(GIOChannel *channel, GIOCondition type, CTelnetCon* _this)
{
//	g_source_remove(m_IOChannelID);
	_this->m_IOChannelID = 0;
	g_io_channel_unref(channel);
	_this->m_IOChannel = NULL;
	_this->OnConnect( (type & G_IO_OUT) ? 0 : -1);
	return false;	// The event source will be removed.
}

void CTelnetCon::ConnectAsync()
{
	sockaddr_in sock_addr;
	sock_addr.sin_addr = m_InAddr;
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(m_Port);

	m_SockFD = socket(PF_INET, SOCK_STREAM, 0);
	int sock_flags = fcntl(m_SockFD, F_GETFL, 0);
	fcntl(m_SockFD, F_SETFL, sock_flags | O_NONBLOCK);
	int err = connect( m_SockFD, (sockaddr*)&sock_addr, sizeof(sockaddr_in) );
	fcntl(m_SockFD, F_SETFL, sock_flags );

	if( err == 0 )
		OnConnect( 0 );
	else if( errno == EINPROGRESS )
	{
		m_IOChannel = g_io_channel_unix_new(m_SockFD);
		m_IOChannelID = g_io_add_watch( m_IOChannel, 
			GIOCondition(G_IO_ERR|G_IO_HUP|G_IO_OUT|G_IO_IN|G_IO_NVAL), (GIOFunc)CTelnetCon::OnConnectCB, this );
	}
	else
		OnConnect(-1);
}

void CTelnetCon::ProcessDNSQueue(gpointer unused)
{
	INFO("begin run dns threads\n");
	g_mutex_lock(m_DNSMutex);
	list<CDNSRequest*>::iterator it = m_DNSQueue.begin(), prev_it;
	while( it != m_DNSQueue.end() )
	{
		CDNSRequest* data = *it;
		data->m_Running = true;
		if( data->m_pCon )
		{
			g_mutex_unlock(m_DNSMutex);
			DoDNSLookup(data);
			g_mutex_lock(m_DNSMutex);
			data->m_Running = false;
		}
		prev_it = it;
		++it;
		m_DNSQueue.erase(prev_it);
		delete *prev_it;
		INFO("thread obj deleted in CTelnetCon::ProcessDNSQueue()\n");
	}
	g_idle_add((GSourceFunc)&CTelnetCon::OnProcessDNSQueueExit, NULL);
	g_mutex_unlock(m_DNSMutex);
	INFO("CTelnetCon::ProcessDNSQueue() returns\n");
}

bool CTelnetCon::OnProcessDNSQueueExit(gpointer unused)
{
	g_mutex_lock(m_DNSMutex);
	g_thread_join( m_DNSThread );

	m_DNSThread = NULL;
	if( !m_DNSQueue.empty() )
	{
		INFO("A new thread has to be started\n");
		m_DNSThread = g_thread_create( (GThreadFunc)&CTelnetCon::ProcessDNSQueue, NULL, true, NULL);
		// If some DNS requests are queued just before the thread exits, 
		// we should start a new thread.
	}
	g_mutex_unlock(m_DNSMutex);
	INFO("all threads end\n");
	return false;
}
