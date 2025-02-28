//========= Copyright � 1996-2008, Valve LLC, All rights reserved. ============
//
// Purpose: Main class for the game engine -- win32 implementation
//
// $NoKeywords: $
//=============================================================================

#include "stdafx.h"
#include "GameEngineWin32.h"
#include <map>
#include "steam/steamvr.h"

#ifdef WIN32
#include <direct.h>
#else
#define MAX_PATH PATH_MAX
#define _getcwd getcwd
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE( x ) if ( 0 != ( x ) ) { ( x )->Release(); x = 0; }
#endif

// Allocate static member
std::map<HWND, CGameEngineWin32* > CGameEngineWin32::m_MapEngineInstances;

//-----------------------------------------------------------------------------
// Purpose: WndProc
//-----------------------------------------------------------------------------
LRESULT CALLBACK GameWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam ) 
{
	switch ( msg )
	{    
	case WM_CLOSE:
	case WM_DESTROY:
	case WM_QUIT:
		{
			CGameEngineWin32 *pGameEngine = CGameEngineWin32::FindEngineInstanceForHWND( hWnd );
			if ( pGameEngine )
				pGameEngine->Shutdown();
			else
				OutputDebugString( "Failed to find game engine instance for hwnd\n" );

			PostQuitMessage( 0 );
			return(0);
		} break;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		{
			CGameEngineWin32 *pGameEngine = CGameEngineWin32::FindEngineInstanceForHWND( hWnd );
			if ( pGameEngine )
			{
				pGameEngine->RecordKeyDown( (DWORD) wParam );
				return 0;
			}
			else
			{
				OutputDebugString( "Failed to find game engine for hwnd, key down event lost\n" );
			}
		}
		break;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		{
			CGameEngineWin32 *pGameEngine = CGameEngineWin32::FindEngineInstanceForHWND( hWnd );
			if ( pGameEngine )
			{
				pGameEngine->RecordKeyUp( (DWORD) wParam );
				return 0;
			}
			else
			{
				OutputDebugString( "Failed to find game engine for hwnd, key up event lost\n" );
			}
		}
		break;

		// Add additional handlers for things like input here...
	default: 
		break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

class CVoiceContext : public IXAudio2VoiceCallback
{
public:
	CVoiceContext() : m_hBufferEndEvent( CreateEvent( NULL, FALSE, FALSE, NULL ) )
	{
		m_pSourceVoice = NULL;
	}
	virtual ~CVoiceContext()
	{
		CloseHandle( m_hBufferEndEvent );
	}

	STDMETHOD_( void, OnVoiceProcessingPassStart )( UINT32 )
	{
	}
	STDMETHOD_( void, OnVoiceProcessingPassEnd )()
	{
	}
	STDMETHOD_( void, OnStreamEnd )()
	{
	}
	STDMETHOD_( void, OnBufferStart )( void* )
	{
	}
	STDMETHOD_( void, OnBufferEnd )( void* pContext )
	{
		free( pContext ); // free the sound buffer
		SetEvent( m_hBufferEndEvent );
	}
	STDMETHOD_( void, OnLoopEnd )( void* )
	{
	}
	STDMETHOD_( void, OnVoiceError )( void*, HRESULT )
	{
	}

	HANDLE m_hBufferEndEvent;
	IXAudio2SourceVoice* m_pSourceVoice;
};


//-----------------------------------------------------------------------------
// Purpose: Constructor for game engine instance
//-----------------------------------------------------------------------------
CGameEngineWin32::CGameEngineWin32( HINSTANCE hInstance, int nShowCommand, int32 nWindowWidth, int32 nWindowHeight, bool bUseVR )
{
	m_bEngineReadyForUse = false;
	m_bShuttingDown = false;
	m_hInstance = hInstance;
	m_hWnd = NULL;
	m_pD3D9Interface = NULL;
	m_pD3D9Device = NULL;
	m_pXAudio2 = NULL;
	m_pMasteringVoice = NULL;
	m_unVoiceChannelCount = 0; // 0 == invalid handle
	m_nWindowWidth = nWindowWidth;
	m_nWindowHeight = nWindowHeight;
	m_nNextFontHandle = 1;
	m_nNextVertBufferHandle = 1;
	m_nNextTextureHandle = 1;
	m_hLineBuffer = NULL;
	m_pLineVertexes = NULL;
	m_dwLinesToFlush = 0;
	m_dwLineBufferBatchPos = 0;
	m_hPointBuffer = NULL;
	m_pPointVertexes = NULL;
	m_dwPointsToFlush = 0;
	m_dwPointBufferBatchPos = 0;

	m_hQuadBuffer = NULL;
	m_pQuadVertexes = NULL;
	m_dwQuadsToFlush = 0;
	m_dwQuadBufferBatchPos = 0;

	m_h3DQuadBuffer = NULL;
	m_p3DQuadVertexes = NULL;
	m_dw3DQuadsToFlush = 0;
	m_dw3DQuadBufferBatchPos = 0;

	m_hTextureWhite = NULL;
	m_bDeviceLost = false;
	m_hLastTexture = NULL;
	m_ulPreviousGameTickCount = 0;
	m_ulGameTickCount = 0;
	m_dwBackgroundColor = D3DCOLOR_ARGB(0, 255, 255, 255 );
	m_pBackbufferDepth = NULL;
	m_hVRDistortionMap[0] = NULL;
	m_hVRDistortionMap[1] = NULL;
	m_pVRDistortionPixelShader = NULL;

	if ( !bUseVR )
	{
		m_pVRHmd = NULL;
	}
	else
	{
		vr::HmdError error;
		m_pVRHmd = vr::VR_Init( &error );
		if ( !m_pVRHmd )
		{
			::MessageBoxA( NULL, "Failed to initialize VR.\n\nGame will now exit.", "SteamworksExample - Fatal error", MB_OK | MB_ICONERROR );
			return;
		}
	}

	// for XAudio2
	CoInitializeEx( NULL, COINIT_MULTITHREADED );
	
	// restrict this main game thread to the first processor, so query performance counter won't jump on crappy AMD cpus
	DWORD dwThreadAffinityMask = 0x01;
	::SetThreadAffinityMask( ::GetCurrentThread(), dwThreadAffinityMask );

	// Setup timing data
	LARGE_INTEGER l;
	::QueryPerformanceFrequency( &l );
	m_ulPerfCounterToMillisecondsDivisor = l.QuadPart/1000;

	::QueryPerformanceCounter( &l );
	m_ulFirstQueryPerformanceCounterValue = l.QuadPart;

	if ( !BCreateGameWindow( nShowCommand ) || !m_hWnd )
	{
		OutputDebugString( "Failed creating game window\n" );
		return;
	}

	CGameEngineWin32::AddInstanceToHWNDMap( this, m_hWnd );

	if ( !BInitializeD3D9() )
	{
		::MessageBoxA( NULL, "Failed to initialize D3D9.\n\nGame will now exit.", "SteamworksExample - Fatal error", MB_OK | MB_ICONERROR );
		return;
	}

	if ( m_pVRHmd )
	{
		// these sizes are used for the 2D rendering
		m_nViewportWidth = 640;
		m_nViewportHeight = 480;
		m_hVR2DRenderTarget = HCreateTexture( NULL, m_nViewportWidth, m_nViewportHeight );

		uint32 w, h;
		m_pVRHmd->GetRecommendedRenderTargetSize( &w, &h );
		m_hVRSceneRenderTarget = HCreateTexture( NULL, w, h );

	}
	else
	{
		RECT r;
		::GetClientRect( m_hWnd, &r );
		m_nViewportWidth = r.right - r.left;
		m_nViewportHeight = r.bottom - r.top;

		m_hVR2DRenderTarget = NULL;
		m_hVRSceneRenderTarget = NULL;
	}

	// initialize XAudio2 interface
	if( FAILED( XAudio2Create( &m_pXAudio2, 0 ) ) )
	{
		::MessageBoxA( NULL, "Failed to init XAudio2 engine (grab the latest \"DirectX End-User Runtime Web Installer\" )", "SteamworksExample - Fatal error", MB_OK | MB_ICONERROR );
		return;
	}

	// Create a mastering voice
	if( FAILED( m_pXAudio2->CreateMasteringVoice( &m_pMasteringVoice, XAUDIO2_DEFAULT_CHANNELS, VOICE_OUTPUT_SAMPLE_RATE ) ) )
	{
		::MessageBoxA( NULL, "Failed to create mastering voice", "SteamworksExample - Fatal error", MB_OK | MB_ICONERROR );
		return;
	}

	m_bEngineReadyForUse = true;
}


//-----------------------------------------------------------------------------
// Purpose: Shutdown the game engine
//-----------------------------------------------------------------------------
void CGameEngineWin32::Shutdown()
{
	// Flag that we are shutting down so the frame loop will stop running
	m_bShuttingDown = true;

	// Cleanup D3D fonts
	{
		std::map<HGAMEFONT, ID3DXFont *>::iterator iter;
		for( iter = m_MapFontInstances.begin(); iter != m_MapFontInstances.end(); ++iter )
		{
			SAFE_RELEASE( iter->second );
		}
		m_MapFontInstances.clear();
	}

	// Cleanup D3D vertex buffers
	{
		std::map<HGAMEVERTBUF, VertBufData_t>::iterator iter;
		for( iter = m_MapVertexBuffers.begin(); iter != m_MapVertexBuffers.end(); ++iter )
		{
			if ( iter->second.m_bIsLocked )
				iter->second.m_pBuffer->Unlock();
			SAFE_RELEASE( iter->second.m_pBuffer );
		}
		m_MapVertexBuffers.clear();
	}

	// Cleanup D3D textures
	{
		std::map<HGAMETEXTURE, TextureData_t>::iterator iter;
		for( iter = m_MapTextures.begin(); iter != m_MapTextures.end(); ++iter )
		{
			if ( iter->second.m_pRGBAData )
			{
				delete[] iter->second.m_pRGBAData;
				iter->second.m_pRGBAData = NULL;
			}
			if ( iter->second.m_pTexture )
			{
				SAFE_RELEASE( iter->second.m_pTexture );
			}
		}
		m_MapTextures.clear();
	}

	// All XAudio2 interfaces are released when the engine is destroyed, but being tidy
	if ( m_pMasteringVoice )
	{
		m_pMasteringVoice->DestroyVoice();
		m_pMasteringVoice = NULL;
	}

	// Cleanup D3D
	SAFE_RELEASE( m_pBackbufferDepth );
	SAFE_RELEASE( m_pD3D9Device );
	SAFE_RELEASE( m_pD3D9Interface );
	SAFE_RELEASE( m_pXAudio2 );
	SAFE_RELEASE( m_pVRDistortionPixelShader );

	// Destroy our window
	if ( m_hWnd )
	{
		if ( !DestroyWindow( m_hWnd ) )
		{
			// We failed to destroy our window. This shouldn't ever happen.
			OutputDebugString( "Failed destroying window\n" );
		}
		else
		{
			// Clean up any pending messages.
			MSG msg;
			while( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
			{
				DispatchMessage(&msg);
			}
		}

		CGameEngineWin32::RemoveInstanceFromHWNDMap( m_hWnd );
		m_hWnd = NULL;
	}

	// Unregister our window class
	if ( m_hInstance )
	{
		if ( !UnregisterClass( "SteamworksExample", m_hInstance ) )
		{
			OutputDebugString( "Failed unregistering window class\n" );
		}
		m_hInstance = NULL;
	}

	CoUninitialize();

	if ( m_pVRHmd )
	{
		vr::VR_Shutdown();
		m_pVRHmd = NULL;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Handle losing the d3d device (ie, release resources)
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BHandleLostDevice()
{
	bool bFullySuccessful = true;

	// Clear our saved FVF so that we will manually reset it once we re-acquire the device
	m_dwCurrentFVF = NULL;

	// Clear our internal buffers and batching data
	m_dwLinesToFlush = 0;
	m_dwLineBufferBatchPos = 0;
	m_pLineVertexes = NULL;
	m_hLineBuffer = NULL;
	m_dwPointsToFlush = 0;
	m_dwPointBufferBatchPos = 0;
	m_pPointVertexes = NULL;
	m_hPointBuffer = NULL;
	m_dwQuadsToFlush = 0;
	m_dwQuadBufferBatchPos = 0;
	m_pQuadVertexes = NULL;
	m_hQuadBuffer = NULL;

	// Fonts are easy since we used d3dx, they have their own handlers
	{
		std::map<HGAMEFONT, ID3DXFont *>::iterator iter;
		for( iter = m_MapFontInstances.begin(); iter != m_MapFontInstances.end(); ++iter )
		{
			if ( FAILED( iter->second->OnLostDevice() ) )
			{
				bFullySuccessful = false;
				OutputDebugString( "Failed OnLostDevice on a font object\n" );
			}
		}
	}

	// Vertex buffers we have to release and then re-create later, since we only use them internal
	// to the engine we can just free them all, and we'll know how to recreate them later on demand
	{
		std::map<HGAMEVERTBUF, VertBufData_t>::iterator iter;
		for( iter = m_MapVertexBuffers.begin(); iter != m_MapVertexBuffers.end(); ++iter )
		{
			if ( iter->second.m_pBuffer )
			{
				if ( iter->second.m_bIsLocked )
					iter->second.m_pBuffer->Unlock();
				iter->second.m_bIsLocked = false;
				iter->second.m_pBuffer->Release();
				iter->second.m_pBuffer = NULL;
			}
		}
		m_MapVertexBuffers.clear();
	}

	// Textures we can just release, and they will be recreated on demand when used again
	{
		std::map<HGAMETEXTURE, TextureData_t>::iterator iter;
		for( iter = m_MapTextures.begin(); iter != m_MapTextures.end(); ++iter )
		{
			if ( iter->second.m_pTexture )
			{
				iter->second.m_pTexture->Release();
				iter->second.m_pTexture = NULL;
			}
			if ( iter->second.m_pDepthSurface )
			{
				iter->second.m_pDepthSurface->Release( );
				iter->second.m_pDepthSurface = NULL;
			}
		}
	}

	// if we have a VR shader, free it We'll re-create it on demand
	if ( m_pVRDistortionPixelShader )
	{
		m_pVRDistortionPixelShader->Release();
		m_pVRDistortionPixelShader = NULL;
	}

	return bFullySuccessful;
}

//-----------------------------------------------------------------------------
// Purpose: Handle device reset after losing it
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BHandleResetDevice()
{
	bool bFullySuccessful = true;

	ResetRenderStates();

	// Fonts are easy since we used d3dx, they have their own handlers
	{
		std::map<HGAMEFONT, ID3DXFont *>::iterator iter;
		for( iter = m_MapFontInstances.begin(); iter != m_MapFontInstances.end(); ++iter )
		{
			if ( FAILED( iter->second->OnResetDevice() ) )
			{
				OutputDebugString( "Reset for a font object failed\n" );
				bFullySuccessful = false;
			}
		}
	}

	// Vertex buffers we only use internal to the class, so we know
	// how to recreate them on demand.  Nothing to do here for them.

	return bFullySuccessful;
}


//-----------------------------------------------------------------------------
// Purpose: Updates current tick count for the game engine
//-----------------------------------------------------------------------------
void CGameEngineWin32::UpdateGameTickCount()
{
	LARGE_INTEGER l;
	::QueryPerformanceCounter( &l );

	m_ulPreviousGameTickCount = m_ulGameTickCount;
	m_ulGameTickCount = (l.QuadPart - m_ulFirstQueryPerformanceCounterValue) / m_ulPerfCounterToMillisecondsDivisor;
}


//-----------------------------------------------------------------------------
// Purpose: Tell the game engine to sleep for a bit if needed to limit frame rate.  You must keep
// calling this repeatedly until it returns false.  If it returns true it's slept a little, but more
// time may be needed.
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BSleepForFrameRateLimit( uint32 ulMaxFrameRate )
{
	// Frame rate limiting
	float flDesiredFrameMilliseconds = 1000.0f/ulMaxFrameRate;

	LARGE_INTEGER l;
	::QueryPerformanceCounter( &l );

	uint64 ulGameTickCount = (l.QuadPart - m_ulFirstQueryPerformanceCounterValue) / m_ulPerfCounterToMillisecondsDivisor;

	float flMillisecondsElapsed = (float)(ulGameTickCount - m_ulGameTickCount);
	if ( flMillisecondsElapsed < flDesiredFrameMilliseconds )
	{
		// If enough time is left sleep, otherwise just keep spinning so we don't go over the limit...
		if ( flDesiredFrameMilliseconds - flMillisecondsElapsed > 3.0f )
		{
			Sleep( 2 );
		}
		else
		{
			// Just return right away so we busy loop, don't want to sleep too long and go over
		}
		
		return true;
	}
	else
	{
		return false;
	}
}



//-----------------------------------------------------------------------------
// Purpose: Resets all the render, texture, and sampler states to our defaults
//-----------------------------------------------------------------------------
void CGameEngineWin32::ResetRenderStates()
{
	// Since we are just a really basic rendering engine we'll setup our initial 
	// render states here and we can just assume that they don't change later
	m_pD3D9Device->SetRenderState( D3DRS_LIGHTING, FALSE );
	m_pD3D9Device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
	m_pD3D9Device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_BOTHSRCALPHA );
	m_pD3D9Device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );

	// texture stage state
	m_pD3D9Device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
	m_pD3D9Device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	m_pD3D9Device->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_CURRENT );
	m_pD3D9Device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
	m_pD3D9Device->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
	m_pD3D9Device->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_CURRENT );

	// sampler state
	m_pD3D9Device->SetSamplerState(	0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	m_pD3D9Device->SetSamplerState(	0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	m_pD3D9Device->SetSamplerState(	0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR );
	m_pD3D9Device->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
	m_pD3D9Device->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
	m_pD3D9Device->SetSamplerState( 1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
	m_pD3D9Device->SetSamplerState( 1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	m_pD3D9Device->SetSamplerState( 1, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR );
	m_pD3D9Device->SetSamplerState( 1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
	m_pD3D9Device->SetSamplerState( 1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
}


//-----------------------------------------------------------------------------
// Purpose: Creates the window for the game to use
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BCreateGameWindow( int nShowCommand )
{
	int windowX = 0;
	int windowY = 0;

	if ( m_pVRHmd )
	{
		int32 x, y;
		uint32 w, h;
		m_pVRHmd->GetWindowBounds( &x, &y, &w, &h );
		m_nWindowWidth = w;
		m_nWindowHeight = h;
		windowX = x;
		windowY = y;
	}

	WNDCLASS wc;
	DWORD	 style;

	// Register the window class.
	wc.lpszClassName	= "SteamworksExample";
	wc.lpfnWndProc		= GameWndProc;
	wc.style			= 0;
	wc.cbClsExtra		= 0;
	wc.cbWndExtra		= 0;
	wc.hInstance		= m_hInstance;
	wc.hIcon			= LoadIcon(NULL,IDI_APPLICATION);
	wc.hCursor			= LoadCursor(NULL,IDC_ARROW);
	wc.hbrBackground	= (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszMenuName		= NULL;

	if ( !RegisterClass( &wc ) )
	{
		OutputDebugString( "Failure registering window class\n" );
		return false;
	}

	// Set parent window mode (normal system window with overlap/draw-ordering)
	if ( m_pVRHmd )
		style = WS_POPUP;
	else
		style = WS_OVERLAPPED|WS_SYSMENU;

	// Create actual window
	m_hWnd = CreateWindow( "SteamworksExample", 
		"SteamworksExample",
		style,
		windowX,
		windowY, 
		m_nWindowWidth, 
		m_nWindowHeight,
		NULL, 
		NULL, 
		m_hInstance,
		NULL );

	if ( m_hWnd == NULL ) 
	{
		OutputDebugString( "Failed to create window for CGameEngine\n" );
		return false;
	}

	// Give focus to newly created app window.
	::ShowWindow( m_hWnd, nShowCommand );
	::UpdateWindow( m_hWnd );
	::SetFocus( m_hWnd );

	return true;
}

bool CGameEngineWin32::BInitializeD3D9()
{
	if ( !m_pD3D9Interface )
	{
		// Initialize the d3d interface
		m_pD3D9Interface = Direct3DCreate9( D3D_SDK_VERSION );
		if ( m_pD3D9Interface == NULL )
		{
			OutputDebugString( "Direct3DCreate9 failed\n" );
			return false;
		}
	}

	if ( !m_pD3D9Device )
	{
		D3DDISPLAYMODE d3ddisplaymode;

		// Get the current desktop display mode, only needed if running in a window.
		HRESULT hRes = m_pD3D9Interface->GetAdapterDisplayMode( D3DADAPTER_DEFAULT, &d3ddisplaymode );
		if (FAILED(hRes))
		{
			OutputDebugString( "GetAdapterDisplayMode failed\n");
			return false;
		}

		// Setup presentation parameters
		ZeroMemory( &m_d3dpp, sizeof( m_d3dpp ) );
		m_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD; 
		m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; // no v-sync
		m_d3dpp.hDeviceWindow = m_hWnd;
		m_d3dpp.BackBufferCount = 1; 
		m_d3dpp.EnableAutoDepthStencil = TRUE;
		m_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
		m_d3dpp.Windowed = TRUE; // bugbug jmccaskey - make a parameter?
		m_d3dpp.BackBufferFormat  = d3ddisplaymode.Format; 

		UINT nAdapter = D3DADAPTER_DEFAULT;
		if ( m_pVRHmd )
		{
			nAdapter = m_pVRHmd->GetD3D9AdapterIndex();
		}

		// Create Direct3D9 device 
		// (if it fails to create hardware vertex processing, then go with the software alternative).
		hRes = m_pD3D9Interface->CreateDevice(	
			nAdapter, 
			D3DDEVTYPE_HAL,	
			m_hWnd, 
			D3DCREATE_HARDWARE_VERTEXPROCESSING, 
			&m_d3dpp, 
			&m_pD3D9Device );

		// Could not create a hardware device, create a software one instead (slow....)
		if ( FAILED( hRes ) )
		{
			hRes = m_pD3D9Interface->CreateDevice(
				D3DADAPTER_DEFAULT, 
				D3DDEVTYPE_HAL, 
				m_hWnd, 
				D3DCREATE_SOFTWARE_VERTEXPROCESSING, 
				&m_d3dpp, 
				&m_pD3D9Device );
		}

		// If we couldn't create a device even with software vertex processing then 
		// it's a fatal error
		if ( FAILED( hRes ) )
		{
			// Make sure the pointer is NULL after failures (seems it sometimes gets modified even when failing)
			m_pD3D9Device = NULL;

			OutputDebugString( "Failed to create D3D9 device\n" );
			return false;
		}

		if ( FAILED( m_pD3D9Device->GetDepthStencilSurface( &m_pBackbufferDepth ) ) )
		{
			m_pBackbufferDepth = NULL;
			OutputDebugString( "Failed to get the backbuffer depth buffer\n" );
			return false;
		}

		//Initialize our render, texture, and sampler stage states
		ResetRenderStates();

	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Set the background color to clear to
//-----------------------------------------------------------------------------
void CGameEngineWin32::SetBackgroundColor( short a, short r, short g, short b )
{
	m_dwBackgroundColor = D3DCOLOR_ARGB( a, r, g, b );
}

//-----------------------------------------------------------------------------
// Purpose: Start a new frame
//-----------------------------------------------------------------------------
bool CGameEngineWin32::StartFrame()
{
	// Before doing anything else pump messages
	MessagePump();

	// Detect if we lose focus and cause all keys do go up so they aren't stuck
	if ( ::GetForegroundWindow() != m_hWnd )
	{
		m_SetKeysDown.clear();
	}

	// Message pump may have lead us to shutdown, check
	if ( BShuttingDown() )
		return false;

	if ( !m_pD3D9Device )
		return false;

	// Test that we haven't lost the device
	HRESULT hRes = m_pD3D9Device->TestCooperativeLevel();
	if ( hRes == D3DERR_DEVICELOST )
	{
		// Device is currently lost, can't render frames, but lets just let things continue so the
		// simulation keeps running, the game should probably pause itself

		// If it is newly lost then release resources
		if ( !m_bDeviceLost )
		{
			OutputDebugString( "Device lost\n" );

			// HandleLostDevice() will free all our resources
			if ( !BHandleLostDevice() )
				OutputDebugString( "Failed to release all resources for lost device\n" );
		}
		m_bDeviceLost = true;

	}
	else if ( hRes == D3DERR_DEVICENOTRESET )
	{
		OutputDebugString( "Getting ready to reset device\n" );

		// Reset the device
		hRes = m_pD3D9Device->Reset( &m_d3dpp );
		if ( !FAILED( hRes ) )
		{
			m_bDeviceLost = false;
			// Acquire all our resources again
			if ( !BHandleResetDevice() )
			{
				OutputDebugString( "Failed to acquire all resources again after device reset\n" );
			}
		}
		else
		{
			OutputDebugString( "Reset() call on device failed\n" );
			::MessageBox( m_hWnd, "m_pD3D9Device->Reset() call has failed unexpectedly\n", "Fatal Error", MB_OK );
			Shutdown();
			return false;
		}
	}

	// Return true even though we can't render, frames can still run otherwise
	// and the game should continue its simulation or choose to pause on its own
	if ( m_bDeviceLost )
		return true;

	hRes = m_pD3D9Device->BeginScene();
	if ( FAILED( hRes ) )
		return false;

	// if we're in VR mode, set the 2D offscreen rendertarget
	if ( m_pVRHmd )
	{
		if ( !BSetRenderTarget( m_hVR2DRenderTarget ) )
			return false;

		// Clear the back buffer and z-buffer with an opaque color
		hRes = m_pD3D9Device->Clear( 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB( 255, 0, 0, 0), 1.0f, 0 );
		if ( FAILED( hRes ) )
			return false;
	}
	else
	{
		// Clear the back buffer and z-buffer
		hRes = m_pD3D9Device->Clear( 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, m_dwBackgroundColor, 1.0f, 0 );
		if ( FAILED( hRes ) )
			return false;
	}


	return true;
}


//-----------------------------------------------------------------------------
// Purpose: End the current frame
//-----------------------------------------------------------------------------
void CGameEngineWin32::EndFrame()
{
	if ( BShuttingDown() )
		return;

	if ( !m_pD3D9Device )
		return;

	if ( m_bDeviceLost )
		return;

	// See if we lost the device in the middle of the frame
	HRESULT hRes = m_pD3D9Device->TestCooperativeLevel();
	if ( hRes == D3DERR_DEVICELOST )
	{
		// Ok, StartFrame will handle this next time through, but bail out early here
		return;
	}

	// Flush point buffer
	BFlushPointBuffer();

	// Flush line buffer
	BFlushLineBuffer();

	// Flush quad buffer
	BFlushQuadBuffer();

	// draw the VR mode offscreen render target on a quad somewhere
	if ( m_pVRHmd )
	{
		// make sure we have a distortion shader
		if ( !m_pVRDistortionPixelShader )
		{
			if ( !BInitializeVRShader() )
			{
				OutputDebugString( "Unable to init VR shader\n" );
				return;
			}
		}

		// draw some lines around the edge of the "screen" in the 2D texture so that it's very obvious 
		// where the boundary is to aid stereo fusion
		DWORD dwBorderColor = D3DCOLOR_ARGB( 255, 128, 128, 128 );
		float fViewWidth = (float)GetViewportWidth();
		float fViewHeight = (float)GetViewportHeight();

		for ( float f = 0; f < 5.f; f += 1.f )
		{
			BDrawLine( 0 + f, 0 + f, dwBorderColor, fViewWidth - f, 0 + f, dwBorderColor );
			BDrawLine( 0 + f, fViewHeight - f, dwBorderColor, fViewWidth - f, fViewHeight - f, dwBorderColor );
			BDrawLine( 0 + f, 0 + f, dwBorderColor, 0 + f, fViewHeight - f, dwBorderColor );
			BDrawLine( fViewWidth - f, 0 + f, dwBorderColor, fViewWidth - f, fViewHeight - f, dwBorderColor );
		}
		BFlushLineBuffer();

		// Clear the frame buffer
		BUnsetRenderTarget();
		hRes = m_pD3D9Device->Clear( 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, m_dwBackgroundColor, 1.0f, 0 );

		float fHeight = (float)GetViewportHeight( ) / (float)GetViewportWidth( );
		Textured3DQuadVertex_t vScreenQuad[4] = 
		{
			{ 
				-1.f, fHeight, -2.f,
				0xFFFFFFFF,
				0.f, 0.f,
			},
			{
				1.f, fHeight , -2.f,
				0xFFFFFFFF,
				1.f, 0.f,
			},
			{
				-1.f, -fHeight , -2.f,
				0xFFFFFFFF,
				0.f, 1.f,
			},
			{
				1.f, -fHeight , -2.f,
				0xFFFFFFFF,
				1.f, 1.f,
			}
		};
		Textured3DQuadVertex_t vScreenQuadInverse[4];
		for ( int i = 0; i < 4; i++ )
		{
			vScreenQuadInverse[i] = vScreenQuad[3 - i];
		}

		vr::HmdTrackingResult eResult;
		vr::HmdMatrix44_t matLeftView, matRightView;
		bool bGotPose = m_pVRHmd->GetViewMatrix( 0, &matLeftView, &matRightView, &eResult );
		for ( int i = 0; i < 2; i++ )
		{

			vr::Hmd_Eye eye = (vr::Hmd_Eye)i;

			if ( !BSetRenderTarget( m_hVRSceneRenderTarget ) )
			{
				OutputDebugString( "Unable to set scene render target\n" );
				return;
			}
			hRes = m_pD3D9Device->Clear( 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB( 0, 50, 50, 50 ), 1.0f, 0 );

			if ( !m_hTextureWhite )
			{
				byte *pRGBAData = new byte[1 * 1 * 4];
				memset( pRGBAData, 255, 1 * 1 * 4 );
				m_hTextureWhite = HCreateTexture( pRGBAData, 1, 1 );
				delete[] pRGBAData;
			}

			vr::HmdMatrix44_t matProjection = m_pVRHmd->GetProjectionMatrix( eye, 0.1f, 100.f, vr::API_DirectX );
			D3DXMATRIX matD3DProj = D3DXMATRIX( &matProjection.m[0][0] );
			D3DXMATRIX matD3DProj_transposed;
			D3DXMatrixTranspose( &matD3DProj_transposed, &matD3DProj );
			m_pD3D9Device->SetTransform( D3DTS_PROJECTION, &matD3DProj_transposed );

			D3DXMATRIX matD3DView;
			if ( eye == vr::Eye_Left )
				matD3DView = D3DXMATRIX( &matLeftView.m[0][0] );
			else
				matD3DView = D3DXMATRIX( &matRightView.m[0][0] );
			D3DXMATRIX matD3DView_transposed;
			D3DXMatrixTranspose( &matD3DView_transposed, &matD3DView );
			m_pD3D9Device->SetTransform( D3DTS_VIEW, &matD3DView_transposed );


			// draw the 2D texture on a quad into the 3D render target
			BDraw3DTexturedQuad( vScreenQuad, m_hVR2DRenderTarget );
			//BDraw3DTexturedQuad( vScreenQuadInverse, m_hVR2DRenderTarget );
			BFlush3DQuadBuffer( );

			// now draw the scene to the frame buffer with the distortion texture
			BUnsetRenderTarget();

			// get the viewport from the Hmd
			uint32 vpx, vpy, vpw, vph;
			m_pVRHmd->GetEyeOutputViewport( eye, &vpx, &vpy, &vpw, &vph );

			if ( FAILED( m_pD3D9Device->SetPixelShader( m_pVRDistortionPixelShader ) ) )
			{
				OutputDebugString( "Couldn't set VR pixel shader\n" );
			}

			if ( !BReadyTexture( m_hVRDistortionMap[i] ) )
			{
				OutputDebugString( "Couldn't ready distortion map\n" );
			}
			if ( FAILED( m_pD3D9Device->SetTexture( 1, m_MapTextures[m_hVRDistortionMap[i]].m_pTexture ) ) )
			{
				OutputDebugString( "Couldn't set distortion map\n" );
			}


			BDrawTexturedRect( (float)vpx, (float)vpy, (float)(vpx + vpw), (float)(vpy + vph), 0, 0, 1.f, 1.f, D3DCOLOR_ARGB( 255, 255, 255, 255 ), m_hVRSceneRenderTarget );
			BFlushQuadBuffer();

			m_pD3D9Device->SetTexture( 1, NULL );
			m_pD3D9Device->SetPixelShader( NULL );
		}
	}

	hRes = m_pD3D9Device->EndScene();
	if ( FAILED( hRes ) ) 
	{
		OutputDebugString( "EndScene() call failed\n" );
		return;
	}

	hRes = m_pD3D9Device->Present( NULL, NULL, NULL, NULL );
	if ( FAILED( hRes ) )
	{
		OutputDebugString( "Present() call failed\n" );
		return;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Creates a new vertex buffer
//-----------------------------------------------------------------------------
HGAMEVERTBUF CGameEngineWin32::HCreateVertexBuffer( uint32 nSizeInBytes, DWORD dwUsage, DWORD dwFVF )
{
	if ( !m_pD3D9Device )
		return false;

	// Create a vertex buffer object
	IDirect3DVertexBuffer9 *pVertBuffer;
	HRESULT hRes = m_pD3D9Device->CreateVertexBuffer( nSizeInBytes, dwUsage,
		dwFVF, D3DPOOL_DEFAULT, &pVertBuffer, NULL );
	if ( FAILED( hRes ) )
	{
		OutputDebugString( "Failed creating vertex buffer\n" );
		return 0;
	}

	HGAMEFONT hVertBuf = m_nNextVertBufferHandle;
	++m_nNextVertBufferHandle;

	VertBufData_t data;
	data.m_bIsLocked = false;
	data.m_pBuffer = pVertBuffer;

	m_MapVertexBuffers[ hVertBuf ] = data;
	return hVertBuf;
}


//-----------------------------------------------------------------------------
// Purpose: Locks an entire vertex buffer with the specified flags into memory
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BLockEntireVertexBuffer( HGAMEVERTBUF hVertBuf, void **ppVoid, DWORD dwFlags )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return false;

	if ( !hVertBuf )
	{
		OutputDebugString( "Someone is calling BLockEntireVertexBuffer() with a null handle\n" );
		return false;
	}

	// Find the font object for the passed handle
	std::map<HGAMEVERTBUF, VertBufData_t>::iterator iter;
	iter = m_MapVertexBuffers.find( hVertBuf );
	if ( iter == m_MapVertexBuffers.end() )
	{
		OutputDebugString( "Invalid vertex buffer handle passed to BLockEntireVertexBuffer()\n" );
		return false;
	}

	// Make sure the pointer is valid
	if ( !iter->second.m_pBuffer )
	{
		OutputDebugString( "Pointer to vertex buffer is invalid (lost device and not recreated?)!\n" );
		return false;
	}


	// Make sure its not already locked
	if ( iter->second.m_bIsLocked )
	{
		OutputDebugString( "Trying to lock an already locked vertex buffer!\n" );
		return false;
	}

	// we have the buffer, try to lock it
	if( FAILED( iter->second.m_pBuffer->Lock( 0, 0, ppVoid, dwFlags ) ) )
	{
		OutputDebugString( "BLockEntireVertexBuffer call failed\n" );
		return false;
	}

	// Track that we are now locked
	iter->second.m_bIsLocked = true;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Unlocks a vertex buffer
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BUnlockVertexBuffer( HGAMEVERTBUF hVertBuf )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return false;

	if ( !hVertBuf )
	{
		OutputDebugString( "Someone is calling BUnlockVertexBuffer() with a null handle\n" );
		return false;
	}

	// Find the vertex buffer for the passed handle
	std::map<HGAMEVERTBUF, VertBufData_t>::iterator iter;
	iter = m_MapVertexBuffers.find( hVertBuf );
	if ( iter == m_MapVertexBuffers.end() )
	{
		OutputDebugString( "Invalid vertex buffer handle passed to BUnlockVertexBuffer()\n" );
		return false;
	}

	// Make sure the pointer is valid
	if ( !iter->second.m_pBuffer )
	{
		OutputDebugString( "Pointer to vertex buffer is invalid (lost device and not recreated?)!\n" );
		return false;
	}

	// Make sure we are locked if someone is trying to unlock
	if ( !iter->second.m_bIsLocked )
	{
		OutputDebugString( "Trying to unlock a vertex buffer that is not locked!\n" );
		return false;
	}

	// we have the buffer, try to lock it
	if( FAILED( iter->second.m_pBuffer->Unlock() ) )
	{
		OutputDebugString( "BUnlockVertexBuffer call failed\n" );
		return false;
	}

	// Track that we are now unlocked
	iter->second.m_bIsLocked = false;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Release a vertex buffer and free its resources
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BReleaseVertexBuffer( HGAMEVERTBUF hVertBuf )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return false;

	if ( !hVertBuf )
	{
		OutputDebugString( "Someone is calling BReleaseVertexBuffer() with a null handle\n" );
		return false;
	}

	// Find the vertex buffer object for the passed handle
	std::map<HGAMEVERTBUF, VertBufData_t>::iterator iter;
	iter = m_MapVertexBuffers.find( hVertBuf );
	if ( iter == m_MapVertexBuffers.end() )
	{
		OutputDebugString( "Invalid vertex buffer handle passed to BReleaseVertexBuffer()\n" );
		return false;
	}

	// Make sure the pointer is valid
	if ( !iter->second.m_pBuffer )
	{
		OutputDebugString( "Pointer to vertex buffer is invalid (lost device and not recreated?)!\n" );
		return false;
	}

	// Make sure its unlocked, if it isn't locked this will just fail quietly
	if ( iter->second.m_bIsLocked )
		iter->second.m_pBuffer->Unlock();

	// Release the resources
	iter->second.m_pBuffer->Release();

	// Remove from the map
	m_MapVertexBuffers.erase( iter );

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Set the FVF
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BSetFVF( DWORD dwFormat )
{
	if ( !m_pD3D9Device )
		return false;

	// Can't set the FVF when the device is lost
	if ( m_bDeviceLost )
		return false;

	// Short circuit if the request is a noop
	if ( m_dwCurrentFVF == dwFormat )
		return true;

	if ( FAILED( m_pD3D9Device->SetFVF( dwFormat ) ) )
	{
		OutputDebugString( "SetFVF() call failed\n" );
		return false;
	}

	m_dwCurrentFVF = dwFormat;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Draw a line, the engine internally manages a vertex buffer for batching these
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BDrawLine( float xPos0, float yPos0, DWORD dwColor0, float xPos1, float yPos1, DWORD dwColor1 )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return true; // Fail silently in this case

	if ( !m_hLineBuffer )
	{
		// Create the line buffer
		m_hLineBuffer = HCreateVertexBuffer( sizeof( LineVertex_t ) * LINE_BUFFER_TOTAL_SIZE * 2, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_XYZRHW | D3DFVF_DIFFUSE );

		if ( !m_hLineBuffer )
		{
			OutputDebugString( "Can't BDrawLine() because vertex buffer creation failed\n" );
			return false;
		}
	}

	// Check if we are out of room and need to flush the buffer
	if ( m_dwLinesToFlush == LINE_BUFFER_BATCH_SIZE )
	{
		BFlushLineBuffer();
	}

	// Set FVF
	if ( !BSetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE ) )
		return false;

	// Lock the vertex buffer into memory
	if ( !m_pLineVertexes )
	{
		if ( !BLockEntireVertexBuffer( m_hLineBuffer, (void**)&m_pLineVertexes, m_dwLineBufferBatchPos ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD ) )
		{
			m_pLineVertexes = NULL;
			OutputDebugString( "BDrawLine failed because locking vertex buffer failed\n" );
			return false;
		}
	}

	LineVertex_t *pVertData = &m_pLineVertexes[ m_dwLineBufferBatchPos*2+m_dwLinesToFlush*2 ];
	pVertData[0].rhw = 1.0;
	pVertData[0].z = 1.0;
	pVertData[0].x = xPos0;
	pVertData[0].y = yPos0;
	pVertData[0].color = dwColor0;

	pVertData[1].rhw = 1.0;
	pVertData[1].z = 1.0;
	pVertData[1].x = xPos1;
	pVertData[1].y = yPos1;
	pVertData[1].color = dwColor1;

	++m_dwLinesToFlush;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Flush batched lines to the screen
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BFlushLineBuffer()
{
	// If the vert buffer isn't already locked into memory, then there is nothing to flush
	if ( m_pLineVertexes == NULL )
		return true; // consider this successful since there was no error

	// OK, it is locked, so unlock now
	if ( !BUnlockVertexBuffer( m_hLineBuffer ) )
	{
		OutputDebugString( "Failed flushing line buffer because BUnlockVertexBuffer failed\n" );
		return false;
	}

	// Clear the memory pointer as its invalid now that we unlocked
	m_pLineVertexes = NULL;

	// If there is nothing to actual flush, we are done
	if ( m_dwLinesToFlush == 0 )
		return true;

	// Set FVF (will short circuit if this is already the set FVF)
	if ( !BSetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE ) )
	{
		OutputDebugString( "Failed flushing line buffer because BSetFVF failed\n" );
		return false;
	}

	if ( !BSetStreamSource( m_hLineBuffer, 0, sizeof( LineVertex_t ) ) )
	{
		OutputDebugString( "Failed flushing line buffer because BSetStreamSource failed\n" );
		return false;
	}

	// Actual render calls
	if ( !BRenderPrimitive( D3DPT_LINELIST, m_dwLineBufferBatchPos*2, m_dwLinesToFlush ) )
	{
		OutputDebugString( "Failed flushing line buffer because BRenderPrimitive failed\n" );
		return false;
	}

	m_dwLinesToFlush = 0;
	m_dwLineBufferBatchPos += LINE_BUFFER_BATCH_SIZE;
	if ( m_dwLineBufferBatchPos >= LINE_BUFFER_TOTAL_SIZE )
	{
		m_dwLineBufferBatchPos = 0;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Draw a point, the engine internally manages a vertex buffer for batching these
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BDrawPoint( float xPos, float yPos, DWORD dwColor )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return true; // Fail silently in this case

	if ( !m_hPointBuffer )
	{
		// Create the point buffer
		m_hPointBuffer = HCreateVertexBuffer( sizeof( PointVertex_t ) * POINT_BUFFER_TOTAL_SIZE * 2, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_XYZRHW | D3DFVF_DIFFUSE );

		if ( !m_hPointBuffer )
		{
			OutputDebugString( "Can't BDrawPoint() because vertex buffer creation failed\n" );
			return false;
		}
	}

	// Check if we are out of room and need to flush the buffer
	if ( m_dwPointsToFlush == POINT_BUFFER_BATCH_SIZE )	
	{
		BFlushPointBuffer();
	}

	// Set FVF
	if ( !BSetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE ) )
		return false;

	// Lock the vertex buffer into memory
	if ( !m_pPointVertexes )
	{
		if ( !BLockEntireVertexBuffer( m_hPointBuffer, (void**)&m_pPointVertexes, (m_dwPointBufferBatchPos+m_dwPointsToFlush) ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD ) )
		{
			m_pPointVertexes = NULL;
			OutputDebugString( "BDrawPoint failed because locking vertex buffer failed\n" );
			return false;
		}
	}

	PointVertex_t *pVertData = &m_pPointVertexes[ m_dwPointBufferBatchPos+m_dwPointsToFlush ];
	pVertData[0].rhw = 1.0;
	pVertData[0].z = 1.0;
	pVertData[0].x = xPos;
	pVertData[0].y = yPos;
	pVertData[0].color = dwColor;

	++m_dwPointsToFlush;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Flush batched points to the screen
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BFlushPointBuffer()
{
	// If the vert buffer isn't already locked into memory, then there is nothing to flush
	if ( m_pPointVertexes == NULL )
		return true; // consider this successful since there was no error

	// OK, it is locked, so unlock now
	if ( !BUnlockVertexBuffer( m_hPointBuffer ) )
	{
		OutputDebugString( "Failed flushing point buffer because BUnlockVertexBuffer failed\n" );
		return false;
	}

	// Clear the memory pointer as its invalid now that we unlocked
	m_pPointVertexes = NULL;

	// If there is nothing to actual flush, we are done
	if ( m_dwPointsToFlush == 0 )
		return true;

	// Set FVF (will short circuit if this is already the set FVF)
	if ( !BSetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE ) )
	{
		OutputDebugString( "Failed flushing point buffer because BSetFVF failed\n" );
		return false;
	}

	if ( !BSetStreamSource( m_hPointBuffer, 0, sizeof( PointVertex_t ) ) )
	{
		OutputDebugString( "Failed flushing point buffer because BSetStreamSource failed\n" );
		return false;
	}

	// Actual render calls
	if ( !BRenderPrimitive( D3DPT_POINTLIST, m_dwPointBufferBatchPos, m_dwPointsToFlush ) )
	{
		OutputDebugString( "Failed flushing point buffer because BRenderPrimitive failed\n" );
		return false;
	}

	m_dwPointsToFlush = 0;
	m_dwPointBufferBatchPos += POINT_BUFFER_BATCH_SIZE;
	if ( m_dwPointBufferBatchPos >= POINT_BUFFER_TOTAL_SIZE )
	{
		m_dwPointBufferBatchPos = 0;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Draw a filled quad
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BDrawFilledRect( float xPos0, float yPos0, float xPos1, float yPos1, DWORD dwColor )
{
	if ( !m_hTextureWhite )
	{
		byte *pRGBAData = new byte[ 1 * 1 * 4 ];
		memset( pRGBAData, 255, 1*1*4 );
		m_hTextureWhite = HCreateTexture( pRGBAData, 1, 1 );
		delete[] pRGBAData;
	}

	return BDrawTexturedRect( xPos0, yPos0, xPos1, yPos1, 0.0f, 0.0f, 1.0f, 1.0f, dwColor, m_hTextureWhite );
}

//-----------------------------------------------------------------------------
// Purpose: Draw a textured quad
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BDrawTexturedRect( float xPos0, float yPos0, float xPos1, float yPos1, float u0, float v0, float u1, float v1, DWORD dwColor, HGAMETEXTURE hTexture )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return true; // Fail silently in this case

	// Find the texture
	std::map<HGAMETEXTURE, TextureData_t>::iterator iter;
	iter = m_MapTextures.find( hTexture );
	if ( iter == m_MapTextures.end() )
	{
		OutputDebugString( "BDrawTexturedQuad called with invalid hTexture value\n" );
		return false;
	}

	if ( !m_hQuadBuffer )
	{
		// Create the line buffer
		m_hQuadBuffer = HCreateVertexBuffer( sizeof(TexturedQuadVertex_t)* QUAD_BUFFER_TOTAL_SIZE * 4, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 );

		if ( !m_hQuadBuffer )
		{
			OutputDebugString( "Can't BDrawTexturedQuad() because vertex buffer creation failed\n" );
			return false;
		}
	}

	// Check if we are out of room and need to flush the buffer
	if ( m_dwQuadsToFlush == QUAD_BUFFER_BATCH_SIZE )
	{
		BFlushQuadBuffer();
	}

	// Check if the texture changed so we need to flush the buffer
	if ( m_hLastTexture != hTexture )
	{
		BFlushQuadBuffer();
	}

	// Save the texture to use for next flush
	m_hLastTexture = hTexture;

	if ( !BSetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 ) )
	{
		OutputDebugString( "Setting FVF failed for textured rect drawing\n" );
		return false;
	}

	// Lock the vertex buffer into memory
	if ( !m_pQuadVertexes )
	{
		if ( !BLockEntireVertexBuffer( m_hQuadBuffer, (void**)&m_pQuadVertexes, m_dwQuadBufferBatchPos ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD ) )
		{
			m_pQuadVertexes = NULL;
			OutputDebugString( "BDrawTexturedQuad failed because locking vertex buffer failed\n" );
			return false;
		}
	}

	TexturedQuadVertex_t *pVertData = &m_pQuadVertexes[m_dwQuadBufferBatchPos * 4 + m_dwQuadsToFlush * 4];

	pVertData[0].color = dwColor;
	pVertData[0].rhw = 1.0f;
	pVertData[0].z = 1.0f;
	pVertData[0].x = xPos0;
	pVertData[0].y = yPos0;
	pVertData[0].u = u0;
	pVertData[0].v = v0;

	pVertData[1].color = dwColor;
	pVertData[1].rhw = 1.0f;
	pVertData[1].z = 1.0f;
	pVertData[1].x = xPos1;
	pVertData[1].y = yPos0;
	pVertData[1].u = u1;
	pVertData[1].v = v0;

	pVertData[2].color = dwColor;
	pVertData[2].rhw = 1.0f;
	pVertData[2].z = 1.0f;
	pVertData[2].x = xPos0;
	pVertData[2].y = yPos1;
	pVertData[2].u = u0;
	pVertData[2].v = v1;

	pVertData[3].color = dwColor;
	pVertData[3].rhw = 1.0f;
	pVertData[3].z = 1.0f;
	pVertData[3].x = xPos1;
	pVertData[3].y = yPos1;
	pVertData[3].u = u1;
	pVertData[3].v = v1;

	++m_dwQuadsToFlush;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Draw a textured quad
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BDrawTexturedQuad( float xPos0, float yPos0, float xPos1, float yPos1, float xPos2, float yPos2 , float xPos3, float yPos3,
	float u0, float v0, float u1, float v1, DWORD dwColor, HGAMETEXTURE hTexture )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return true; // Fail silently in this case

	// Find the texture
	std::map<HGAMETEXTURE, TextureData_t>::iterator iter;
	iter = m_MapTextures.find( hTexture );
	if ( iter == m_MapTextures.end() )
	{
		OutputDebugString( "BDrawTexturedQuad called with invalid hTexture value\n" );
		return false;
	}

	if ( !m_hQuadBuffer )
	{
		// Create the line buffer
		m_hQuadBuffer = HCreateVertexBuffer( sizeof(TexturedQuadVertex_t)* QUAD_BUFFER_TOTAL_SIZE * 4, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 );

		if ( !m_hQuadBuffer )
		{
			OutputDebugString( "Can't BDrawTexturedQuad() because vertex buffer creation failed\n" );
			return false;
		}
	}

	// Check if we are out of room and need to flush the buffer
	if ( m_dwQuadsToFlush == QUAD_BUFFER_BATCH_SIZE )
	{
		BFlushQuadBuffer();
	}

	// Check if the texture changed so we need to flush the buffer
	if ( m_hLastTexture != hTexture )
	{
		BFlushQuadBuffer();
	}

	// Save the texture to use for next flush
	m_hLastTexture = hTexture;

	if ( !BSetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 ) )
	{
		OutputDebugString( "Setting FVF failed for textured rect drawing\n" );
		return false;
	}

	// Lock the vertex buffer into memory
	if ( !m_pQuadVertexes )
	{
		if ( !BLockEntireVertexBuffer( m_hQuadBuffer, (void**)&m_pQuadVertexes, m_dwQuadBufferBatchPos ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD ) )
		{
			m_pQuadVertexes = NULL;
			OutputDebugString( "BDrawTexturedQuad failed because locking vertex buffer failed\n" );
			return false;
		}
	}

	TexturedQuadVertex_t *pVertData = &m_pQuadVertexes[m_dwQuadBufferBatchPos * 4 + m_dwQuadsToFlush * 4];

	pVertData[0].color = dwColor;
	pVertData[0].rhw = 1.0f;
	pVertData[0].z = 1.0f;
	pVertData[0].x = xPos0;
	pVertData[0].y = yPos0;
	pVertData[0].u = u0;
	pVertData[0].v = v0;

	pVertData[1].color = dwColor;
	pVertData[1].rhw = 1.0f;
	pVertData[1].z = 1.0f;
	pVertData[1].x = xPos1;
	pVertData[1].y = yPos1;
	pVertData[1].u = u1;
	pVertData[1].v = v0;

	pVertData[2].color = dwColor;
	pVertData[2].rhw = 1.0f;
	pVertData[2].z = 1.0f;
	pVertData[2].x = xPos2;
	pVertData[2].y = yPos2;
	pVertData[2].u = u0;
	pVertData[2].v = v1;

	pVertData[3].color = dwColor;
	pVertData[3].rhw = 1.0f;
	pVertData[3].z = 1.0f;
	pVertData[3].x = xPos3;
	pVertData[3].y = yPos3;
	pVertData[3].u = u1;
	pVertData[3].v = v1;

	++m_dwQuadsToFlush;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Draw a 3D textured quad
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BDraw3DTexturedQuad( Textured3DQuadVertex_t vert[4], HGAMETEXTURE hTexture )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return true; // Fail silently in this case

	// Find the texture
	std::map<HGAMETEXTURE, TextureData_t>::iterator iter;
	iter = m_MapTextures.find( hTexture );
	if ( iter == m_MapTextures.end() )
	{
		OutputDebugString( "BDrawTexturedQuad called with invalid hTexture value\n" );
		return false;
	}

	if ( !m_h3DQuadBuffer )
	{
		// Create the line buffer
		m_h3DQuadBuffer = HCreateVertexBuffer( sizeof(Textured3DQuadVertex_t)* QUAD_BUFFER_TOTAL_SIZE * 4, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 );

		if ( !m_h3DQuadBuffer )
		{
			OutputDebugString( "Can't BDraw3DTexturedQuad() because vertex buffer creation failed\n" );
			return false;
		}
	}

	// Check if we are out of room and need to flush the buffer
	if ( m_dw3DQuadsToFlush == QUAD_BUFFER_BATCH_SIZE )
	{
		BFlush3DQuadBuffer();
	}

	// Check if the texture changed so we need to flush the buffer
	if ( m_h3DLastTexture != hTexture )
	{
		BFlush3DQuadBuffer( );
	}

	// Save the texture to use for next flush
	m_h3DLastTexture = hTexture;

	if ( !BSetFVF( D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 ) )
	{
		OutputDebugString( "Setting FVF failed for textured rect drawing\n" );
		return false;
	}

	// Lock the vertex buffer into memory
	if ( !m_p3DQuadVertexes )
	{
		if ( !BLockEntireVertexBuffer( m_h3DQuadBuffer, (void**)&m_p3DQuadVertexes, m_dw3DQuadBufferBatchPos ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD ) )
		{
			m_p3DQuadVertexes = NULL;
			OutputDebugString( "BDrawTexturedQuad failed because locking vertex buffer failed\n" );
			return false;
		}
	}

	Textured3DQuadVertex_t *pVertData = &m_p3DQuadVertexes[m_dw3DQuadBufferBatchPos * 4 + m_dw3DQuadsToFlush * 4];

	for ( int i = 0; i < 4; i++ )
	{
		pVertData[i] = vert[i];
	}

	++m_dw3DQuadsToFlush;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Flush buffered quads
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BFlushQuadBuffer()
{
	// If the vert buffer isn't already locked into memory, then there is nothing to flush
	if ( m_pQuadVertexes == NULL )
		return true; // consider this successful since there was no error

	// OK, it is locked, so unlock now
	if ( !BUnlockVertexBuffer( m_hQuadBuffer ) )
	{
		OutputDebugString( "Failed flushing quad buffer because BUnlockVertexBuffer failed\n" );
		return false;
	}

	// Clear the memory pointer as its invalid now that we unlocked
	m_pQuadVertexes = NULL;

	// If there is nothing to actual flush, we are done
	if ( m_dwQuadsToFlush == 0 )
		return true;

	// Set FVF (will short circuit if this is already the set FVF)
	if ( !BSetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 ) )
	{
		OutputDebugString( "Failed flushing quad buffer because BSetFVF failed\n" );
		return false;
	}

	// Find the texture
	if ( !BSetTexture( m_hLastTexture ) )
		return false;

	if ( !BSetStreamSource( m_hQuadBuffer, 0, sizeof(TexturedQuadVertex_t) ) )
	{
		OutputDebugString( "Failed flushing quad buffer because BSetStreamSource failed\n" );
		m_pD3D9Device->SetTexture( 0, NULL ); // need to clear the texture before other drawing ops
		return false;
	}

	// Actual render calls
	for ( DWORD i=0; i < m_dwQuadsToFlush*4; i += 4 )
	{
		if ( !BRenderPrimitive( D3DPT_TRIANGLESTRIP, (m_dwQuadBufferBatchPos*4)+i, 2 ) )
		{
			OutputDebugString( "Failed flushing line buffer because BRenderPrimitive failed\n" );
			m_pD3D9Device->SetTexture( 0, NULL ); // need to clear the texture before other drawing ops
			return false;
		}
	}

	m_pD3D9Device->SetTexture( 0, NULL ); // need to clear the texture before other drawing ops

	m_dwQuadsToFlush = 0;
	m_dwQuadBufferBatchPos += QUAD_BUFFER_BATCH_SIZE;
	if ( m_dwQuadBufferBatchPos >= QUAD_BUFFER_TOTAL_SIZE )
	{
		m_dwQuadBufferBatchPos = 0;
	}

	return true;


}

//-----------------------------------------------------------------------------
// Purpose: Flush buffered 3D quads
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BFlush3DQuadBuffer()
{
	// If the vert buffer isn't already locked into memory, then there is nothing to flush
	if ( m_p3DQuadVertexes == NULL )
		return true; // consider this successful since there was no error

	// OK, it is locked, so unlock now
	if ( !BUnlockVertexBuffer( m_h3DQuadBuffer ) )
	{
		OutputDebugString( "Failed flushing quad buffer because BUnlockVertexBuffer failed\n" );
		return false;
	}

	// Clear the memory pointer as its invalid now that we unlocked
	m_p3DQuadVertexes = NULL;

	// If there is nothing to actual flush, we are done
	if ( m_dw3DQuadsToFlush == 0 )
		return true;

	// Set FVF (will short circuit if this is already the set FVF)
	if ( !BSetFVF( D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 ) )
	{
		OutputDebugString( "Failed flushing quad buffer because BSetFVF failed\n" );
		return false;
	}

	// Find the texture
	if ( !BSetTexture( m_h3DLastTexture ) )
		return false;

	if ( !BSetStreamSource( m_h3DQuadBuffer, 0, sizeof(Textured3DQuadVertex_t) ) )
	{
		OutputDebugString( "Failed flushing 3D quad buffer because BSetStreamSource failed\n" );
		m_pD3D9Device->SetTexture( 0, NULL ); // need to clear the texture before other drawing ops
		return false;
	}

	// Actual render calls
	for ( DWORD i = 0; i < m_dw3DQuadsToFlush * 4; i += 4 )
	{
		if ( !BRenderPrimitive( D3DPT_TRIANGLESTRIP, (m_dw3DQuadBufferBatchPos * 4) + i, 2 ) )
		{
			OutputDebugString( "Failed flushing line buffer because BRenderPrimitive failed\n" );
			m_pD3D9Device->SetTexture( 0, NULL ); // need to clear the texture before other drawing ops
			return false;
		}
	}

	m_pD3D9Device->SetTexture( 0, NULL ); // need to clear the texture before other drawing ops

	m_dw3DQuadsToFlush = 0;
	m_dw3DQuadBufferBatchPos += QUAD_BUFFER_BATCH_SIZE;
	if ( m_dw3DQuadBufferBatchPos >= QUAD_BUFFER_TOTAL_SIZE )
	{
		m_dw3DQuadBufferBatchPos = 0;
	}

	return true;


}

//-----------------------------------------------------------------------------
// Purpose: Set the current stream source (this always set stream 0, we don't support more than 1 stream presently)
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BSetStreamSource( HGAMEVERTBUF hVertBuf, uint32 uOffset, uint32 uStride )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return false;

	if ( !hVertBuf )
	{
		OutputDebugString( "Someone is calling BSetStreamSource() with a null handle\n" );
		return false;
	}

	// Find the vertex buffer for the passed handle
	std::map<HGAMEVERTBUF, VertBufData_t>::iterator iter;
	iter = m_MapVertexBuffers.find( hVertBuf );
	if ( iter == m_MapVertexBuffers.end() )
	{
		OutputDebugString( "Invalid vertex buffer handle passed to BSetStreamSource()\n" );
		return false;
	}

	// Make sure the pointer is valid
	if ( !iter->second.m_pBuffer )
	{
		OutputDebugString( "Pointer to vertex buffer is invalid (lost device and not recreated?)!\n" );
		return false;
	}

	if ( FAILED( m_pD3D9Device->SetStreamSource( 0, iter->second.m_pBuffer, uOffset, uStride ) ) )
	{
		OutputDebugString( "SetStreamSource() call failed\n" );
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Renders primitives using the current stream source 
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BRenderPrimitive( D3DPRIMITIVETYPE primType, uint32 uStartVertex, uint32 uCount )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return true; // Fail silently in this case

	if ( FAILED( m_pD3D9Device->DrawPrimitive( primType, uStartVertex, uCount ) ) )
	{
		OutputDebugString( "BRenderPrimtive() call failed\n" );
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Creates a new texture 
//-----------------------------------------------------------------------------
HGAMETEXTURE CGameEngineWin32::HCreateTexture( byte *pRGBAData, uint32 uWidth, uint32 uHeight, ETEXTUREFORMAT eTextureFormat )
{
	if ( !m_pD3D9Device )
		return 0;

	D3DFORMAT eD3DDeviceFormat = D3DFMT_A8R8G8B8;

	switch ( eTextureFormat )
	{
	case eTextureFormat_RGBA:
	case eTextureFormat_BGRA:
		eD3DDeviceFormat = D3DFMT_A8R8G8B8;
		break;
	case eTextureFormat_BGRA16:
		eD3DDeviceFormat = D3DFMT_A16B16G16R16;
	}

	TextureData_t TexData;
	TexData.m_uWidth = uWidth;
	TexData.m_uHeight = uHeight;
	if ( pRGBAData )
	{
		size_t dataSize = 0;
		if ( eD3DDeviceFormat == D3DFMT_A8R8G8B8 )
		{
			dataSize = uWidth*uHeight * 4;
		}
		else if ( eD3DDeviceFormat == D3DFMT_A16B16G16R16 )
		{
			dataSize = sizeof(uint16)* 4 * uWidth * uHeight;
		}
		TexData.m_pRGBAData = new byte[dataSize];
		memcpy( TexData.m_pRGBAData, pRGBAData, dataSize );
	}
	else
	{
		TexData.m_pRGBAData = NULL;
	}
	TexData.m_pTexture = NULL;
	TexData.m_pDepthSurface = NULL;
	TexData.m_eFormat = eD3DDeviceFormat;
	TexData.m_eTextureFormat = eTextureFormat;

	int nHandle = m_nNextTextureHandle;
	++m_nNextTextureHandle;
	m_MapTextures[nHandle] = TexData;

	return nHandle;
}


//-----------------------------------------------------------------------------
// Purpose: Creates a new font
//-----------------------------------------------------------------------------
HGAMEFONT CGameEngineWin32::HCreateFont( int nHeight, int nFontWeight, bool bItalic, const char * pchFont )
{
	if ( !m_pD3D9Device )
		return 0;

	// Create a D3DX font object
	LPD3DXFONT pFont;
	HRESULT hRes = D3DXCreateFont( m_pD3D9Device, nHeight, 0, nFontWeight, 0, bItalic, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, pchFont, &pFont );
	if ( FAILED( hRes ) )
	{
		OutputDebugString( "Failed creating font via D3DXCreateFont\n" );
		return 0;
	}

	HGAMEFONT hFont = m_nNextFontHandle;
	++m_nNextFontHandle;

	m_MapFontInstances[ hFont ] = pFont;
	return hFont;
}


//-----------------------------------------------------------------------------
// Purpose: Draws text to the screen inside the given rectangular region, using the given font
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BDrawString( HGAMEFONT hFont, RECT rect, DWORD dwColor, DWORD dwFormat, const char *pchText )
{
	if ( !m_pD3D9Device )
		return false;

	if ( m_bDeviceLost )
		return true; // Fail silently in this case

	if ( !hFont )
	{
		OutputDebugString( "Someone is calling BDrawString with a null font handle\n" );
		return false;
	}

	// Find the font object for the passed handle
	std::map<HGAMEFONT, LPD3DXFONT>::iterator iter;
	iter = m_MapFontInstances.find( hFont );
	if ( iter == m_MapFontInstances.end() )
	{
		OutputDebugString( "Invalid font handle passed to BDrawString()\n" );
		return false;
	}

	// we have the font, try to draw with it
	if( !iter->second->DrawText( NULL, pchText, -1, &rect, dwFormat, dwColor ) )
	{
		OutputDebugString( "DrawText call failed\n" );
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Message pump for OS messages
//-----------------------------------------------------------------------------
void CGameEngineWin32::MessagePump()
{
	MSG msg;
	BOOL bRet;
	while( PeekMessage( &msg, m_hWnd, 0, 0, PM_NOREMOVE ) )
	{
		bRet = GetMessage( &msg, m_hWnd, 0, 0 );
		if( bRet != 0 && bRet != -1 )
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Track keys which are down
//-----------------------------------------------------------------------------
void CGameEngineWin32::RecordKeyDown( DWORD dwVK )
{
	m_SetKeysDown.insert( dwVK );
}


//-----------------------------------------------------------------------------
// Purpose: Track keys which are up
//-----------------------------------------------------------------------------
void CGameEngineWin32::RecordKeyUp( DWORD dwVK )
{
	std::set<DWORD>::iterator iter;
	iter = m_SetKeysDown.find( dwVK );
	if ( iter != m_SetKeysDown.end() )
		m_SetKeysDown.erase( iter );
}


//-----------------------------------------------------------------------------
// Purpose: Find out if a key is currently down
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BIsKeyDown( DWORD dwVK )
{
	std::set<DWORD>::iterator iter;
	iter = m_SetKeysDown.find( dwVK );
	if ( iter != m_SetKeysDown.end() )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Get a down key value
//-----------------------------------------------------------------------------
bool CGameEngineWin32::BGetFirstKeyDown( DWORD *pdwVK )
{
	std::set<DWORD>::iterator iter;
	iter = m_SetKeysDown.begin();
	if ( iter != m_SetKeysDown.end() )
	{
		*pdwVK = *iter;
		m_SetKeysDown.erase( iter );
		return true;
	}
	else
	{
		return false;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Find the engine instance tied to a given hwnd
//-----------------------------------------------------------------------------
CGameEngineWin32 * CGameEngineWin32::FindEngineInstanceForHWND( HWND hWnd )
{
	std::map<HWND, CGameEngineWin32 *>::iterator iter;
	iter = m_MapEngineInstances.find( hWnd );
	if ( iter == m_MapEngineInstances.end() )
		return NULL;
	else
		return iter->second;
}


//-----------------------------------------------------------------------------
// Purpose: Add the engine instance tied to a given hwnd to our static map
//-----------------------------------------------------------------------------
void CGameEngineWin32::AddInstanceToHWNDMap( CGameEngineWin32* pInstance, HWND hWnd )
{
	m_MapEngineInstances[hWnd] = pInstance;
}


//-----------------------------------------------------------------------------
// Purpose: Removes the instance associated with a given HWND from the map
//-----------------------------------------------------------------------------
void CGameEngineWin32::RemoveInstanceFromHWNDMap( HWND hWnd )
{
	std::map<HWND, CGameEngineWin32 *>::iterator iter;
	iter = m_MapEngineInstances.find( hWnd );
	if ( iter != m_MapEngineInstances.end() )
		m_MapEngineInstances.erase( iter );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
HGAMEVOICECHANNEL CGameEngineWin32::HCreateVoiceChannel()
{
	if ( !m_pXAudio2 )
		return 0;

	m_unVoiceChannelCount++;
	CVoiceContext* pVoiceContext = new CVoiceContext;

	// The format we sample voice in.
	WAVEFORMATEX voicesampleformat =
	{
		WAVE_FORMAT_PCM,		// wFormatTag
		1,						// nChannels
		VOICE_OUTPUT_SAMPLE_RATE,// nSamplesPerSec
		VOICE_OUTPUT_SAMPLE_RATE*BYTES_PER_SAMPLE, // nAvgBytesPerSec
		2,						// nBlockAlign
		8*BYTES_PER_SAMPLE,		// wBitsPerSample
		sizeof(WAVEFORMATEX)	// cbSize
	};

	if( FAILED( m_pXAudio2->CreateSourceVoice( &pVoiceContext->m_pSourceVoice, &voicesampleformat , 0, 1.0f, pVoiceContext ) ) )
	{
		delete pVoiceContext;
		return 0; // failed
	}

	pVoiceContext->m_pSourceVoice->Start( 0, 0 );

	m_MapVoiceChannel[m_unVoiceChannelCount] = pVoiceContext;

	return m_unVoiceChannelCount;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGameEngineWin32::DestroyVoiceChannel( HGAMEVOICECHANNEL hChannel )
{
	std::map<HGAMEVOICECHANNEL, CVoiceContext* >::iterator iter;
	iter = m_MapVoiceChannel.find( hChannel );
	if ( iter != m_MapVoiceChannel.end() )
	{
		CVoiceContext* pVoiceContext = iter->second;
		XAUDIO2_VOICE_STATE state;

		for(;;)
		{
			pVoiceContext->m_pSourceVoice->GetState( &state );
			if( !state.BuffersQueued )
				break;

			WaitForSingleObject( pVoiceContext->m_hBufferEndEvent, INFINITE );
		}

		pVoiceContext->m_pSourceVoice->Stop( 0 );
		pVoiceContext->m_pSourceVoice->DestroyVoice();

		delete pVoiceContext;

		m_MapVoiceChannel.erase( iter );
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CGameEngineWin32::AddVoiceData( HGAMEVOICECHANNEL hChannel, const uint8 *pVoiceData, uint32 uLength )
{
	std::map<HGAMEVOICECHANNEL, CVoiceContext* >::iterator iter;
	iter = m_MapVoiceChannel.find( hChannel );
	if ( iter == m_MapVoiceChannel.end() )
		return false; // channel not found
	
	CVoiceContext* pVoiceContext = iter->second;

	//
	// At this point we have a buffer full of audio and enough room to submit it, so
	// let's submit it and get another read request going.
	
	uint8 *pBuffer = (uint8 *) malloc( uLength );
	memcpy( pBuffer, pVoiceData, uLength );

	XAUDIO2_BUFFER buf = {0};
	buf.AudioBytes = uLength;
	buf.pAudioData = pBuffer;
	buf.pContext = pBuffer; // the buffer is freed again in CVoiceContext::OnBufferEnd

	if ( FAILED( pVoiceContext->m_pSourceVoice->SubmitSourceBuffer( &buf ) ) )
	{
		free ( pBuffer );
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: update an existing texture
//-----------------------------------------------------------------------------
bool CGameEngineWin32::UpdateTexture( HGAMETEXTURE hTexture, byte *pRGBAData, uint32 uWidth, uint32 uHeight, ETEXTUREFORMAT eTextureFormat )
{
	std::map<HGAMETEXTURE, TextureData_t>::iterator iter;
	iter = m_MapTextures.find( hTexture );
	if (iter == m_MapTextures.end())
	{
		OutputDebugString( "BFlushQuadBuffer failed with invalid m_hLastTexture value\n" );
		return false;
	}

	// Put the data into the texture
	D3DLOCKED_RECT rect;
	HRESULT hRes = iter->second.m_pTexture->LockRect( 0, &rect, NULL, 0 );
	if (FAILED( hRes ))
	{
		OutputDebugString( "LockRect call failed\n" );
		iter->second.m_pTexture->Release();
		iter->second.m_pTexture = NULL;
		return false;
	}

	if (iter->second.m_eFormat == D3DFMT_A8R8G8B8)
	{
		DWORD *pARGB = (DWORD *)rect.pBits;
		byte *pRGBA = (byte *)pRGBAData;

		byte r, g, b, a;
		for (uint32 y = 0; y < uHeight; ++y)
		{
			for (uint32 x = 0; x < uWidth; ++x)
			{
				// swap position of alpha value from back to front to be in correct format for d3d...
				r = *pRGBA++;
				g = *pRGBA++;
				b = *pRGBA++;
				a = *pRGBA++;

				if ( eTextureFormat == eTextureFormat_RGBA )
					*pARGB++ = D3DCOLOR_ARGB( a, r, g, b );
				else if ( eTextureFormat == eTextureFormat_BGRA)
					*pARGB++ = D3DCOLOR_ARGB( a, b, g, r );
			}
		}
	}
	else if (iter->second.m_eFormat == D3DFMT_A16B16G16R16)
	{
		uint16 *pDest = (uint16 *)rect.pBits;
		uint16 *pSrc = (uint16 *)pRGBAData;

		if ( eTextureFormat != eTextureFormat_BGRA16 )
			OutputDebugString( "Unsupported texture format for BGRA16 texture \n" );

		memcpy( pDest, pSrc, sizeof( uint16 )* uWidth * uHeight * 4 );
	}

	hRes = iter->second.m_pTexture->UnlockRect( 0 );
	if (FAILED( hRes ))
	{
		OutputDebugString( "UnlockRect call failed\n" );
		iter->second.m_pTexture->Release();
		iter->second.m_pTexture = NULL;
		return false;
	}

	return true;
}


bool CGameEngineWin32::BReadyTexture( HGAMETEXTURE hTexture )
{
	std::map<HGAMETEXTURE, TextureData_t>::iterator iter;
	iter = m_MapTextures.find( hTexture );
	if ( iter == m_MapTextures.end() )
	{
		OutputDebugString( "BFlushQuadBuffer failed with invalid m_hLastTexture value\n" );
		return false;
	}

	// See if we need to actually create the d3d texture
	if ( !iter->second.m_pTexture )
	{
		// render targets get a special flag
		DWORD dwUsage = 0;
		D3DPOOL pool = D3DPOOL_MANAGED;
		if ( !iter->second.m_pRGBAData )
		{
			dwUsage = D3DUSAGE_RENDERTARGET;
			pool = D3DPOOL_DEFAULT;
		}

		HRESULT hRes = m_pD3D9Device->CreateTexture(
			iter->second.m_uWidth,
			iter->second.m_uHeight,
			1, // mip map levels (0 = generate all levels down to 1x1 if supported)
			dwUsage, // have to set the right flag here if you want to autogen mipmaps... we don't
			iter->second.m_eFormat,
			pool,
			&iter->second.m_pTexture,
			NULL );
		if ( FAILED( hRes ) )
		{
			OutputDebugString( "BFlushQuadBuffer failed because CreateTexture() call failed\n" );
			iter->second.m_pTexture = NULL;
			return false;
		}

		// if the texture has source data, copy that in. Render targets don't
		// have source data.
		if ( iter->second.m_pRGBAData )
		{
			// Put the data into the texture
			D3DLOCKED_RECT rect;
			hRes = iter->second.m_pTexture->LockRect( 0, &rect, NULL, 0 );
			if ( FAILED( hRes ) )
			{
				OutputDebugString( "LockRect call failed\n" );
				iter->second.m_pTexture->Release();
				iter->second.m_pTexture = NULL;
				return false;
			}

			if ( iter->second.m_eFormat == D3DFMT_A8R8G8B8 )
			{
				DWORD *pARGB = (DWORD *)rect.pBits;
				byte *pRGBA = (byte *)iter->second.m_pRGBAData;

				byte r, g, b, a;
				for ( uint32 y = 0; y < iter->second.m_uHeight; ++y )
				{
					for ( uint32 x = 0; x < iter->second.m_uWidth; ++x )
					{
						// swap position of alpha value from back to front to be in correct format for d3d...
						r = *pRGBA++;
						g = *pRGBA++;
						b = *pRGBA++;
						a = *pRGBA++;

						if ( iter->second.m_eTextureFormat == eTextureFormat_RGBA )
							*pARGB++ = D3DCOLOR_ARGB( a, r, g, b );
						else
							*pARGB++ = D3DCOLOR_ARGB( a, b, g, r );
					}
				}
			}
			else if ( iter->second.m_eFormat == D3DFMT_A16B16G16R16 )
			{
				uint16 *pDest = (uint16 *)rect.pBits;
				uint16 *pSrc = (uint16 *)iter->second.m_pRGBAData;
		
				if ( iter->second.m_eTextureFormat != eTextureFormat_BGRA16 )
					OutputDebugString( "Unsupported texture format for BGRA16 texture \n" );

				memcpy( pDest, pSrc, sizeof(uint16)* iter->second.m_uWidth * iter->second.m_uHeight * 4 );
			}

			hRes = iter->second.m_pTexture->UnlockRect( 0 );
			if ( FAILED( hRes ) )
			{
				OutputDebugString( "UnlockRect call failed\n" );
				iter->second.m_pTexture->Release();
				iter->second.m_pTexture = NULL;
				return false;
			}
		}
		else
		{
			// for render targets we also need to create the depth buffer
			hRes = m_pD3D9Device->CreateDepthStencilSurface( iter->second.m_uWidth, iter->second.m_uHeight, D3DFMT_D32F_LOCKABLE, D3DMULTISAMPLE_NONE, 0, FALSE, &iter->second.m_pDepthSurface, NULL );
			if ( FAILED( hRes ) )
			{
				OutputDebugString( "BReadyTexture - CreateDepthStencilSurface failed\n" );
				iter->second.m_pTexture->Release();
				iter->second.m_pTexture = NULL;
				return false;
			}
		}
	}

	return true;
}

bool CGameEngineWin32::BSetTexture( HGAMETEXTURE hTexture )
{
	if ( !BReadyTexture( hTexture ) )
		return false;

	// Ok, texture should be created ok, do the drawing work
	if ( FAILED( m_pD3D9Device->SetTexture( 0, m_MapTextures[ hTexture ].m_pTexture ) ) )
	{
		OutputDebugString( "BFlushQuadBuffer failed setting texture\n" );
		return false;
	}

	return true;
}


// sets the texture as a render target. 
bool CGameEngineWin32::BSetRenderTarget( HGAMETEXTURE hTexture )
{
	if ( !BReadyTexture( hTexture ) )
	{
		OutputDebugString( "BSetRenderTarget couldn't ready the texture\n" );
		return false;
	}

	TextureData_t & tex = m_MapTextures[hTexture];
	LPDIRECT3DTEXTURE9 pTex = tex.m_pTexture;
	if ( !pTex )
	{
		OutputDebugString( "BSetRenderTarget - no texture\n" );
		return false;
	}

	IDirect3DSurface9 *pSurface;
	if ( FAILED( pTex->GetSurfaceLevel( 0, &pSurface ) ) )
	{
		OutputDebugString( "BSetRenderTarget - no surface\n" );
		return false;
	}

	if ( FAILED( m_pD3D9Device->SetRenderTarget( 0, pSurface ) ) )
	{
		OutputDebugString( "BSetRenderTarget failed\n" );
		return false;
	}

	if ( FAILED( m_pD3D9Device->SetDepthStencilSurface( tex.m_pDepthSurface ) ) )
	{
		OutputDebugString( "BSetRenderTarget Depth stencil surface\n" );
		return false;
	}

	return true;
}


// sets the render target back to the frame buffer
bool CGameEngineWin32::BUnsetRenderTarget()
{
	IDirect3DSurface9 *pSurface;
	if ( FAILED( m_pD3D9Device->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &pSurface ) ) )
	{
		OutputDebugString( "BClearRenderTarget - no surface\n" );
		return false;
	}

	if ( FAILED( m_pD3D9Device->SetRenderTarget( 0, pSurface ) ) )
	{
		OutputDebugString( "BClearRenderTarget - SetRenderTarget failed\n" );
		return false;
	}

	if ( FAILED( m_pD3D9Device->SetDepthStencilSurface( m_pBackbufferDepth ) ) )
	{
		OutputDebugString( "BClearRenderTarget Depth stencil surface\n" );
		return false;
	}
	return true;
}


// Quick and dirty wrapper for working with UV coordinates.
template<typename T>
struct TUV
{
	T x, y;
	TUV() {}
	TUV( T _x, T _y )
		: x( _x ), y( _y ) {}
	TUV( const float a[2] )
		: x( a[0] ), y( a[1] ) {}
	TUV( const TUV& other )
		: x( other.x ), y( other.y ) {}
	inline TUV operator*(T scale) const
	{
		return TUV( x * scale, y * scale );
	}
	inline TUV operator+(const TUV& other) const
	{
		return TUV( x + other.x, y + other.y );
	}
	inline TUV operator-(const TUV& other) const
	{
		return TUV( x - other.x, y - other.y );
	}
};

template<typename T>
inline TUV<T> operator*(T scale, const TUV<T> v)
{
	return TUV<T>( scale * v.x, scale * v.y );
}

typedef TUV<float> UVf;
typedef TUV<double> UVd;

// Loads the VR distortion shader off disk
bool CGameEngineWin32::BInitializeVRShader()
{
	if ( !m_pVRHmd )
	{
		OutputDebugString( "Don't call BInitializeVRShader unless the game is in VR mode\n" );
		return false;
	}

	char szCurDir[MAX_PATH];
	_getcwd( szCurDir, sizeof(szCurDir) );
	char szShaderPath[MAX_PATH];
	sprintf_safe( szShaderPath, "%s\\D3D9VRDistort.cso", szCurDir );

	FILE *pFile = fopen( szShaderPath, "rb" );
	if ( !pFile )
	{
		OutputDebugString( "Failed opening D3D9Overlay.cso for reading\n" );
		return false;
	}

	// Figure out the filesize
	fseek( pFile, 0, SEEK_END );
	size_t lSize = ftell( pFile );
	bool bSuccess = false;
	rewind( pFile );
	if ( lSize )
	{
		// Allocate a buffer to read the file into
		char *rgchBuffer = new char[lSize];
		if ( !rgchBuffer )
		{
			OutputDebugString( "Memory allocation failure\n" );
		}
		else
		{
			size_t result = fread( rgchBuffer, 1, lSize, pFile );
			if ( result != lSize )
			{
				OutputDebugString( "Failed reading from cso file\n" );
			}
			else
			{
				HRESULT hRes = m_pD3D9Device->CreatePixelShader( (DWORD*)rgchBuffer, &m_pVRDistortionPixelShader );
				if ( SUCCEEDED( hRes ) && m_pVRDistortionPixelShader )
				{
					OutputDebugString( "Created IDirect3DPixelShader9 from memory ok!\n" );
					bSuccess = true;
				}
				else
				{
					OutputDebugString( "Creating effect from memory failed!\n" );
				}
			}

			delete[] rgchBuffer;
		}
	}
	fclose( pFile );

	if ( !bSuccess )
		return false;

	int nDistSize = 128;
	int elem_span = 4;

	struct DistortionSample_t
	{
		uint16 redU;
		uint16 redV;
		uint16 blueU;
		uint16 blueV;
	};

	for ( int i = 0; i < 2; i++ )
	{
		// we only need to do this once
		if ( m_hVRDistortionMap[i] != 0 )
			continue;

		vr::Hmd_Eye eye = (vr::Hmd_Eye)i;

		uint16 *pData = (uint16 *)malloc( sizeof(uint16)* elem_span * nDistSize * nDistSize );

		for ( int yi = 0; yi < nDistSize; yi++ )
		{
			const float v = (yi + 0.5f) / (float)nDistSize;
			for ( int xi = 0; xi < nDistSize; xi++ )
			{
				const float u = (xi + 0.5f) / (float)nDistSize;

				vr::DistortionCoordinates_t coords = m_pVRHmd->ComputeDistortion( eye, u, v );
				UVf samp_red( coords.rfRed );
				UVf samp_green( coords.rfGreen );
				UVf samp_blue( coords.rfBlue );

				static const float rg_to_rb_ratio = 0.522f;
				UVf red_to_blue = samp_blue - samp_red;
				UVf tex_samp_red = samp_green - rg_to_rb_ratio * red_to_blue;
				UVf tex_samp_blue = samp_green + (1 - rg_to_rb_ratio) * red_to_blue;

				if ( tex_samp_red.x < 0.0 || tex_samp_blue.x < 0.0 )
				{
					tex_samp_red.x = tex_samp_blue.x = 0.0;
				}

				if ( tex_samp_red.x > 1.0 || tex_samp_blue.x > 1.0 )
				{
					tex_samp_red.x = tex_samp_blue.x = 1.0;
				}

				if ( tex_samp_red.y < 0.0 || tex_samp_blue.y < 0.0 )
				{
					tex_samp_red.y = tex_samp_blue.y = 0.0;
				}

				if ( tex_samp_red.y > 1.0 || tex_samp_blue.y > 1.0 )
				{
					tex_samp_red.y = tex_samp_blue.y = 1.0;
				}

				int idx = yi * nDistSize + xi;

				pData[idx * elem_span + 0] = (uint16)(tex_samp_red.x * 65535.f );
				pData[idx * elem_span + 1] = (uint16)(tex_samp_red.y * 65535.f );
				pData[idx * elem_span + 2] = (uint16)(tex_samp_blue.x * 65535.f );
				pData[idx * elem_span + 3] = (uint16)(tex_samp_blue.y * 65535.f );
			}
		}

		m_hVRDistortionMap[i] = HCreateTexture( (byte *)pData, nDistSize, nDistSize, eTextureFormat_BGRA16 );
		free( pData );
		if ( !m_hVRDistortionMap[i] )
		{
			OutputDebugString( "Unable to create distortion texture\n" );
			return false;
		}

	}
	return true;
}

