
package com.nfsmw.androidevolved;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Launcher options. GameActivity reads them when the game process starts and hands them to the native
 * runtime before the guest boots; changing one takes effect on the next JUGAR.
 */
final class GameOptions {
    static final class Option {
        final String key, title, defaultValue;
        final String[] values, labels;

        Option(String key, String title, String defaultValue, String[] values, String[] labels) {
            this.key = key;
            this.title = title;
            this.defaultValue = defaultValue;
            this.values = values;
            this.labels = labels;
        }

        String label(String value) {
            for (int i = 0; i < values.length; i++)
                if (values[i].equals(value)) return labels[i];
            return value;
        }
    }

    static final String RESOLUTION = "resolution",
            FPS = "fps",
            WIDESCREEN = "widescreen",
            LATENCY = "latency",
            TEXTURES = "textures",
            MINIMAP = "minimap",
            LANGUAGE = "language",
            GAMEPAD = "gamepad";

    static final Option[] ALL = {
            new Option(RESOLUTION, "Resolución interna", "1080",
                    new String[] {
                            "720", "900", "1080", "1200", "1440", "2160",
                            "100", "75", "50"
                    },
                    new String[] {
                            "720p · rendimiento",
                            "900p · equilibrada",
                            "1080p · Full HD",
                            "1200p · alta calidad",
                            "2K · 1440p",
                            "4K · 2160p (muy exigente)",
                            "Nativa · máxima calidad",
                            "75% · equilibrada",
                            "50% · máximo rendimiento"
                    }),

            new Option(FPS, "Límite de FPS", "60",
                    new String[] {"30", "60", "120", "0"},
                    new String[] {
                            "30 FPS · ahorra batería",
                            "60 FPS",
                            "120 FPS",
                            "Sin límite"
                    }),

            new Option(WIDESCREEN, "Pantalla panorámica", "on",
                    new String[] {"on", "off"},
                    new String[] {"HUD proporcional", "HUD original estirado"}),

            new Option(MINIMAP, "Minimapa", "top",
                    new String[] {"top", "bottom"},
                    new String[] {"Arriba a la izquierda", "Abajo a la izquierda (original)"}),

            new Option(LATENCY, "Sincronía CPU/GPU", "2",
                    new String[] {"2", "1"},
                    new String[] {
                            "Rendimiento · CPU y GPU en paralelo",
                            "Baja latencia · menos FPS"
                    }),

            new Option(TEXTURES, "Texturas comprimidas", "auto",
                    new String[] {"auto", "cpu"},
                    new String[] {
                            "Automático · GPU si las soporta",
                            "Convertir en CPU · compatibilidad"
                    }),

            new Option(GAMEPAD, "Mando Bluetooth/USB", "keys",
                    new String[] {"keys", "directinput"},
                    new String[] {
                            "Como teclado · compatible",
                            "Joystick DirectInput · analógico, experimental"
                    }),
    };

    private static final String PREFS = "nfsmw_game";

    private GameOptions() {}

    static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    static Option find(String key) {
        for (Option o : ALL)
            if (o.key.equals(key)) return o;
        throw new IllegalArgumentException(key);
    }

    static String get(Context context, String key) {
        Option o = find(key);
        String value = prefs(context).getString(key, o.defaultValue);

        for (String allowed : o.values)
            if (allowed.equals(value)) return value;

        return o.defaultValue;
    }

    static void set(Context context, String key, String value) {
        prefs(context).edit().putString(key, value).apply();
    }

    static final String ONLINE_SERVER = "online_server",
            DEFAULT_ONLINE_SERVER = "eahub.eu";

    /** EA Nation replacement server. */
    static String onlineServer(Context context) {
        String value = prefs(context).getString(ONLINE_SERVER, DEFAULT_ONLINE_SERVER);
        return validServer(value) ? value : DEFAULT_ONLINE_SERVER;
    }

    static boolean validServer(String value) {
        return value != null && value.length() <= 253 &&
                value.matches("[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)*");
    }

    /** The game's language folder name. */
    static String language(Context context) {
        return prefs(context).getString(LANGUAGE, "Spanish");
    }

    /** Render size for the selected resolution on a display of width × height. */
    static int[] renderSize(Context context, int width, int height) {
        String value = get(context, RESOLUTION);

        int renderHeight;

        switch (value) {
            case "720":
            case "900":
            case "1080":
            case "1200":
            case "1440":
            case "2160":
                renderHeight = Integer.parseInt(value);
                break;

            default:
                float scale = Integer.parseInt(value) / 100f;
                int scaledWidth = Math.round(width * scale / 2f) * 2;
                int scaledHeight = Math.round(height * scale / 2f) * 2;
                return new int[] {
                        Math.max(2, scaledWidth),
                        Math.max(2, scaledHeight)
                };
        }

        int renderWidth = Math.round(
                (width * (float) renderHeight / height) / 2f
        ) * 2;

        return new int[] {
                Math.max(2, renderWidth),
                Math.max(2, renderHeight)
        };
    }
}
