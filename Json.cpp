/**
 Copyright 2023 Amazon.com, Inc. or its affiliates.
 Copyright 2023 Netflix Inc.
 Copyright 2023 Google LLC
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at
 http://www.apache.org/licenses/LICENSE-2.0
 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>
#include <exception>

#include "Json.h"

jsonElement::jsonElement( char const **str )
{
	while ( isSpace ( **str ) ) (*str)++;		// skip spaces and eol characters

	if ( (*str)[0] == '{' )
	{
		// we're a json object
		(*str)++;

		value = jsonElement::objectType();

		auto &obj = std::get<jsonElement::objectType>( value );

		bool first = true;
		for ( ;; )
		{
			while ( isSpace ( **str ) ) (*str)++;		// skip spaces and eol characters

			if ( (*str)[0] == '}' )
			{
				(*str)++;
				break;
			}
			if ( !first )
			{
				if ( (*str)[0] != ',' )
				{
					throw "missing comma";
				}
				(*str)++;
				while ( isSpace ( **str ) ) (*str)++;		// skip spaces and eol characters
				if ( (*str)[0] == '}' )
				{
					(*str)++;
					break;
				}
			}
			first = false;

			std::string name;
			if ( (*str)[0] == '"' )
			{
				(*str)++;
				while ( **str && (*str)[0] != '"' )
				{
					name += *((*str)++);
				}
				if ( **str )
				{
					(*str)++;
				} else
				{
					throw "missing \"";
				}
			} else
			{
				if ( !isSymbol( **str ) )
				{
					throw "invalid json symbol value";
				}
				while ( isSymbolB( **str ) )
				{
					name += *((*str)++);
				}
			}

			while ( isSpace ( **str ) ) (*str)++;		// skip spaces and eol characters
			if ( (*str)[0] != ':' )
			{
				throw "missing name/value separator";
			}
			(*str)++;

			while ( isSpace ( **str ) ) (*str)++;		// skip spaces and eol characters

			obj[name] = jsonElement( str );
		}
	} else if ( (*str)[0] == '[' )
	{
		(*str)++;
		value = jsonElement::arrayType();

		auto &arr = std::get<jsonElement::arrayType>( value );

		bool first = true;
		for ( ;; )
		{
			while ( isSpace ( **str ) ) (*str)++;		// skip spaces and eol characters

			if ( (*str)[0] == ']' )
			{
				(*str)++;
				break;
			}
			if ( !first )
			{
				if ( (*str)[0] != ',' )
				{
					throw "missing comma";
				}
				(*str)++;
				while ( isSpace ( **str ) ) (*str)++;		// skip spaces and eol characters
			}
			first = false;

			arr.push_back( jsonElement( str ) );
		}
	} else if ( (*str)[0] == '"' )
	{
		// we're a string
		(*str)++;

		std::string v;
		while ( **str && (*str)[0] != '"' )
		{
			if ( (*str)[0] == '\\' && (*str)[1] == '"' )
			{
				v += '"';
				(*str) += 2;
			} else if ( (*str)[0] == '\\' && (*str)[1] == 'r' )
			{
				v += '\r';
				(*str) += 2;
			} else if ( (*str)[0] == '\\' && (*str)[1] == 'n' )
			{
				v += '\n';
				(*str) += 2;
			} else if ( (*str)[0] == '\\' && (*str)[1] == 't' )
			{
				v += '\t';
				(*str) += 2;
			} else if ( (*str)[0] == '\\' && (*str)[1] )
			{
				v += (*str)[1];
				(*str) += 2;
			} else
			{
				v += *((*str)++);
			}
		}
		if ( **str )
		{
			(*str)++;
		} else
		{
			throw "missing \"";
		}
		value = v;
	} else if ( isNumB ( **str ) )
	{
		std::string v;
		bool isFloat = false;
		while ( isNum( **str ) )
		{
			if ( **str == '.' ) isFloat = true;
			if ( **str == 'e' ) isFloat = true;
			v += *((*str)++);
		}
		if ( isFloat )
		{
			value = std::stod( v.c_str() );
		} else
		{
			value = std::stoll( v.c_str() );
		}
	} else if ( !memcmp( *str, "true", 4 ) )
	{
		value = true;
		*str += 4;
	} else if ( !memcmp( *str, "false", 5 ) )
	{
		value = false;
		*str += 5;
	} else if ( !memcmp( *str, "null", 4 ) )
	{
		value = std::monostate();
		*str += 4;
	} else
	{
		throw "missing \"";
	}
}

// ----------------------------------------------- creater functions
jsonElement::operator bool()
{
	if ( std::holds_alternative<int64_t>( value ) )
	{
		value = std::get<int64_t>( value ) ? true : false;
	} else if ( !std::holds_alternative<bool>( value ) )
	{
		value = false;
	}
	return std::get<bool>( value );
}
jsonElement::operator int64_t ()
{
	if ( std::holds_alternative<double>( value ) )
	{
		value = (int64_t) std::get<double>( value );
	}
	if ( !std::holds_alternative<int64_t>( value ) )
	{
		value = (int64_t) 0;
	}
	return std::get<int64_t>( value );
}
jsonElement::operator double()
{
	if ( std::holds_alternative<int64_t>( value ) )
	{
		value = (double) std::get<int64_t>( value );
	} else if ( !std::holds_alternative<double>( value ) )
	{
		value = 0.0;
	}
	return std::get<double>( value );
}
jsonElement::operator std::string ()
{
	if ( std::holds_alternative<int64_t>( value ) )
	{
		value = (double) std::get<int64_t>( value );
	} else if ( !std::holds_alternative<std::string>( value ) )
	{
		value = std::string();
	}
	return std::get<std::string>( value );
}

// ------------------------------------- reader functions
jsonElement::operator int64_t const () const
{
	if ( std::holds_alternative<int64_t>( value ) )
	{
		return std::get<int64_t>( value );
	}
	throw "invalid json integer value";
}
jsonElement::operator bool const () const
{
	if ( std::holds_alternative<bool>( value ) )
	{
		return std::get<bool>( value );
	}
	throw "invalid json integer value";
}
jsonElement::operator double const () const
{
	if ( std::holds_alternative<double>( value ) )
	{
		return std::get<double>( value );
	}
	throw "invalid json double value";
}

jsonElement::operator std::string const &() const
{
	if ( std::holds_alternative<std::string>( value ) )
	{
		return std::get<std::string>( value );
	}
	throw "invalid json string value";
}

size_t jsonElement::size() const
{
	if ( std::holds_alternative<objectType>( value ) )
	{
		auto &obj = std::get<objectType>( value );
		return obj.size();
	} else if ( std::holds_alternative<arrayType>( value ) )
	{
		auto &arr = std::get<arrayType>( value );
		return arr.size();
	} else if ( std::holds_alternative<std::monostate>( value ) )
	{
		return 0;
	}
	throw "invalid usage";
}
// ------------------------------- serialization
void jsonElement::serialize( std::string &buff, bool quoteNames ) const
{
	if ( std::holds_alternative<objectType>( value ) )
	{
		auto &obj = std::get<objectType>( value );
		buff.push_back( '{' );
		bool first = true;
		for ( auto &&[name, v] : obj )
		{
			if ( !first )
			{
				buff.push_back( ',' );
			}
			first = false;
			if ( quoteNames ) buff.push_back( '\"' );
			buff.append ( name );
			if ( quoteNames ) buff.push_back( '\"' );
			buff.push_back( ':' );
			v.serialize( buff, quoteNames );
		}
		buff.push_back ( '}' );
	} else if ( std::holds_alternative<arrayType>( value ) )
	{
		auto &arr = std::get<arrayType>( value );
		buff.push_back( '[' );
		bool first = true;
		for ( auto &it : arr )
		{
			if ( !first )
			{
				buff.push_back( ',' );
			}
			first = false;
			it.serialize( buff, quoteNames );
		}
		buff.push_back( ']' );
	} else if ( std::holds_alternative<int64_t>( value ) )
	{
		auto v = std::get<int64_t>( value );
		buff.append( std::to_string( v ) );
	} else if ( std::holds_alternative<double>( value ) )
	{
		auto v = std::get<double>( value );
		buff.append( std::to_string( v ) );
	} else if ( std::holds_alternative<std::string>( value ) )
	{
		auto &v = std::get<std::string>( value );
		buff.push_back( '\"' );
		for ( auto &it : v )
		{
			switch ( it )
			{
				case '\"':
					buff.append( "\\\"", 2 );
					break;
				case '\\':
					buff.append( "\\\\", 2 );
					break;
				case '\r':
					buff.append( "\\r", 2 );
					break;
				case '\n':
					buff.append( "\\n", 2 );
					break;
				case '\t':
					buff.append( "\\t", 2 );
					break;
				default:
					if ( it < 32 || it > 127 )
					{
						buff.push_back( '%' );
						buff.push_back( "0123456789ABCDEF"[(it & 0xF0) >> 4] );
						buff.push_back( "0123456789ABCDEF"[(it & 0x0F)] );
					} else
					{
						buff.push_back( it );
					}
			}
		}
		buff.push_back( '\"' );
	} else	if ( std::holds_alternative<bool>( value ) )
	{
		if ( std::get<bool>( value ) )
		{
			buff.append( "true", 4 );
		} else
		{
			buff.append( "false", 5 );
		}
	} else if ( std::holds_alternative<std::monostate>( value ) )
	{
		buff.append( "null", 4 );
	}
}

jsonElement jsonParser( char const *str )
{
	auto tmpStr = &str;
	auto result = jsonElement( tmpStr );
	while ( jsonElement::isSpace ( **tmpStr ) ) (*tmpStr)++;		// skip spaces and eol characters
	if ( **tmpStr )
	{
		throw "invalid json";
	}
	return result;
}
