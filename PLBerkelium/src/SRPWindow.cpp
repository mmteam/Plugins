//[-------------------------------------------------------]
//[ Header                                                ]
//[-------------------------------------------------------]
#include "PLBerkelium/SRPWindow.h"


//[-------------------------------------------------------]
//[ Namespace                                             ]
//[-------------------------------------------------------]
using namespace PLCore;
using namespace PLRenderer;
using namespace PLScene;
using namespace PLMath;
using namespace PLGraphics;

namespace PLBerkelium {


//[-------------------------------------------------------]
//[ RTTI interface                                        ]
//[-------------------------------------------------------]
pl_implement_class(SRPWindow)


//[-------------------------------------------------------]
//[ Functions		                                      ]
//[-------------------------------------------------------]
SRPWindow::SRPWindow(const String &sName) :
	m_pBerkeliumWindow(nullptr),
	m_sWindowName(sName),
	m_pCurrentSceneRenderer(nullptr),
	m_pCurrentRenderer(nullptr),
	m_pVertexBuffer(nullptr),
	m_pVertexShader(nullptr),
	m_pFragmentShader(nullptr),
	m_pProgramWrapper(nullptr),
	m_pTextureBuffer(nullptr),
	m_pTextureBufferNew(nullptr),
	m_cImage(),
	m_psWindowsData(new sWindowsData),
	m_bInitialized(false),
	m_bReadyToDraw(false),
	m_pBerkeliumContext(nullptr),
	m_pToolTip(nullptr),
	m_bToolTipEnabled(false),
	m_pmapDefaultCallBacks(new HashMap<String, sCallBack*>),
	m_pmapCallBackFunctions(new HashMap<PLCore::String, PLCore::DynFuncPtr>),
	m_bIgnoreBufferUpdate(false),
	m_pmapWidgets(new HashMap<Berkelium::Widget*, sWidget*>)
{
	// we need to create a berkelium context
	// it might be wise to centralize the context back to the Gui class because each context is represented my a Berkelium.exe process on runtime
	// the only downside of this is when a window crashes it needs to refresh all windows within the context so they will lose their current state
	//todo: [10-07-2012 Icefire] this needs to be moved back to gui
	CreateBerkeliumContext();
}


SRPWindow::~SRPWindow()
{
	// we should clear the callbacks
	RemoveCallBacks();
	// we destroy the used berkelium window
	DestroyBerkeliumWindow();
	// destroy the context
	DestroyContext();
	// destroy the tool tip window
	DestroyToolTipWindow();
	// cleanup
	if (nullptr != m_pVertexBuffer)
	{
		delete m_pVertexBuffer;
	}
	if (nullptr != m_pProgramWrapper)
	{
		delete m_pProgramWrapper;
	}
	if (nullptr != m_pFragmentShader)
	{
		delete m_pFragmentShader;
	}
	if (nullptr != m_pVertexShader)
	{
		delete m_pVertexShader;
	}
	if (nullptr != m_pTextureBuffer)
	{
		delete m_pTextureBuffer;
	}
	if (nullptr != m_pTextureBufferNew)
	{
		delete m_pTextureBufferNew;
	}
}


void SRPWindow::DebugToConsole(const String &sString)
{
	//undone: [10-07-2012 Icefire] this should be deprecated when not needed anymore
	System::GetInstance()->GetConsole().Print("PLBerkelium::SRPWindow - " + sString);
}


void SRPWindow::Draw(Renderer &cRenderer, const SQCull &cCullQuery)
{
	if (m_bReadyToDraw)
	{
		// draw the window and widgets if we are ready
		DrawWindow();
		DrawWidgets();
	}
}


void SRPWindow::onPaint(Berkelium::Window *win, const unsigned char *sourceBuffer, const Berkelium::Rect &sourceBufferRect, size_t numCopyRects, const Berkelium::Rect *copyRects, int dx, int dy, const Berkelium::Rect &scrollRect)
{
	if (!m_bIgnoreBufferUpdate)
	{
		if (m_psWindowsData->bNeedsFullUpdate)
		{
			// awaiting a full update disregard all partials ones until the full one comes in
			BufferCopyFull(m_cImage.GetBuffer()->GetData(), m_psWindowsData->nFrameWidth, m_psWindowsData->nFrameHeight, sourceBuffer, sourceBufferRect);
			BufferUploadToGPU();
			m_psWindowsData->bNeedsFullUpdate = false;
		}
		else
		{
			if (sourceBufferRect.width() == m_psWindowsData->nFrameWidth && sourceBufferRect.height() == m_psWindowsData->nFrameHeight)
			{
				// did not suspect a full update but got it anyway, it might happen and is ok
				BufferCopyFull(m_cImage.GetBuffer()->GetData(), m_psWindowsData->nFrameWidth, m_psWindowsData->nFrameHeight, sourceBuffer, sourceBufferRect);
				BufferUploadToGPU();
			}
			else
			{
				if (dx != 0 || dy != 0)
				{
					// a scroll has taken place
					BufferCopyScroll(m_cImage.GetBuffer()->GetData(), m_psWindowsData->nFrameWidth, m_psWindowsData->nFrameHeight, sourceBuffer, sourceBufferRect, numCopyRects, copyRects, dx, dy, scrollRect);
					BufferUploadToGPU();
				}
				else
				{
					// normal partial updates
					BufferCopyRects(m_cImage.GetBuffer()->GetData(), m_psWindowsData->nFrameWidth, m_psWindowsData->nFrameHeight, sourceBuffer, sourceBufferRect, numCopyRects, copyRects);
					BufferUploadToGPU();
				}
			}
		}
	}
}


void SRPWindow::onCreatedWindow(Berkelium::Window *win, Berkelium::Window *newWindow, const Berkelium::Rect &initialRect)
{
	DebugToConsole("onCreatedWindow()\n");
	//undone: [10-07-2012 Icefire] should create certain windows on Gui (new context) or this one!
	newWindow->destroy(); // discard for now
}


VertexBuffer *SRPWindow::CreateVertexBuffer(const Vector2 &vPosition, const Vector2 &vImageSize)
{
	// lets create a vertex buffer
	VertexBuffer *pVertexBuffer = m_pCurrentRenderer->CreateVertexBuffer();
	if (pVertexBuffer)
	{
		// setup and allocate the vertex buffer
		pVertexBuffer->AddVertexAttribute(VertexBuffer::Position, 0, VertexBuffer::Float2);
		pVertexBuffer->AddVertexAttribute(VertexBuffer::TexCoord, 0, VertexBuffer::Float2);
		pVertexBuffer->Allocate(4, Usage::WriteOnly);

		// fill the vertex buffer
		UpdateVertexBuffer(pVertexBuffer, vPosition, vImageSize);

		// return the created vertex buffer
		return pVertexBuffer;
	}
	else
	{
		// something when wrong so we return nothing
		return nullptr;
	}
}


void SRPWindow::SetRenderer(Renderer *pRenderer)
{
	m_pCurrentRenderer = pRenderer;
}


ProgramWrapper *SRPWindow::CreateProgramWrapper()
{
	// is there already a program instance?
	if (nullptr == m_pProgramWrapper)
	{
		// declare vertex and fragment shader
		String sVertexShaderSourceCode;
		String sFragmentShaderSourceCode;

		// account for OpenGL version
		if (m_pCurrentRenderer->GetAPI() == "OpenGL ES 2.0")
		{
			#include "ARGBtoRGBA_GLSL.h"
			sVertexShaderSourceCode   = "#version 100\n" + sBerkeliumVertexShaderSourceCodeGLSL;
			sFragmentShaderSourceCode = "#version 100\n" + sBerkeliumFragmentShaderSourceCodeGLSL;
		}
		else
		{
			#include "ARGBtoRGBA_GLSL.h"
			sVertexShaderSourceCode   = "#version 110\n" + Shader::RemovePrecisionQualifiersFromGLSL(sBerkeliumVertexShaderSourceCodeGLSL);
			sFragmentShaderSourceCode = "#version 110\n" + Shader::RemovePrecisionQualifiersFromGLSL(sBerkeliumFragmentShaderSourceCodeGLSL);
		}

		// create the vertex and fragment shader
		m_pVertexShader = m_pCurrentRenderer->GetShaderLanguage(m_pCurrentRenderer->GetDefaultShaderLanguage())->CreateVertexShader(sVertexShaderSourceCode, "arbvp1");
		m_pFragmentShader = m_pCurrentRenderer->GetShaderLanguage(m_pCurrentRenderer->GetDefaultShaderLanguage())->CreateFragmentShader(sFragmentShaderSourceCode, "arbfp1");

		// create the program wrapper
		m_pProgramWrapper = static_cast<ProgramWrapper*>(m_pCurrentRenderer->GetShaderLanguage(m_pCurrentRenderer->GetDefaultShaderLanguage())->CreateProgram(m_pVertexShader, m_pFragmentShader));
		if (m_pProgramWrapper)
		{
			// return the created program wrapper
			return m_pProgramWrapper;
		}
		else
		{
			// return nothing because the program wrapper could not be created
			return nullptr;
		}
	}
	else
	{
		// return the already existing program instance
		return m_pProgramWrapper;
	}
}


bool SRPWindow::UpdateVertexBuffer(VertexBuffer *pVertexBuffer, const Vector2 &vPosition, const Vector2 &vImageSize)
{
	//hack: [10-07-2012 Icefire] i am not sure yet if this method is right for this use case

	if (!pVertexBuffer)
	{
		DebugToConsole("VertexBuffer not valid!\n");
		return false;
	}
	else
	{
		// fill the vertex buffer data
		if (pVertexBuffer->Lock(Lock::WriteOnly))
		{
			Vector2 vTextureCoordinate(Vector2::Zero);
			Vector2 vTextureCoordinateSize(Vector2::One);
			float fTextureCoordinateScaleX(1.0f);
			float fTextureCoordinateScaleY(1.0f);

			// Vertex 0
			float *pfVertex = static_cast<float*>(pVertexBuffer->GetData(0, VertexBuffer::Position));
			pfVertex[0] = vPosition.x;
			pfVertex[1] = vPosition.y + vImageSize.y;
			pfVertex	= static_cast<float*>(pVertexBuffer->GetData(0, VertexBuffer::TexCoord));
			pfVertex[0] = vTextureCoordinate.x*fTextureCoordinateScaleX;
			pfVertex[1] = (vTextureCoordinate.y + vTextureCoordinateSize.y)*fTextureCoordinateScaleY;

			// Vertex 1
			pfVertex	= static_cast<float*>(pVertexBuffer->GetData(1, VertexBuffer::Position));
			pfVertex[0] = vPosition.x + vImageSize.x;
			pfVertex[1] = vPosition.y + vImageSize.y;
			pfVertex	= static_cast<float*>(pVertexBuffer->GetData(1, VertexBuffer::TexCoord));
			pfVertex[0] = (vTextureCoordinate.x + vTextureCoordinateSize.x)*fTextureCoordinateScaleX;
			pfVertex[1] = (vTextureCoordinate.y + vTextureCoordinateSize.y)*fTextureCoordinateScaleY;

			// Vertex 2
			pfVertex	= static_cast<float*>(pVertexBuffer->GetData(2, VertexBuffer::Position));
			pfVertex[0] = vPosition.x;
			pfVertex[1] = vPosition.y;
			pfVertex	= static_cast<float*>(pVertexBuffer->GetData(2, VertexBuffer::TexCoord));
			pfVertex[0] = vTextureCoordinate.x*fTextureCoordinateScaleX;
			pfVertex[1] = vTextureCoordinate.y*fTextureCoordinateScaleY;

			// Vertex 3
			pfVertex	= static_cast<float*>(pVertexBuffer->GetData(3, VertexBuffer::Position));
			pfVertex[0] = vPosition.x + vImageSize.x;
			pfVertex[1] = vPosition.y;
			pfVertex	= static_cast<float*>(pVertexBuffer->GetData(3, VertexBuffer::TexCoord));
			pfVertex[0] = (vTextureCoordinate.x + vTextureCoordinateSize.x)*fTextureCoordinateScaleX;
			pfVertex[1] = vTextureCoordinate.y*fTextureCoordinateScaleY;

			// Unlock the vertex buffer
			pVertexBuffer->Unlock();
		}

		return true;
	}
}

bool SRPWindow::Initialize(Renderer *pRenderer, const Vector2 &vPosition, const Vector2 &vImageSize)
{
	// set the renderer
	SetRenderer(pRenderer);

	//fix: [10-07-2012 Icefire] verify the following to be valid and optimal

	// set the vertex buffer
	m_pVertexBuffer = CreateVertexBuffer(vPosition, vImageSize);

	// set the program wrapper
	m_pProgramWrapper = CreateProgramWrapper();

	if (m_pVertexBuffer && m_pProgramWrapper)
	{
		// create the image
		m_cImage = Image::CreateImage(DataByte, ColorRGBA, Vector3i(m_psWindowsData->nFrameWidth, m_psWindowsData->nFrameHeight, 1));
		// create the texture buffer
		m_pTextureBuffer = reinterpret_cast<TextureBuffer*>(pRenderer->CreateTextureBuffer2D(m_cImage, TextureBuffer::Unknown, 0));

		if (m_pTextureBuffer)
		{
			// create the image buffer
			if (nullptr != m_cImage.GetBuffer()->GetData())
			{
				// create a berkelium window
				CreateBerkeliumWindow();
				// set the default window settings
				SetWindowSettings();
				// set the default callback functions
				SetDefaultCallBackFunctions();
				m_bInitialized = true;
				return true;
			}
			// image buffer could not be created
			return false;
		}
		// texture buffer could not be created
		return false;
	}
	else
	{
		// vertex buffer or program wrapper could not be created
		return false;
	}
}


void SRPWindow::DestroyInstance() const
{
	// cleanup this instance
	delete this;
}


void SRPWindow::DrawWindow()
{
	if (m_bInitialized && m_psWindowsData->bIsVisable) // should suffice
	{
		// set the program
		m_pCurrentRenderer->SetProgram(m_pProgramWrapper);
		// set the render state to allow for transparency
		m_pCurrentRenderer->SetRenderState(RenderState::BlendEnable, true);

		//todo: [10-07-2012 Icefire] let (re)sizing be handled by the program uniform, see http://dev.pixellight.org/forum/viewtopic.php?f=6&t=503
		{
			const Rectangle &cViewportRect = m_pCurrentRenderer->GetViewport();
			float fX1 = cViewportRect.vMin.x;
			float fY1 = cViewportRect.vMin.y;
			float fX2 = cViewportRect.vMax.x;
			float fY2 = cViewportRect.vMax.y;

			Matrix4x4 m_mObjectSpaceToClipSpace;
			m_mObjectSpaceToClipSpace.OrthoOffCenter(fX1, fX2, fY1, fY2, -1.0f, 1.0f);

			// create program uniform
			ProgramUniform *pProgramUniform = m_pProgramWrapper->GetUniform("ObjectSpaceToClipSpaceMatrix");
			if (pProgramUniform)
				pProgramUniform->Set(m_mObjectSpaceToClipSpace);

			const int nTextureUnit = m_pProgramWrapper->Set("TextureMap", m_pTextureBuffer);
			if (nTextureUnit >= 0)
			{
				// set sampler states
				m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::AddressU, TextureAddressing::Clamp);
				m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::AddressV, TextureAddressing::Clamp);
				m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::MagFilter, TextureFiltering::None);
				m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::MinFilter, TextureFiltering::None);
				m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::MipFilter, TextureFiltering::None);
			}

			// set vertex attributes
			m_pProgramWrapper->Set("VertexPosition", m_pVertexBuffer, VertexBuffer::Position);
			m_pProgramWrapper->Set("VertexTexCoord", m_pVertexBuffer, VertexBuffer::TexCoord);
		}

		// draw primitives
		m_pCurrentRenderer->DrawPrimitives(Primitive::TriangleStrip, 0, 4);
	}
}


void SRPWindow::BufferCopyFull(uint8 *pImageBuffer, int &nWidth, int &nHeight, const unsigned char *sourceBuffer, const Berkelium::Rect &sourceBufferRect)
{
	if (sourceBufferRect.left() == 0 && sourceBufferRect.top() == 0 && sourceBufferRect.right() == nWidth && sourceBufferRect.bottom() == nHeight)
	{
		MemoryManager::Copy(pImageBuffer, sourceBuffer, sourceBufferRect.right() * sourceBufferRect.bottom() * 4);
	}
}


bool SRPWindow::AddSceneRenderPass(SceneRenderer *pSceneRenderer)
{
	// add scene render pass
	if (pSceneRenderer->Add(*reinterpret_cast<SceneRendererPass*>(this)))
	{
		m_pCurrentSceneRenderer = pSceneRenderer;
		return true;
	}
	else
	{
		return false;
	}
}


bool SRPWindow::RemoveSceneRenderPass()
{
	if (m_pCurrentSceneRenderer)
	{
		// remove scene render pass
		if (m_pCurrentSceneRenderer->Remove(*reinterpret_cast<SceneRendererPass*>(this)))
		{
			return true;
		}
		else
		{
			// unable remove scene render pass
			return false;
		}
	}
	// current scene renderer not set
	return false;
}


void SRPWindow::CreateBerkeliumWindow()
{
	// check if berkelium window is already created
	if (!m_pBerkeliumWindow)
	{
		// create berkelium window
		m_pBerkeliumWindow = Berkelium::Window::create(m_pBerkeliumContext);
	}
}


void SRPWindow::BufferCopyRects(PLCore::uint8 *pImageBuffer, int &nWidth, int &nHeight, const unsigned char *sourceBuffer, const Berkelium::Rect &sourceBufferRect, size_t numCopyRects, const Berkelium::Rect *copyRects)
{
	for (size_t i = 0; i < numCopyRects; i++)
	{
		int nCrWidth = copyRects[i].width();
		int nCrHeight = copyRects[i].height();
		int nCrTop = copyRects[i].top() - sourceBufferRect.top();
		int nCrLeft = copyRects[i].left() - sourceBufferRect.left();
		int nBrTop = copyRects[i].top();
		int nBrLeft = copyRects[i].left();

		for(int nCrHeightIndex = 0; nCrHeightIndex < nCrHeight; nCrHeightIndex++)
		{
			int nStartPosition = (nWidth - nBrLeft) * nBrTop + (nBrTop * nBrLeft) + nBrLeft + (nWidth * nCrHeightIndex);
			MemoryManager::Copy(
				&pImageBuffer[nStartPosition * 4],
				sourceBuffer + (nCrLeft + (nCrHeightIndex + nCrTop) * sourceBufferRect.width()) * 4,
				nCrWidth * 4
				);
		}
	}
}


void SRPWindow::BufferUploadToGPU()
{
	if (m_bInitialized)
	{
		// upload data to GPU
		if (m_pTextureBufferNew)
		{
			m_pTextureBufferNew->CopyDataFrom(0, TextureBuffer::R8G8B8A8, m_cImage.GetBuffer()->GetData());
			if (m_pTextureBuffer)
				delete m_pTextureBuffer;
			m_pTextureBuffer = m_pTextureBufferNew;
			m_pTextureBufferNew = nullptr;
			UpdateVertexBuffer(m_pVertexBuffer, Vector2(float(m_psWindowsData->nXPos), float(m_psWindowsData->nYPos)), Vector2(float(m_psWindowsData->nFrameWidth), float(m_psWindowsData->nFrameHeight)));
		}
		else
		{
			m_pTextureBuffer->CopyDataFrom(0, TextureBuffer::R8G8B8A8, m_cImage.GetBuffer()->GetData());
		}
		// set state for future usage
		if (!m_bReadyToDraw) m_bReadyToDraw = true;
	}
}


void SRPWindow::BufferCopyScroll(PLCore::uint8 *pImageBuffer, int &nWidth, int &nHeight, const unsigned char *sourceBuffer, const Berkelium::Rect &sourceBufferRect, size_t numCopyRects, const Berkelium::Rect *copyRects, int dx, int dy, const Berkelium::Rect &scrollRect)
{
	Berkelium::Rect scrolled_rect = scrollRect.translate(-dx, -dy);
	Berkelium::Rect scrolled_shared_rect = scrollRect.intersect(scrolled_rect);
	if (scrolled_shared_rect.width() > 0 && scrolled_shared_rect.height() > 0)
	{
		int wid = scrollRect.width();
		int hig = scrollRect.height();
		int top = scrollRect.top();
		int left = scrollRect.left();
		int tw = nWidth;

		if (dy < 0) // scroll down
		{
			for (int y = -dy; y < hig; y++)
			{
				unsigned int tb = ((top + y) * tw + left) * 4;
				unsigned int tb2 = tb + dy * tw * 4;
				MemoryManager::Copy(&pImageBuffer[tb2], &pImageBuffer[tb], wid * 4);
			}
		}
		else if (dy > 0) // scroll up
		{
			for (int y = hig - dy; y > -1; y--)
			{
				unsigned int tb = ((top + y) * tw + left) * 4;
				unsigned int tb2 = tb + dy * tw * 4;
				MemoryManager::Copy(&pImageBuffer[tb2], &pImageBuffer[tb], wid * 4);
			}
		}
		if(dx != 0) // scroll horizontal !! WIERD CRASH HERE !!
		{
			//fix: [10-07-2012 Icefire] buffer overflow within this scope
			// int subx = dx > 0 ? 0 : -dx;
			for (int y = 0; y < hig; y++)
			{
				unsigned int tb = ((top + y) * tw + left) * 4;
				unsigned int tb2 = tb - dx * 4;
				MemoryManager::Copy(&pImageBuffer[tb], &pImageBuffer[tb2], wid * 4 - 0);
			}
		}
	}
	// new data for scrolling
	for (size_t i = 0; i < numCopyRects; i++)
	{
		int nCrWidth = copyRects[i].width();
		int nCrHeight = copyRects[i].height();
		int nCrTop = copyRects[i].top() - sourceBufferRect.top();
		int nCrLeft = copyRects[i].left() - sourceBufferRect.left();
		int nBrTop = copyRects[i].top();
		int nBrLeft = copyRects[i].left();

		for(int nCrHeightIndex = 0; nCrHeightIndex < nCrHeight; nCrHeightIndex++)
		{
			int nStartPosition = (nWidth - nBrLeft) * nBrTop + (nBrTop * nBrLeft) + nBrLeft + (nWidth * nCrHeightIndex);
			MemoryManager::Copy(
				&pImageBuffer[nStartPosition * 4],
				sourceBuffer + (nCrLeft + (nCrHeightIndex + nCrTop) * sourceBufferRect.width()) * 4,
				nCrWidth * 4
				);
		}
	}
}


void SRPWindow::onLoad(Berkelium::Window *win)
{
	m_psWindowsData->bLoaded = true;
}


void SRPWindow::onLoadingStateChanged(Berkelium::Window *win, bool isLoading)
{
	if (isLoading)
	{
		m_psWindowsData->bLoaded = false;
	}
}


void SRPWindow::MoveToFront()
{
	if (m_bInitialized && m_pCurrentSceneRenderer)
	{
		// move scene render pass to front
		m_pCurrentSceneRenderer->MoveElement(m_pCurrentSceneRenderer->GetIndex(*reinterpret_cast<SceneRendererPass*>(this)), m_pCurrentSceneRenderer->GetNumOfElements() - 1);
	}
}


void SRPWindow::onCrashedWorker(Berkelium::Window *win)
{
	//undone: [10-07-2012 Icefire] not used or implemented
	DebugToConsole("onCrashedWorker()\n");
}


void SRPWindow::onCrashedPlugin(Berkelium::Window *win, Berkelium::WideString pluginName)
{
	//undone: [10-07-2012 Icefire] not used or implemented
	DebugToConsole("onCrashedPlugin()\n");
}


void SRPWindow::onConsoleMessage(Berkelium::Window *win, Berkelium::WideString message, Berkelium::WideString sourceId, int line_no)
{
	DebugToConsole("onConsoleMessage: " + String(line_no) + " - " + String(message.data()) + "\n");
}


void SRPWindow::onScriptAlert(Berkelium::Window *win, Berkelium::WideString message, Berkelium::WideString defaultValue, Berkelium::URLString url, int flags, bool &success, Berkelium::WideString &value)
{
	DebugToConsole("onScriptAlert()\n");
	DebugToConsole("message: " + String(message.data()) + "\n");
	DebugToConsole("defaultValue: " + String(defaultValue.data()) + "\n");
	DebugToConsole("url: " + String(url.data()) + "\n");
	DebugToConsole("flags: " + String(flags) + "\n");
	DebugToConsole("success: " + String(success) + "\n");
	DebugToConsole("value: " + String(value.data()) + "\n");
}


void SRPWindow::onCrashed(Berkelium::Window *win)
{
	// the window has crashed so we should recreate it
	RecreateWindow();
}


void SRPWindow::onUnresponsive(Berkelium::Window *win)
{
	//undone: [10-07-2012 Icefire] not used or implemented
	DebugToConsole("onUnresponsive()\n");
}


void SRPWindow::onResponsive(Berkelium::Window *win)
{
	//undone: [10-07-2012 Icefire] not used or implemented
	DebugToConsole("onResponsive()\n");
}


void SRPWindow::onAddressBarChanged(Berkelium::Window *win, Berkelium::URLString newURL)
{
	m_sLastKnownUrl = newURL.data();
}


void SRPWindow::RecreateWindow()
{
	m_bInitialized = false;
	// destroy the berkelium window
	DestroyBerkeliumWindow();
	// destroy the context
	DestroyContext();
	// create a new context
	CreateBerkeliumContext();
	// create a new berkelium window
	CreateBerkeliumWindow();
	// set the window settings
	SetWindowSettings();
	m_bInitialized = true;
}


void SRPWindow::CreateBerkeliumContext()
{
	//todo: [10-07-2012 Icefire] should move back to gui
	m_pBerkeliumContext = Berkelium::Context::create();
}


void SRPWindow::DestroyContext()
{
	//todo: [10-07-2012 Icefire] should move back to gui
	m_pBerkeliumContext->destroy();
	m_pBerkeliumContext = nullptr;
}


Berkelium::Window *SRPWindow::GetBerkeliumWindow() const
{
	return m_pBerkeliumWindow;
}


sWindowsData *SRPWindow::GetData() const
{
	return m_psWindowsData;
}


String SRPWindow::GetName() const
{
	return m_sWindowName;
}


void SRPWindow::DestroyBerkeliumWindow()
{
	if (m_pBerkeliumWindow)
	{
		// stop window navigation
		m_pBerkeliumWindow->stop();
		// unfocus window
		m_pBerkeliumWindow->unfocus();
		// destroy window
		m_pBerkeliumWindow->destroy();
		// set nullptr
		m_pBerkeliumWindow = nullptr;
	}
}


void SRPWindow::SetWindowSettings()
{
	m_pBerkeliumWindow->resize(m_psWindowsData->nFrameWidth, m_psWindowsData->nFrameHeight);
	m_pBerkeliumWindow->setTransparent(m_psWindowsData->bTransparent);
	m_pBerkeliumWindow->setDelegate(this);
	m_pBerkeliumWindow->navigateTo(m_psWindowsData->sUrl.GetASCII(), m_psWindowsData->sUrl.GetLength());
}


Vector2i SRPWindow::GetPosition() const
{
	if (m_psWindowsData->nXPos && m_psWindowsData->nYPos)
	{
		// return the position of the window
		return Vector2i(m_psWindowsData->nXPos, m_psWindowsData->nYPos);
	}
	else
	{
		return Vector2i::NegativeOne;
	}
}


Vector2i SRPWindow::GetSize() const
{
	if (m_psWindowsData->nFrameWidth && m_psWindowsData->nFrameHeight)
	{
		// return the size of the window
		return Vector2i(m_psWindowsData->nFrameWidth, m_psWindowsData->nFrameHeight);
	}
	else
	{
		return Vector2i::NegativeOne;
	}
}


Vector2i SRPWindow::GetRelativeMousePosition(const Vector2i &vMousePos) const
{
	return Vector2i(vMousePos.x - m_psWindowsData->nXPos, vMousePos.y - m_psWindowsData->nYPos);
}


int SRPWindow::GetSceneRenderPassIndex()
{
	if (m_pCurrentSceneRenderer)
	{
		// return scene render pass index
		return m_pCurrentSceneRenderer->GetIndex(*reinterpret_cast<SceneRendererPass*>(this));
	}
	else
	{
		return -1;
	}
}


void SRPWindow::onJavascriptCallback(Berkelium::Window *win, void *replyMsg, Berkelium::URLString origin, Berkelium::WideString funcName, Berkelium::Script::Variant *args, size_t numArgs)
{
	//	Javascript has called a bound function on this Window.
	//	Parameters:
	//		win			Window instance that fired this event.
	// 		pReplyMsg	If non-NULL, opaque reply identifier to be passed to synchronousScriptReturn.
	// 		url			Origin of the sending script.
	// 		funcName	name of function to call.
	// 		args		list of variants passed into function.
	// 		numArgs		number of arguments.

	// create new callback
	sCallBack *pCallBack = new sCallBack;

	// set the callback attributes
	pCallBack->pWindow = win;
	pCallBack->sFunctionName = funcName.data();
	pCallBack->nNumberOfParameters = numArgs;
	pCallBack->OriginUrl = origin;
	pCallBack->pReplyMsg = replyMsg;
	pCallBack->pParameters = args;

	String sParams = "";

	// loop trough the arguments
	for (size_t i = 0; i < numArgs; i++)
	{
		Berkelium::WideString jsonStr = toJSON(args[i]);

		//todo: [10-07-2012 Icefire] add additional argument types and verify them working
		if (args[i].type() == Berkelium::Script::Variant::JSSTRING)
		{
			// string
			//question: [10-07-2012 Icefire] what if more than one parameter and one of them has a whitespace, needs testing
			sParams = sParams + "Param" + String(i) + "=" + String(args[i].toString().data()) + " ";
		}
		else if (args[i].type() == Berkelium::Script::Variant::JSBOOLEAN)
		{
			// boolean
			sParams = sParams + "Param" + String(i) + "=" + String(jsonStr.data()) + " ";
		}
		else if (args[i].type() == Berkelium::Script::Variant::JSDOUBLE)
		{
			// double
			sParams = sParams + "Param" + String(i) + "=" + String(jsonStr.data()) + " ";
		}
		else
		{
			// else or undefined
			sParams = sParams + "Param" + String(i) + "=" + String(jsonStr.data()) + " ";
		}

		Berkelium::Script::toJSON_free(jsonStr);
	}

	if (numArgs > 0)
	{
		DebugToConsole("!Parameters from Javascript resulted in the following being send to the callback function!\n");
		DebugToConsole("\t>> " + sParams + " <<\n\n");
	}

	// create dynamic function pointer
	DynFuncPtr pDynFuncPtr = m_pmapCallBackFunctions->Get(pCallBack->sFunctionName);
	if (pDynFuncPtr)
	{
		// check if there is a return type
		if (pDynFuncPtr->GetReturnTypeID() == TypeNull || pDynFuncPtr->GetReturnTypeID() == TypeInvalid)
		{
			// call the function without a return
			pDynFuncPtr->Call(sParams);
		}
		else
		{
			// call the function with a return
			String sResult = pDynFuncPtr->CallWithReturn(sParams);

			// check if javascript is waiting for a response
			if (replyMsg)
			{
				// send result back to javascript
				win->synchronousScriptReturn(replyMsg, Berkelium::Script::Variant(sResult.GetUnicode()));
			}
		}
	}

	// check if the callback is part of the default ones
	if (pCallBack->sFunctionName == DRAGWINDOW ||
		pCallBack->sFunctionName == HIDEWINDOW ||
		pCallBack->sFunctionName == CLOSEWINDOW ||
		pCallBack->sFunctionName == RESIZEWINDOW)
	{
		// add the callback to the hashmap so that the Gui class can process it
		m_pmapDefaultCallBacks->Add(pCallBack->sFunctionName, pCallBack);
	}
	else
	{
		// we do not need the callback struct and its data anymore

		// i am not sure if you need to cleanup the children of a struct when you delete a struct
		delete pCallBack->pParameters;
		delete pCallBack->pReplyMsg;
		delete pCallBack;
	}
}


void SRPWindow::MoveWindow(const int &nX, const int &nY)
{
	//question: [10-07-2012 Icefire] i am not sure yet if this method is right for this use case

	m_psWindowsData->nXPos = nX;
	m_psWindowsData->nYPos = nY;
	UpdateVertexBuffer(m_pVertexBuffer, Vector2(float(nX), float(nY)), Vector2(float(m_psWindowsData->nFrameWidth), float(m_psWindowsData->nFrameHeight)));
}


void SRPWindow::onTooltipChanged(Berkelium::Window *win, Berkelium::WideString text)
{
	//undone: [10-07-2012 Icefire] deprecate

	if (m_bToolTipEnabled)
	{
		if (!m_pToolTip)
		{
			// tool tip needs to be created
			SetupToolTipWindow();
		}
		// set the tooltip
		SetToolTip(String(text.data()));
	}
}


void SRPWindow::SetupToolTipWindow()
{
	//undone: [10-07-2012 Icefire] deprecate

	// create tool tip window and set data
	m_pToolTip = new SRPWindow("ToolTip");
	m_pToolTip->GetData()->bIsVisable = false;
	m_pToolTip->GetData()->sUrl = "file:///D:/plice/PLMain/Code/GameClient/bin/tooltip.html";
	m_pToolTip->GetData()->nFrameWidth = 512;
	m_pToolTip->GetData()->nFrameHeight = 64;
	m_pToolTip->GetData()->nXPos = 0;
	m_pToolTip->GetData()->nYPos = 0;
	m_pToolTip->GetData()->bTransparent = true;
	m_pToolTip->GetData()->bKeyboardEnabled = false;
	m_pToolTip->GetData()->bMouseEnabled = false;
	m_pToolTip->GetData()->bNeedsFullUpdate = true;
	m_pToolTip->GetData()->bLoaded = false;

	// initialize the tool tip
	if (m_pToolTip->Initialize(m_pCurrentRenderer, Vector2::Zero, Vector2(float(512), float(64))))
	{
		// add the scene render pass for the tool tip
		m_pToolTip->AddSceneRenderPass(m_pCurrentSceneRenderer);
	}
	else
	{
		// destroy the left overs because the tooltip could not be initialized
		DestroyToolTipWindow();
	}
}


void SRPWindow::DestroyToolTipWindow()
{
	//undone: [10-07-2012 Icefire] deprecate

	if (m_pToolTip)
	{
		m_pToolTip->DestroyInstance();
		m_pToolTip = nullptr;
	}
}


void SRPWindow::SetToolTip(const String &sText)
{
	//undone: [10-07-2012 Icefire] deprecate

	if (m_pToolTip)
	{
		if (sText == "")
		{
			m_pToolTip->GetBerkeliumWindow()->executeJavascript(Berkelium::WideString::point_to(String("fadeEffect(1, 0, 5);").GetUnicode()));
			m_pToolTip->GetData()->bIsVisable = false;
		}
		else
		{
			Frontend &cFrontend = static_cast<FrontendApplication*>(CoreApplication::GetApplication())->GetFrontend();
			m_pToolTip->MoveWindow(cFrontend.GetMousePositionX() + 10, cFrontend.GetMousePositionY() + 6);
			m_pToolTip->GetBerkeliumWindow()->executeJavascript(Berkelium::WideString::point_to(String("SetToolTip('" + sText + "')").GetUnicode()));
			m_pToolTip->MoveToFront();
			m_pToolTip->GetData()->bIsVisable = true;
			m_pToolTip->GetBerkeliumWindow()->executeJavascript(Berkelium::WideString::point_to(String("fadeEffect(0, 1, 15);").GetUnicode()));
		}
	}
}


void SRPWindow::SetToolTipEnabled(const bool &bEnabled)
{
	//undone: [10-07-2012 Icefire] deprecate

	m_bToolTipEnabled = bEnabled;

	if (!bEnabled)
	{
		// clear tool tip because we disabled it
		SetToolTip("");
	}
}


void SRPWindow::RemoveCallBacks() const
{
	// get the iterator for all the callbacks created
	Iterator<sCallBack*> cIterator = m_pmapDefaultCallBacks->GetIterator();
	// loop trough the callbacks
	while (cIterator.HasNext())
	{
		sCallBack *psCallBack = cIterator.Next();
		// cleanup
		// i am not sure if you need to cleanup the children of a struct and the struct itself when you remove the struct from a hashmap
		// delete psCallBack->pParameters; // [12-07-2012 Icefire] this generated an error!
		delete psCallBack->pReplyMsg;
		delete psCallBack;
	}
	m_pmapDefaultCallBacks->Clear();
}


uint32 SRPWindow::GetNumberOfCallBacks() const
{
	return m_pmapDefaultCallBacks->GetNumOfElements();
}


sCallBack *SRPWindow::GetCallBack(const String &sKey) const
{
	return m_pmapDefaultCallBacks->Get(sKey);
}


void SRPWindow::ResizeWindow(const int &nWidth, const int &nHeight)
{
	//fix: [10-07-2012 Icefire] let (re)sizing be handled by the program uniform, see http://dev.pixellight.org/forum/viewtopic.php?f=6&t=503
	// buffer overflows on resize happen to often to accept the current method as is

	m_bIgnoreBufferUpdate = true;

	m_psWindowsData->bNeedsFullUpdate = true;

	m_bReadyToDraw = false;

	m_psWindowsData->nFrameWidth = nWidth;
	m_psWindowsData->nFrameHeight = nHeight;

	m_cImage = Image::CreateImage(DataByte, ColorRGBA, Vector3i(m_psWindowsData->nFrameWidth, m_psWindowsData->nFrameHeight, 1));
	if (nullptr != m_pTextureBufferNew)
	{
		delete m_pTextureBufferNew;
	}
	m_pTextureBufferNew = reinterpret_cast<TextureBuffer*>(m_pCurrentRenderer->CreateTextureBuffer2D(m_cImage, TextureBuffer::Unknown, 0));

	UpdateVertexBuffer(m_pVertexBuffer, Vector2(float(m_psWindowsData->nXPos), float(m_psWindowsData->nYPos)), Vector2(float(m_psWindowsData->nFrameWidth), float(m_psWindowsData->nFrameHeight)));

	GetBerkeliumWindow()->resize(m_psWindowsData->nFrameWidth, m_psWindowsData->nFrameHeight);

	m_bReadyToDraw = true;

	m_bIgnoreBufferUpdate = false;
}


bool SRPWindow::AddCallBackFunction(const DynFuncPtr pDynFunc, String sJSFunctionName, bool bHasReturn)
{
	if (pDynFunc)
	{
		// create function descriptor
		const FuncDesc *pFuncDesc = pDynFunc->GetDesc();
		if (pFuncDesc)
		{
			if (m_pmapCallBackFunctions->Get(pFuncDesc->GetName()) == NULL)
			{
				if (sJSFunctionName == "")
				{
					// the function name is not defined so we use the method name
					sJSFunctionName = pFuncDesc->GetName();
				}
				// we bind the javascript function
				GetBerkeliumWindow()->addBindOnStartLoading(Berkelium::WideString::point_to(sJSFunctionName.GetUnicode()), Berkelium::Script::Variant::bindFunction(Berkelium::WideString::point_to(pFuncDesc->GetName().GetUnicode()), bHasReturn));

				// we add the function pointer to the hashmap
				m_pmapCallBackFunctions->Add(pFuncDesc->GetName(), pDynFunc);
				return true;
			}
		}
	}
	return false;
}


void SRPWindow::onRunFileChooser(Berkelium::Window *win, int mode, Berkelium::WideString title, Berkelium::FileString defaultFile)
{
	//undone: [10-07-2012 Icefire] not used or implemented

	DebugToConsole("onRunFileChooser()\n");
	
	//undone: [10-07-2012 Icefire] is this implemented at all? https://groups.google.com/d/topic/berkelium/vKGuEpt9CbI/discussion
	// awaiting new build for berkelium
}


bool SRPWindow::RemoveCallBack(const String &sKey) const
{
	if (m_pmapDefaultCallBacks->Get(sKey) == NULL)
	{
		return false;
	}
	else
	{
		// i am not sure if you need to cleanup the children of a struct and the struct itself when you remove the struct from a hashmap
		delete m_pmapDefaultCallBacks->Get(sKey)->pParameters;
		delete m_pmapDefaultCallBacks->Get(sKey)->pReplyMsg;
		delete m_pmapDefaultCallBacks->Get(sKey);
		// we remove the callback
		return m_pmapDefaultCallBacks->Remove(sKey);
	}
}


void SRPWindow::SetDefaultCallBackFunctions()
{
	// bind the default javascript functions for use
	// this allows for users to set default javascript functions within their web page to be able to drag, hide, close and resize a window

	GetBerkeliumWindow()->addBindOnStartLoading(Berkelium::WideString::point_to(String(DRAGWINDOW).GetUnicode()), Berkelium::Script::Variant::bindFunction(Berkelium::WideString::point_to(String(DRAGWINDOW).GetUnicode()), false));
	GetBerkeliumWindow()->addBindOnStartLoading(Berkelium::WideString::point_to(String(HIDEWINDOW).GetUnicode()), Berkelium::Script::Variant::bindFunction(Berkelium::WideString::point_to(String(HIDEWINDOW).GetUnicode()), false));
	GetBerkeliumWindow()->addBindOnStartLoading(Berkelium::WideString::point_to(String(CLOSEWINDOW).GetUnicode()), Berkelium::Script::Variant::bindFunction(Berkelium::WideString::point_to(String(CLOSEWINDOW).GetUnicode()), false));
	GetBerkeliumWindow()->addBindOnStartLoading(Berkelium::WideString::point_to(String(RESIZEWINDOW).GetUnicode()), Berkelium::Script::Variant::bindFunction(Berkelium::WideString::point_to(String(RESIZEWINDOW).GetUnicode()), false));
}


void SRPWindow::onWidgetCreated(Berkelium::Window *win, Berkelium::Widget *newWidget, int zIndex)
{
	// we create the widget
	sWidget *psWidget = new sWidget;

	// we set the data
	psWidget->bNeedsFullUpdate = true;
	psWidget->nXPos = m_psWindowsData->nXPos;
	psWidget->nYPos = m_psWindowsData->nYPos;
	psWidget->pProgramWrapper = CreateProgramWrapper();
	psWidget->pVertexBuffer = CreateVertexBuffer(Vector2::Zero, Vector2::Zero);

	// we add the widget to the hashmap
	m_pmapWidgets->Add(newWidget, psWidget);
}


void SRPWindow::onWidgetDestroyed(Berkelium::Window *win, Berkelium::Widget *wid)
{
	// get the widget that got the destroy callback
	sWidget *psWidget = m_pmapWidgets->Get(wid);
	if (psWidget)
	{
		// delete the resources used by this widget
		if (nullptr != psWidget->pVertexBuffer)
		{
			delete psWidget->pVertexBuffer;
			psWidget->pVertexBuffer = nullptr;
		}
		if (nullptr != psWidget->pTextureBuffer)
		{
			delete psWidget->pTextureBuffer;
			psWidget->pTextureBuffer = nullptr;
		}

		// remove it
		m_pmapWidgets->Remove(wid);
	}
}


void SRPWindow::onWidgetMove(Berkelium::Window *win, Berkelium::Widget *wid, int newX, int newY)
{
	// get the widget that got the move callback
	sWidget *psWidget = m_pmapWidgets->Get(wid);
	if (psWidget)
	{
		// set the new position data
		psWidget->nXPos = m_psWindowsData->nXPos + newX;
		psWidget->nYPos = m_psWindowsData->nYPos + newY;

		//todo: [10-07-2012 Icefire] let (re)sizing be handled by the program uniform, see http://dev.pixellight.org/forum/viewtopic.php?f=6&t=503

		// update the buffer
		UpdateVertexBuffer(psWidget->pVertexBuffer, Vector2(float(psWidget->nXPos), float(psWidget->nYPos)), Vector2(float(psWidget->nWidth), float(psWidget->nHeight)));
	}
}


void SRPWindow::onWidgetPaint(Berkelium::Window *win, Berkelium::Widget *wid, const unsigned char *sourceBuffer, const Berkelium::Rect &sourceBufferRect, size_t numCopyRects, const Berkelium::Rect *copyRects, int dx, int dy, const Berkelium::Rect &scrollRect)
{
	// get the widget that got the paint callback
	sWidget *psWidget = m_pmapWidgets->Get(wid);
	if (psWidget)
	{
		if (psWidget->bNeedsFullUpdate)
		{
			// awaiting a full update disregard all partials ones until the full comes in
			uint8 *pImageBuffer = psWidget->cImage.GetBuffer()->GetData();
			BufferCopyFull(pImageBuffer, psWidget->nWidth, psWidget->nHeight, sourceBuffer, sourceBufferRect);
			psWidget->pTextureBuffer->CopyDataFrom(0, TextureBuffer::R8G8B8A8, pImageBuffer);
			psWidget->bNeedsFullUpdate = false;
		}
		else
		{
			if (sourceBufferRect.width() == psWidget->nWidth && sourceBufferRect.height() == psWidget->nWidth)
			{
				// did not suspect a full update but got it anyway, it might happen and is ok
				uint8 *pImageBuffer = psWidget->cImage.GetBuffer()->GetData();
				BufferCopyFull(pImageBuffer, psWidget->nWidth, psWidget->nHeight, sourceBuffer, sourceBufferRect);
				psWidget->pTextureBuffer->CopyDataFrom(0, TextureBuffer::R8G8B8A8, pImageBuffer);
			}
			else
			{
				if (dx != 0 || dy != 0)
				{
					// a scroll has taken place
					uint8 *pImageBuffer = psWidget->cImage.GetBuffer()->GetData();
					BufferCopyScroll(pImageBuffer, psWidget->nWidth, psWidget->nHeight, sourceBuffer, sourceBufferRect, numCopyRects, copyRects, dx, dy, scrollRect);
					psWidget->pTextureBuffer->CopyDataFrom(0, TextureBuffer::R8G8B8A8, pImageBuffer);
				}
				else
				{
					// normal partial updates
					uint8 *pImageBuffer = psWidget->cImage.GetBuffer()->GetData();
					BufferCopyRects(pImageBuffer, psWidget->nWidth, psWidget->nHeight, sourceBuffer, sourceBufferRect, numCopyRects, copyRects);
					psWidget->pTextureBuffer->CopyDataFrom(0, TextureBuffer::R8G8B8A8, pImageBuffer);
				}
			}
		}
	}
}


void SRPWindow::onWidgetResize(Berkelium::Window *win, Berkelium::Widget *wid, int newWidth, int newHeight)
{
	// get the widget that got the resize callback
	sWidget *psWidget = m_pmapWidgets->Get(wid);
	if (psWidget)
	{
		// set the new size
		psWidget->nWidth = newWidth;
		psWidget->nHeight = newHeight;

		//todo: [10-07-2012 Icefire] let (re)sizing be handled by the program uniform, see http://dev.pixellight.org/forum/viewtopic.php?f=6&t=503

		// update the buffer
		UpdateVertexBuffer(psWidget->pVertexBuffer, Vector2(float(psWidget->nXPos), float(psWidget->nYPos)), Vector2(float(psWidget->nWidth), float(psWidget->nHeight)));

		// recreate the image
		psWidget->cImage = Image::CreateImage(DataByte, ColorRGBA, Vector3i(psWidget->nWidth, psWidget->nHeight, 1));

		// recreate the texture buffer
		if (nullptr != psWidget->pTextureBuffer)
		{
			delete psWidget->pTextureBuffer;
		}
		psWidget->pTextureBuffer = reinterpret_cast<TextureBuffer*>(m_pCurrentRenderer->CreateTextureBuffer2D(psWidget->cImage, TextureBuffer::Unknown, 0));
	}
}


void SRPWindow::DrawWidget(sWidget *psWidget)
{
	// set program
	m_pCurrentRenderer->SetProgram(psWidget->pProgramWrapper);
	// set render state to allow for transparency
	m_pCurrentRenderer->SetRenderState(RenderState::BlendEnable, true);

	//todo: [10-07-2012 Icefire] let (re)sizing be handled by the program uniform, see http://dev.pixellight.org/forum/viewtopic.php?f=6&t=503

	{
		const Rectangle &cViewportRect = m_pCurrentRenderer->GetViewport();
		float fX1 = cViewportRect.vMin.x;
		float fY1 = cViewportRect.vMin.y;
		float fX2 = cViewportRect.vMax.x;
		float fY2 = cViewportRect.vMax.y;

		Matrix4x4 m_mObjectSpaceToClipSpace;
		m_mObjectSpaceToClipSpace.OrthoOffCenter(fX1, fX2, fY1, fY2, -1.0f, 1.0f);

		// create the program uniform
		ProgramUniform *pProgramUniform = psWidget->pProgramWrapper->GetUniform("ObjectSpaceToClipSpaceMatrix");
		if (pProgramUniform)
			pProgramUniform->Set(m_mObjectSpaceToClipSpace);

		const int nTextureUnit = psWidget->pProgramWrapper->Set("TextureMap", psWidget->pTextureBuffer);
		if (nTextureUnit >= 0)
		{
			m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::AddressU, TextureAddressing::Clamp);
			m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::AddressV, TextureAddressing::Clamp);
			m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::MagFilter, TextureFiltering::None);
			m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::MinFilter, TextureFiltering::None);
			m_pCurrentRenderer->SetSamplerState(nTextureUnit, Sampler::MipFilter, TextureFiltering::None);
		}

		// set the vertex attributes
		psWidget->pProgramWrapper->Set("VertexPosition", psWidget->pVertexBuffer, VertexBuffer::Position);
		psWidget->pProgramWrapper->Set("VertexTexCoord", psWidget->pVertexBuffer, VertexBuffer::TexCoord);
	}

	// draw the primitives
	m_pCurrentRenderer->DrawPrimitives(Primitive::TriangleStrip, 0, 4);
}


void SRPWindow::DrawWidgets()
{
	// check if there are widgets that need to be drawn
	if (m_pmapWidgets->GetNumOfElements() > 0)
	{
		// get the iterator for the widgets
		Iterator<sWidget*> cIterator = m_pmapWidgets->GetIterator();
		// loop trough the widgets
		while (cIterator.HasNext())
		{
			// draw the widget
			DrawWidget(cIterator.Next());
		}
	}
}


HashMap<Berkelium::Widget*, sWidget*> *SRPWindow::GetWidgets() const
{
	return m_pmapWidgets;
}


Vector2i SRPWindow::GetRelativeMousePositionWidget(const sWidget *psWidget, const Vector2i &vMousePos) const
{
	return Vector2i(vMousePos.x - psWidget->nXPos, vMousePos.y - psWidget->nYPos);
}


void SRPWindow::ExecuteJavascript(const String &sJavascript) const
{
	// execute the javascript function
	GetBerkeliumWindow()->executeJavascript(Berkelium::WideString::point_to(sJavascript.GetUnicode()));
}


bool SRPWindow::IsLoaded() const
{
	return m_psWindowsData->bLoaded;
}


Image SRPWindow::GetImage() const
{
	// we return the image
	return Image(m_cImage);
	//question: [10-07-2012 Icefire]
	// 1# will this send the image as a copy?
	// 2# will all elements of the image be copied (image buffer / buffer data)?
	// 3# is there a better way to send a class like this? (pointer by ref)
}


//[-------------------------------------------------------]
//[ Namespace                                             ]
//[-------------------------------------------------------]
} // PLBerkelium