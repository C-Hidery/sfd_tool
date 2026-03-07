import os

def process_file(filepath):
    print(f"Processing {filepath}...")
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Include i18n
    if '#include "i18n.h"' not in content:
        if '#include "GtkWidgetHelper.hpp"' in content:
            content = content.replace('#include "GtkWidgetHelper.hpp"\n', '#include "GtkWidgetHelper.hpp"\n#include "i18n.h"\n')
        elif '#include <execinfo.h>' in content:
            content = content.replace('#include <execinfo.h>\n', '#include <execinfo.h>\n#include "i18n.h"\n')
        elif 'int main(int argc, char** argv) {' in content:
            # Just add it near the top for main_console.cpp or similar if it lacks execinfo.h
            content = '#include "i18n.h"\n' + content

        # Fix setlocale missing in main.cpp
        if 'int main(int argc, char** argv) {' in content and 'setlocale(LC_ALL' not in content:
            setup_code = """	setlocale(LC_ALL, "");\n	bindtextdomain("sfd_tool", "./locale");\n	textdomain("sfd_tool");\n	bind_textdomain_codeset("sfd_tool", "UTF-8");\n\n"""
            content = content.replace('int main(int argc, char** argv) {\n', 'int main(int argc, char** argv) {\n' + setup_code)

    # Apply the conn_wait fixes
    content = content.replace('int ret, wait = 30', 'int ret, conn_wait = 30')
    content = content.replace('int ret = 0, wait = 30', 'int ret = 0, conn_wait = 30')
    content = content.replace('wait > 0) { wait--;', 'conn_wait > 0) { conn_wait--;')
    content = content.replace('if (wait == 0)', 'if (conn_wait == 0)')
    content = content.replace('if(wait == 0)', 'if(conn_wait == 0)')
    
    # Fix the logic bug `if(i_o = io->part_count){`
    content = content.replace('if(i_o = io->part_count){', 'if(i_o == io->part_count){')

    # One more string: "\r\nPartition write complete! 分区写入完成"
    content = content.replace('"\r\nPartition write complete! 分区写入完成"', '_("\r\nPartition write complete!")')

    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)

process_file('main.cpp')
process_file('GtkWidgetHelper.cpp')
process_file('main_console.cpp')
