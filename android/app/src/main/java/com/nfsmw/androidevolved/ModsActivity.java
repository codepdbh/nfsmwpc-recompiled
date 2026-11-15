package com.nfsmw.androidevolved;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.Manifest;
import android.content.pm.PackageManager;
import android.provider.Settings;
import android.os.Environment;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Companion Mods Manager. Mods are installed as read-only mirrored game folders. */
public final class ModsActivity extends Activity {
    private static final int PICK_ARCHIVE = 71;
    private static final long MAX_EXPANDED_BYTES = 1024L * 1024L * 1024L;
    private static final int MAX_ENTRIES = 12000;
    private LinearLayout list;
    private TextView summary;
    private Button add;
    private volatile boolean importing;

    private boolean storageAllowed() {
        return Build.VERSION.SDK_INT >= 30 ? Environment.isExternalStorageManager()
                : checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestStorageAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } else {
            requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE}, 72);
        }
    }

    @Override protected void onResume() {
        super.onResume();
        if (list != null) refresh();
    }

    private File gameRoot() { return new File(Environment.getExternalStorageDirectory(), "nfsmw"); }
    private File modsRoot() { return new File(gameRoot(), "modloader"); }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN);
        setContentView(buildScreen());
        refresh();
    }

    private LinearLayout buildScreen() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(22), dp(16), dp(22), dp(16));
        root.setBackgroundColor(0xFF100906);

        TextView title = text("MODS MANAGER", 23, 0xFFFF8A28, true);
        root.addView(title);
        TextView subtitle = text("PC version · Android companion", 13, 0x99FFFFFF, false);
        root.addView(subtitle);

        Button storage = button("ALLOW GAME FOLDER ACCESS", false);
        storage.setOnClickListener(v -> requestStorageAccess());
        LinearLayout.LayoutParams storageParams = new LinearLayout.LayoutParams(-1, dp(42));
        storageParams.topMargin = dp(10);
        root.addView(storage, storageParams);

        summary = text("", 13, 0xDDFFFFFF, false);
        summary.setPadding(0, dp(12), 0, dp(10));
        root.addView(summary);

        add = button("IMPORT ZIP", true);
        add.setOnClickListener(v -> chooseArchive());
        root.addView(add, new LinearLayout.LayoutParams(-1, dp(48)));

        TextView hint = text("Each mod mirrors the game's folders (CARS, GLOBAL, TRACKS, FRONTEND…). Disabled mods are kept but not loaded. Changes apply after restarting the game.", 12, 0x99FFFFFF, false);
        hint.setPadding(0, dp(10), 0, dp(12));
        root.addView(hint);

        ScrollView scroll = new ScrollView(this);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1f));

        Button back = button("BACK TO GAME LAUNCHER", false);
        back.setOnClickListener(v -> finish());
        LinearLayout.LayoutParams backParams = new LinearLayout.LayoutParams(-1, dp(46));
        backParams.topMargin = dp(10);
        root.addView(back, backParams);
        return root;
    }

    private void refresh() {
        list.removeAllViews();
        File folder = modsRoot();
        if (!storageAllowed()) {
            summary.setText("Storage access is required to manage nfsmw/modloader.");
            add.setEnabled(false);
            return;
        }
        if (!folder.isDirectory() && !folder.mkdirs()) {
            summary.setText("Cannot create modloader folder:\n" + folder);
            add.setEnabled(false);
            return;
        }
        File[] children = folder.listFiles(file -> file.isDirectory() && !java.nio.file.Files.isSymbolicLink(file.toPath()));
        List<File> mods = new ArrayList<>();
        if (children != null) for (File child : children)
            if ((!child.getName().startsWith(".") || child.getName().length() > 1) && !child.getName().startsWith(".import-") && !java.nio.file.Files.isSymbolicLink(child.toPath())) mods.add(child);
        Collections.sort(mods, Comparator.comparing(f -> f.getName().toLowerCase(Locale.ROOT)));
        int enabled = 0;
        for (File mod : mods) if (!mod.getName().startsWith(".")) enabled++;
        summary.setText(enabled + " enabled · " + (mods.size() - enabled) + " disabled\n" + folder.getAbsolutePath());
        add.setEnabled(!importing);
        if (mods.isEmpty()) {
            TextView empty = text("No mods installed. Import a ZIP archive to begin.", 15, 0xBBFFFFFF, false);
            empty.setGravity(Gravity.CENTER);
            empty.setPadding(0, dp(30), 0, dp(30));
            list.addView(empty);
        }
        for (File mod : mods) addModRow(mod);
    }

    private void addModRow(File mod) {
        boolean disabled = mod.getName().startsWith(".");
        String visibleName = disabled ? mod.getName().substring(1) : mod.getName();
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(dp(12), dp(10), dp(12), dp(10));
        row.setBackgroundColor(0xFF24140E);
        LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(-1, -2);
        rowParams.topMargin = dp(6);
        list.addView(row, rowParams);

        LinearLayout top = new LinearLayout(this);
        top.setGravity(Gravity.CENTER_VERTICAL);
        TextView name = text(visibleName, 15, disabled ? 0xFFAAAAAA : 0xFFFFFFFF, true);
        top.addView(name, new LinearLayout.LayoutParams(0, -2, 1f));
        TextView state = text(disabled ? "DISABLED" : "ENABLED", 11,
                disabled ? 0xFFAAAAAA : 0xFF83D99A, true);
        top.addView(state);
        row.addView(top);

        TextView path = text(relativeFileCount(mod) + " files · " + mod.getName(), 11, 0x88FFFFFF, false);
        path.setPadding(0, dp(4), 0, dp(8));
        row.addView(path);

        LinearLayout actions = new LinearLayout(this);
        actions.setGravity(Gravity.END);
        Button toggle = smallButton(disabled ? "ENABLE" : "DISABLE");
        toggle.setOnClickListener(v -> renameMod(mod, disabled));
        actions.addView(toggle);
        Button remove = smallButton("DELETE");
        remove.setOnClickListener(v -> confirmDelete(mod));
        LinearLayout.LayoutParams removeParams = new LinearLayout.LayoutParams(-2, dp(38));
        removeParams.leftMargin = dp(8);
        actions.addView(remove, removeParams);
        row.addView(actions);
    }

    private int relativeFileCount(File root) {
        File[] children = root.listFiles();
        if (children == null) return 0;
        int count = 0;
        for (File child : children) count += child.isDirectory() ? relativeFileCount(child) : 1;
        return count;
    }

    private void renameMod(File mod, boolean currentlyDisabled) {
        String name = mod.getName();
        String targetName = currentlyDisabled ? name.substring(1) : "." + name;
        File target = new File(modsRoot(), targetName);
        if (target.exists() || !mod.renameTo(target)) {
            toast("Could not change mod state");
            return;
        }
        refresh();
    }

    private void confirmDelete(File mod) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Delete " + mod.getName() + "?")
                .setMessage("This permanently removes the mod folder. The original game files are not affected.")
                .setNegativeButton("CANCEL", null)
                .setPositiveButton("DELETE", (d, w) -> {
                    if (!deleteTree(mod)) toast("Could not delete mod folder");
                    refresh();
                }).show();
    }

    private boolean deleteTree(File file) {
        if (java.nio.file.Files.isSymbolicLink(file.toPath())) return file.delete();
        File[] children = file.listFiles();
        if (children != null) for (File child : children) if (!deleteTree(child)) return false;
        return !file.exists() || file.delete();
    }

    private void chooseArchive() {
        if (importing) return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/zip");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
        startActivityForResult(intent, PICK_ARCHIVE);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ARCHIVE || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        String displayName = archiveName(uri);
        String modName = safeModName(displayName);
        if (modName.isEmpty()) { toast("Invalid archive name"); return; }
        File destination = new File(modsRoot(), modName);
        File[] installed = modsRoot().listFiles();
        if (installed != null) for (File item : installed) {
            if (item.getName().equalsIgnoreCase(modName)) {
                toast("A mod named " + item.getName() + " already exists");
                return;
            }
        }
        importing = true;
        refresh();
        new Thread(() -> {
            File staging = new File(modsRoot(), ".import-" + System.currentTimeMillis());
            String error = null;
            try (InputStream input = getContentResolver().openInputStream(uri)) {
                if (input == null) throw new IllegalStateException("Cannot open selected archive");
                if (!staging.mkdirs()) throw new IllegalStateException("Cannot create temporary folder");
                extractZip(input, staging);
                File payload = unwrapSingleFolder(staging);
                if (!payload.renameTo(destination)) {
                    copyTree(payload, destination);
                    if (!deleteTree(staging)) throw new IllegalStateException("Could not clean temporary files");
                }
                if (staging.exists() && !deleteTree(staging)) throw new IllegalStateException("Could not clean temporary files");
            } catch (Exception e) {
                error = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
                deleteTree(staging);
                deleteTree(destination);
            }
            final String result = error;
            runOnUiThread(() -> {
                importing = false;
                refresh();
                toast(result == null ? "Imported " + modName : "Import failed: " + result);
            });
        }, "mod-zip-import").start();
    }

    private void extractZip(InputStream source, File destination) throws Exception {
        long total = 0;
        int entries = 0;
        byte[] buffer = new byte[64 * 1024];
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(source))) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (++entries > MAX_ENTRIES) throw new IllegalStateException("Archive contains too many entries");
                String raw = entry.getName().replace('\\', '/');
                if (raw.startsWith("/") || raw.matches("^[A-Za-z]:.*"))
                    throw new IllegalStateException("Archive contains an absolute path");
                File output = new File(destination, raw);
                String base = destination.getCanonicalPath() + File.separator;
                String canonical = output.getCanonicalPath();
                if (!canonical.startsWith(base)) throw new IllegalStateException("Archive path traversal blocked");
                if (entry.isDirectory()) {
                    if (!output.isDirectory() && !output.mkdirs()) throw new IllegalStateException("Cannot create folder");
                } else {
                    File parent = output.getParentFile();
                    if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                        throw new IllegalStateException("Cannot create folder");
                    try (FileOutputStream out = new FileOutputStream(output)) {
                        int read;
                        while ((read = zip.read(buffer)) != -1) {
                            total += read;
                            if (total > MAX_EXPANDED_BYTES) throw new IllegalStateException("Archive expands beyond 1 GiB");
                            out.write(buffer, 0, read);
                        }
                    }
                }
                zip.closeEntry();
            }
        }
        if (entries == 0) throw new IllegalStateException("Archive is empty or unsupported");
    }

    private File unwrapSingleFolder(File staging) {
        File[] children = staging.listFiles();
        if (children != null && children.length == 1 && children[0].isDirectory()) return children[0];
        return staging;
    }

    private void copyTree(File source, File destination) throws Exception {
        if (source.isDirectory()) {
            if (!destination.isDirectory() && !destination.mkdirs()) throw new IllegalStateException("Cannot create destination folder");
            File[] children = source.listFiles();
            if (children != null) for (File child : children) copyTree(child, new File(destination, child.getName()));
        } else {
            File parent = destination.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IllegalStateException("Cannot create destination folder");
            try (FileInputStream in = new FileInputStream(source); FileOutputStream out = new FileOutputStream(destination)) {
                byte[] buffer = new byte[64 * 1024];
                int read;
                while ((read = in.read(buffer)) != -1) out.write(buffer, 0, read);
            }
        }
    }

    private String archiveName(Uri uri) {
        String name = uri.getLastPathSegment();
        if (name == null) name = "Imported_Mod";
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        try (android.database.Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
                if (index >= 0) name = cursor.getString(index);
            }
        } catch (Exception ignored) {}
        if (name.toLowerCase(Locale.ROOT).endsWith(".zip")) name = name.substring(0, name.length() - 4);
        return name;
    }

    private String safeModName(String name) {
        name = name.replaceAll("[^A-Za-z0-9._ -]", "_").trim();
        while (name.startsWith(".")) name = name.substring(1);
        while (name.endsWith(".")) name = name.substring(0, name.length() - 1);
        return name.length() > 64 ? name.substring(0, 64).trim() : name;
    }

    private TextView text(String value, int size, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        if (bold) view.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        return view;
    }

    private Button button(String label, boolean primary) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextSize(14);
        button.setTextColor(primary ? 0xFF210A03 : 0xFFFFFFFF);
        button.setBackgroundColor(primary ? 0xFFFF8A28 : 0xFF3A2419);
        return button;
    }

    private Button smallButton(String label) {
        Button button = button(label, false);
        button.setTextSize(11);
        return button;
    }

    private void toast(String message) { Toast.makeText(this, message, Toast.LENGTH_LONG).show(); }
}
