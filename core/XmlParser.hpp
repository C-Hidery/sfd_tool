#ifndef XML_PARSER_HPP
#define XML_PARSER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cctype>
#include <fstream>   // 用于 std::ifstream
#include <cstdio>
#include <cstdlib>

#ifndef __cplusplus
    #error "This header requires C++. Please compile with a C++ compiler."
#endif


// ==================== XML 节点类 ====================
class XmlNode : public std::enable_shared_from_this<XmlNode> {
public:
    std::string name;
    std::map<std::string, std::string> attributes;
    std::string text;
    std::vector<std::shared_ptr<XmlNode>> children;
    XmlNode* parent = nullptr;

    XmlNode() = default;
    explicit XmlNode(const std::string& n) : name(n) {}

    std::shared_ptr<XmlNode> getFirstChild(const std::string& tagName) const {
        for (auto& child : children) {
            if (child->name == tagName) return child;
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<XmlNode>> getChildren(const std::string& tagName) const {
        std::vector<std::shared_ptr<XmlNode>> res;
        for (auto& child : children) {
            if (child->name == tagName) res.push_back(child);
        }
        return res;
    }

    std::shared_ptr<XmlNode> getFirstDescendant(const std::string& tagName) {
        if (name == tagName) return shared_from_this();
        for (auto& child : children) {
            auto found = child->getFirstDescendant(tagName);
            if (found) return found;
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<XmlNode>> getDescendants(const std::string& tagName) {
        std::vector<std::shared_ptr<XmlNode>> res;
        if (name == tagName) res.push_back(shared_from_this());
        for (auto& child : children) {
            auto sub = child->getDescendants(tagName);
            res.insert(res.end(), sub.begin(), sub.end());
        }
        return res;
    }

    std::string getTextContent() const {
        if (children.empty()) return text;
        std::string result;
        for (auto& child : children) {
            result += child->getTextContent();
        }
        return result;
    }
    std::string toXml() const {
        std::string result = "<" + name;
        // 添加属性
        for (auto& attr : attributes) {
            result += " " + attr.first + "=\"" + attr.second + "\"";
        }
        result += ">";
        // 添加文本内容（如果存在且没有子节点，或者为了保留原始格式可直接添加）
        result += text;
        // 递归添加子节点
        for (auto& child : children) {
            result += child->toXml();
        }
        result += "</" + name + ">";
        return result;
    }
    std::string getAttribute(const std::string& name, const std::string& defaultValue = "") const {
        auto it = attributes.find(name);
        return (it != attributes.end()) ? it->second : defaultValue;
    }
    void setAttribute(const std::string& key, const std::string& value) {
        attributes[key] = value;
    }

    void setText(const std::string& newText) {
        text = newText;
    }

    void addChild(std::shared_ptr<XmlNode> child) {
        child->parent = this;
        children.push_back(child);
    }
    void removeChild(const std::string& tagName) {
        children.erase(std::remove_if(children.begin(), children.end(),
            [&](auto& child) { return child->name == tagName; }), children.end());
    }
    void removeSelf() {
        if (parent) {
            auto& siblings = parent->children;
            siblings.erase(std::remove_if(siblings.begin(), siblings.end(),
                [this](const std::shared_ptr<XmlNode>& child) {
                    return child.get() == this;
                }), siblings.end());
        }
    }
    void removeChild(std::shared_ptr<XmlNode> child) {
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }
    void removeDescendants(const std::string& tagName) {
        children.erase(std::remove_if(children.begin(), children.end(),
            [&](auto& child) {
                if (child->name == tagName) return true;
                child->removeDescendants(tagName);
                return false;
            }), children.end());
    }
    void removeChildIf(std::function<bool(const std::shared_ptr<XmlNode>&)> predicate) {
        children.erase(std::remove_if(children.begin(), children.end(), predicate), children.end());
    }
    bool hasChild(const std::string& tagName) const {
        for (auto& child : children) {
            if (child->name == tagName) return true;
        }
        return false;
    }
    bool hasDescendant(const std::string& tagName) const {
        if (name == tagName) return true;
        for (auto& child : children) {
            if (child->hasDescendant(tagName)) return true;
        }
        return false;
    }
    bool hasAttribute(const std::string& attrName) const {
        return attributes.find(attrName) != attributes.end();
    }
    bool hasAttributeWithValue(const std::string& attrName, const std::string& value) const {
        auto it = attributes.find(attrName);
        return (it != attributes.end()) && (it->second == value);
    }
    bool hasChildWithAttribute(const std::string& tagName, const std::string& attrName, const std::string& value) const {
        for (auto& child : children) {
            if (child->name == tagName && child->hasAttributeWithValue(attrName, value)) return true;
        }
        return false;
    }
    bool hasDescendantWithAttribute(const std::string& tagName, const std::string& attrName, const std::string& value) const {
        if (name == tagName && hasAttributeWithValue(attrName, value)) return true;
        for (auto& child : children) {
            if (child->hasDescendantWithAttribute(tagName, attrName, value)) return true;
        }
        return false;
    }
    bool hasChildWithText(const std::string& tagName, const std::string& textValue) const {
        for (auto& child : children) {
            if (child->name == tagName && child->getTextContent() == textValue) return true;
        }
        return false;
    }
    bool hasDescendantWithText(const std::string& tagName, const std::string& textValue) const {
        if (name == tagName && getTextContent() == textValue) return true;
        for (auto& child : children) {
            if (child->hasDescendantWithText(tagName, textValue)) return true;
        }
        return false;
    }
    bool saveXmlFile(const std::string& filename) const {
        std::ofstream file(filename);
        if (!file.is_open()) return false;
        file << toXml();
        return true;
    }
};

// ==================== XML 解析器类 ====================
class XmlParser {
public:
    std::shared_ptr<XmlNode> parseString(const std::string& xml) {
        pos = 0;
        data = xml;
        skipWhitespace();

        // 跳过 XML 声明
        if (peek() == '<' && data.compare(pos, 5, "<?xml") == 0) {
            while (pos < data.size() && !(data[pos] == '?' && pos + 1 < data.size() && data[pos + 1] == '>'))
                pos++;
            pos += 2;
            skipWhitespace();
        }

        if (peek() == '<') {
            return parseElement();
        }
        return nullptr;
    }

    std::shared_ptr<XmlNode> parseFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return nullptr;
        
        std::streamsize size = file.tellg();
        if (size <= 0) return nullptr;
        
        file.seekg(0, std::ios::beg);
        std::string content(static_cast<size_t>(size), '\0');
        if (!file.read(&content[0], size)) return nullptr;
        
        return parseString(content);
    }

private:
    std::string data;
    size_t pos = 0;

    char peek() const {
        if (pos >= data.size()) return 0;
        return data[pos];
    }

    char consume() {
        if (pos >= data.size()) return 0;
        return data[pos++];
    }

    void skipWhitespace() {
        while (pos < data.size() && std::isspace(static_cast<unsigned char>(data[pos])))
            pos++;
    }

    bool expect(char ch) {
        if (peek() == ch) {
            consume();
            return true;
        }
        return false;
    }

    std::string parseName() {
        std::string name;
        while (pos < data.size() && (std::isalnum(static_cast<unsigned char>(data[pos])) ||
               data[pos] == '_' || data[pos] == ':' || data[pos] == '-'))
            name += consume();
        return name;
    }

    std::string parseAttributeValue() {
        char quote = consume(); // ' or "
        std::string value;
        while (pos < data.size() && data[pos] != quote) {
            if (data[pos] == '&')
                value += parseEntity();
            else
                value += consume();
        }
        if (peek() == quote) consume();
        return value;
    }

    std::string parseEntity() {
        if (data[pos] != '&') return "";
        consume();
        std::string entity;
        while (pos < data.size() && data[pos] != ';')
            entity += consume();
        if (peek() == ';') consume();
        if (entity == "lt")   return "<";
        if (entity == "gt")   return ">";
        if (entity == "amp")  return "&";
        if (entity == "quot") return "\"";
        if (entity == "apos") return "'";
        // 数字实体
        if (!entity.empty() && entity[0] == '#') {
            if (entity.size() > 1 && entity[1] == 'x') {
                long code = strtol(entity.c_str() + 2, nullptr, 16);
                if (code > 0 && code < 0x80) return std::string(1, static_cast<char>(code));
            } else {
                long code = strtol(entity.c_str() + 1, nullptr, 10);
                if (code > 0 && code < 0x80) return std::string(1, static_cast<char>(code));
            }
        }
        return "&" + entity + ";";
    }

    std::shared_ptr<XmlNode> parseElement() {
        if (!expect('<')) return nullptr;
        std::string name = parseName();
        if (name.empty()) return nullptr;
        auto node = std::make_shared<XmlNode>(name);

        // 属性
        skipWhitespace();
        while (pos < data.size() && peek() != '>' && peek() != '/') {
            std::string attrName = parseName();
            skipWhitespace();
            if (expect('=')) {
                skipWhitespace();
                if (peek() == '"' || peek() == '\'') {
                    std::string attrValue = parseAttributeValue();
                    node->attributes[attrName] = attrValue;
                }
            }
            skipWhitespace();
        }

        // 自闭合
        if (expect('/')) {
            if (!expect('>')) return nullptr;
            return node;
        }
        if (!expect('>')) return nullptr;

        // 子内容
        while (pos < data.size() && !(peek() == '<' && pos + 1 < data.size() && data[pos + 1] == '/')) {
            if (peek() == '<') {
                // 注释
                if (data.compare(pos, 4, "<!--") == 0) {
                    pos += 4;
                    while (pos < data.size() && !(data[pos] == '-' && pos + 2 < data.size() &&
                           data[pos+1] == '-' && data[pos+2] == '>'))
                        pos++;
                    pos += 3;
                    continue;
                }
                // CDATA
                if (data.compare(pos, 9, "<![CDATA[") == 0) {
                    pos += 9;
                    std::string cdata;
                    while (pos < data.size() && !(data[pos] == ']' && pos + 2 < data.size() &&
                           data[pos+1] == ']' && data[pos+2] == '>'))
                        cdata += consume();
                    pos += 3;
                    node->text += cdata;
                    continue;
                }
                // 处理指令
                if (data.compare(pos, 2, "<?") == 0) {
                    pos += 2;
                    while (pos < data.size() && !(data[pos] == '?' && pos + 1 < data.size() && data[pos+1] == '>'))
                        pos++;
                    pos += 2;
                    continue;
                }
                // 子元素
                auto child = parseElement();
                if (child) {
                    child->parent = node.get();
                    node->children.push_back(child);
                }
            } else {
                std::string text;
                while (pos < data.size() && peek() != '<') {
                    if (peek() == '&')
                        text += parseEntity();
                    else
                        text += consume();
                }
                node->text += text;
            }
        }

        // 结束标签
        if (expect('<') && expect('/')) {
            std::string endName = parseName();
            skipWhitespace();
            expect('>');  // 消耗掉 '>'
        }
        return node;
    }
};

#endif // XML_PARSER_HPP