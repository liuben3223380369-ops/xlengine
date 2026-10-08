package com.xlengine.app;

import android.app.Activity;
import android.os.Bundle;
import android.text.TextUtils;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * 最小可用的外壳：输入一条公式，调用引擎，把结果显示出来。
 *
 * 这是刻意保持的最小形态 —— 引擎原生的终端界面（TUI）在 Android 上不存在，
 * 所以只把"求值"这一件事暴露出来。要做成完整表格还需另写网格控件与交互层。
 */
public class MainActivity extends Activity {

    private EditText input;
    private TextView output;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        input = findViewById(R.id.input);
        output = findViewById(R.id.output);

        Button run = findViewById(R.id.run);
        Button demo = findViewById(R.id.demo);

        run.setOnClickListener(v -> runOne());
        demo.setOnClickListener(v -> runDemo());

        runDemo();
    }

    private void runOne() {
        String f = input.getText().toString().trim();
        if (TextUtils.isEmpty(f)) {
            return;
        }
        String r;
        try {
            r = Engine.eval(f);
        } catch (Throwable t) {
            r = "异常: " + t.getMessage();
        }
        append("> " + f + "\n  = " + r + "\n");
        input.selectAll();
    }

    /** 一批覆盖 Excel 典型"暗坑"的示例，也顺带当作引擎自检。 */
    private void runDemo() {
        output.setText("");
        String[] samples = {
                "1+2*3",                 // 优先级
                "-2^2",                  // 一元负号优先于 ^ → 4
                "2^3^2",                 // ^ 是左结合 → 64
                "MOD(-3,2)",             // 与除数同号 → 1
                "INT(-2.5)",             // 向下取整 → -3
                "IF(TRUE,1,1/0)",        // 惰性求值 → 1
                "SUM({1,2,3,4})",
                "LEN(\"中文abc\")",       // 按字符计数 → 5
                "LEFT(\"中文abc\",2)",    // 按字符截取 → 中文
                "NORMSDIST(1.96)",
                "TEXT(0.5,\"0.0%\")",
                "1/0"                    // 错误传播 → #DIV/0!
        };
        for (String s : samples) {
            String r;
            try {
                r = Engine.eval(s);
            } catch (Throwable t) {
                r = "异常: " + t.getMessage();
            }
            append("> " + s + "\n  = " + r + "\n");
        }
    }

    private void append(String line) {
        output.append(line);
        ScrollView sv = findViewById(R.id.scroller);
        sv.post(() -> sv.fullScroll(ScrollView.FOCUS_DOWN));
    }
}
