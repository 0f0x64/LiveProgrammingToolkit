namespace Draw
{ 

	void Clear(color4 color)
	{
		context->ClearRenderTargetView(Textures::Texture[Textures::currentRT].RenderTargetView[0][0], XMVECTORF32{ color.r, color.g,color.b,color.a });
	}

	/*void ClearDepth()
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
	*/

	void Present()
	{
		Textures::UnbindAll();
		swapChain->Present(1, 0);
	}

}

