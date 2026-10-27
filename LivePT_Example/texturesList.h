List(rt) {
    Tex(mainRT, tType::flat, tFormat::u8, dx11::screen.width, dx11::screen.height, false, true)
    Tex(mainRT2, tType::flat, tFormat::u8, dx11::screen.width, dx11::screen.height, false, true)
    Tex(mainRT3, tType::flat, tFormat::u8, dx11::screen.width, dx11::screen.height, false, true)
};

List(sprites) {
    Tex(head, tType::flat, tFormat::u8, 256, 256, false, false)
    Tex(body, tType::flat, tFormat::u8, 256, 256, false, false)
    Tex(body2, tType::flat, tFormat::u8, 256, 256, false, false)
};
