CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -I src
DEPFLAGS  = -MMD -MP

SRCS     := src/functions.cpp src/fn_date.cpp src/fn_math.cpp src/fn_stat.cpp src/fn_stat2.cpp src/fn_array.cpp src/fn_more.cpp \
            src/fn_text.cpp src/fn_eng.cpp src/fn_fin.cpp src/fn_fill.cpp src/fn_fill2.cpp src/fn_fill3.cpp src/fn_fill4.cpp src/ttf.cpp src/pdf.cpp src/sheetpdf.cpp src/chartpdf.cpp src/tui.cpp src/refshift.cpp src/numfmt.cpp src/style.cpp src/cf.cpp src/dv.cpp src/note.cpp src/view.cpp src/image.cpp src/depgraph.cpp src/precedent.cpp \
            src/eval.cpp src/parser.cpp src/sheet.cpp src/layout.cpp src/canvas.cpp src/render.cpp src/zip.cpp src/xml.cpp src/chartxml.cpp src/chartsrc.cpp src/xlsx.cpp src/edit.cpp src/gui_bridge.cpp src/main.cpp
OBJS     := $(SRCS:.cpp=.o)
DEPS     := $(OBJS:.o=.d)
TARGET   := xl

TEST_SRC := tests/test_new.cpp tests/test_new2.cpp tests/test_xlsx.cpp tests/test_chartxlsx.cpp tests/test_fill.cpp tests/test_pdf.cpp tests/test_tui.cpp tests/test_refshift.cpp tests/test_numfmt.cpp tests/test_style.cpp tests/test_smoke.cpp tests/test_cf.cpp tests/test_dv.cpp tests/test_note.cpp tests/test_shared.cpp tests/test_view.cpp tests/test_image.cpp tests/test_definedname.cpp tests/test_depgraph.cpp tests/test_edit.cpp tests/test_robust.cpp tests/test_stress.cpp tests/bench.cpp tools/chartdemo.cpp tools/numfmtdemo.cpp tools/styledemo.cpp tools/cfdemo.cpp tools/dvdemo.cpp tools/notedemo.cpp tools/viewdemo.cpp tools/imagedemo.cpp tools/dndemo.cpp
TEST_BIN := tests/xltest tests/xltest2 tests/xlsxlt tests/chartt tests/fillt tests/pdft tests/tuit tests/refshiftt tests/numfmtt tests/stylet tests/cft tests/dvt tests/notet tests/sharedt tests/viewt tests/imgt tests/dnt tests/dept tests/editt tests/auditt tests/jnitest tests/smoket tests/robustt tests/stresst tests/xlbench tools/chartdemo tools/xlsxdemo

all: $(TARGET)

# ---------------------------------------------------------------------------
# 图形界面：把引擎编成共享库，供 Python（ctypes）调用
# ---------------------------------------------------------------------------
# 注意 -fPIC 是必须的：不加的话链接 .so 会报
#   relocation R_X86_64_TPOFF32 ... can not be used when making a shared object
# 普通可执行文件用的 .o 不能混进来，所以这里单独一套 -fPIC 目标文件。
LIB_DIR   := .build-pic
LIB_SRCS  := $(filter-out src/main.cpp src/tui.cpp,$(SRCS))
LIB_OBJS  := $(patsubst src/%.cpp,$(LIB_DIR)/%.o,$(LIB_SRCS))
LIB_NAME  := libxlengine.so

lib: $(LIB_NAME)

$(LIB_NAME): $(LIB_OBJS)
	$(CXX) -std=c++17 -O2 -shared -o $@ $(LIB_OBJS) -lz

# 依赖文件必须自己生成 —— 少了 $(DEPFLAGS) 的话，改了头文件不会触发重编，
# 于是 .so 里混着按旧结构体布局编译出来的对象文件。
# 表现是运行期莫名其妙的 std::bad_array_new_length / 段错误，
# 而且编译零告警、链接也过得去。
$(LIB_DIR)/%.o: src/%.cpp | $(LIB_DIR)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -fPIC -I src $(DEPFLAGS) -MF $(@:.o=.d) -c $< -o $@

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

lib-clean:
	rm -rf $(LIB_DIR) $(LIB_NAME)

# ---------------------------------------------------------------------------
# 图形界面
# ---------------------------------------------------------------------------
# 图形界面
# ---------------------------------------------------------------------------
# 界面用 Python + PySide6 写，通过 ctypes 调上面的 .so。
# 所以跑界面前必须先 `make lib`。
gui: lib
	@echo "启动图形界面…"
	@python3 -m gui.app || \
	  echo "" && \
	  echo "启动失败。若提示 No module named 'PySide6'，请先安装：" && \
	  echo "    pip install PySide6" && false

gui-test: lib
	@python3 -m gui.selftest

gui-shot: lib
	@python3 gui/run_offscreen.py

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) -lz

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# JNI 桥接的桌面验证。
# 桩头文件在 tests/jni_stub/jni.h（jstring 就是 std::string），
# 这样 Android 端的桥接逻辑能在桌面上真跑一遍 —— APK 只能在 CI 编，
# 本地跑不了真机，光靠语法检查拦不住"数字被当文本存"这类逻辑错误。
tests/jnitest: tests/test_jni.cpp android/app/src/main/cpp/jni_bridge.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -I tests/jni_stub -o $@ tests/test_jni.cpp android/app/src/main/cpp/jni_bridge.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/xltest: tests/test_new.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_new.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/xltest2: tests/test_new2.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_new2.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/xlsxlt: tests/test_xlsx.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_xlsx.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/chartt: tests/test_chartxlsx.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_chartxlsx.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/fillt: tests/test_fill.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_fill.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/pdft: tests/test_pdf.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_pdf.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/tuit: tests/test_tui.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_tui.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/refshiftt: tests/test_refshift.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_refshift.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/numfmtt: tests/test_numfmt.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_numfmt.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/stylet: tests/test_style.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_style.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/auditt: tests/test_audit.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tests/smoket: tests/test_smoke.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_smoke.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/cft: tests/test_cf.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tests/dvt: tests/test_dv.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tests/notet: tests/test_note.cpp tests/test_shared.cpp tests/test_view.cpp tests/test_image.cpp tests/test_definedname.cpp tests/test_depgraph.cpp tests/test_edit.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tests/sharedt: tests/test_shared.cpp tests/test_view.cpp tests/test_image.cpp tests/test_definedname.cpp tests/test_depgraph.cpp tests/test_edit.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tests/viewt: tests/test_view.cpp tests/test_image.cpp tests/test_definedname.cpp tests/test_depgraph.cpp tests/test_edit.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tests/imgt: tests/test_image.cpp tests/test_definedname.cpp tests/test_depgraph.cpp tests/test_edit.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tests/dnt: tests/test_definedname.cpp tests/test_depgraph.cpp tests/test_edit.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tests/dept: tests/test_depgraph.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_depgraph.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/editt: tests/test_edit.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tests/robustt: tests/test_robust.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_robust.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/stresst: tests/test_stress.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/test_stress.cpp $(filter-out src/main.o,$(OBJS)) -lz

tests/xlbench: tests/bench.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tests/bench.cpp $(filter-out src/main.o,$(OBJS)) -lz

# 跑全部语义测试（Excel 行为 + 新函数）
test: $(TARGET) tests/xltest tests/xltest2 tests/xlsxlt tests/chartt tests/fillt tests/pdft tests/tuit tests/refshiftt tests/numfmtt tests/stylet tests/cft tests/dvt tests/notet tests/sharedt tests/viewt tests/imgt tests/dnt tests/dept tests/editt tests/auditt tests/smoket tests/robustt tests/stresst
	sh tools/runtest.sh ./$(TARGET) --test
	@echo ""
	sh tools/runtest.sh ./tests/xltest
	@echo ""
	sh tools/runtest.sh ./tests/xltest2
	@echo ""
	sh tools/runtest.sh ./tests/xlsxlt
	@echo ""
	sh tools/runtest.sh ./tests/chartt
	@echo ""
	sh tools/runtest.sh ./tests/fillt
	@echo ""
	sh tools/runtest.sh ./tests/pdft
	@echo ""
	sh tools/runtest.sh ./tests/tuit
	@echo ""
	sh tools/runtest.sh ./tests/refshiftt
	@echo ""
	sh tools/runtest.sh ./tests/numfmtt
	@echo ""
	./xl --funcs | sort -u > /tmp/xl_funcs.txt
	sh tools/runtest.sh ./tests/smoket /tmp/xl_funcs.txt
	sh tools/runtest.sh ./tests/cft
	sh tools/runtest.sh ./tests/dvt
	sh tools/runtest.sh ./tests/notet
	sh tools/runtest.sh ./tests/sharedt
	sh tools/runtest.sh ./tests/viewt
	sh tools/runtest.sh ./tests/imgt
	sh tools/runtest.sh ./tests/dnt
	sh tools/runtest.sh ./tests/dept
	sh tools/runtest.sh ./tests/editt
	@echo ""
	sh tools/runtest.sh ./tests/auditt
	sh tools/runtest.sh ./tests/jnitest
	sh tools/runtest.sh ./tests/robustt
	sh tools/runtest.sh ./tests/stresst
	sh tools/runtest.sh ./tests/stylet

bench: tests/xlbench
	./tests/xlbench

funcs: $(TARGET)
	./$(TARGET) --funcs

# 打包 Windows EXE。
# Linux 走 mingw 交叉编译，Windows 本机请用 build.bat。
exe:
	@./build_exe.sh

exe32:
	@./build_exe.sh 32

exe-clean:
	@./build_exe.sh clean

# Windows 分支的静态检查。
#
# 交叉编译环境常常没有（本机有 mingw 也可以直接编），但 Windows 代码路径
# 又最容易在没编译过的情况下烂掉。这里提供两类检查：
#   make wincheck   —— 用 shim 头做语法检查，不需要 mingw
#   make winbuild   —— 真编译（需要 mingw）
WINCHECK_DIR ?= .wincheck-shim
wincheck:
	@echo "生成最小 Windows API shim 到 $(WINCHECK_DIR) ..."
	@mkdir -p $(WINCHECK_DIR)
	@cp tools/winshim/windows.h tools/winshim/conio.h $(WINCHECK_DIR)/
	@for f in src/*.cpp; do \
	    $(CXX) $(CXXFLAGS) -D_WIN32 -I$(WINCHECK_DIR) -I src -fsyntax-only $$f || exit 1; \
	done
	@echo "Windows 分支语法检查通过（仅语法，不保证运行时行为）"

winbuild:
	@./build_exe.sh

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET) $(TEST_BIN)
	rm -rf $(WINCHECK_DIR)

-include $(DEPS)
-include $(LIB_OBJS:.o=.d)

.PHONY: all test bench funcs clean

tools/chartdemo: tools/chartdemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/numfmtdemo: tools/numfmtdemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/styledemo: tools/styledemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/dvdemo: tools/dvdemo.cpp tools/notedemo.cpp tools/viewdemo.cpp tools/imagedemo.cpp tools/dndemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/cfdemo: tools/cfdemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/notedemo: tools/notedemo.cpp tools/viewdemo.cpp tools/imagedemo.cpp tools/dndemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/viewdemo: tools/viewdemo.cpp tools/imagedemo.cpp tools/dndemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/imagedemo: tools/imagedemo.cpp tools/dndemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/prof: tools/prof.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -O2 -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/dndemo: tools/dndemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJS)) -lz

tools/xlsxdemo: tools/xlsxdemo.cpp $(filter-out src/main.o,$(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ tools/xlsxdemo.cpp $(filter-out src/main.o,$(OBJS)) -lz

charts: tools/chartdemo
	@mkdir -p out
	./tools/chartdemo

.PHONY: charts

# 用第三方实现交叉验证写出的 xlsx。
# 必须做：自写格式时自测只能证明自洽，证明不了合规 —— deflate/zlib 那个 bug 就是这么挖出来的。
evalbatch: tools/evalbatch
tools/evalbatch: tools/evalbatch.cpp
	$(CXX) $(CXXFLAGS) -I src -o tools/evalbatch tools/evalbatch.cpp $(filter-out src/main.o,$(OBJS)) -lz

verify: tools/xlsxdemo
	@mkdir -p out
	@./tools/xlsxdemo > /dev/null
	@python3 tools/verify.py

# 与 SciPy / NumPy 的数值交叉验证（需要 python3 + scipy + numpy）
xcheck: tools/evalbatch
	@python3 tools/xcheck.py

# 与 LibreOffice 的语义级交叉验证（需要 soffice）
locheck: tools/evalbatch
	@python3 tools/locheck.py

.PHONY: xcheck locheck

demo: tools/xlsxdemo
	@mkdir -p out
	./tools/xlsxdemo

.PHONY: verify demo
