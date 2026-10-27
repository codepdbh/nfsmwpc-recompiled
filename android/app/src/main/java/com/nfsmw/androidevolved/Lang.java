package com.nfsmw.androidevolved;

import android.content.Context;
import android.content.SharedPreferences;
import java.util.HashMap;
import java.util.Locale;

/**
 * Launcher and touch-control texts in Spanish, English or Portuguese, chosen with the flags on the first
 * launch. The Spanish text is the key: a missing translation shows the Spanish original. The choice is
 * stored in the launcher preferences, which the :game process reads when it starts.
 */
final class Lang {
    static final String ES = "es", EN = "en", PT = "pt";
    /** {code, flag, name}, in the order of the picker. */
    static final String[][] CHOICES = {{ES, "🇪🇸", "Español"}, {EN, "🇬🇧", "English"}, {PT, "🇧🇷", "Português"}};
    private static final String PREFS = "nfsmw_launcher", KEY = "launcher_language";
    private static String current;
    private static final HashMap<String, String[]> TEXTS = new HashMap<>();

    private Lang() {}

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** True once the player picked a flag. */
    static boolean chosen(Context context) {
        return !prefs(context).getString(KEY, "").isEmpty();
    }

    static String code(Context context) {
        if (current == null) current = prefs(context).getString(KEY, "");
        if (current.isEmpty()) {
            // Before the choice: follow the phone, Spanish otherwise.
            String phone = Locale.getDefault().getLanguage();
            return phone.equals(EN) || phone.equals(PT) ? phone : ES;
        }
        return current;
    }

    static void set(Context context, String code) {
        current = code;
        prefs(context).edit().putString(KEY, code).commit();
    }

    static String name(Context context) {
        String code = code(context);
        for (String[] choice : CHOICES) if (choice[0].equals(code)) return choice[1] + "  " + choice[2];
        return code;
    }

    /** The text in the chosen language; extra arguments fill its %s / %d fields. */
    static String t(Context context, String spanish, Object... args) {
        String text = spanish;
        String code = code(context);
        if (!code.equals(ES)) {
            String[] translated = TEXTS.get(spanish);
            if (translated != null) text = code.equals(EN) ? translated[0] : translated[1];
        }
        return args.length == 0 ? text : String.format(Locale.ROOT, text, args);
    }

    static String[] t(Context context, String[] spanish) {
        String[] result = new String[spanish.length];
        for (int i = 0; i < spanish.length; i++) result[i] = t(context, spanish[i]);
        return result;
    }

    private static void add(String es, String en, String pt) { TEXTS.put(es, new String[] {en, pt}); }

    static {
        // Language picker.
        add("Elige el idioma del launcher", "Choose the launcher language", "Escolha o idioma do launcher");
        add("Idioma del launcher", "Launcher language", "Idioma do launcher");
        add("Puedes cambiarlo después en Opciones.", "You can change it later in Options.", "Você pode mudar depois em Opções.");

        // Launcher screen.
        add("PC · recompilado de forma nativa para Android", "PC · natively recompiled for Android", "PC · recompilado nativamente para Android");
        add("ARCHIVOS DEL JUEGO", "GAME FILES", "ARQUIVOS DO JOGO");
        add("JUGAR", "PLAY", "JOGAR");
        add("Permitir acceso a memoria interna/nfsmw", "Allow access to internal storage/nfsmw", "Permitir acesso ao armazenamento interno/nfsmw");
        add("OPCIONES", "OPTIONS", "OPÇÕES");
        add("Se aplican al pulsar JUGAR.", "Applied when you press PLAY.", "Aplicadas ao tocar em JOGAR.");
        add("Carpetas del juego", "Game folders", "Pastas do jogo");
        add("Faltan carpetas: %s", "Missing folders: %s", "Pastas faltando: %s");
        add(" · opcional", " · optional", " · opcional");
        add("Permite el acceso a archivos para leer memoria interna/nfsmw.",
                "Allow file access so the launcher can read internal storage/nfsmw.",
                "Permita o acesso aos arquivos para ler o armazenamento interno/nfsmw.");
        add("Memoria interna/nfsmw", "Internal storage/nfsmw", "Armazenamento interno/nfsmw");
        add("Copia los archivos de tu juego en memoria interna/nfsmw.", "Copy your game files to internal storage/nfsmw.",
                "Copie os arquivos do seu jogo para o armazenamento interno/nfsmw.");
        add("Copia los archivos de tu juego (Speed.exe y sus carpetas) dentro de:\n%s",
                "Copy your game files (Speed.exe and its folders) into:\n%s",
                "Copie os arquivos do seu jogo (Speed.exe e suas pastas) para dentro de:\n%s");
        add("Los archivos están en «%s»: mueve su contenido directamente a memoria interna/nfsmw.",
                "The files are inside \"%s\": move its contents directly into internal storage/nfsmw.",
                "Os arquivos estão em \"%s\": mova o conteúdo diretamente para o armazenamento interno/nfsmw.");
        add("Falta LANGUAGES/<idioma>.bin", "LANGUAGES/<language>.bin is missing", "Falta LANGUAGES/<idioma>.bin");
        add("Driver Vulkan", "Vulkan driver", "Driver Vulkan");
        add("Del sistema", "System", "Do sistema");
        add("Probar driver", "Test driver", "Testar driver");
        add("GPU y funciones Vulkan", "GPU and Vulkan features", "GPU e recursos Vulkan");
        add("Idioma del juego", "Game language", "Idioma do jogo");
        add("Servidor online", "Online server", "Servidor online");
        add("Buscar actualizaciones", "Check for updates", "Procurar atualizações");
        add("Comprobando GitHub…", "Checking GitHub…", "Verificando o GitHub…");
        add("Versión %s", "Version %s", "Versão %s");
        add("Reportar un problema", "Report a problem", "Reportar um problema");
        add("Log completo a GitHub", "Full log to GitHub", "Log completo para o GitHub");
        add("No se encontraron idiomas en memoria interna/nfsmw/LANGUAGES.", "No languages found in internal storage/nfsmw/LANGUAGES.",
                "Nenhum idioma encontrado em armazenamento interno/nfsmw/LANGUAGES.");
        add("Aceptar", "OK", "OK");
        add("Cancelar", "Cancel", "Cancelar");
        add("Guardar", "Save", "Salvar");
        add("Restaurar", "Restore", "Restaurar");
        add("Volver", "Back", "Voltar");
        add("Cerrar", "Close", "Fechar");
        add("El modo online del juego (EA Nation) se conecta a este servidor de la comunidad en lugar de los servidores de EA, ya cerrados.",
                "The game's online mode (EA Nation) connects to this community server instead of EA's servers, which are closed.",
                "O modo online do jogo (EA Nation) se conecta a este servidor da comunidade em vez dos servidores da EA, já desativados.");
        add("\"%s\" no es un nombre de servidor válido.", "\"%s\" is not a valid server name.", "\"%s\" não é um nome de servidor válido.");
        add("Driver del sistema", "System driver", "Driver do sistema");
        add("Importar ZIP", "Import ZIP", "Importar ZIP");
        add("Eliminar seleccionado", "Delete selected", "Excluir selecionado");
        add("No se pudo usar el driver: %s", "The driver could not be used: %s", "Não foi possível usar o driver: %s");
        add("error al iniciar Vulkan.", "Vulkan failed to start.", "erro ao iniciar o Vulkan.");
        add("Texturas BC: ", "BC textures: ", "Texturas BC: ");
        add("en la GPU", "on the GPU", "na GPU");
        add("se convierten en la CPU", "converted on the CPU", "convertidas na CPU");
        add("Tipos de vértice sin soporte: ", "Unsupported vertex types: ", "Tipos de vértice sem suporte: ");
        add("Compatible con el juego.", "Compatible with the game.", "Compatível com o jogo.");
        add("Faltan funciones: ", "Missing features: ", "Recursos faltando: ");
        add("Prueba del driver Vulkan", "Vulkan driver test", "Teste do driver Vulkan");
        add("Usar driver del sistema", "Use system driver", "Usar driver do sistema");
        add("Jugar igualmente", "Play anyway", "Jogar mesmo assim");
        add("Diagnóstico no válido: ", "Invalid diagnostics: ", "Diagnóstico inválido: ");
        add("La prueba se cerró o fue cancelada.", "The test closed or was cancelled.", "O teste foi fechado ou cancelado.");
        add("Importando driver Vulkan…", "Importing Vulkan driver…", "Importando driver Vulkan…");
        add("Driver importado: %s. Usa Probar driver para comprobarlo.", "Driver imported: %s. Use Test driver to check it.",
                "Driver importado: %s. Use Testar driver para verificá-lo.");
        add("No se pudo importar el driver: ", "Could not import the driver: ", "Não foi possível importar o driver: ");
        add("Actualizaciones", "Updates", "Atualizações");
        add("Tienes la versión %s. No hay una versión más reciente en GitHub.", "You have version %s. There is no newer version on GitHub.",
                "Você tem a versão %s. Não há uma versão mais recente no GitHub.");
        add("Nueva versión disponible: %s", "New version available: %s", "Nova versão disponível: %s");
        add("Puedes descargarla desde GitHub o seguir jugando con esta versión. Instala el APK encima del actual para conservar tus partidas y ajustes.",
                "You can download it from GitHub or keep playing this version. Install the APK over the current one to keep your saves and settings.",
                "Você pode baixá-la no GitHub ou continuar jogando esta versão. Instale o APK por cima do atual para manter seus saves e ajustes.");
        add("Actualizar", "Update", "Atualizar");
        add("Más tarde", "Later", "Mais tarde");
        add("¡Actualizado a la versión %s!", "Updated to version %s!", "Atualizado para a versão %s!");
        add("Cargando las novedades…", "Loading what's new…", "Carregando as novidades…");
        add("Ver en GitHub", "View on GitHub", "Ver no GitHub");
        add("No se pudieron cargar las novedades (sin conexión o aún no publicadas). Puedes verlas en GitHub.",
                "Couldn't load what's new (offline or not published yet). You can read it on GitHub.",
                "Não foi possível carregar as novidades (sem conexão ou ainda não publicadas). Você pode vê-las no GitHub.");
        add("El juego se cerró", "The game closed", "O jogo fechou");
        add("%s.\n\n¿Quieres enviar el registro completo a GitHub para que se pueda corregir?",
                "%s.\n\nDo you want to send the full log to GitHub so it can be fixed?",
                "%s.\n\nQuer enviar o registro completo ao GitHub para que possa ser corrigido?");
        add("Reportar en GitHub", "Report on GitHub", "Reportar no GitHub");
        add("Ahora no", "Not now", "Agora não");
        add("Preparando el registro…", "Preparing the log…", "Preparando o registro…");
        add("El registro completo se ha guardado en:\n%s\n\nSe abrirá la issue en GitHub con un resumen técnico ya escrito. Describe qué pasó y, si puedes, adjunta ese ZIP (GitHub no permite adjuntarlo automáticamente).",
                "The full log was saved to:\n%s\n\nA GitHub issue will open with a technical summary already written. Describe what happened and, if you can, attach that ZIP (GitHub does not allow attaching it automatically).",
                "O registro completo foi salvo em:\n%s\n\nUma issue será aberta no GitHub com um resumo técnico já escrito. Descreva o que aconteceu e, se puder, anexe esse ZIP (o GitHub não permite anexá-lo automaticamente).");
        add("Abrir GitHub", "Open GitHub", "Abrir GitHub");
        add("No se pudo preparar el registro: ", "Could not prepare the log: ", "Não foi possível preparar o registro: ");
        add("No hay un navegador para abrir %s", "No browser available to open %s", "Não há navegador para abrir %s");

        // Game options (GameOptions).
        add("Resolución interna", "Internal resolution", "Resolução interna");
        add("Nativa · máxima calidad", "Native · best quality", "Nativa · máxima qualidade");
        add("75% · equilibrada", "75% · balanced", "75% · equilibrada");
        add("50% · máximo rendimiento", "50% · best performance", "50% · máximo desempenho");
        add("720p · adaptada a la pantalla", "720p · fitted to the screen", "720p · ajustada à tela");
        add("480p · rendimiento", "480p · performance", "480p · desempenho");
        add("360p · máximo rendimiento", "360p · best performance", "360p · máximo desempenho");
        add("Límite de FPS", "FPS limit", "Limite de FPS");
        add("30 FPS · ahorra batería", "30 FPS · saves battery", "30 FPS · economiza bateria");
        add("Sin límite", "Unlimited", "Sem limite");
        add("Pantalla panorámica", "Widescreen", "Tela panorâmica");
        add("HUD proporcional", "Proportional HUD", "HUD proporcional");
        add("HUD original estirado", "Original stretched HUD", "HUD original esticado");
        add("Minimapa", "Minimap", "Minimapa");
        add("Canal del desarrollador oficial", "Official developer channel", "Canal del desarrollador oficial");
        add("Arriba a la izquierda", "Top left", "Superior esquerdo");
        add("Abajo a la izquierda (original)", "Bottom left (original)", "Inferior esquerdo (original)");
        add("Sincronía CPU/GPU", "CPU/GPU sync", "Sincronia CPU/GPU");
        add("Rendimiento · CPU y GPU en paralelo", "Performance · CPU and GPU in parallel", "Desempenho · CPU e GPU em paralelo");
        add("Baja latencia · menos FPS", "Low latency · fewer FPS", "Baixa latência · menos FPS");
        add("Texturas comprimidas", "Compressed textures", "Texturas comprimidas");
        add("Automático · GPU si las soporta", "Automatic · GPU if supported", "Automático · GPU se suportar");
        add("Convertir en CPU · compatibilidad", "Convert on CPU · compatibility", "Converter na CPU · compatibilidade");
        add("Mando Bluetooth/USB", "Bluetooth/USB gamepad", "Controle Bluetooth/USB");
        add("Como teclado · compatible", "As keyboard · compatible", "Como teclado · compatível");
        add("Joystick DirectInput · analógico, experimental", "DirectInput joystick · analog, experimental", "Joystick DirectInput · analógico, experimental");

        // In-game dialogs and touch controls (GameActivity, RacingControlsView).
        add("Este dispositivo no dispone de acelerómetro", "This device has no accelerometer", "Este dispositivo não tem acelerômetro");
        add("Girar inclinando el celular", "Steer by tilting the phone", "Virar inclinando o celular");
        add("Invertir dirección", "Invert direction", "Inverter direção");
        add("Sensibilidad: %s×", "Sensitivity: %s×", "Sensibilidade: %s×");
        add("Sostén el celular en tu posición de juego al aplicar. La inclinación activa izquierda/derecha; los pedales siguen siendo táctiles.",
                "Hold the phone in your playing position when applying. Tilting steers left/right; the pedals stay on the touch screen.",
                "Segure o celular na sua posição de jogo ao aplicar. A inclinação vira para a esquerda/direita; os pedais continuam na tela.");
        add("Conducción por inclinación", "Tilt steering", "Direção por inclinação");
        add("Aplicar y calibrar", "Apply and calibrate", "Aplicar e calibrar");
        add("Nombre, correo o contraseña", "Name, e-mail or password", "Nome, e-mail ou senha");
        add("Reemplazar el nombre actual", "Replace the current name", "Substituir o nome atual");
        add("Escribir en el juego", "Type in the game", "Escrever no jogo");
        add("Escribir", "Type", "Escrever");
        add("Hasta 40 caracteres: letras sin tildes, números y . , - _ @ + ! # $ % & * ( ) = ? / ; :",
                "Up to 40 characters: letters without accents, digits and . , - _ @ + ! # $ % & * ( ) = ? / ; :",
                "Até 40 caracteres: letras sem acentos, números e . , - _ @ + ! # $ % & * ( ) = ? / ; :");
        add("Permite acceso a archivos para leer la carpeta nfsmw de la memoria interna",
                "Allow file access to read the nfsmw folder in internal storage",
                "Permita o acesso aos arquivos para ler a pasta nfsmw do armazenamento interno");
        add("Controles del juego", "Game controls", "Controles do jogo");
        add("RESTABLECER", "RESET", "REDEFINIR");
        add("GUARDAR", "SAVE", "SALVAR");
        add("CANCELAR", "CANCEL", "CANCELAR");
        add("Opciones de controles", "Control options", "Opções de controles");
        add("Estilo: clásico / Xbox / PlayStation", "Style: classic / Xbox / PlayStation", "Estilo: clássico / Xbox / PlayStation");
        add("Guardar layout con nombre", "Save layout with a name", "Salvar layout com nome");
        add("Cargar layout personalizado", "Load custom layout", "Carregar layout personalizado");
        add("Estilo de conducción", "Driving style", "Estilo de direção");
        add("Clásico · flechas y pedales", "Classic · arrows and pedals", "Clássico · setas e pedais");
        add("Xbox · volante y A / B / X / Y", "Xbox · wheel and A / B / X / Y", "Xbox · volante e A / B / X / Y");
        add("PlayStation · volante y × / ○ / □ / △", "PlayStation · wheel and × / ○ / □ / △", "PlayStation · volante e × / ○ / □ / △");
        add("Nombre del layout", "Layout name", "Nome do layout");
        add("Guardar diseño de conducción", "Save driving layout", "Salvar layout de direção");
        add("Guardar diseño de menú", "Save menu layout", "Salvar layout de menu");
        add("Escribe un nombre", "Enter a name", "Digite um nome");
        add("Layout guardado: %s", "Layout saved: %s", "Layout salvo: %s");
        add("No se pudo guardar el layout", "The layout could not be saved", "Não foi possível salvar o layout");
        add("Todavía no hay layouts guardados para este modo", "No layouts saved for this mode yet", "Ainda não há layouts salvos para este modo");
        add("Cargar layout", "Load layout", "Carregar layout");
        add("No se pudo cargar el layout", "The layout could not be loaded", "Não foi possível carregar o layout");
    }
}
