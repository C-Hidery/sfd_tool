// PacXMLParser.hpp
#ifndef PAC_XML_PARSER_HPP
#define PAC_XML_PARSER_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "xmlutil.h"


// ==================== 数据结构（原样保留）====================

struct BlockInfo {
    std::string id;
    std::string base;
    std::string size;
};

struct FileInfo {
    std::string id;
    std::string idAlias;
    std::string type;
    int flag = 0;
    int checkFlag = 0;
    std::string description;
    std::vector<BlockInfo> blocks;
};

struct SchemeInfo {
    std::string name;
    std::vector<FileInfo> files;
};

struct ProductInfo {
    std::string name;
    std::string schemeName;
};

#include "xmlutil.h"

// ==================== PAC XML 解析器 ====================

class PacXMLParser {
public:
    bool loadFromFile(const std::string& filename) {
        // XML_PARSE_NONET   : 禁止网络实体（安全）
        // XML_PARSE_NOERROR : 不向 stderr 打印错误（自己处理）
        xmlDocPtr doc = xmlReadFile(filename.c_str(), nullptr,
            XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
        if (!doc) {
            std::cerr << "Failed to parse XML: " << filename << std::endl;
            return false;
        }
        bool ok = parseDoc(doc);
        xmlFreeDoc(doc);
        return ok;
    }

    bool loadFromString(const std::string& xmlContent) {
        xmlDocPtr doc = xmlReadMemory(xmlContent.c_str(),
            static_cast<int>(xmlContent.size()),
            "pac.xml", nullptr,
            XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
        if (!doc) {
            std::cerr << "Failed to parse XML from string" << std::endl;
            return false;
        }
        bool ok = parseDoc(doc);
        xmlFreeDoc(doc);
        return ok;
    }

    // ---------- 收集所有 ID / Alias（原接口不变）----------

    std::vector<std::string> getAllIds() const {
        std::vector<std::string> ids;
        for (const auto& schemeName : m_schemeOrder) {
            auto it = m_schemeMap.find(schemeName);
            if (it == m_schemeMap.end()) continue;
            for (const auto& file : it->second.files)
                if (!file.id.empty()) ids.push_back(file.id);
        }
        return ids;
    }

    std::vector<std::string> getAllAliases() const {
        std::vector<std::string> aliases;
        for (const auto& schemeName : m_schemeOrder) {
            auto it = m_schemeMap.find(schemeName);
            if (it == m_schemeMap.end()) continue;
            for (const auto& file : it->second.files)
                aliases.push_back(file.idAlias.empty() ? file.id : file.idAlias);
        }
        return aliases;
    }

    std::vector<std::string> getIdsByScheme(const std::string& schemeName) const {
        std::vector<std::string> ids;
        auto it = m_schemeMap.find(schemeName);
        if (it == m_schemeMap.end()) return ids;
        for (const auto& file : it->second.files)
            if (!file.id.empty()) ids.push_back(file.id);
        return ids;
    }

    // ---------- 查询接口（原样保留）----------

    std::vector<std::string> getProductNames() const {
        std::vector<std::string> names;
        for (const auto& p : m_products) names.push_back(p.name);
        return names;
    }

    std::string getSchemeName(const std::string& productName) const {
        auto it = m_productMap.find(productName);
        return (it != m_productMap.end()) ? it->second : "";
    }

    std::vector<FileInfo> getFilesByScheme(const std::string& schemeName) const {
        auto it = m_schemeMap.find(schemeName);
        return (it != m_schemeMap.end()) ? it->second.files : std::vector<FileInfo>();
    }

    std::vector<FileInfo> getFilesByProduct(const std::string& productName) const {
        std::string scheme = getSchemeName(productName);
        if (scheme.empty()) return {};
        return getFilesByScheme(scheme);
    }

    FileInfo getFileInfoByOperation(const std::string& opName) const {
        for (const auto& pair : m_schemeMap) {
            for (const auto& file : pair.second.files) {
                if (file.idAlias == opName || file.id == opName) return file;
            }
        }
        return FileInfo{};
    }

    std::string getPartitionByOperation(const std::string& opName) const {
        auto info = getFileInfoByOperation(opName);
        return info.blocks.empty() ? "" : info.blocks.front().id;
    }

    std::string getBaseByOperation(const std::string& opName) const {
        auto info = getFileInfoByOperation(opName);
        return info.blocks.empty() ? "" : info.blocks.front().base;
    }

    std::string getSizeByOperation(const std::string& opName) const {
        auto info = getFileInfoByOperation(opName);
        return info.blocks.empty() ? "" : info.blocks.front().size;
    }

    std::vector<std::string> getSchemeNames() const { return m_schemeOrder; }

private:
    std::vector<ProductInfo> m_products;
    std::map<std::string, std::string> m_productMap;
    std::map<std::string, SchemeInfo> m_schemeMap;
    std::vector<std::string> m_schemeOrder;

    // ---------- 核心解析：用 libxml2 API 遍历 ----------
    bool parseDoc(xmlDocPtr doc) {
        xmlNodePtr root = xmlDocGetRootElement(doc);
        if (!root) {
            std::cerr << "Empty XML document" << std::endl;
            return false;
        }

        m_products.clear();
        m_productMap.clear();
        m_schemeMap.clear();
        m_schemeOrder.clear();

        // ---- ProductList ----
        if (xmlNodePtr productList = xmlutil::firstChild(root, "ProductList")) {
            for (xmlNodePtr pNode : xmlutil::children(productList, "Product")) {
                ProductInfo product;
                product.name       = xmlutil::prop(pNode, "name");
                product.schemeName = xmlutil::childText(pNode, "SchemeName");
                if (!product.name.empty()) {
                    m_products.push_back(product);
                    m_productMap[product.name] = product.schemeName;
                }
            }
        }

        // ---- SchemeList ----
        if (xmlNodePtr schemeList = xmlutil::firstChild(root, "SchemeList")) {
            for (xmlNodePtr sNode : xmlutil::children(schemeList, "Scheme")) {
                SchemeInfo scheme;
                scheme.name = xmlutil::prop(sNode, "name");

                for (xmlNodePtr fNode : xmlutil::children(sNode, "File")) {
                    FileInfo file;
                    file.id          = xmlutil::childText(fNode, "ID");
                    file.idAlias     = xmlutil::childText(fNode, "IDAlias");
                    if (file.idAlias.empty()) file.idAlias = file.id;
                    file.type        = xmlutil::childText(fNode, "Type");
                    file.flag        = xmlutil::childInt(fNode, "Flag", 0);
                    file.checkFlag   = xmlutil::childInt(fNode, "CheckFlag", 0);
                    file.description = xmlutil::childText(fNode, "Description");

                    for (xmlNodePtr bNode : xmlutil::children(fNode, "Block")) {
                        BlockInfo block;
                        block.id   = xmlutil::prop(bNode, "id");
                        block.base = xmlutil::childText(bNode, "Base");
                        block.size = xmlutil::childText(bNode, "Size");
                        file.blocks.push_back(block);
                    }
                    scheme.files.push_back(file);
                }

                if (!scheme.name.empty()) {
                    m_schemeMap[scheme.name] = scheme;
                    m_schemeOrder.push_back(scheme.name);
                }
            }
        }
        return true;
    }
};

#endif // PAC_XML_PARSER_HPP