namespace Draw
{ 
	void SetViewport(int texId, int level = 0)
	{
		XMFLOAT2 size = Textures::Texture[texId].size;
		float denom = powf(2, level);

		D3D11_VIEWPORT vp = {
			.TopLeftX = 0,
			.TopLeftY = 0,
			.Width = (FLOAT)size.x / denom,
			.Height = (FLOAT)size.y / denom,
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f
		};


		context->RSSetViewports(1, &vp);
				
		Rasterizer::Scissors(int4{ 0,0,(int)vp.Width ,(int)vp.Height });
	}

	void SetRenderTarget(auto target, unsigned int level = 0)
	{
		Textures::currentRT = (int)target;

		auto depthStencil = Textures::Texture[(int)target].depth ? Textures::Texture[(int)target].DepthStencilView[0] : 0;

		if (Textures::Texture[(int)target].type == tType::flat)
		{
			context->OMSetRenderTargets(1, &Textures::Texture[(int)target].RenderTargetView[0][0], depthStencil);
		}

		if (Textures::Texture[(int)target].type == tType::cube)
		{
			context->OMSetRenderTargets(6, &Textures::Texture[(int)target].RenderTargetView[0][0], 0);
		}

		SetViewport((int)target, level);
	}

	void Clear(color4 color)
	{
		context->ClearRenderTargetView(Textures::Texture[Textures::currentRT].RenderTargetView[0][0], XMVECTORF32{ color.r, color.g,color.b,color.a });
	}

	void ClearDepth()
	{
		context->ClearDepthStencilView(Textures::Texture[Textures::currentRT].DepthStencilView[0], D3D11_CLEAR_DEPTH, 1.0f, 0);
	}

	void NullDrawer(int quadCount, unsigned int instances = 1)
	{
		context->DrawInstanced(quadCount * 6, instances, 0, 0);
	}

	void NullDrawerTri(int triCount, unsigned int instances = 1)
	{
		context->DrawInstanced(triCount * 3, instances, 0, 0);
	}
	

	void Present()
	{
		Textures::UnbindAll();
		swapChain->Present(1, 0);
	}

}

