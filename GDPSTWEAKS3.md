# Geometry Dash Wrapper 0.9.6-gdpstweaks3

Branch: `gdpstweaks3`

## Fullscreen/resizing image-quality fix

- Keeps the gdpstweaks1/2 resizable and borderless-fullscreen implementation.
- Intercepts `glTexParameteri` on x86, legacy ARM and ARMv7.
- If an old game requests `GL_TEXTURE_MAG_FILTER = GL_NEAREST`, the wrapper substitutes `GL_LINEAR`.
- Minification filters, mipmaps, wrap modes and every other texture parameter are left unchanged.
- This specifically targets the blocky/cubed nearest-neighbour appearance seen when the 1280x720 presentation is magnified in fullscreen or a larger window.

No Extras UI and no abandoned 1.3 hybrid code are reintroduced.
