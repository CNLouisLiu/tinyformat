// tinyformat.h
// Copyright (C) 2011, Chris Foster [chris42f (at) gmail (d0t) com]
//
// Boost Software License - Version 1.0
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
//
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

//------------------------------------------------------------------------------
// Tinyformat: A minimal type safe printf-replacement library for C++
//
// This library aims to support 95% of casual C++ string formatting needs with
// a single lightweight header file.  Anything you can do with this library
// can also be done with the standard C++ streams, but probably with
// considerably more typing :)
//
// Design goals:
//
// * Simplicity and minimalism.  A single header file to include and distribute
//   with your own projects.
// * Type safety and extensibility for user defined types.
// * Parse standard C99 format strings, and support most features.
// * Support as many commonly used ``printf()`` features as practical without
//   compromising on simplicity.
//
//
// Example usage
// -------------
//
// To print the date, we might have
//
// std::string weekday = "Wednesday";
// const char* month = "July";
// long day = 27;
// int hour = 14;
// int min = 44;
//
// tfm::format(std::cout, "%s, %s %d, %.2d:%.2d\n",
//             weekday, month, day, hour, min);
//
// (The types here are intentionally odd to emphasize the type safety of the
// interface.)  The same thing could be achieved using either of the two
// convenience functions.  One returns a std::string:
//
// std::string date = tfm::format("%s, %s %d, %.2d:%.2d\n",
//                                weekday, month, day, hour, min);
// std::cout << date;
//
// The other prints to the std::cout stream:
//
// tfm::printf("%s, %s %d, %.2d:%.2d\n", weekday, month, day, hour, min);
//
//
// Brief outline of functionality
// ------------------------------
//
// (For full docs, see the accompanying README)
//
//
// Interface functions:
//
//  template<typename T1, typename T2, ...>
//  void format(std::ostream& stream, const char* formatString,
//              const T1& value1, const T2& value1, ...)
//
//  template<typename T1, typename T2, ...>
//  std::string format(const char* formatString,
//                     const T1& value1, const T2& value1, ...)
//
//  template<typename T1, typename T2, ...>
//  void printf(const char* formatString,
//              const T1& value1, const T2& value1, ...)
//
//
// Error handling: Define TINYFORMAT_ERROR to customize the error handling,
// otherwise calls assert() on error.
//
// User defined types: Overload formatValue() or formatValueBasic() to
// customize printing of user defined types.  Uses operator<< by default.
//
// Wrapping tfm::format inside a user defined format function: See the macros
// TINYFORMAT_WRAP_FORMAT and TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS.



#ifndef TINYFORMAT_H_INCLUDED
#define TINYFORMAT_H_INCLUDED

#include <cassert>
#include <iostream>
#include <sstream>

namespace tinyformat {}
//------------------------------------------------------------------------------
// Config section.  Customize to your liking!

// Namespace alias to encourage brevity
namespace tfm = tinyformat;

// Error handling; calls assert() by default.
// #define TINYFORMAT_ERROR(reasonString) your_error_handler(reasonString)

// Define for C++0x variadic templates which make the code shorter & more
// general.  If you don't define this, C++0x support is autodetected below.
// #define TINYFORMAT_USE_VARIADIC_TEMPLATES


//------------------------------------------------------------------------------
// Implementation details.
#ifndef TINYFORMAT_ERROR
#   define TINYFORMAT_ERROR(reason) assert(0 && reason)
#endif

#if !defined(TINYFORMAT_USE_VARIADIC_TEMPLATES) && !defined(TINYFORMAT_NO_VARIADIC_TEMPLATES)
#   ifdef __GXX_EXPERIMENTAL_CXX0X__
#       define TINYFORMAT_USE_VARIADIC_TEMPLATES
#   endif
#endif

#ifdef __GNUC__
#   define TINYFORMAT_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#   define TINYFORMAT_NOINLINE __declspec(noinline)
#else
#   define TINYFORMAT_NOINLINE
#endif


namespace tinyformat {

//------------------------------------------------------------------------------
namespace detail {

// Parse and return an integer from the string c, as atoi()
// On return, c is set to one past the end of the integer.
inline int parseIntAndAdvance(const char*& c)
{
    int i = 0;
    for(;*c >= '0' && *c <= '9'; ++c)
        i = 10*i + (*c - '0');
    return i;
}


// Flags for features not representable with standard stream state
enum ExtraFormatFlags
{
    Flag_TruncateToPrecision = 1<<0, // truncate length to stream precision()
    Flag_SpacePadPositive    = 1<<1, // pad positive values with spaces
};


// Parse the format string and set the stream state accordingly.
//
// The format mini-language recognized here is meant to be the one from C99,
// with the form "%[flags][width][.precision][length]type".
//
// The return value is a bitwise combination of values from the
// ExtraFormatFlags enum, containing formatting options which can't be natively
// represented using the ostream state.
inline unsigned int streamStateFromFormat(std::ostream& out,
                                          const char* fmtStart,
                                          const char* fmtEnd)
{
    // Reset stream state to defaults.
    out.width(0);
    out.precision(6);
    out.fill(' ');
    // Reset most flags, though leave the boolalpha state alone - there's no
    // equivalent format flag anyway.  Also ignore irrelevant unitbuf & skipws.
    out.unsetf(std::ios::adjustfield | std::ios::basefield |
               std::ios::floatfield | std::ios::showbase |
               std::ios::showpoint | std::ios::showpos | std::ios::uppercase);
    unsigned int extraFlags = 0;
    bool precisionSet = false;
    bool widthSet = false;
    const char* c = fmtStart;
    // 1) Parse flags
    for(;; ++c)
    {
        switch(*c)
        {
            case '#':
                out.setf(std::ios::showpoint | std::ios::showbase);
                continue;
            case '0':
                // overridden by left alignment ('-' flag)
                if(!(out.flags() & std::ios::left))
                {
                    // Use internal padding so that numeric values are
                    // formatted correctly, eg -00010 rather than 000-10
                    out.fill('0');
                    out.setf(std::ios::internal, std::ios::adjustfield);
                }
                continue;
            case '-':
                out.fill(' ');
                out.setf(std::ios::left, std::ios::adjustfield);
                continue;
            case ' ':
                // overridden by show positive sign, '+' flag.
                if(!(out.flags() & std::ios::showpos))
                    extraFlags |= Flag_SpacePadPositive;
                continue;
            case '+':
                out.setf(std::ios::showpos);
                extraFlags &= ~Flag_SpacePadPositive;
                continue;
        }
        break;
    }
    // 2) Parse width
    if(*c >= '0' && *c <= '9')
    {
        widthSet = true;
        out.width(parseIntAndAdvance(c));
    }
    if(*c == '*')
        TINYFORMAT_ERROR("tinyformat: variable field widths not supported");
    // 3) Parse precision
    if(*c == '.')
    {
        ++c;
        if(*c == '*')
            TINYFORMAT_ERROR("tinyformat: variable precision not supported");
        int precision = 0;
        if(*c >= '0' && *c <= '9')
            precision = parseIntAndAdvance(c);
        else if(*c == '-') // negative precisions ignored, treated as zero.
            parseIntAndAdvance(++c);
        out.precision(precision);
        precisionSet = true;
    }
    // 4) Ignore any C99 length modifier
    while(*c == 'l' || *c == 'h' || *c == 'L' ||
          *c == 'j' || *c == 'z' || *c == 't')
        ++c;
    // 5) We're up to the conversion specifier character.
    char type = 's';
    if(c < fmtEnd)
        type = *c;
    // Set stream flags based on conversion specifier (thanks to the
    // boost::format class for forging the way here).
    bool intConversion = false;
    switch(type)
    {
        case 'u': case 'd': case 'i':
            out.setf(std::ios::dec, std::ios::basefield);
            intConversion = true;
            break;
        case 'o':
            out.setf(std::ios::oct, std::ios::basefield);
            intConversion = true;
            break;
        case 'X':
            out.setf(std::ios::uppercase);
        case 'x': case 'p':
            out.setf(std::ios::hex, std::ios::basefield);
            intConversion = true;
            break;
        case 'E':
            out.setf(std::ios::uppercase);
        case 'e':
            out.setf(std::ios::scientific, std::ios::floatfield);
            out.setf(std::ios::dec, std::ios::basefield);
            break;
        case 'F':
            out.setf(std::ios::uppercase);
        case 'f':
            out.setf(std::ios::fixed, std::ios::floatfield);
            break;
        case 'G':
            out.setf(std::ios::uppercase);
        case 'g':
            out.setf(std::ios::dec, std::ios::basefield);
            // As in boost::format, let stream decide float format.
            out.flags(out.flags() & ~std::ios::floatfield);
            break;
        case 'a': case 'A':
            break; // C99 hexadecimal floating point??  punt!
        case 'c':
            // Handled as special case inside formatValue()
            break;
        case 's':
            if(precisionSet)
                extraFlags |= Flag_TruncateToPrecision;
            break;
        case 'n':
            // Not supported - will cause problems!
            TINYFORMAT_ERROR("tinyformat: %n conversion spec not supported");
            break;
    }
    if(intConversion && precisionSet && !widthSet)
    {
        // "precision" for integers gives the minimum number of digits (to be
        // padded with zeros on the left).  This isn't really supported by the
        // iostreams, but we can approximately simulate it with the width if
        // the width isn't otherwise used.
        out.width(out.precision());
        out.setf(std::ios::internal, std::ios::adjustfield);
        out.fill('0');
    }
    // we shouldn't be past the end, though we may equal it if the input
    // format was broken and ended with '\0'.
    assert(c <= fmtEnd);
    return extraFlags;
}


// Print literal part of format string and return next format spec position.
//
// Skips over any occurrences of '%%', printing a literal '%' to the output.
// The position of the first non-'%' character of the next format spec is
// returned, or the end of string.
inline const char* printFormatStringLiteral(std::ostream& out, const char* fmt)
{
    const char* c = fmt;
    for(; *c != '\0'; ++c)
    {
        if(*c == '%')
        {
            out.write(fmt, static_cast<std::streamsize>(c - fmt));
            fmt = ++c;
            if(*c != '%')
                return c;
            // for '%%' the required '%' will be tacked onto the next section.
        }
    }
    out.write(fmt, static_cast<std::streamsize>(c - fmt));
    return c;
}


// Skip to end of format spec & return it.  fmt is expected to point to the
// character after the '%' in the spec.
inline const char* findFormatSpecEnd(const char* fmt)
{
    // Advance to end of format specifier.
    const char* c = fmt;
    if(*c == '\0')
        TINYFORMAT_ERROR("tinyformat: Not enough conversion specifiers in format string");
    for(; *c != '\0'; ++c)
    {
        // For compatibility with C, argument length modifiers don't terminate
        // the format
        if(*c == 'l' || *c == 'h' || *c == 'L' ||
           *c == 'j' || *c == 'z' || *c == 't')
            continue;
        // ... but for generality any other upper or lower case letter does
        if((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z'))
            return c+1;
    }
    TINYFORMAT_ERROR("tinyformat: Conversion spec incorrectly terminated by end of string");
    return c;
}


// Test whether type T1 is convertible to type T2
template <typename T1, typename T2>
struct is_convertible
{
    private:
        // two types of different size
        struct fail { char dummy[2]; };
        struct succeed { char dummy; };
        // Try to convert a T1 to a T2 by plugging into tryConvert
        static fail tryConvert(...);
        static succeed tryConvert(const T2&);
        static const T1& makeT1();
    public:
#       ifdef _MSC_VER
        // Disable spurious loss of precision warning in tryConvert(makeT1())
#       pragma warning(push)
#       pragma warning(disable:4244)
#       endif
        // Standard trick: the (...) version of tryConvert will be chosen from
        // the overload set only if the version taking a T2 doesn't match.
        // Then we compare the sizes of the return types to check which
        // function matched.  Very neat, in a disgusting kind of way :)
        static const bool value =
            sizeof(tryConvert(makeT1())) == sizeof(succeed);
#       ifdef _MSC_VER
#       pragma warning(pop)
#       endif
};


// Format the value by casting to type fmtT.  This default implementation
// should never be called.
template<typename T, typename fmtT, bool convertible>
struct formatValueAsType
{
    static void invoke(std::ostream& out, const T& value) { assert(0); }
};
// Specialized version for types that can actually be converted to fmtT, as
// indicated by the "convertible" template parameter.
template<typename T, typename fmtT>
struct formatValueAsType<T,fmtT,true>
{
    static void invoke(std::ostream& out, const T& value)
        { out << static_cast<fmtT>(value); }
};


// Format at most truncLen characters of a C string to the given stream.
// Return true if formatting proceeded (generic version always returns false)
template<typename T>
inline bool formatCStringTruncate(std::ostream& out, const T& value,
                                  std::streamsize truncLen)
{
    return false;
}
#define TINYFORMAT_DEFINE_FORMAT_C_STRING_TRUNCATE(type)             \
inline bool formatCStringTruncate(std::ostream& out, type* value,    \
                                  std::streamsize truncLen)          \
{                                                                    \
    std::streamsize len = 0;                                         \
    while(len < truncLen && value[len] != 0)                         \
        ++len;                                                       \
    out.write(value, len);                                           \
    return true;                                                     \
}
// Overload for const char* and char*.  Could overload for signed & unsigned
// char too, but these are technically unneeded for printf compatibility.
TINYFORMAT_DEFINE_FORMAT_C_STRING_TRUNCATE(const char)
TINYFORMAT_DEFINE_FORMAT_C_STRING_TRUNCATE(char)
#undef TINYFORMAT_DEFINE_FORMAT_C_STRING_TRUNCATE

} // namespace detail


//------------------------------------------------------------------------------
// Variable formatting functions.  May be overridden for user-defined types if
// desired.


// Format a value into a stream. Called from format() for all types by default.
//
// Users may override this for their own types.  When this function is called,
// the stream flags will have been modified according to the format string.
// The format specification is provided in the range [fmtBegin, fmtEnd).
//
// By default, formatValue() uses the usual stream insertion operator
// operator<< to format the type T, with special cases for the %c and %p
// conversions.
template<typename T>
inline void formatValue(std::ostream& out, const char* fmtBegin,
                        const char* fmtEnd, const T& value)
{
    // The mess here is to support the %c and %p conversions: if these
    // conversions are active we try to convert the type to a char or const
    // void* respectively and format that instead of the value itself.  For the
    // %p conversion it's important to avoid dereferencing the pointer, which
    // could otherwise lead to a crash when printing a dangling (const char*).
    const bool canConvertToChar = detail::is_convertible<T,char>::value;
    const bool canConvertToVoidPtr = detail::is_convertible<T, const void*>::value;
    if(canConvertToChar && *(fmtEnd-1) == 'c')
        detail::formatValueAsType<T, char, canConvertToChar>::invoke(out, value);
    else if(canConvertToVoidPtr && *(fmtEnd-1) == 'p')
        detail::formatValueAsType<T, const void*, canConvertToVoidPtr>::invoke(out, value);
    else
        out << value;
}


// Overloaded version for char types to support printing as an integer
#define TINYFORMAT_DEFINE_FORMATVALUE_CHAR(charType)                  \
inline void formatValue(std::ostream& out, const char* fmtBegin,      \
                        const char* fmtEnd, charType value)           \
{                                                                     \
    switch(*(fmtEnd-1))                                               \
    {                                                                 \
        case 'u': case 'd': case 'i': case 'o': case 'X': case 'x':   \
            out << static_cast<int>(value); break;                    \
        default:                                                      \
            out << value;                   break;                    \
    }                                                                 \
}
// per 3.9.1: char, signed char and unsigned char are all distinct types
TINYFORMAT_DEFINE_FORMATVALUE_CHAR(char)
TINYFORMAT_DEFINE_FORMATVALUE_CHAR(signed char)
TINYFORMAT_DEFINE_FORMATVALUE_CHAR(unsigned char)
#undef TINYFORMAT_DEFINE_FORMATVALUE_CHAR


// Format a value into a stream, called for all types by format()
//
// Users should override this function for their own types if they intend to
// completely customize the formatting, and don't want tinyformat to attempt
// to set the stream flags based on the format specifier string.
//
// The format specification is provided in the range [fmtBegin, fmtEnd).
//
// Trying to avoid inlining here greatly reduces bloat in optimized builds
template<typename T>
TINYFORMAT_NOINLINE
void formatValueBasic(std::ostream& out, const char* fmtBegin,
                      const char* fmtEnd, const T& value)
{
    // Save stream state
    std::streamsize width = out.width();
    std::streamsize precision = out.precision();
    std::ios::fmtflags flags = out.flags();
    char fill = out.fill();
    // Set stream state.
    unsigned extraFlags = detail::streamStateFromFormat(out, fmtBegin, fmtEnd);
    // Format the value into the stream.
    if(!extraFlags)
        formatValue(out, fmtBegin, fmtEnd, value);
    else
    {
        // The following are special cases where there's no direct
        // correspondence between stream formatting and the printf() behaviour.
        // Instead, we simulate the behaviour crudely by formatting into a
        // temporary string stream and munging the resulting string.
        std::ostringstream tmpStream;
        tmpStream.copyfmt(out);
        if(extraFlags & detail::Flag_SpacePadPositive)
            tmpStream.setf(std::ios::showpos);
        // formatCStringTruncate is required for truncating conversions like
        // "%.4s" where at most 4 characters of the c-string should be read.
        // If we didn't include this special case, we might read off the end.
        if(!( (extraFlags & detail::Flag_TruncateToPrecision) &&
             detail::formatCStringTruncate(tmpStream, value, out.precision()) ))
        {
            // Not a truncated c-string; just format normally.
            formatValue(tmpStream, fmtBegin, fmtEnd, value);
        }
        std::string result = tmpStream.str(); // allocates... yuck.
        if(extraFlags & detail::Flag_SpacePadPositive)
        {
            for(size_t i = 0, iend = result.size(); i < iend; ++i)
                if(result[i] == '+')
                    result[i] = ' ';
        }
        if((extraFlags & detail::Flag_TruncateToPrecision) &&
           (int)result.size() > (int)out.precision())
            out.write(result.c_str(), out.precision());
        else
            out << result;
    }
    // Restore stream state
    out.width(width);
    out.precision(precision);
    out.flags(flags);
    out.fill(fill);
}


//------------------------------------------------------------------------------
// Format function, 0-argument case.
inline void format(std::ostream& out, const char* fmt)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    if(*fmt != '\0')
        TINYFORMAT_ERROR("tinyformat: Too many conversion specifiers in format string");
}


// Define N-argument format function.
//
// There's two cases here: c++0x and c++98.

#ifdef TINYFORMAT_USE_VARIADIC_TEMPLATES

// First, the simple definition for C++0x:

// N-argument case; formats one element and calls N-1 argument case.
template<typename T1, typename... Args>
void format(std::ostream& out, const char* fmt, const T1& value1,
            const Args&... args)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, value1);
    format(out, fmtEnd, args...);
}

#else

// For C++98 we don't have variadic templates so we need to generate code
// outside the language.  We could do this with some ugly macros but instead
// let's use a short snippet of python code with the help of the excellent cog
// code generation script ( http://nedbatchelder.com/code/cog/ )

/*[[[cog

maxParams = 10

# prepend a comma if the string isn't empty.
def prependComma(str):
    return '' if str == '' else ', ' + str

# Append backslashes to lines so they appear as a macro in C++
# lineLen is the desired padding before the backslash
def formatAsMacro(str, lineLen=75):
    lines = str.splitlines()
    lines = [l+' '*max(1, lineLen-len(l)) for l in lines]
    return '\\\n'.join(lines) + '\\'

# Fill out the given string template.
def fillTemplate(template, minParams=0, formatFunc=lambda s: s):
    for i in range(minParams,maxParams+1):
        paramRange = range(1,i+1)
        templateSpec = ', '.join(['typename T%d' % (j,) for j in paramRange])
        if templateSpec == '':
            templateSpec = 'inline'
        else:
            templateSpec = 'template<%s>' % (templateSpec,)
        paramList = prependComma(', '.join(['const T%d& v%d' % (j,j)
                                            for j in paramRange]))
        argList = prependComma(', '.join(['v%d' % (j,) for j in paramRange]))
        argListNoHead = prependComma(', '.join(['v%d' % (j,)
                                                for j in paramRange[1:]]))
        cog.outl(formatFunc(template % locals()))

fillTemplate(
'''%(templateSpec)s
void format(std::ostream& out, const char* fmt %(paramList)s)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd %(argListNoHead)s);
}''', minParams=1)

]]]*/
template<typename T1>
void format(std::ostream& out, const char* fmt , const T1& v1)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd );
}
template<typename T1, typename T2>
void format(std::ostream& out, const char* fmt , const T1& v1, const T2& v2)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd , v2);
}
template<typename T1, typename T2, typename T3>
void format(std::ostream& out, const char* fmt , const T1& v1, const T2& v2, const T3& v3)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd , v2, v3);
}
template<typename T1, typename T2, typename T3, typename T4>
void format(std::ostream& out, const char* fmt , const T1& v1, const T2& v2, const T3& v3, const T4& v4)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd , v2, v3, v4);
}
template<typename T1, typename T2, typename T3, typename T4, typename T5>
void format(std::ostream& out, const char* fmt , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd , v2, v3, v4, v5);
}
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
void format(std::ostream& out, const char* fmt , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd , v2, v3, v4, v5, v6);
}
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
void format(std::ostream& out, const char* fmt , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6, const T7& v7)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd , v2, v3, v4, v5, v6, v7);
}
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
void format(std::ostream& out, const char* fmt , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6, const T7& v7, const T8& v8)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd , v2, v3, v4, v5, v6, v7, v8);
}
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9>
void format(std::ostream& out, const char* fmt , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6, const T7& v7, const T8& v8, const T9& v9)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd , v2, v3, v4, v5, v6, v7, v8, v9);
}
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9, typename T10>
void format(std::ostream& out, const char* fmt , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6, const T7& v7, const T8& v8, const T9& v9, const T10& v10)
{
    fmt = detail::printFormatStringLiteral(out, fmt);
    const char* fmtEnd = detail::findFormatSpecEnd(fmt);
    formatValueBasic(out, fmt, fmtEnd, v1);
    format(out, fmtEnd , v2, v3, v4, v5, v6, v7, v8, v9, v10);
}
//[[[end]]]

#endif // End C++98 variadic template emulation for format()


//------------------------------------------------------------------------------
// Define the macro TINYFORMAT_WRAP_FORMAT, which can be used to wrap a call
// to tfm::format for C++98 support.
//
// We make this available in both C++0x and C++98 mode for convenience so that
// users can choose not to write out the C++0x version if they're primarily
// interested in C++98 support, but still have things work with C++0x.
//
// Note that TINYFORMAT_WRAP_EXTRA_ARGS cannot be a macro parameter because it
// must expand to a comma separated list (or nothing, as here)/

/*[[[cog
cog.outl(formatAsMacro(
'''#define TINYFORMAT_WRAP_FORMAT(returnType, funcName, bodyPrefix, streamName, bodySuffix)'''))

fillTemplate(
r'''%(templateSpec)s
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt
                    %(paramList)s)
{
    bodyPrefix
    tinyformat::format(streamName, fmt %(argList)s);
    bodySuffix
}''', minParams=0, formatFunc=formatAsMacro)
cog.outl()

]]]*/
#define TINYFORMAT_WRAP_FORMAT(returnType, funcName, bodyPrefix, streamName, bodySuffix) \
inline                                                                     \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    )                                                      \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt );                                  \
    bodySuffix                                                             \
}                                                                          \
template<typename T1>                                                      \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1)                                        \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1);                              \
    bodySuffix                                                             \
}                                                                          \
template<typename T1, typename T2>                                         \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1, const T2& v2)                          \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1, v2);                          \
    bodySuffix                                                             \
}                                                                          \
template<typename T1, typename T2, typename T3>                            \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1, const T2& v2, const T3& v3)            \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1, v2, v3);                      \
    bodySuffix                                                             \
}                                                                          \
template<typename T1, typename T2, typename T3, typename T4>               \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1, const T2& v2, const T3& v3, const T4& v4) \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1, v2, v3, v4);                  \
    bodySuffix                                                             \
}                                                                          \
template<typename T1, typename T2, typename T3, typename T4, typename T5>  \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5) \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1, v2, v3, v4, v5);              \
    bodySuffix                                                             \
}                                                                          \
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6> \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6) \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1, v2, v3, v4, v5, v6);          \
    bodySuffix                                                             \
}                                                                          \
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7> \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6, const T7& v7) \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1, v2, v3, v4, v5, v6, v7);      \
    bodySuffix                                                             \
}                                                                          \
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8> \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6, const T7& v7, const T8& v8) \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1, v2, v3, v4, v5, v6, v7, v8);  \
    bodySuffix                                                             \
}                                                                          \
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9> \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6, const T7& v7, const T8& v8, const T9& v9) \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1, v2, v3, v4, v5, v6, v7, v8, v9); \
    bodySuffix                                                             \
}                                                                          \
template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9, typename T10> \
returnType funcName(TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS const char* fmt      \
                    , const T1& v1, const T2& v2, const T3& v3, const T4& v4, const T5& v5, const T6& v6, const T7& v7, const T8& v8, const T9& v9, const T10& v10) \
{                                                                          \
    bodyPrefix                                                             \
    tinyformat::format(streamName, fmt , v1, v2, v3, v4, v5, v6, v7, v8, v9, v10); \
    bodySuffix                                                             \
}                                                                          \

//[[[end]]]


//------------------------------------------------------------------------------
// Implement convenience functions in terms of format(stream, fmt, ...).
// Again, there's two cases.
#ifdef TINYFORMAT_USE_VARIADIC_TEMPLATES

// C++0x - the simple case
template<typename... Args>
std::string format(const char* fmt, const Args&... args)
{
    std::ostringstream oss;
    format(oss, fmt, args...);
    return oss.str();
}

template<typename... Args>
void printf(const char* fmt, const Args&... args)
{
    format(std::cout, fmt, args...);
}

#else

// C++98 - define the convenience functions using the wrapping macros
//
// Neither format() or printf() has extra args, so define to nothing.
#define TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS
// std::string format(const char* fmt, const Args&... args);
TINYFORMAT_WRAP_FORMAT(std::string, format,
                       std::ostringstream oss;, oss,
                       return oss.str();)
// void printf(const char* fmt, const Args&... args)
TINYFORMAT_WRAP_FORMAT(void, printf, /*empty*/, std::cout, /*empty*/)
#undef TINYFORMAT_WRAP_FORMAT_EXTRA_ARGS

#endif

} // namespace tinyformat

#endif // TINYFORMAT_H_INCLUDED
