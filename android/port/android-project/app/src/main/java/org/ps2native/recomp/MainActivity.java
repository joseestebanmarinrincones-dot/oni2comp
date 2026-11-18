package org.ps2native.recomp;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.Log;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Actividad mínima para SDL2 + libmain.so.
 *
 * SDLActivity carga las librerías con SDL.loadLibrary(nombre), que añade el
 * prefijo "lib" y la extensión ".so". Por eso los nombres son "SDL2" y "main",
 * no "libSDL2.so" ni "libmain.so".
 *
 * El orden importa: SDL2 primero, porque libmain.so depende de ella.
 *
 * Si no existe disc.iso en Android/data/<pkg>/files, esta actividad abre el
 * selector nativo (ACTION_OPEN_DOCUMENT) y entrega el descriptor a libmain.
 * El fd se lee con pread en C++ (IsoReader::OpenFd), sin copiar los 3,78 GB.
 */
public class MainActivity extends SDLActivity {

    private static final String TAG = "OnimushaDisc";
    private static final String PREFS = "disc_iso";
    private static final String PREF_URI = "uri";
    private static final int REQUEST_DISC_ISO = 0x1502;

    private static final String[] LIBRARIES = {
        "SDL2",
        "main"
    };

    private boolean discPickerShown = false;

    @Override
    protected String[] getLibraries() {
        return LIBRARIES;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Deja terminar onCreate de SDLActivity antes de abrir otra Activity.
        getWindow().getDecorView().post(this::checkDiscIso);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_DISC_ISO) return;

        discPickerShown = false;
        if (resultCode != Activity.RESULT_OK || data == null ||
            data.getData() == null) {
            Log.i(TAG, "Selección de disc.iso cancelada");
            Toast.makeText(this, "Sin disc.iso no se puede iniciar el juego",
                           Toast.LENGTH_LONG).show();
            notifyUnavailable("selección cancelada por el usuario");
            return;
        }

        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException error) {
            // Algunos proveedores no permiten persistir; se intenta leer igual.
            Log.w(TAG, "El proveedor no permite permiso persistente", error);
        }

        if (!openDiscUri(uri, true)) {
            // No relanzar en bucle: el usuario puede reintentar al reabrir.
            Toast.makeText(this, "No se pudo leer disc.iso", Toast.LENGTH_LONG)
                .show();
        }
    }

    /**
     * Punto de entrada del onboarding: disco local, URI persistido o selector.
     */
    private void checkDiscIso() {
        File local = findLocalDiscIso();
        if (local != null) {
            Log.i(TAG, "disc.iso local: " + local.getAbsolutePath() +
                           " (" + local.length() + " bytes)");
            notifyFilePresent();
            return;
        }

        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        String saved = prefs.getString(PREF_URI, null);
        if (saved != null && !saved.isEmpty()) {
            Uri uri = Uri.parse(saved);
            Log.i(TAG, "Reabriendo disc.iso persistido: " + uri);
            if (openDiscUri(uri, false)) return;
            // El archivo se movió o se revocó el permiso: pedir otro.
            prefs.edit().remove(PREF_URI).apply();
        }

        launchDiscPicker();
    }

    private File findLocalDiscIso() {
        File external = getExternalFilesDir(null);
        if (external != null) {
            File file = new File(external, "disc.iso");
            if (file.isFile() && file.length() > 0) return file;
        }
        File internal = new File(getFilesDir(), "disc.iso");
        if (internal.isFile() && internal.length() > 0) return internal;
        return null;
    }

    private void launchDiscPicker() {
        if (discPickerShown) return;
        discPickerShown = true;

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // */* para que Downloads muestre también .iso/.bin que algunos
        // proveedores etiquetan como octet-stream. libmain valida ISO 9660.
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);

        Log.i(TAG, "Abriendo ACTION_OPEN_DOCUMENT para elegir disc.iso");
        Toast.makeText(this, "Selecciona tu imagen disc.iso", Toast.LENGTH_LONG)
            .show();
        try {
            startActivityForResult(intent, REQUEST_DISC_ISO);
        } catch (ActivityNotFoundException error) {
            discPickerShown = false;
            Log.e(TAG, "No hay selector de documentos", error);
            notifyUnavailable("no hay selector de documentos en el dispositivo");
        }
    }

    /**
     * Abre el fd del documento y lo entrega a libmain (que lo duplica).
     * Devuelve true si el descriptor quedó en manos del lado nativo.
     */
    private boolean openDiscUri(Uri uri, boolean fromPicker) {
        ParcelFileDescriptor pfd = null;
        try {
            pfd = getContentResolver().openFileDescriptor(uri, "r");
            if (pfd == null) {
                throw new FileNotFoundException(
                    "openFileDescriptor devolvió null");
            }

            long size = pfd.getStatSize();
            if (size == 0) {
                throw new FileNotFoundException("el documento está vacío");
            }

            String name = queryDisplayName(uri);
            try {
                nativeSetDiscFd(pfd.getFd(), name);
            } catch (UnsatisfiedLinkError error) {
                Log.e(TAG, "libmain no expone nativeSetDiscFd", error);
                if (fromPicker) {
                    notifyUnavailable("libmain no aceptó el descriptor");
                }
                return false;
            }

            getSharedPreferences(PREFS, MODE_PRIVATE)
                .edit()
                .putString(PREF_URI, uri.toString())
                .apply();
            Log.i(TAG, "disc.iso listo por fd: " + name + " (" + size +
                           " bytes)");
            return true;
        } catch (Exception error) {
            Log.w(TAG, "No se pudo abrir " + uri, error);
            if (fromPicker) {
                notifyUnavailable("no se pudo abrir disc.iso: " +
                                  error.getMessage());
            }
            return false;
        } finally {
            if (pfd != null) {
                try {
                    pfd.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(
                 uri, new String[] {OpenableColumns.DISPLAY_NAME},
                 null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    String name = cursor.getString(index);
                    if (name != null && !name.isEmpty()) return name;
                }
            }
        } catch (Exception error) {
            Log.w(TAG, "No se pudo leer el nombre del documento", error);
        }
        String last = uri.getLastPathSegment();
        return last != null ? last : "disc.iso";
    }

    private static void notifyFilePresent() {
        try {
            nativeSetDiscFilePresent();
        } catch (UnsatisfiedLinkError error) {
            Log.e(TAG, "libmain no expone nativeSetDiscFilePresent", error);
        }
    }

    private static void notifyUnavailable(String reason) {
        try {
            nativeSetDiscUnavailable(reason);
        } catch (UnsatisfiedLinkError error) {
            Log.e(TAG, "libmain no expone nativeSetDiscUnavailable", error);
        }
    }

    private static native void nativeSetDiscFd(int fd, String name);

    private static native void nativeSetDiscUnavailable(String reason);

    private static native void nativeSetDiscFilePresent();
}
