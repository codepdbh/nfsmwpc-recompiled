package com.nfsmw.androidevolved;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Launcher in the style of the MW and Carbon ports: game files and JUGAR on the left, options on the
 * right (Vulkan driver, resolution, frame cap, widescreen, compatibility).
 */
public final class LauncherActivity extends Activity {
    private static final int REQUEST_DRIVER_ZIP = 45, REQUEST_DRIVER_PROBE = 46;
    // Launcher accent: Underground orange/red (the NFSU2 port is green).
    private static final int ACCENT = 0xFFFF7A1A;
    /** {label, guest language name, LANGUAGES/<file>.bin}. */
    private static final String[][] LANGUAGES = {
        {"Español", "Spanish", "Spanish"}, {"English", "English", "English"},
        {"Français", "French", "French"}, {"Deutsch", "German", "German"},
        {"Italiano", "Italian", "Italian"}, {"Nederlands", "Dutch", "Dutch"},
        {"Svenska", "Swedish", "Swedish"}, {"Dansk", "Danish", "Danish"},
        {"Suomi", "Finnish", "Finnish"}, {"Polski", "Polish", "Polish"},
        {"한국어", "Korean", "Korean"}, {"中文", "Chinese", "Chinese"}
    };

    private LinearLayout checksList, optionsList;
    private TextView status;
    private Button play, access;
    private boolean playAfterProbe, checkingUpdates, resumed;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        setContentView(buildScreen());
        // First launch: the three flags come before any option.
        if (!Lang.chosen(this)) showLanguagePicker();
        checkUpdates(false);
        String updated = ReleaseUpdates.pendingWhatsNew(this);
        if (updated != null) showWhatsNew(updated);
    }

    /** First launch after an update: the installed version's release notes from GitHub. */
    private void showWhatsNew(String version) {
        AlertDialog dialog = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(tr("¡Actualizado a la versión %s!", version))
                .setMessage(tr("Cargando las novedades…"))
                .setPositiveButton(tr("Aceptar"), (d, which) -> ReleaseUpdates.whatsNewSeen(this))
                .setNeutralButton(tr("Ver en GitHub"), (d, which) -> {
                    ReleaseUpdates.whatsNewSeen(this);
                    openUrl(Uri.parse("https://github.com/codepdbh/nfsmwpc-recompiled/releases/tag/v" + Uri.encode(version)));
                })
                .setOnCancelListener(d -> ReleaseUpdates.whatsNewSeen(this))
                .show();
        ReleaseUpdates.releaseNotes(this, version, notes -> {
            if (!dialog.isShowing()) return;
            // In the app, skip the download/install preamble of the release page.
            int start = notes == null ? -1 : notes.indexOf("Novedades");
            if (start > 0) notes = notes.substring(start);
            dialog.setMessage(notes != null ? notes
                    : tr("No se pudieron cargar las novedades (sin conexión o aún no publicadas). Puedes verlas en GitHub."));
        });
    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        refresh();
        String crash = Diagnostics.unreportedCrash(this);
        if (crash != null) {
            Diagnostics.acknowledgeCrashes(this);
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle(tr("El juego se cerró")).setMessage(tr("%s.\n\n¿Quieres enviar el registro completo a GitHub para que se pueda corregir?", crash))
                    .setPositiveButton(tr("Reportar en GitHub"), (dialog, which) -> prepareReport(crash))
                    .setNegativeButton(tr("Ahora no"), null).show();
        }
    }

    @Override protected void onPause() {
        resumed = false;
        super.onPause();
    }

    // ---- Screen ------------------------------------------------------------------------------------------

    private View buildScreen() {
        FrameLayout screen = new FrameLayout(this);
        screen.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[] {0xFF0E0604, 0xFF2E0B05, 0xFF0E0604}));
        LinearLayout columns = new LinearLayout(this);
        columns.setPadding(dp(24), dp(10), dp(24), dp(10));
        screen.addView(columns, new FrameLayout.LayoutParams(-1, -1));

        // Left: logo, game files and JUGAR.
        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setGravity(Gravity.CENTER_HORIZONTAL);
        ScrollView leftScroll = new ScrollView(this);
        leftScroll.setFillViewport(true);
        leftScroll.addView(left);
        columns.addView(leftScroll, new LinearLayout.LayoutParams(0, -1, 1.1f));

        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.icono);
        logo.setAdjustViewBounds(true);
        header.addView(logo, new LinearLayout.LayoutParams(dp(76), dp(76)));
        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.setPadding(dp(14), 0, 0, 0);
        titles.addView(label("Need for Speed Most Wanted", 18, Color.WHITE, true));
        titles.addView(label(tr("PC · recompilado de forma nativa para Android"), 12, 0x99FFFFFF, false));
        header.addView(titles);
        left.addView(header, new LinearLayout.LayoutParams(-1, -2));

        LinearLayout card = card();
        LinearLayout.LayoutParams cardParams = new LinearLayout.LayoutParams(-1, -2);
        cardParams.topMargin = dp(8);
        left.addView(card, cardParams);
        card.addView(label(tr("ARCHIVOS DEL JUEGO"), 12, ACCENT, true));
        status = label("", 13, 0xDDFFFFFF, false);
        status.setPadding(0, dp(4), 0, dp(6));
        card.addView(status);
        checksList = new LinearLayout(this);
        checksList.setOrientation(LinearLayout.VERTICAL);
        card.addView(checksList);

        play = actionButton(tr("JUGAR"), true);
        play.setOnClickListener(view -> play());
        LinearLayout.LayoutParams playParams = new LinearLayout.LayoutParams(-1, dp(54));
        playParams.topMargin = dp(8);
        left.addView(play, playParams);

        access = actionButton(tr("Permitir acceso a memoria interna/nfsmw"), false);
        access.setOnClickListener(view -> requestAccess());
        LinearLayout.LayoutParams accessParams = new LinearLayout.LayoutParams(-1, dp(46));
        accessParams.topMargin = dp(10);
        left.addView(access, accessParams);

        Button channel = actionButton(tr("Canal del desarrollador oficial"), false);
        channel.setOnClickListener(view -> openChannel());
        LinearLayout.LayoutParams channelParams = new LinearLayout.LayoutParams(-1, dp(46));
        channelParams.topMargin = dp(10);
        left.addView(channel, channelParams);

        // Right: options.
        LinearLayout right = card();
        LinearLayout.LayoutParams rightParams = new LinearLayout.LayoutParams(0, -1, 1f);
        rightParams.leftMargin = dp(24);
        columns.addView(right, rightParams);
        right.addView(label(tr("OPCIONES"), 12, ACCENT, true));
        TextView note = label(tr("Se aplican al pulsar JUGAR."), 12, 0x88FFFFFF, false);
        note.setPadding(0, dp(2), 0, dp(4));
        right.addView(note);
        ScrollView scroll = new ScrollView(this);
        optionsList = new LinearLayout(this);
        optionsList.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(optionsList);
        right.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1f));
        return screen;
    }

    private void refresh() {
        boolean allowed = allowed();
        File root = gameRoot();
        checksList.removeAllViews();
        boolean exe = allowed && new File(root, "Speed.exe").isFile();
        addCheck("Speed.exe", exe, true);
        StringBuilder missing = new StringBuilder();
        for (String folder : new String[] {"GLOBAL", "TRACKS", "CARS", "FRONTEND", "LANGUAGES"})
            if (!(allowed && new File(root, folder).isDirectory())) missing.append(missing.length() == 0 ? "" : ", ").append(folder);
        addCheck(missing.length() == 0 ? tr("Carpetas del juego") : tr("Faltan carpetas: %s", missing), missing.length() == 0, true);
        boolean language = allowed && !languages().isEmpty();
        if (exe && !language) addCheck(tr("Falta LANGUAGES/<idioma>.bin"), false, true);
        boolean ready = exe && language;
        if (allowed && !root.isDirectory()) root.mkdirs();  // Show the player where the files go.
        String nested = allowed && !exe ? nestedGameFolder(root) : null;
        status.setText(!allowed ? tr("Permite el acceso a archivos para leer memoria interna/nfsmw.")
                : ready ? tr("Memoria interna/nfsmw")
                : nested != null ? tr("Los archivos están en «%s»: mueve su contenido directamente a memoria interna/nfsmw.", nested)
                : tr("Copia los archivos de tu juego (Speed.exe y sus carpetas) dentro de:\n%s", root.getAbsolutePath()));
        play.setEnabled(ready);
        play.setAlpha(ready ? 1f : .45f);
        access.setVisibility(allowed ? View.GONE : View.VISIBLE);
        refreshOptions();
    }

    private void refreshOptions() {
        optionsList.removeAllViews();
        GpuDrivers.Driver driver = GpuDrivers.selected(this);
        // Custom drivers (libadrenotools) exist only in the 64-bit build.
        if (android.os.Process.is64Bit())
            optionsList.addView(optionRow(tr("Driver Vulkan"), driver == null ? tr("Del sistema") : driver.name, this::chooseDriver));
        optionsList.addView(optionRow(tr("Probar driver"), tr("GPU y funciones Vulkan"), () -> probeDriver(false)));
        optionsList.addView(optionRow(tr("Idioma del launcher"), Lang.name(this), this::showLanguagePicker));
        optionsList.addView(optionRow(tr("Idioma del juego"), languageLabel(), this::chooseLanguage));
        for (GameOptions.Option option : GameOptions.ALL)
            optionsList.addView(optionRow(tr(option.title), tr(option.label(GameOptions.get(this, option.key))), () -> chooseOption(option)));
        optionsList.addView(optionRow(tr("Servidor online"), GameOptions.onlineServer(this), this::chooseOnlineServer));
        optionsList.addView(optionRow(tr("Buscar actualizaciones"),
                checkingUpdates ? tr("Comprobando GitHub…") : tr("Versión %s", ReleaseUpdates.installed(this)), () -> checkUpdates(true)));
        optionsList.addView(optionRow(tr("Reportar un problema"), tr("Log completo a GitHub"), () -> prepareReport(null)));
    }

    // ---- Options ---------------------------------------------------------------------------------------

    /** A copy one folder too deep (nfsmw/<game folder>/Speed.exe), a common mistake when copying. */
    private static String nestedGameFolder(File root) {
        File[] children = root.listFiles();
        if (children == null) return null;
        for (File child : children)
            if (child.isDirectory() && (new File(child, "Speed.exe").isFile() || new File(child, "speed.exe").isFile()))
                return "nfsmw/" + child.getName();
        return null;
    }

    private String tr(String spanish, Object... args) { return Lang.t(this, spanish, args); }

    /** Three flags (Spanish, English, Portuguese): the launcher texts follow the choice. */
    private void showLanguagePicker() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER_HORIZONTAL);
        box.setPadding(dp(20), dp(16), dp(20), dp(12));
        box.addView(label("Elige tu idioma · Choose your language · Escolha seu idioma", 15, Color.WHITE, true));
        LinearLayout flags = new LinearLayout(this);
        flags.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams flagsParams = new LinearLayout.LayoutParams(-1, -2);
        flagsParams.topMargin = dp(14);
        box.addView(flags, flagsParams);
        TextView hint = label(tr("Puedes cambiarlo después en Opciones."), 12, 0x99FFFFFF, false);
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(0, dp(12), 0, 0);
        box.addView(hint, new LinearLayout.LayoutParams(-1, -2));
        AlertDialog dialog = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setView(box).setCancelable(Lang.chosen(this)).create();
        for (String[] choice : Lang.CHOICES) {
            LinearLayout flag = new LinearLayout(this);
            flag.setOrientation(LinearLayout.VERTICAL);
            flag.setGravity(Gravity.CENTER);
            flag.setPadding(dp(10), dp(10), dp(10), dp(10));
            GradientDrawable bg = new GradientDrawable();
            boolean current = Lang.chosen(this) && Lang.code(this).equals(choice[0]);
            bg.setColor(current ? 0x44FF7A1A : 0x16FFFFFF);
            bg.setStroke(dp(current ? 2 : 1), current ? ACCENT : 0x44FFFFFF);
            bg.setCornerRadius(dp(14));
            flag.setBackground(bg);
            TextView emoji = label(choice[1], 46, Color.WHITE, false);
            emoji.setGravity(Gravity.CENTER);
            flag.addView(emoji);
            TextView name = label(choice[2], 15, Color.WHITE, true);
            name.setGravity(Gravity.CENTER);
            flag.addView(name);
            flag.setClickable(true);
            flag.setContentDescription(choice[2]);
            flag.setOnClickListener(v -> {
                Lang.set(this, choice[0]);
                dialog.dismiss();
                setContentView(buildScreen());
                refresh();
            });
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, -2, 1f);
            params.leftMargin = params.rightMargin = dp(6);
            flags.addView(flag, params);
        }
        dialog.show();
    }

    private List<String[]> languages() {
        List<String[]> found = new ArrayList<>();
        File root = gameRoot();
        for (String[] language : LANGUAGES)
            if (new File(root, "LANGUAGES/" + language[2] + ".bin").isFile()) found.add(language);
        return found;
    }

    private String languageLabel() {
        String current = GameOptions.language(this);
        for (String[] language : LANGUAGES) if (language[1].equals(current)) return language[0];
        return current;
    }

    private void chooseLanguage() {
        List<String[]> found = allowed() ? languages() : new ArrayList<>();
        if (found.isEmpty()) { message(tr("Idioma del juego"), tr("No se encontraron idiomas en memoria interna/nfsmw/LANGUAGES.")); return; }
        String[] names = new String[found.size()];
        int checked = 0;
        for (int i = 0; i < names.length; i++) {
            names[i] = found.get(i)[0];
            if (found.get(i)[1].equals(GameOptions.language(this))) checked = i;
        }
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(tr("Idioma del juego"))
                .setSingleChoiceItems(names, checked, (dialog, which) -> {
                    GameOptions.prefs(this).edit().putString(GameOptions.LANGUAGE, found.get(which)[1]).apply();
                    dialog.dismiss(); refreshOptions();
                }).setNegativeButton(tr("Cancelar"), null).show();
    }

    private void chooseOption(GameOptions.Option option) {
        String current = GameOptions.get(this, option.key);
        int checked = 0;
        for (int i = 0; i < option.values.length; i++) if (option.values[i].equals(current)) checked = i;
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(tr(option.title))
                .setSingleChoiceItems(Lang.t(this, option.labels), checked, (dialog, which) -> {
                    GameOptions.set(this, option.key, option.values[which]);
                    dialog.dismiss(); refreshOptions();
                }).setNegativeButton(tr("Cancelar"), null).show();
    }

    private void chooseOnlineServer() {
        android.widget.EditText input = new android.widget.EditText(this);
        input.setSingleLine(true);
        input.setText(GameOptions.onlineServer(this));
        input.setSelection(input.getText().length());
        input.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_URI);
        android.widget.FrameLayout box = new android.widget.FrameLayout(this);
        int pad = (int) (20 * getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad / 2, pad, 0);
        box.addView(input);
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(tr("Servidor online"))
                .setMessage(tr("El modo online del juego (EA Nation) se conecta a este servidor de la comunidad en lugar de los servidores de EA, ya cerrados."))
                .setView(box)
                .setPositiveButton(tr("Guardar"), (dialog, which) -> {
                    String value = input.getText().toString().trim();
                    if (!GameOptions.validServer(value)) { message(tr("Servidor online"), tr("\"%s\" no es un nombre de servidor válido.", value)); return; }
                    GameOptions.prefs(this).edit().putString(GameOptions.ONLINE_SERVER, value).apply();
                    refreshOptions();
                })
                .setNeutralButton(tr("Restaurar"), (dialog, which) -> {
                    GameOptions.prefs(this).edit().remove(GameOptions.ONLINE_SERVER).apply();
                    refreshOptions();
                })
                .setNegativeButton(tr("Cancelar"), null).show();
    }

    private void chooseDriver() {
        List<GpuDrivers.Driver> drivers = GpuDrivers.list(this);
        String[] names = new String[drivers.size() + 1];
        names[0] = tr("Driver del sistema");
        int selected = 0;
        for (int i = 0; i < drivers.size(); ++i) {
            names[i + 1] = drivers.get(i).name;
            if (drivers.get(i).id.equals(GpuDrivers.selectedId(this))) selected = i + 1;
        }
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(tr("Driver Vulkan"))
                .setSingleChoiceItems(names, selected, (dialog, which) -> {
                    GpuDrivers.select(this, which == 0 ? "" : drivers.get(which - 1).id);
                    refreshOptions(); dialog.dismiss();
                })
                .setPositiveButton(tr("Importar ZIP"), (dialog, which) -> startActivityForResult(
                        new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE),
                        REQUEST_DRIVER_ZIP))
                .setNeutralButton(tr("Eliminar seleccionado"), (dialog, which) -> {
                    GpuDrivers.Driver driver = GpuDrivers.selected(this);
                    if (driver == null) return;
                    try { GpuDrivers.remove(this, driver); refreshOptions(); }
                    catch (java.io.IOException error) { message(tr("Driver Vulkan"), error.getMessage()); }
                })
                .setNegativeButton(tr("Volver"), null).show();
    }

    private void probeDriver(boolean thenPlay) {
        playAfterProbe = thenPlay;
        play.setEnabled(false);
        startActivityForResult(new Intent(this, GpuProbeActivity.class), REQUEST_DRIVER_PROBE);
    }

    private void showDriverReport(String report) {
        play.setEnabled(true);
        try {
            org.json.JSONObject gpu = new org.json.JSONObject(report);
            boolean failed = gpu.optBoolean("driverLoadFailed") || gpu.has("probeError");
            if (playAfterProbe && !failed && gpu.optBoolean("compatible")) { playAfterProbe = false; launch(); return; }
            // The player pressed PLAY: a driver that loads but lacks a feature the
            // probe checks may still run the game (issue #1107), so offer it.
            boolean wantedToPlay = playAfterProbe && !failed;
            playAfterProbe = false;
            String text = failed ? tr("No se pudo usar el driver: %s", gpu.optString("probeError", tr("error al iniciar Vulkan.")))
                    : "GPU: " + gpu.optString("gpu") + "\nVulkan: " + gpu.optString("vulkan")
                      + "\nDriver: " + gpu.optString("driverName", "—") + " " + gpu.optString("driverInfo", "")
                      + "\n" + tr("Texturas BC: ") + (gpu.optBoolean("bcTextures") ? tr("en la GPU") : tr("se convierten en la CPU"))
                      + (gpu.optString("vertexFormatsMissing").isEmpty() ? ""
                              : "\n" + tr("Tipos de vértice sin soporte: ") + gpu.optString("vertexFormatsMissing"))
                      + "\n" + (gpu.optBoolean("compatible") ? tr("Compatible con el juego.")
                              : tr("Faltan funciones: ") + gpu.optJSONArray("missing"));
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle(tr("Prueba del driver Vulkan")).setMessage(text).setPositiveButton(tr("Aceptar"), null)
                    .setNeutralButton(tr("Usar driver del sistema"), (dialog, which) -> { GpuDrivers.select(this, ""); refreshOptions(); })
                    .setNegativeButton(wantedToPlay ? tr("Jugar igualmente") : null, wantedToPlay ? (dialog, which) -> launch() : null)
                    .show();
        } catch (Exception error) { message(tr("Prueba del driver Vulkan"), tr("Diagnóstico no válido: ") + error.getMessage()); }
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_DRIVER_PROBE) {
            showDriverReport(resultCode == RESULT_OK && data != null ? data.getStringExtra("gpu_report")
                    : "{\"probeError\":\"" + tr("La prueba se cerró o fue cancelada.") + "\",\"driverLoadFailed\":true}");
        } else if (requestCode == REQUEST_DRIVER_ZIP && resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri zip = data.getData();
            status.setText(tr("Importando driver Vulkan…"));
            worker.execute(() -> {
                try {
                    GpuDrivers.Driver driver = GpuDrivers.importZip(this, zip);
                    main.post(() -> {
                        GpuDrivers.select(this, driver.id); refresh();
                        status.setText(tr("Driver importado: %s. Usa Probar driver para comprobarlo.", driver.name));
                    });
                } catch (Exception error) {
                    main.post(() -> { refresh(); message(tr("Driver Vulkan"), tr("No se pudo importar el driver: ") + error.getMessage()); });
                }
            });
        }
    }

    // ---- Updates and reports ---------------------------------------------------------------------------

    private void checkUpdates(boolean manual) {
        if (checkingUpdates) return;
        checkingUpdates = true;
        if (optionsList != null) refreshOptions();
        ReleaseUpdates.check(this, manual, result -> {
            checkingUpdates = false;
            if (isFinishing() || isDestroyed()) return;
            refreshOptions();
            if (result.error != null) { if (manual) message(tr("Actualizaciones"), result.error); return; }
            if (!ReleaseVersion.newer(result.tag, ReleaseUpdates.installed(this))) {
                if (manual) message(tr("Actualizaciones"), tr("Tienes la versión %s. No hay una versión más reciente en GitHub.", ReleaseUpdates.installed(this)));
                return;
            }
            android.content.SharedPreferences updates = ReleaseUpdates.prefs(this);
            if (!manual && result.tag.equals(updates.getString("notified_tag", ""))) return;
            updates.edit().putString("notified_tag", result.tag).apply();
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle(tr("Nueva versión disponible: %s", result.tag))
                    .setMessage(tr("Puedes descargarla desde GitHub o seguir jugando con esta versión. Instala el APK encima del actual para conservar tus partidas y ajustes."))
                    .setPositiveButton(tr("Actualizar"), (dialog, which) -> openUrl(Uri.parse(result.url)))
                    .setNegativeButton(tr("Más tarde"), null).show();
        });
    }

    /**
     * Builds the report ZIP, saves it to Download/NFSMW and opens a pre-filled GitHub issue. GitHub
     * cannot take an attachment through the link, so the issue already carries a technical summary
     * and the player is told where the ZIP is to attach it.
     */
    private void prepareReport(String crash) {
        status.setText(tr("Preparando el registro…"));
        worker.execute(() -> {
            try {
                File report = Diagnostics.createReport(this);
                String location = Diagnostics.saveToDownloads(this, report);
                Uri issue = Diagnostics.issueUri(this, crash, location);
                main.post(() -> {
                    refresh();
                    new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                            .setTitle(tr("Reportar en GitHub"))
                            .setMessage(tr("El registro completo se ha guardado en:\n%s\n\nSe abrirá la issue en GitHub con un resumen técnico ya escrito. Describe qué pasó y, si puedes, adjunta ese ZIP (GitHub no permite adjuntarlo automáticamente).", location))
                            .setPositiveButton(tr("Abrir GitHub"), (dialog, which) -> openUrl(issue))
                            .setNegativeButton(tr("Cancelar"), null).show();
                });
            } catch (Exception error) {
                main.post(() -> { refresh(); message(tr("Reportar un problema"), tr("No se pudo preparar el registro: ") + error.getMessage()); });
            }
        });
    }

    private void openUrl(Uri uri) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, uri)); }
        catch (android.content.ActivityNotFoundException error) { message("GitHub", tr("No hay un navegador para abrir %s", uri)); }
    }

    // ---- Launch ----------------------------------------------------------------------------------------

    private void play() {
        if (android.os.Process.is64Bit() && GpuDrivers.selected(this) != null) probeDriver(true);
        else launch();
    }

    private void launch() {
        // Each launch uses a fresh guest machine in the dedicated :game process.
        android.app.ActivityManager manager = (android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE);
        List<android.app.ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
        if (processes != null) for (android.app.ActivityManager.RunningAppProcessInfo process : processes)
            if (process.uid == android.os.Process.myUid() && process.processName.equals(getPackageName() + ":game"))
                android.os.Process.killProcess(process.pid);
        DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        int[] size = GameOptions.renderSize(this, Math.max(metrics.widthPixels, metrics.heightPixels),
                Math.min(metrics.widthPixels, metrics.heightPixels));
        String[] loader = GpuDrivers.loaderArguments(this);
        startActivity(new Intent(this, GameActivity.class)
                .putExtra("language", GameOptions.language(this))
                .putExtra("renderWidth", size[0]).putExtra("renderHeight", size[1])
                .putExtra("frameCap", Integer.parseInt(GameOptions.get(this, GameOptions.FPS)))
                .putExtra("widescreen", GameOptions.get(this, GameOptions.WIDESCREEN).equals("on"))
                .putExtra("minimapTop", GameOptions.get(this, GameOptions.MINIMAP).equals("top"))
                .putExtra("backBuffers", Integer.parseInt(GameOptions.get(this, GameOptions.LATENCY)))
                .putExtra("cpuTextures", GameOptions.get(this, GameOptions.TEXTURES).equals("cpu"))
                .putExtra("padJoystick", GameOptions.get(this, GameOptions.GAMEPAD).equals("directinput"))
                .putExtra("onlineServer", GameOptions.onlineServer(this))
                .putExtra("driverLoader", loader));
    }

    // ---- Storage ---------------------------------------------------------------------------------------

    private static File gameRoot() { return new File(Environment.getExternalStorageDirectory(), "nfsmw"); }

    private boolean allowed() {
        return Build.VERSION.SDK_INT >= 30 ? Environment.isExternalStorageManager()
            : checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestAccess() {
        if (Build.VERSION.SDK_INT >= 30)
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        else requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
    }

    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        refresh();
    }

    @Override protected void onDestroy() {
        worker.shutdown();
        super.onDestroy();
    }

    // ---- Widgets ---------------------------------------------------------------------------------------

    private void addCheck(String name, boolean ok, boolean required) {
        TextView row = label((ok ? "✓  " : required ? "✗  " : "!  ") + name + (ok || required ? "" : tr(" · opcional")), 13,
                ok ? 0xFF7CD992 : required ? 0xFFFF6B6B : 0xFFFFC857, false);
        row.setPadding(0, dp(2), 0, dp(2));
        checksList.addView(row);
    }

    private View optionRow(String title, String value, Runnable onClick) {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(12), dp(8), dp(12), dp(8));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0x16FF7A1A);
        bg.setCornerRadius(dp(10));
        row.setBackground(bg);
        row.setClickable(true);
        row.setOnClickListener(v -> onClick.run());
        row.addView(label(title, 14, Color.WHITE, false), new LinearLayout.LayoutParams(0, -2, 1f));
        TextView current = label(value + "  ›", 13, ACCENT, true);
        current.setGravity(Gravity.END);
        row.addView(current, new LinearLayout.LayoutParams(0, -2, 1f));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = dp(5);
        row.setLayoutParams(lp);
        return row;
    }

    private LinearLayout card() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(16), dp(12), dp(16), dp(12));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xCC1C0D08);
        bg.setCornerRadius(dp(16));
        bg.setStroke(dp(1), 0x44FF7A1A);
        card.setBackground(bg);
        return card;
    }

    // The ports' official developer channel.
    static final String CHANNEL = "https://www.youtube.com/@daniel54bhp";
    private void openChannel() {
        try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(CHANNEL))); }
        catch (android.content.ActivityNotFoundException e) { android.widget.Toast.makeText(this, CHANNEL, android.widget.Toast.LENGTH_LONG).show(); }
    }

    private Button actionButton(String text, boolean primary) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(primary ? 0xFF1F0802 : Color.WHITE);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, primary ? 22 : 15);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setLetterSpacing(primary ? .12f : 0f);
        GradientDrawable bg = primary
                ? new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[] {0xFFFFB13D, 0xFFE3261B})
                : new GradientDrawable();
        if (!primary) { bg.setColor(0x22FFFFFF); bg.setStroke(dp(1), 0x55FFFFFF); }
        bg.setCornerRadius(dp(14));
        b.setBackground(bg);
        b.setStateListAnimator(null);
        return b;
    }

    private TextView label(String text, int sp, int color, boolean bold) {
        if (text == null) text = "";
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        if (bold) { t.setTypeface(Typeface.DEFAULT_BOLD); t.setLetterSpacing(.06f); }
        return t;
    }

    private void message(String title, String text) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(title).setMessage(text).setPositiveButton(tr("Aceptar"), null).show();
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
}
