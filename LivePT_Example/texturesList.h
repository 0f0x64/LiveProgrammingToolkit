List(rt) {
    Tex(mainRT, tType::flat, tFormat::u8, dx11::screen.width, dx11::screen.height, false, true)
};

List(sprites) {
    Tex(spr1, tType::flat, tFormat::u8, 512, 512, false, false, "textures/sprite.png")
    Tex(spr2, tType::flat, tFormat::u8, 512, 512, false, false, "textures/fanta.png")
};
