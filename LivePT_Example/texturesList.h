List(rt) {
    Tex(mainRT, tType::flat, tFormat::u8, dx11::screen.width, dx11::screen.height, false, true)
};

List(sprites) {
    Tex(head, tType::flat, tFormat::u8, 256, 256, false, false)
    Tex(body, tType::flat, tFormat::u8, 256, 256, false, false)
};
