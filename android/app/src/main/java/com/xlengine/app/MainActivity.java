package com.xlengine.app;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.webkit.JavascriptInterface;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * 主界面：WebView 承载表格网格。
 *
 * 为什么用 WebView 而不是原生控件：
 *
 *   网格需要"只画可见格子"的虚拟化（几十万行也不能卡），
 *   用 RecyclerView 做需要写大量适配器与复用逻辑；
 *   而同样的虚拟化在 DOM/Canvas 里几行就能实现，且滚动、选中、
 *   就地编辑这些交互浏览器已经做好了。
 *
 *   引擎是 C++，通过 JNI 暴露（见 Engine.java）；
 *   JS 只负责画格子和收集输入，不做任何计算 —— 计算全在引擎里。
 *
 * 一句话：JS 是界面，C++ 是引擎，Java 是中间那层薄薄的桥。
 */
public class MainActivity extends Activity {

    private static final String TAG = "xlengine";
    private static final int REQ_OPEN = 1001;
    private static final int REQ_SAVE = 1002;

    private WebView web;
    /** 当前文档在私有目录里的路径；新建/打开都指向它 */
    private File current;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        web = new WebView(this);
        WebSettings ws = web.getSettings();
        ws.setJavaScriptEnabled(true);
        ws.setDomStorageEnabled(true);
        // 表格是本地资源，不需要网络
        ws.setAllowFileAccess(true);
        web.addJavascriptInterface(new Bridge(), "Android");
        setContentView(web);

        current = new File(getFilesDir(), "book.xlsx");
        web.loadUrl("file:///android_asset/sheet.html");
    }

    // ------------------------------------------------------------------
    // JS -> Java
    // ------------------------------------------------------------------
    private final class Bridge {

        @JavascriptInterface
        public String grid(int c0, int r0, int c1, int r1) {
            return Engine.grid(c0, r0, c1, r1);
        }

        @JavascriptInterface
        public String cellRaw(int c, int r) {
            return Engine.cellRaw(c, r);
        }

        @JavascriptInterface
        public String cellText(int c, int r) {
            return Engine.cellText(c, r);
        }

        @JavascriptInterface
        public int setCell(int c, int r, String text) {
            int rc = Engine.setCell(c, r, text);
            if (rc == 0) Engine.recalc();
            return rc;
        }

        @JavascriptInterface
        public int recalc() {
            return Engine.recalc();
        }

        @JavascriptInterface
        public int sheetCount() {
            return Engine.sheetCount();
        }

        @JavascriptInterface
        public String sheetName(int i) {
            return Engine.sheetName(i);
        }

        @JavascriptInterface
        public int setSheet(int i) {
            return Engine.setSheet(i);
        }

        @JavascriptInterface
        public int addSheet(String name) {
            return Engine.addSheet(name);
        }

        @JavascriptInterface
        public String usedRange() {
            return Engine.usedRange();
        }

        @JavascriptInterface
        public int insertRows(int at, int n) { return Engine.insertRows(at, n); }
        @JavascriptInterface
        public int deleteRows(int at, int n) { return Engine.deleteRows(at, n); }
        @JavascriptInterface
        public int insertCols(int at, int n) { return Engine.insertCols(at, n); }
        @JavascriptInterface
        public int deleteCols(int at, int n) { return Engine.deleteCols(at, n); }

        @JavascriptInterface
        public String eval(String f) {
            return Engine.eval(f);
        }

        @JavascriptInterface
        public String lastError() {
            return Engine.lastError();
        }

        /** 保存到私有目录，返回是否成功 */
        @JavascriptInterface
        public boolean save() {
            int rc = Engine.wbSave(current.getAbsolutePath());
            if (rc != 0) toast("保存失败: " + Engine.lastError());
            return rc == 0;
        }

        @JavascriptInterface
        public boolean doNew() {
            Engine.wbNew();
            return true;
        }

        @JavascriptInterface
        public String openFilePicker() {
            // 系统文件选择器必须在 UI 线程发起
            runOnUiThread(() -> {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                startActivityForResult(i, REQ_OPEN);
            });
            return "";
        }

        @JavascriptInterface
        public String exportAs() {
            runOnUiThread(() -> {
                Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
                i.putExtra(Intent.EXTRA_TITLE, "xlengine.xlsx");
                startActivityForResult(i, REQ_SAVE);
            });
            return "";
        }
    }

    // ------------------------------------------------------------------
    private void toast(String s) {
        runOnUiThread(() -> Toast.makeText(this, s, Toast.LENGTH_SHORT).show());
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();

        try {
            if (req == REQ_OPEN) {
                // SAF 给的是 URI，引擎只认真实路径 —— 拷到私有目录再加载。
                // 直接把 URI 字符串传给 C++ 会因为不是文件路径而失败。
                File tmp = new File(getCacheDir(), "import.xlsx");
                copy(uri, tmp);
                int rc = Engine.wbLoad(tmp.getAbsolutePath());
                if (rc != 0) {
                    toast("打开失败: " + Engine.lastError());
                    return;
                }
                current = new File(getFilesDir(), "book.xlsx");
                web.evaluateJavascript("onOpened()", null);
                toast("已打开");
            } else if (req == REQ_SAVE) {
                File tmp = new File(getCacheDir(), "export.xlsx");
                if (Engine.wbSave(tmp.getAbsolutePath()) != 0) {
                    toast("保存失败: " + Engine.lastError());
                    return;
                }
                OutputStream os = getContentResolver().openOutputStream(uri);
                if (os == null) { toast("无法写入目标位置"); return; }
                java.io.FileInputStream fis = new java.io.FileInputStream(tmp);
                byte[] buf = new byte[8192];
                int n;
                while ((n = fis.read(buf)) > 0) os.write(buf, 0, n);
                fis.close();
                os.close();
                toast("已导出");
            }
        } catch (Exception e) {
            Log.e(TAG, "文件操作失败", e);
            toast("文件操作失败: " + e.getMessage());
        }
    }

    private void copy(Uri uri, File dst) throws Exception {
        InputStream is = getContentResolver().openInputStream(uri);
        if (is == null) throw new Exception("无法读取该文件");
        FileOutputStream os = new FileOutputStream(dst);
        byte[] buf = new byte[8192];
        int n;
        while ((n = is.read(buf)) > 0) os.write(buf, 0, n);
        is.close();
        os.close();
    }

    // ------------------------------------------------------------------
    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        menu.add(0, 1, 0, "新建");
        menu.add(0, 2, 0, "打开…");
        menu.add(0, 3, 0, "保存");
        menu.add(0, 4, 0, "导出为…");
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case 1:
                Engine.wbNew();
                web.evaluateJavascript("onOpened()", null);
                return true;
            case 2:
                new Bridge().openFilePicker();
                return true;
            case 3:
                new Bridge().save();
                return true;
            case 4:
                new Bridge().exportAs();
                return true;
        }
        return super.onOptionsItemSelected(item);
    }
}
