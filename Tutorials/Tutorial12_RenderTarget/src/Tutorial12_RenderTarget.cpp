/*     Copyright 2019 Diligent Graphics LLC
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include "Tutorial12_RenderTarget.h"
#include "MapHelper.h"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "CommonlyUsedStates.h"
#include "../../Common/src/TexturedCube.h"

namespace Diligent
{

SampleBase* CreateSample()
{
    return new Tutorial12_RenderTarget();
}

void Tutorial12_RenderTarget::GetEngineInitializationAttribs(DeviceType         DevType,
                                                             EngineCreateInfo&  Attribs,
                                                             SwapChainDesc&     SCDesc)
{
    SampleBase::GetEngineInitializationAttribs(DevType, Attribs, SCDesc);
    // In this tutorial we will be using off-screen depth-stencil buffer, so
    // we do not need the one in the swap chain.
    SCDesc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;
}

void Tutorial12_RenderTarget::CreateCubePSO()
{
    // Create a shader source stream factory to load shaders from files.
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);

    m_pCubePSO = TexturedCube::CreatePipelineState(m_pDevice, 
        RenderTargetFormat,
        DepthBufferFormat,
        pShaderSourceFactory,
        "cube.vsh",
        "cube.psh");

    // Create dynamic uniform buffer that will store our transformation matrix
    // Dynamic buffers can be frequently updated by the CPU
    CreateUniformBuffer(m_pDevice, sizeof(float4x4), "VS constants CB", &m_CubeVSConstants);
       
    // Since we did not explcitly specify the type for 'Constants' variable, default
    // type (SHADER_RESOURCE_VARIABLE_TYPE_STATIC) will be used. Static variables never
    // change and are bound directly through the pipeline state object.
    m_pCubePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_CubeVSConstants);

    // Since we are using mutable variable, we must create a shader resource binding object
    // http://diligentgraphics.com/2016/03/23/resource-binding-model-in-diligent-engine-2-0/
    m_pCubePSO->CreateShaderResourceBinding(&m_pCubeSRB, true);
}

void Tutorial12_RenderTarget::CreateRenderTargetPSO()
{
    PipelineStateDesc RTPSODesc;
    // Pipeline state name is used by the engine to report issues
    // It is always a good idea to give objects descriptive names
    RTPSODesc.Name                                          = "Render Target PSO";
    // This is a graphics pipeline
    RTPSODesc.IsComputePipeline                             = false;
    // This tutorial will render to a single render target
    RTPSODesc.GraphicsPipeline.NumRenderTargets             = 1;
    // Set render target format which is the format of the swap chain's color buffer
    RTPSODesc.GraphicsPipeline.RTVFormats[0]                = m_pSwapChain->GetDesc().ColorBufferFormat;
    // Set depth buffer format which is the format of the swap chain's back buffer
    RTPSODesc.GraphicsPipeline.DSVFormat                    = m_pSwapChain->GetDesc().DepthBufferFormat;
    // Primitive topology defines what kind of primitives will be rendered by this pipeline state
    RTPSODesc.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    // Cull back faces
    RTPSODesc.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_BACK;
    // Enable depth testing
    RTPSODesc.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

    ShaderCreateInfo ShaderCI;
    // Tell the system that the shader source code is in HLSL.
    // For OpenGL, the engine will convert this into GLSL under the hood.
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

    // OpenGL backend requires emulated combined HLSL texture samplers (g_Texture + g_Texture_sampler combination)
    ShaderCI.UseCombinedTextureSamplers = true;

    // In this tutorial, we will load shaders from file. To be able to do that,
    // we need to create a shader source stream factory
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    // Create a vertex shader
    RefCntAutoPtr<IShader> pRTVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Render Target VS";
        ShaderCI.FilePath        = "rendertarget.vsh";
        m_pDevice->CreateShader(ShaderCI, &pRTVS);
    }

    // Create a pixel shader
    RefCntAutoPtr<IShader> pRTPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Render Target PS";
        ShaderCI.FilePath        = "rendertarget.psh";
            
        m_pDevice->CreateShader(ShaderCI, &pRTPS);

        // Create dynamic uniform buffer that will store our transformation matrix
        // Dynamic buffers can be frequently updated by the CPU
        BufferDesc CBDesc;
        CBDesc.Name           = "RTPS constants CB";
        CBDesc.uiSizeInBytes  = sizeof(float4);
        CBDesc.Usage          = USAGE_DYNAMIC;
        CBDesc.BindFlags      = BIND_UNIFORM_BUFFER;
        CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        m_pDevice->CreateBuffer( CBDesc, nullptr, &m_RTPSConstants );
    }

    RTPSODesc.GraphicsPipeline.pVS = pRTVS;
    RTPSODesc.GraphicsPipeline.pPS = pRTPS;

    // Define variable type that will be used by default
    RTPSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

    // Shader variables should typically be mutable, which means they are expected
    // to change on a per-instance basis
    ShaderResourceVariableDesc Vars[] =
    {
        { SHADER_TYPE_PIXEL, "g_Texture", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE }
    };
    RTPSODesc.ResourceLayout.Variables    = Vars;
    RTPSODesc.ResourceLayout.NumVariables = _countof(Vars);

    // Define static sampler for g_Texture. Static samplers should be used whenever possible
    StaticSamplerDesc StaticSamplers[] =
    {
        { SHADER_TYPE_PIXEL, "g_Texture", Sam_LinearClamp }
    };
    RTPSODesc.ResourceLayout.StaticSamplers    = StaticSamplers;
    RTPSODesc.ResourceLayout.NumStaticSamplers = _countof(StaticSamplers);

    m_pDevice->CreatePipelineState(RTPSODesc, &m_pRTPSO);

    // Since we did not explcitly specify the type for Constants, default type
    // (SHADER_RESOURCE_VARIABLE_TYPE_STATIC) will be used. Static variables never change and are bound directly
    // to the pipeline state object.
    m_pRTPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Constants")->Set(m_RTPSConstants);
}

void Tutorial12_RenderTarget::Initialize(IEngineFactory*   pEngineFactory,
                                         IRenderDevice*    pDevice,
                                         IDeviceContext**  ppContexts,
                                         Uint32            NumDeferredCtx,
                                         ISwapChain*       pSwapChain)
{
    SampleBase::Initialize(pEngineFactory, pDevice, ppContexts, NumDeferredCtx, pSwapChain);
    
    CreateCubePSO();
    CreateRenderTargetPSO();

    // Load textured cube
    m_CubeVertexBuffer = TexturedCube::CreateVertexBuffer(pDevice);
    m_CubeIndexBuffer  = TexturedCube::CreateIndexBuffer(pDevice);
    m_CubeTextureSRV = TexturedCube::LoadTexture(pDevice, "DGLogo.png")->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    // Set cube texture SRV in the SRB
    m_pCubeSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_CubeTextureSRV);
}

void Tutorial12_RenderTarget::WindowResize(Uint32 Width, Uint32 Height)
{
    // Create window-size offscreen render target
    RefCntAutoPtr<ITexture> pRTColor;
    TextureDesc RTColorDesc;
    RTColorDesc.Type        = RESOURCE_DIM_TEX_2D;
    RTColorDesc.Width       = m_pSwapChain->GetDesc().Width;
    RTColorDesc.Height      = m_pSwapChain->GetDesc().Height;
    RTColorDesc.MipLevels   = 1;
    RTColorDesc.Format      = RenderTargetFormat;
    // The render target can be bound as a shader resource and as a render target
    RTColorDesc.BindFlags   = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    // Define optimal clear value
    RTColorDesc.ClearValue.Format = RTColorDesc.Format;
    RTColorDesc.ClearValue.Color[0] = 0.350f;
    RTColorDesc.ClearValue.Color[1] = 0.350f;
    RTColorDesc.ClearValue.Color[2] = 0.350f;
    RTColorDesc.ClearValue.Color[3] = 1.f;
    m_pDevice->CreateTexture(RTColorDesc, nullptr, &pRTColor);
    // Store the render target view
    m_pColorRTV = pRTColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    

    // Create window-size depth buffer
    RefCntAutoPtr<ITexture> pRTDepth;
    TextureDesc RTDepthDesc = RTColorDesc;
    RTDepthDesc.Format = DepthBufferFormat;
    // Define optimal clear value
    RTDepthDesc.ClearValue.Format = RTDepthDesc.Format;
    RTDepthDesc.ClearValue.DepthStencil.Depth = 1;
    RTDepthDesc.ClearValue.DepthStencil.Stencil = 0;
    // The depth buffer can be bound as a shader resource and as a depth-stencil buffer
    RTDepthDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_DEPTH_STENCIL;
    m_pDevice->CreateTexture(RTDepthDesc, nullptr, &pRTDepth);
    // Store the depth-stencil view
    m_pDepthDSV = pRTDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);

    // We need to release and create a new SRB that references new off-screen render target SRV
    m_pRTSRB.Release();
    m_pRTPSO->CreateShaderResourceBinding(&m_pRTSRB, true);

    // Set render target color texture SRV in the SRB
    m_pRTSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(pRTColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
}

// Render a frame
void Tutorial12_RenderTarget::Render()
{
    // Clear the offscreen render target and depth buffer
    const float ClearColor[] = { 0.350f,  0.350f,  0.350f, 1.0f };
    m_pImmediateContext->SetRenderTargets(1, &m_pColorRTV, m_pDepthDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearRenderTarget(m_pColorRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearDepthStencil(m_pDepthDSV, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        // Map the cube's constant buffer and fill it in with its model-view-projection matrix
        MapHelper<float4x4> CBConstants(m_pImmediateContext, m_CubeVSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        *CBConstants = m_WorldViewProjMatrix.Transpose();
    }

    {
        // Map the render target PS constant buffer and fill it in with current time
        MapHelper<float4> CBConstants(m_pImmediateContext, m_RTPSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        *CBConstants = float4(m_fCurrentTime, 0 , 0, 0);
    }

    // Bind vertex and index buffers
    Uint32 offset = 0;
    IBuffer* pBuffs[] = {m_CubeVertexBuffer};
    m_pImmediateContext->SetVertexBuffers(0, 1, pBuffs, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
    m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set the cube's pipeline state
    m_pImmediateContext->SetPipelineState(m_pCubePSO);

    // Commit the cube shader's resources
    m_pImmediateContext->CommitShaderResources(m_pCubeSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Draw the cube
    DrawIndexedAttribs DrawAttrs;
    DrawAttrs.IndexType   = VT_UINT32; // Index type
    DrawAttrs.NumIndices  = 36;
    DrawAttrs.Flags       = DRAW_FLAG_VERIFY_ALL; // Verify the state of vertex and index buffers
    m_pImmediateContext->DrawIndexed(DrawAttrs);

    // Clear the default render target
    const float Zero[] = { 0.0f,  0.0f,  0.0f, 1.0f };
    m_pImmediateContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearRenderTarget(nullptr, Zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set the render target pipeline state
    m_pImmediateContext->SetPipelineState(m_pRTPSO);

    // Commit the render target shader's resources
    m_pImmediateContext->CommitShaderResources(m_pRTSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Draw the render target's vertices
    DrawAttribs RTDrawAttrs;
    RTDrawAttrs.NumVertices = 4;
    RTDrawAttrs.Flags       = DRAW_FLAG_VERIFY_ALL; // Verify the state of vertex and index buffers
    m_pImmediateContext->Draw(RTDrawAttrs);
}

void Tutorial12_RenderTarget::Update(double CurrTime, double ElapsedTime)
{
    SampleBase::Update(CurrTime, ElapsedTime);

    m_fCurrentTime = static_cast<float>(CurrTime);
    // Set cube world view matrix
    float4x4 CubeWorldView = float4x4::RotationY(static_cast<float>(CurrTime)) * float4x4::RotationX(-PI_F * 0.1f) * float4x4::Translation(0.0f, 0.0f, 5.0f);
    float NearPlane = 0.1f;
    float FarPlane = 100.f;
    float aspectRatio = static_cast<float>(m_pSwapChain->GetDesc().Width) / static_cast<float>(m_pSwapChain->GetDesc().Height);

    // Projection matrix differs between DX and OpenGL
    auto Proj = float4x4::Projection(PI_F / 4.0f, aspectRatio, NearPlane, FarPlane, m_pDevice->GetDeviceCaps().IsGLDevice());

    // Compute world-view-projection matrix
    m_WorldViewProjMatrix = CubeWorldView * Proj;
}

}
