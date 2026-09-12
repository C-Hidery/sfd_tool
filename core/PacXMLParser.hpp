// PacXMLParser.hpp
#ifndef PAC_XML_PARSER_HPP
#define PAC_XML_PARSER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdlib>
#include <iostream>
#include "XmlParser.hpp"

// ==================== 数据结构 ====================

struct BlockInfo {
    std::string id;      // 分区名（如 "l_modem"）
    std::string base;    // 基址（如 "0x00005000"）
    std::string size;    // 大小（如 "0x0"）
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

// ==================== PAC XML 解析器 ====================

class PacXMLParser {
public:
    bool loadFromFile(const std::string& filename) {
        XmlParser parser;
        auto root = parser.parseFile(filename);
        if (!root) {
            std::cerr << "Failed to parse XML: " << filename << std::endl;
            return false;
        }
        m_products.clear();
        m_productMap.clear();
        m_schemeMap.clear();
        m_schemeOrder.clear();
        parseRoot(root);
        return true;
    }

    // ---------- 新增：收集所有 ID ----------

    // 收集所有方案下、所有 File 的 <ID>，按 XML 中原始顺序返回
    std::vector<std::string> getAllIds() const {
        std::vector<std::string> ids;
        for (const auto& schemeName : m_schemeOrder) {
            auto it = m_schemeMap.find(schemeName);
            if (it == m_schemeMap.end()) continue;
            for (const auto& file : it->second.files) {
                if (!file.id.empty()) ids.push_back(file.id);
            }
        }
        return ids;
    }

    // 收集所有方案下、所有 File 的 <IDAlias>（为空时回退到 <ID>）
    std::vector<std::string> getAllAliases() const {
        std::vector<std::string> aliases;
        for (const auto& schemeName : m_schemeOrder) {
            auto it = m_schemeMap.find(schemeName);
            if (it == m_schemeMap.end()) continue;
            for (const auto& file : it->second.files) {
                aliases.push_back(file.idAlias.empty() ? file.id : file.idAlias);
            }
        }
        return aliases;
    }

    // 收集指定方案下所有 File 的 <ID>
    std::vector<std::string> getIdsByScheme(const std::string& schemeName) const {
        std::vector<std::string> ids;
        auto it = m_schemeMap.find(schemeName);
        if (it == m_schemeMap.end()) return ids;
        for (const auto& file : it->second.files) {
            if (!file.id.empty()) ids.push_back(file.id);
        }
        return ids;
    }

    // ---------- 查询接口 ----------

    // 获取所有产品名
    std::vector<std::string> getProductNames() const {
        std::vector<std::string> names;
        for (const auto& p : m_products) names.push_back(p.name);
        return names;
    }

    // 根据产品名获取方案名
    std::string getSchemeName(const std::string& productName) const {
        auto it = m_productMap.find(productName);
        return (it != m_productMap.end()) ? it->second : "";
    }

    // 根据方案名获取文件列表
    std::vector<FileInfo> getFilesByScheme(const std::string& schemeName) const {
        auto it = m_schemeMap.find(schemeName);
        return (it != m_schemeMap.end()) ? it->second.files : std::vector<FileInfo>();
    }

    // 根据产品名获取文件列表（通过其方案）
    std::vector<FileInfo> getFilesByProduct(const std::string& productName) const {
        std::string scheme = getSchemeName(productName);
        if (scheme.empty()) return {};
        return getFilesByScheme(scheme);
    }

    // 根据操作名（优先匹配 IDAlias，若无则匹配 ID）获取文件信息
    FileInfo getFileInfoByOperation(const std::string& opName) const {
        for (const auto& pair : m_schemeMap) {
            for (const auto& file : pair.second.files) {
                if (file.idAlias == opName || file.id == opName) return file;
            }
        }
        return FileInfo{};
    }

    // 获取操作名对应的分区名（第一个 Block 的 id）
    std::string getPartitionByOperation(const std::string& opName) const {
        auto info = getFileInfoByOperation(opName);
        if (!info.blocks.empty()) return info.blocks.front().id;
        return "";
    }

    // 获取操作名对应的基址（第一个 Block 的 base）
    std::string getBaseByOperation(const std::string& opName) const {
        auto info = getFileInfoByOperation(opName);
        if (!info.blocks.empty()) return info.blocks.front().base;
        return "";
    }

    // 获取操作名对应的大小（第一个 Block 的 size）
    std::string getSizeByOperation(const std::string& opName) const {
        auto info = getFileInfoByOperation(opName);
        if (!info.blocks.empty()) return info.blocks.front().size;
        return "";
    }

    // 获取所有方案名（按 XML 中出现顺序）
    std::vector<std::string> getSchemeNames() const {
        return m_schemeOrder;
    }

private:
    std::vector<ProductInfo> m_products;
    std::map<std::string, std::string> m_productMap;      // 产品名 -> 方案名
    std::map<std::string, SchemeInfo> m_schemeMap;        // 方案名 -> SchemeInfo
    std::vector<std::string> m_schemeOrder;               // 方案在 XML 中的原始顺序

    // 解析根节点
    void parseRoot(const std::shared_ptr<XmlNode>& root) {
        // 处理 ProductList
        auto productList = root->getFirstChild("ProductList");
        if (productList) {
            auto productNodes = productList->getChildren("Product");
            for (auto& pNode : productNodes) {
                ProductInfo product;
                product.name = pNode->getAttribute("name");
                auto schemeNode = pNode->getFirstChild("SchemeName");
                if (schemeNode) {
                    product.schemeName = schemeNode->getTextContent();
                }
                m_products.push_back(product);
                m_productMap[product.name] = product.schemeName;
            }
        }

        // 处理 SchemeList
        auto schemeList = root->getFirstChild("SchemeList");
        if (schemeList) {
            auto schemeNodes = schemeList->getChildren("Scheme");
            for (auto& sNode : schemeNodes) {
                SchemeInfo scheme;
                scheme.name = sNode->getAttribute("name");

                auto fileNodes = sNode->getChildren("File");
                for (auto& fNode : fileNodes) {
                    FileInfo file;
                    file.id = getChildText(fNode, "ID");
                    file.idAlias = getChildText(fNode, "IDAlias");
                    if (file.idAlias.empty()) file.idAlias = file.id;

                    file.type = getChildText(fNode, "Type");
                    file.flag = getChildInt(fNode, "Flag", 0);
                    file.checkFlag = getChildInt(fNode, "CheckFlag", 0);
                    file.description = getChildText(fNode, "Description");

                    auto blockNodes = fNode->getChildren("Block");
                    for (auto& bNode : blockNodes) {
                        BlockInfo block;
                        block.id = bNode->getAttribute("id");
                        block.base = getChildText(bNode, "Base");
                        block.size = getChildText(bNode, "Size");
                        file.blocks.push_back(block);
                    }
                    scheme.files.push_back(file);
                }
                m_schemeMap[scheme.name] = scheme;
                m_schemeOrder.push_back(scheme.name);
            }
        }
    }

    // 辅助：获取子节点文本
    static std::string getChildText(const std::shared_ptr<XmlNode>& node, const std::string& tag) {
        auto child = node->getFirstChild(tag);
        return child ? child->getTextContent() : "";
    }

    // 辅助：获取子节点文本并转换为整数（支持 0x 前缀）
    static int getChildInt(const std::shared_ptr<XmlNode>& node, const std::string& tag, int defaultVal = 0) {
        std::string text = getChildText(node, tag);
        if (text.empty()) return defaultVal;
        if (text.find("0x") == 0 || text.find("0X") == 0) {
            return static_cast<int>(strtol(text.c_str(), nullptr, 16));
        }
        return std::stoi(text);
    }
};

#endif // PAC_XML_PARSER_HPP