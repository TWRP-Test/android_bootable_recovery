#ifndef _ABX_HPP
#define _ABX_HPP

#include <iosfwd>
#include <string>

// Stream-level: XML text stream -> Binary XML stream.
int encode_abx(std::istream& in, std::ostream& out);

// Stream-level: Binary XML stream -> XML text stream.
int decode_abx(std::istream& in, std::ostream& out);

int xml2abx(const std::string& input, const std::string& output, bool in_place);

int abx2xml(const std::string& input, const std::string& output, bool in_place);

#endif  // _ABX_HPP
