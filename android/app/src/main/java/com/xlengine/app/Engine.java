package com.xlengine.app;

/**
 * 引擎的 Java 侧声明。
 *
 * 这里曾经只有一个 eval() —— 于是整个 APK 就是个"公式输入框"。
 * 真正的表格 App 需要整个工作簿：多表、网格读写、打开/保存 xlsx、重算。
 *
 * 所有方法都操作同一个全局工作簿（见 jni_bridge.cpp 的说明），
 * 所以 Java 侧不需要持有任何指针。
 */
public final class Engine {

    static {
        System.loadLibrary("xlengine");
    }

    private Engine() {}

    // ---- 工作簿 ----
    public static native int wbNew();
    public static native int wbLoad(String path);
    public static native int wbSave(String path);
    public static native String lastError();

    // ---- 工作表 ----
    public static native int sheetCount();
    public static native String sheetName(int i);
    public static native int setSheet(int i);
    public static native int addSheet(String name);

    // ---- 单元格 ----
    /** 单元格原文（公式带 '='），用于回填编辑框 */
    public static native String cellRaw(int col, int row);
    /** 显示值，用于绘制网格 */
    public static native String cellText(int col, int row);
    public static native int setCell(int col, int row, String text);
    public static native int recalc();

    // ---- 网格 ----
    /** 可见区域的显示值，JSON 二维数组。逐格调用在手机上会掉帧 */
    public static native String grid(int c0, int r0, int c1, int r1);
    /** "c0,r0,c1,r1"，空表返回空串 */
    public static native String usedRange();

    // ---- 结构性编辑 ----
    public static native int insertRows(int at, int n);
    public static native int deleteRows(int at, int n);
    public static native int insertCols(int at, int n);
    public static native int deleteCols(int at, int n);

    // ---- 快速求值（保留） ----
    public static native String eval(String formula);
}
