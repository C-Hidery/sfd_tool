// core/xmlutil.hpp
#ifndef SFD_XMLUTIL_HPP
#define SFD_XMLUTIL_HPP

#include <string>
#include <vector>
#include <memory>
#include <cctype>
#include <cstdlib>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlstring.h>
#include <libxml/globals.h>   // 显式声明 xmlFree，避免个别平台/版本找不到

#include "file_io.h" // 额外file库

namespace xmlutil
{
    // ==================== 字符串 / 节点基础工具 ====================

    // xmlChar* → std::string（nullptr 安全）
    inline std::string toStr(const xmlChar* s)
    {
        return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
    }

    // 判断节点是否为指定名字的元素
    inline bool isElement(xmlNodePtr n, const char* name)
    {
        return n && n->type == XML_ELEMENT_NODE &&
            xmlStrcmp(n->name, BAD_CAST name) == 0;
    }

    // 取节点标签名（调试用）
    inline std::string nodeName(xmlNodePtr n)
    {
        return (n && n->name) ? toStr(n->name) : std::string();
    }

    // 去首尾空白
    inline void trim(std::string& s)
    {
        auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    }

    // ==================== 子节点查找 ====================

    // 找第一个名为 name 的直接子元素
    inline xmlNodePtr firstChild(xmlNodePtr parent, const char* name)
    {
        if (!parent) return nullptr;
        for (xmlNodePtr c = parent->children; c; c = c->next)
            if (isElement(c, name)) return c;
        return nullptr;
    }

    // 收集所有名为 name 的直接子元素
    inline std::vector<xmlNodePtr> children(xmlNodePtr parent, const char* name)
    {
        std::vector<xmlNodePtr> out;
        if (!parent) return out;
        for (xmlNodePtr c = parent->children; c; c = c->next)
            if (isElement(c, name)) out.push_back(c);
        return out;
    }

    // 递归查找第一个名为 name 的后代元素（含 node 自身）
    inline xmlNodePtr findFirstDescendant(xmlNodePtr node, const char* name)
    {
        if (!node) return nullptr;
        if (isElement(node, name)) return node;
        for (xmlNodePtr c = node->children; c; c = c->next)
            if (xmlNodePtr found = findFirstDescendant(c, name))
                return found;
        return nullptr;
    }

    // 递归收集所有名为 name 的后代元素（含 node 自身）
    inline void collectDescendants(xmlNodePtr node, const char* name,
                                   std::vector<xmlNodePtr>& out)
    {
        if (!node) return;
        if (isElement(node, name)) out.push_back(node);
        for (xmlNodePtr c = node->children; c; c = c->next)
            collectDescendants(c, name, out);
    }

    // 递归收集的返回值版本
    inline std::vector<xmlNodePtr> descendants(xmlNodePtr node, const char* name)
    {
        std::vector<xmlNodePtr> out;
        collectDescendants(node, name, out);
        return out;
    }

    // ==================== 文本 / 属性 ====================

    // 取子元素文本（拼接内部所有文本 / CDATA / 实体），自动 trim
    inline std::string childText(xmlNodePtr parent, const char* tag)
    {
        xmlNodePtr c = firstChild(parent, tag);
        if (!c) return "";
        xmlChar* content = xmlNodeGetContent(c);
        std::string result = toStr(content);
        if (content) xmlFree(content);
        trim(result);
        return result;
    }

    // 取子元素文本并转换为整数，支持 0x / 0X 前缀
    inline int childInt(xmlNodePtr parent, const char* tag, int defaultVal = 0)
    {
        std::string text = childText(parent, tag);
        if (text.empty()) return defaultVal;
        if (text.size() > 2 && text[0] == '0' &&
            (text[1] == 'x' || text[1] == 'X'))
            return static_cast<int>(std::strtol(text.c_str(), nullptr, 16));
        return std::atoi(text.c_str());
    }

    // 取属性值，属性不存在返回空字符串
    inline std::string prop(xmlNodePtr node, const char* name)
    {
        if (!node) return "";
        xmlChar* v = xmlGetProp(node, BAD_CAST name);
        if (!v) return "";
        std::string result(reinterpret_cast<const char*>(v));
        xmlFree(v);
        return result;
    }

    // ==================== 序列化 ====================

    // 把单个节点序列化为字符串（含自身标签）
    //   pretty=false（默认）: 紧凑格式，无缩进
    //   pretty=true         : 2 空格缩进（libxml2 默认）
    inline std::string nodeToString(xmlNodePtr node,
                                    xmlDocPtr doc = nullptr,
                                    bool pretty = false)
    {
        if (!node) return "";
        xmlBufferPtr buf = xmlBufferCreate();
        if (!buf) return "";
        xmlNodeDump(buf, doc, node, 0, pretty ? 1 : 0);
        std::string result(
            reinterpret_cast<const char*>(xmlBufferContent(buf)),
            xmlBufferLength(buf));
        xmlBufferFree(buf);
        return result;
    }

    // 把整份文档序列化为字符串（带 <?xml ...?> 声明）
    inline std::string docToString(xmlDocPtr doc, bool pretty = true)
    {
        if (!doc) return "";
        xmlChar* mem = nullptr;
        int size = 0;
        if (pretty)
            xmlDocDumpFormatMemory(doc, &mem, &size, 1);
        else
            xmlDocDumpMemory(doc, &mem, &size);
        std::string result;
        if (mem)
        {
            result.assign(reinterpret_cast<const char*>(mem), size);
            xmlFree(mem);
        }
        return result;
    }

    // ==================== RAII 包装 ====================

    // 用 unique_ptr 管理 xmlDoc，避免忘记 xmlFreeDoc
    struct DocDeleter
    {
        void operator()(xmlDocPtr p) const noexcept { if (p) xmlFreeDoc(p); }
    };

    using DocPtr = std::unique_ptr<xmlDoc, DocDeleter>;

    // 从字符串加载文档（失败返回空指针）
    inline DocPtr loadString(const std::string& xmlText,
                             const char* virtualName = "inline.xml")
    {
        xmlDocPtr doc = xmlReadMemory(
            xmlText.c_str(), static_cast<int>(xmlText.size()),
            virtualName, nullptr,
            XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
        return DocPtr(doc);
    }

    // 从文件加载文档（失败返回空指针）
    inline DocPtr loadFile(const std::string& filename)
    {
        EnhancedFile fi = oxfopen_enhanced(filename.c_str(), "r");
        if (!fi) return nullptr;
        std::string content = fi.read_all_chunked();
        return loadString(content);
    }

    // 获取根元素（nullptr 安全）
    inline xmlNodePtr root(xmlDocPtr doc)
    {
        return doc ? xmlDocGetRootElement(doc) : nullptr;
    }
} // namespace xmlutil

#endif // SFD_XMLUTIL_HPP
