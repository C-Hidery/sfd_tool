#ifndef PAC_XML_PARSER_HPP
#define PAC_XML_PARSER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <functional>
#include "XmlParser.hpp"

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

class PacXMLParser {
public:
    PacXMLParser() = default;
    ~PacXMLParser() = default;

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
        parseRoot(root);
        return true;
    }

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
        if (!info.blocks.empty()) return info.blocks.front().id;
        return "";
    }

    std::string getBaseByOperation(const std::string& opName) const {
        auto info = getFileInfoByOperation(opName);
        if (!info.blocks.empty()) return info.blocks.front().base;
        return "";
    }

    std::string getSizeByOperation(const std::string& opName) const {
        auto info = getFileInfoByOperation(opName);
        if (!info.blocks.empty()) return info.blocks.front().size;
        return "";
    }

    std::vector<std::string> getSchemeNames() const {
        std::vector<std::string> names;
        for (const auto& pair : m_schemeMap) names.push_back(pair.first);
        return names;
    }

private:
    std::vector<ProductInfo> m_products;
    std::map<std::string, std::string> m_productMap;
    std::map<std::string, SchemeInfo> m_schemeMap;

    void parseRoot(const std::shared_ptr<XmlNode>& root) {
        auto productList = root->getFirstChild("ProductList");
        if (productList) {
            auto productNodes = productList->getChildren("Product");
            for (auto& pNode : productNodes) {
                ProductInfo product;
                product.name = pNode->getAttribute("name");
                auto schemeNode = pNode->getFirstChild("SchemeName");
                if (schemeNode) product.schemeName = schemeNode->getTextContent();
                m_products.push_back(product);
                m_productMap[product.name] = product.schemeName;
            }
        }

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
                m_schemeMap[scheme.name] = scheme;   // 使用 m_schemeMap
            }
        }
    }

    std::string getChildText(const std::shared_ptr<XmlNode>& node, const std::string& tag) {
        auto child = node->getFirstChild(tag);
        return child ? child->getTextContent() : "";
    }

    int getChildInt(const std::shared_ptr<XmlNode>& node, const std::string& tag, int defaultVal = 0) {
        std::string text = getChildText(node, tag);
        if (text.empty()) return defaultVal;
        if (text.find("0x") == 0 || text.find("0X") == 0) {
            return static_cast<int>(strtol(text.c_str(), nullptr, 16));
        }
        return std::stoi(text);
    }
};

#endif // PAC_XML_PARSER_HPP