// 批量求值工具：从文件读公式，逐行求值，输出 "公式\t结果"
// 用于与 SciPy 等第三方库做数值交叉验证 —— 引擎自己算不算得对，
// 光靠自测说不清，得有个独立实现来对照。
#include "sheet.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
using namespace xl;
int main(int argc, char** argv){
    setvbuf(stdout,NULL,_IONBF,0);
    const char* path = (argc > 1) ? argv[1] : "/tmp/eval_list.txt";
    std::ifstream in(path);
    if(!in){ std::cerr<<"无法打开 "<<path<<"\n"; return 1; }
    std::string line;
    int ok=0, err=0;
    while(std::getline(in,line)){
        if(line.empty() || line[0]=='#') continue;
        Sheet s; s.setName("A");
        std::string e = s.setFormula(8,8,line);
        if(!e.empty()){ std::cout<<line<<"\t<PARSE:"<<e<<">\n"; err++; continue; }
        s.recalc();
        Value v = s.valueAt(8,8);
        std::ostringstream o;
        o << std::setprecision(17);
        if(v.isNum()) o << v.n;
        else if(v.isError()) o << "<ERR>" << errText(v.e);
        else if(v.isBool()) o << (v.b ? "TRUE" : "FALSE");
        else if(v.isEmpty()) o << "<EMPTY>";
        else if(v.isStr()) o << "<STR>" << v.s;
        else if(v.isArray()){
            o << "<ARR>";
            if(v.arr) for(size_t r=0;r<(*v.arr).size();r++){
                for(size_t c=0;c<(*v.arr)[r].size();c++)
                    o << std::setprecision(17) << (*v.arr)[r][c].n << ",";
                o << ";";
            }
        }
        std::cout << line << "\t" << o.str() << "\n";
        ok++;
    }
    std::cerr << "求值 " << ok << " 条，解析失败 " << err << " 条\n";
    return 0;
}
