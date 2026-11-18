package org.ps2native.recomp;

import org.libsdl.app.SDLActivity;

/**
 * Actividad mínima para SDL2 + libmain.so.
 *
 * SDLActivity carga las librerías con SDL.loadLibrary(nombre), que añade el
 * prefijo "lib" y la extensión ".so". Por eso los nombres son "SDL2" y "main",
 * no "libSDL2.so" ni "libmain.so".
 *
 * El orden importa: SDL2 primero, porque libmain.so depende de ella.
 */
public class MainActivity extends SDLActivity {

    private static final String[] LIBRARIES = {
        "SDL2",
        "main"
    };

    @Override
    protected String[] getLibraries() {
        return LIBRARIES;
    }
}
