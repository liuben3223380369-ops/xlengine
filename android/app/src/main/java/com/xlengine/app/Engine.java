package com.xlengine.app;

/**
 * 引擎的 JNI 封装。
 *
 * 底层是同一套 C++ 实现（词法 / 语法 / 求值 / 495 个函数 / xlsx / PDF / 图表），
 * 这里只暴露一个"公式进、结果出"的入口。
 */
public final class Engine {

    static {
        System.loadLibrary("xlengine");
    }

    private Engine() {
    }

    /**
     * 求值一条公式。
     *
     * @param formula 公式文本，可以带或不带前导 '='，例如 "SUM({1,2,3})" 或 "=SUM({1,2,3})"
     * @return 显示文本；出错时返回 Excel 风格的错误文本（如 "#DIV/0!"）
     */
    public static native String eval(String formula);
}
