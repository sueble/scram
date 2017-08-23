/*
 * Copyright (C) 2014-2017 Olzhas Rakhimov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/// @file xml.h
/// XML helper facilities to work with libxml2.
/// Adaptors and helper functions provide read-only facilities.
///
/// @note All strings and characters are UTF-8 unless otherwise documented.
///
/// @note The facilities are designed specifically for SCRAM use cases.
///       The XML assumed to be well formed and simple.
///
/// @warning Complex XML features are not handled or expected,
///          for example, DTD, namespaces, entries.

#ifndef SCRAM_SRC_XML_H_
#define SCRAM_SRC_XML_H_

#include <iosfwd>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>

#include <boost/algorithm/string.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <boost/range/adaptor/filtered.hpp>

#include <libxml++/libxml++.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xinclude.h>

#include "error.h"

namespace scram {

namespace xml {

class Element;

}  // namespace xml

std::string GetLine(const xml::Element& xml_node);  // For error reporting.

namespace xml {

/// Gets a number from an XML value.
///
/// @tparam T  Numeric type.
///
/// @param[in] value  The non-empty value string.
///
/// @returns The interpreted value.
///
/// @throws ValidationError  Casting is unsuccessful.
template <typename T>
std::enable_if_t<std::is_arithmetic<T>::value, T>
CastValue(const std::string& value) {
  try {
    return boost::lexical_cast<T>(value);
  } catch (boost::bad_lexical_cast&) {
    throw ValidationError("Failed to interpret value '" + value +
                          "' to a number.");
  }
}

/// Specialization for Boolean values.
template <>
inline bool CastValue<bool>(const std::string& value) {
  if (value == "true" || value == "1")
    return true;
  if (value == "false" || value == "0")
    return false;
  throw LogicError("Boolean types must be validated in schema.");
}

/// Reinterprets the XML library UTF-8 string into C string.
///
/// @param[in] xml_string  The string provided by the XML library.
///
/// @returns The same string adapted for use as C string.
inline const char* from_utf8(const xmlChar* xml_string) noexcept {
  assert(xml_string);
  return reinterpret_cast<const char*>(xml_string);
}

/// Reinterprets C string as XML library UTF-8 string.
///
/// @param[in] c_string  The C byte-array encoding the XML string.
///
/// @returns The same string adapted for use in XML library functions.
///
/// @pre The C string has UTF-8 encoding.
inline const xmlChar* to_utf8(const char* c_string) noexcept {
  assert(c_string);
  return reinterpret_cast<const xmlChar*>(c_string);
}

/// XML Element adaptor.
class Element {
 public:
  /// The range for elements.
  /// This is a simple view adaptor
  /// to the linked list XML elements.
  class Range {
   public:
    using value_type = Element;  ///< Minimal container for Element type.

    /// Iterator over range elements.
    class iterator
        : public boost::iterator_facade<iterator, Element,
                                        std::forward_iterator_tag, Element> {
      friend class boost::iterator_core_access;

     public:
      /// @param[in] element  The starting element in the list.
      ///                     nullptr signifies the end.
      explicit iterator(const xmlElement* element = nullptr)
          : element_(element) {}

     private:
      /// Standard iterator functionality required by the facade facilities.
      /// @{
      void increment() {
        assert(element_ && "Incrementing end iterator!");
        element_ = Range::findElement(element_->next);
      }
      bool equal(const iterator& other) const {
        return element_ == other.element_;
      }
      Element dereference() const { return Element(element_); }
      /// @}

      const xmlElement* element_;  ///< The current element.
    };

    using const_iterator = iterator;  ///< The container is immutable.

    /// Constructs the range for the intrusive list of XML Element nodes.
    ///
    /// @param[in] head  The head node of the list (may be non-Element node!).
    ///                  nullptr if the list is empty.
    explicit Range(const xmlNode* head) : begin_(findElement(head)) {}

    /// The range begin and end iterators.
    /// @{
    iterator begin() const { return begin_; }
    iterator end() const { return iterator(); }
    iterator cbegin() const { return begin_; }
    iterator cend() const { return iterator(); }
    /// @}

    /// @return true if the range contains no elements.
    bool empty() const { return begin() == end(); }

    /// @returns The number of Elements in the list.
    ///
    /// @note O(N) complexity.
    std::size_t size() const { return std::distance(begin(), end()); }

    /// Extracts the element by its position.
    /// This is a temporary helper function to move from xmlpp::NodeSet.
    /// Use iterators and loops instead.
    ///
    /// @param[in] pos  The position of the element.
    ///
    /// @returns The element on the position.
    ///
    /// @throws std::out_of_range  The position is invalid.
    ///
    /// @note O(N) complexity unlike xmlpp::NodeSet O(1).
    ///
    /// @todo Remove.
    Element at(std::size_t pos) const {
      auto it = std::next(begin(), pos);
      if (it == end())
        throw std::out_of_range("The position is out of range.");
      return *it;
    }

   private:
    /// Finds the first Element node in the list.
    ///
    /// @param[in] node  The starting node.
    ///                  nullptr for the end node.
    ///
    /// @returns The first Element type node.
    ///          nullptr if the list does not contain any Element nodes.
    static const xmlElement* findElement(const xmlNode* node) noexcept {
      while (node && node->type != XML_ELEMENT_NODE)
        node = node->next;
      return reinterpret_cast<const xmlElement*>(node);
    }

    iterator begin_;  ///< The first node with XML Element.
  };

  /// @param[in] element  The element in the XML document.
  explicit Element(const xmlElement* element) : element_(element) {}

  /// @returns The URI of the file containing the element.
  ///
  /// @pre The document has been loaded from a file.
  std::string filename() const { return from_utf8(element_->doc->URL); }

  /// @returns The line number of the element.
  int line() const {
    return XML_GET_LINE(reinterpret_cast<const xmlNode*>(element_));
  }

  /// @returns The name of the XML element.
  ///
  /// @pre The element has a name.
  std::string name() const { return from_utf8(element_->name); }

  /// Retrieves the XML element's attribute values.
  ///
  /// @param[in] name  The name of the requested attribute.
  ///
  /// @returns The attribute value or
  ///          empty string if no attribute (optional attribute).
  ///
  /// @pre XML attributes never contain empty strings.
  /// @pre XML attribute values are simple texts w/o DTD processing.
  std::string attribute(const std::string& name) const {
    const xmlAttr* property = xmlHasProp(to_node(), to_utf8(name.c_str()));
    if (!property)
      return "";
    const xmlNode* text_node = property->children;
    assert(text_node && text_node->type == XML_TEXT_NODE);
    assert(text_node->content);
    std::string value = from_utf8(text_node->content);
    boost::trim(value);
    return value;
  }

  /// Queries element attribute existence.
  ///
  /// @param[in] name  The non-empty attribute name.
  ///
  /// @returns true if the element has an attribute with the given name.
  ///
  /// @note This is an inefficient way to work with optional attributes.
  ///       Use the ``attribute(name)`` member function directly for optionals.
  bool has_attribute(const std::string& name) const {
    return xmlHasProp(to_node(), to_utf8(name.c_str())) != nullptr;
  }

  /// @returns The XML element's text.
  ///
  /// @pre The Element has text.
  std::string text() const {
    const xmlNode* text_node = element_->children;
    while (text_node && text_node->type != XML_TEXT_NODE)
      text_node = text_node->next;
    assert(text_node && "Element does not have text.");
    assert(text_node->content && "Missing text in Element.");
    std::string content = from_utf8(text_node->content);
    boost::trim(content);
    return content;
  }

  /// Generic attribute value extraction following XML data types.
  ///
  /// @tparam T  The attribute value type (numeric).
  ///
  /// @param[in] name  The name of the attribute.
  ///
  /// @returns The value of type T interpreted from attribute value.
  ///          None if the attribute doesn't exists (optional).
  ///
  /// @throws ValidationError  Casting is unsuccessful.
  template <typename T>
  std::enable_if_t<std::is_arithmetic<T>::value, boost::optional<T>>
  attribute(const std::string& name) const {
    std::string value = attribute(name);
    if (value.empty())
      return {};
    try {
      return CastValue<T>(value);
    } catch (ValidationError& err) {
      err.msg(GetLine(*this) + "Attribute '" + name + "': " + err.msg());
      throw;
    }
  }

  /// Generic text value extraction following XML data types.
  ///
  /// @tparam T  The attribute value type (numeric).
  ///
  /// @returns The value of type T interpreted from attribute value.
  ///
  /// @pre The text is not empty.
  ///
  /// @throws ValidationError  Casting is unsuccessful.
  template <typename T>
  std::enable_if_t<std::is_arithmetic<T>::value, T> text() const {
    try {
      return CastValue<T>(text());
    } catch (ValidationError& err) {
      err.msg(GetLine(*this) + "Text element: " + err.msg());
      throw;
    }
  }

  /// @param[in] name  The name of the child element.
  ///                  Empty string to request any first child element.
  ///
  /// @returns The first child element (with the given name).
  boost::optional<Element> child(const std::string& name = "") const {
    for (Element element : children()) {
      if (name.empty() || name == element.name())
        return element;
    }
    return {};
  }

  /// @returns All the Element children.
  Range children() const { return Range(element_->children); }

  /// @param[in] name  The name to filter children elements.
  ///
  /// @returns The range of Element children with the given name.
  auto children(std::string name) const {
    return children() |
           boost::adaptors::filtered([name](const Element& element) {
             return element.name() == name;
           });
  }

 private:
  /// Converts the data to its base.
  const xmlNode* to_node() const {
    return reinterpret_cast<const xmlNode*>(element_);
  }

  const xmlElement* element_;  ///< The main data location.
};

/// XML DOM tree document.
class Document {
 public:
  /// @param[in] doc  Fully parsed document.
  explicit Document(const xmlpp::Document* doc) : doc_(*doc) {}

  /// @returns The root element of the document.
  ///
  /// @pre The document has a root node.
  Element root() const {
    return Element(
        reinterpret_cast<const xmlElement*>(doc_.get_root_node()->cobj()));
  }

  /// @returns The underlying data document.
  const xmlpp::Document* get() const { return &doc_; }

 private:
  const xmlpp::Document& doc_;  ///< The XML DOM document.
};

/// RelaxNG validator.
class Validator {
 public:
  /// @param[in] rng_file  The path to the schema file.
  ///
  /// @throws The library provided error for invalid XML RNG schema file.
  ///
  /// @todo Properly wrap the exception for invalid schema files.
  explicit Validator(const std::string& rng_file) : validator_(rng_file) {}

  /// Validates XML DOM documents against the schema.
  ///
  /// @param[in] doc  The initialized XML DOM document.
  ///
  /// @throws ValidationError  The document failed schema validation.
  void validate(const Document& doc) {
    try {
      validator_.validate(doc.get());
    } catch (const xmlpp::validity_error&) {
      throw ValidationError("Document failed schema validation:\n" +
                            xmlpp::format_xml_error());
    }
  }

 private:
  xmlpp::RelaxNGValidator validator_;  ///< The validator from the XML library.
};

/// DOM Parser.
///
/// @note The document lifetime is managed by the parser.
///
/// @todo Decouple the document lifetime from its parser.
class Parser {
 public:
  /// Initializes a DOM parser,
  /// parses XML input document,
  /// and converts library exceptions into local errors.
  ///
  /// All XInclude directives are processed into the final document.
  ///
  /// @param[in] file_path  The path to the document file.
  /// @param[in] validator  Optional validator against the RNG schema.
  ///
  /// @throws ValidationError  There are problems loading the XML file.
  explicit Parser(const std::string& file_path,
                  Validator* validator = nullptr) {
    try {
      parser_ = std::make_unique<xmlpp::DomParser>(file_path);
      xmlXIncludeProcessFlags(parser_->get_document()->cobj(),
                              XML_PARSE_NOBASEFIX);
      parser_->get_document()->process_xinclude();
    } catch (const xmlpp::exception& ex) {
      throw ValidationError("XML file is invalid:\n" + std::string(ex.what()));
    }

    if (validator)
      validator->validate(Document(parser_->get_document()));
  }

  /// Overload to parse stream.
  explicit Parser(std::istream& input_stream, Validator* validator = nullptr) {
    try {
      parser_ = std::make_unique<xmlpp::DomParser>();
      parser_->parse_stream(input_stream);
      xmlXIncludeProcessFlags(parser_->get_document()->cobj(),
                              XML_PARSE_NOBASEFIX);
      parser_->get_document()->process_xinclude();
    } catch (const xmlpp::exception& ex) {
      throw ValidationError("XML input is invalid:\n" + std::string(ex.what()));
    }
    if (validator)
      validator->validate(Document(parser_->get_document()));
  }

  /// @returns The parsed document.
  Document document() const { return Document(parser_->get_document()); }

 private:
  std::unique_ptr<xmlpp::DomParser> parser_;  ///< The XML library DOM parser.
};

}  // namespace xml

/// Returns XML line number message.
inline std::string GetLine(const xml::Element& xml_node) {
  return "Line " + std::to_string(xml_node.line()) + ":\n";
}

}  // namespace scram

#endif  // SCRAM_SRC_XML_H_
