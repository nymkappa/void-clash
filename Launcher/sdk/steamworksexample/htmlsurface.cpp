//========= Copyright � 1996-2008, Valve LLC, All rights reserved. ============
//
// Purpose: Class to render a HTML page on the screen
//
// $NoKeywords: $
//=============================================================================

#include "stdafx.h"
#include "htmlsurface.h"
#include "SpaceWarClient.h"

#define HTML_TEXT_HEIGHT 20

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHTMLSurface::CHTMLSurface( IGameEngine *pGameEngine ) :
	m_CloseBrowser( this, &CHTMLSurface::OnCloseBrowser ),
	m_NeedsPaint( this, &CHTMLSurface::OnNeedsPaint ),
	m_StartRequest( this, &CHTMLSurface::OnStartRequest ),
	m_FinishedRequest( this, &CHTMLSurface::OnFinishedRequest )
{
	m_pGameEngine = pGameEngine;
	m_unBrowserHandle = INVALID_HTMLBROWSER;
	m_hHTMLTexture = -1;

	m_unHTMLWide = m_pGameEngine->GetViewportWidth() - 100;
	m_unHTMLTall = m_pGameEngine->GetViewportHeight() - 100;

	SteamHTMLSurface()->Init();
	SteamHTMLSurface()->SetSize( m_unBrowserHandle, m_unHTMLWide, m_unHTMLTall );
}



//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CHTMLSurface::~CHTMLSurface()
{
	if ( m_unBrowserHandle )
		SteamHTMLSurface()->RemoveBrowser( m_unBrowserHandle );
	m_unBrowserHandle = INVALID_HTMLBROWSER;
}



//-----------------------------------------------------------------------------
// Purpose: RunFrame
//-----------------------------------------------------------------------------
void CHTMLSurface::RunFrame()
{
	if ( m_pGameEngine->BIsKeyDown( VK_ESCAPE ) )
	{
		SpaceWarClient()->SetGameState( k_EClientGameMenu );
	}
}


//-----------------------------------------------------------------------------
// Purpose: draw the page
//-----------------------------------------------------------------------------
void CHTMLSurface::Render()
{
	if (m_hHTMLTexture >= 0)
	{
		RECT rect;
		rect.left = 50;
		rect.top = 50;
		rect.right = m_unHTMLWide + rect.left;
		rect.bottom = m_unHTMLTall + rect.top;

		m_pGameEngine->BDrawTexturedRect( (float)rect.left, (float)rect.top, (float)rect.right, (float)rect.bottom,
			0.0f, 0.0f, 1.0, 1.0, D3DCOLOR_ARGB( 255, 255, 255, 255 ), m_hHTMLTexture );
	}

	RECT rect;
	rect.top = m_unHTMLTall + 70;
	rect.bottom = rect.top + HTML_TEXT_HEIGHT;
	rect.left = m_unHTMLWide/2 - 200;
	rect.right = rect.left + 400;

	char rgchBuffer[256];
	sprintf_safe( rgchBuffer, "Hit ESC to return to main menu" );
	m_pGameEngine->BDrawString( m_hDisplayFont, rect, D3DCOLOR_ARGB( 255, 25, 200, 25 ), TEXTPOS_CENTER | TEXTPOS_VCENTER, rgchBuffer );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHTMLSurface::Show()
{
	m_hDisplayFont = m_pGameEngine->HCreateFont(HTML_TEXT_HEIGHT, FW_MEDIUM, false, "Arial");
	if (!m_hDisplayFont)
		OutputDebugString("RemoteStorage font was not created properly, text won't draw\n");

	SteamAPICall_t hSteamAPICall = SteamHTMLSurface()->CreateBrowser( "SpaceWars Test", NULL );
	m_SteamCallResultBrowserReady.Set( hSteamAPICall, this, &CHTMLSurface::OnBrowserReady );

}


//-----------------------------------------------------------------------------
// Purpose: the page asked to be closed
//-----------------------------------------------------------------------------
void CHTMLSurface::OnCloseBrowser( HTML_CloseBrowser_t *pParam  )
{
	m_unBrowserHandle = INVALID_HTMLBROWSER;

}


//-----------------------------------------------------------------------------
// Purpose: the browser is ready to load pages and start sending us textures to render
//-----------------------------------------------------------------------------
void CHTMLSurface::OnBrowserReady( HTML_BrowserReady_t *pBrowserReady, bool bIOFailure )
{
	if (bIOFailure)
		return;

	m_unBrowserHandle = pBrowserReady->unBrowserHandle;
	SteamHTMLSurface()->SetSize( m_unBrowserHandle, m_unHTMLWide, m_unHTMLWide );
	SteamHTMLSurface()->LoadURL( m_unBrowserHandle, "http://steamcommunity.com/", NULL );
}


//-----------------------------------------------------------------------------
// Purpose: we have a new texture to present
//-----------------------------------------------------------------------------
void CHTMLSurface::OnNeedsPaint( HTML_NeedsPaint_t *pParam  )
{
	if ( m_hHTMLTexture < 0 )
		m_hHTMLTexture = m_pGameEngine->HCreateTexture( (byte *)pParam->pBGRA, pParam->unWide, pParam->unTall, eTextureFormat_BGRA );
	else
		m_pGameEngine->UpdateTexture( m_hHTMLTexture, (byte *)pParam->pBGRA, pParam->unWide, pParam->unTall, eTextureFormat_BGRA );

	if (pParam->unWide != m_unHTMLWide)
		OutputDebugString( "bad texture width for html\n" );
	if (pParam->unTall != m_unHTMLTall)
		OutputDebugString( "bad texture height for html\n" );

}


//-----------------------------------------------------------------------------
// Purpose: the page requested that a URL be loaded, should we allow it?
//-----------------------------------------------------------------------------
void CHTMLSurface::OnStartRequest( HTML_StartRequest_t *pParam )
{
	SteamHTMLSurface()->AllowStartRequest( m_unBrowserHandle, true );
}


//-----------------------------------------------------------------------------
// Purpose: the page is now fully loaded
//-----------------------------------------------------------------------------
void CHTMLSurface::OnFinishedRequest( HTML_FinishedRequest_t *pParam )
{
	// Uncomment this if you want to scale a pages contents when you display it
	//SteamHTMLSurface()->SetPageScaleFactor( m_unBrowserHandle, 2.0, 0, 0 );
}