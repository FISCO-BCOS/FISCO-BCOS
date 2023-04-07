#include <math.h>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_common.h>
#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_json.h>

namespace tars
{

#define FILTER_SPACE while(isspace((int)c)) {c=reader.read();}

JsonValuePtr TC_Json::getValue(BufferJsonReader & reader)
{
	char c=reader.read();
	FILTER_SPACE;

	switch(c)
	{
		case '{':
			return getObj(reader);
			break;
		case '[':
			return getArray(reader);
			break;
		case '"':
			//case '\'':
			return getString(reader,c);
			break;
		case 'T':
		case 't':
		case 'F':
		case 'f':
			return getBoolean(reader,c);
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '-':
			return getNum(reader,c);
			break;
		case 'n':
		case 'N':
			return getNull(reader,c);
		default:
			char s[64];
			snprintf(s, sizeof(s), "buffer overflow when peekBuf, over %u.", (uint32_t)(uint32_t)reader.getCur());
			throw TC_Json_Exception(s);
	}
}

JsonValueObjPtr TC_Json::getObj(BufferJsonReader & reader)
{
	JsonValueObjPtr p = new JsonValueObj();
	bool bFirst=true;
	while(1)
	{
		char c=reader.read();
		FILTER_SPACE;
		if(c == '}' && bFirst)
		{
			return p;
		}
		bFirst=false;

		if(c != '"')
		{
			char s[64];
			snprintf(s, sizeof(s), "get obj error(key is not string)[pos:%u]", (uint32_t)reader.getCur());
			throw TC_Json_Exception(s);
		}
		JsonValueStringPtr pString=getString(reader);
		c=reader.read();
		FILTER_SPACE;
		if(c != ':')
		{
			char s[64];
			snprintf(s, sizeof(s), "get obj error(: not find)[pos:%u]", (uint32_t)reader.getCur());
			throw TC_Json_Exception(s);
		}
		JsonValuePtr pValue=getValue(reader);
		p->value[pString->value]=pValue;

		c=reader.read();
		FILTER_SPACE;

		if(c == ',')
			continue;
		if(c == '}')
			return p;

		char s[64];
		snprintf(s, sizeof(s), "get obj error(, not find)[pos:%u]", (uint32_t)reader.getCur());
		throw TC_Json_Exception(s);
	}
}

JsonValueArrayPtr TC_Json::getArray(BufferJsonReader & reader)
{
	JsonValueArrayPtr p = new JsonValueArray();
	bool bFirst=true;
	while(1)
	{
		char c;
		if(bFirst)
		{
			c=reader.read();
			FILTER_SPACE;
			if(c == ']')
			{
				return p;
			}
			reader.back();
		}
		bFirst=false;

		JsonValuePtr pValue=getValue(reader);
		p->push_back(pValue);

		c=reader.read();
		FILTER_SPACE;
		if(c == ',')
			continue;
		if(c == ']')
			return p;

		char s[64];
		snprintf(s, sizeof(s), "get vector error(, not find )[pos:%u]", (uint32_t)reader.getCur());
		throw TC_Json_Exception(s);
	}
}

JsonValueStringPtr TC_Json::getString(BufferJsonReader & reader,char head)
{
	JsonValueStringPtr p = new JsonValueString();
	const char * pChar=reader.getPoint();
	char c;
	uint32_t i=0;
	while(1)
	{
		c=reader.read();
		if(c == '\\')
		{
			p->value.append(pChar,i);
			pChar=pChar+i+2;
			i=0;
			c=reader.read();
			if(c == '\\' || c == '\"' || c == '/')
				p->value.append(1,c);
			else if(c == 'b')
				p->value.append(1,'\b');
			else if(c == 'f')
				p->value.append(1,'\f');
			else if(c == 'n')
				p->value.append(1,'\n');
			else if(c == 'r')
				p->value.append(1,'\r');
			else if(c == 't')
				p->value.append(1,'\t');
			else if(c == 'u')
			{
				uint32_t iCode=getHex(reader);

				if (iCode < 0x00080)
				{
					p->value.append(1,(char)(iCode & 0xFF));
				}
				else if (iCode < 0x00800)
				{
					p->value.append(1,(char)(0xC0 + ((iCode >> 6) & 0x1F)));
					p->value.append(1,(char)(0x80 + (iCode & 0x3F)));
				}
				else if (iCode < 0x10000)
				{
					p->value.append(1,(char)(0xE0 + ((iCode >> 12) & 0x0F)));
					p->value.append(1,(char)(0x80 + ((iCode >> 6) & 0x3F)));
					p->value.append(1,(char)(0x80 + (iCode & 0x3F)));
				}
				else
				{
					p->value.append(1,(char)(0xF0 + ((iCode >> 18) & 0x07)));
					p->value.append(1,(char)(0x80 + ((iCode >> 12) & 0x3F)));
					p->value.append(1,(char)(0x80 + ((iCode >> 6) & 0x3F)));
					p->value.append(1,(char)(0x80 + (iCode & 0x3F)));
				}

				pChar+=4;

//                char s[64];
//                snprintf(s, sizeof(s), "get string error1(\\u)[pos:%u], code:%x", (uint32_t)reader.getCur(), iCode);
//                cout << s << endl;
//
//                if(iCode>0xff)
//                {
//                    char s[64];
//                    snprintf(s, sizeof(s), "get string error1(\\u)[pos:%u], code:%x", (uint32_t)reader.getCur(), iCode);
//                    throw TC_Json_Exception(s);
//                }
//                pChar+=4;
//                p->value.append(1,(char)iCode);
//#if 0
//                //还要再读一个
//                if(iCode<0xd800)
//                {
//                    p->value.append(1,(char)(iCode>>2&0x00ff));
//                    p->value.append(1,(char)(iCode&0x00ff));
//                }
//                else
//                {
//                    uint16_t iCodeTwelve=0;
//                    c=reader.read();
//                    if(c == '\\' && (c=reader.read(),c=='u'))
//                    {
//                        iCodeTwelve=getHex();
//                    }
//                    if(iCodeTwelve<0xdc00)
//                    {
//                        char s[64];
//                        snprintf(s, sizeof(s), "get string error2(\\u)[pos:%u]", (uint32_t)reader.getCur());
//                        throw TC_Json_Exception(s);
//                    }
//                    int iBuf=0;
//                    iCode=iCode&0x03ff;
//                    iBuf=(iCode<10);
//                    iBuf+=(iCodeTwelve&0x03ff);
//                    iBuf+=0x10000;
//                }
//#endif
			}
		}
		else if(c==head)
			break;
		else
			i++;
	}
	p->value.append(pChar,i);
	return p;
}

JsonValueNumPtr TC_Json::getNum(BufferJsonReader & reader,char head)
{
	bool bOk=true;
	bool bFloat=false;
	bool bExponential=false;
	bool bNegative=false;
	bool bExponentialNegative=false;
	int64_t iInt=0;
	double dFloat=0;
	double dFloatRat=0;
	int64_t iExponential=0;
	if(head == '-')
	{
		bOk=false;
		bNegative=true;
	}
	else
		iInt=head-0x30;
	char c;
	bool bNeedBack=false;
	while(1)
	{
		if(reader.hasEnd())
			break;
		c=reader.read();
		if(c>=0x30 && c<=0x39)
		{
			bOk=true;
			if(bExponential)
				iExponential=iExponential*10+c-0x30;
			else if(bFloat)
			{
				dFloat=dFloat+dFloatRat*(c-0x30);
				dFloatRat=dFloatRat*0.1;
			}
			else
				iInt=iInt*10+c-0x30;
		}
		else if(c == '.' && !bFloat && !bExponential && bOk)
		{
			bOk=false;
			bFloat=true;
			dFloatRat=0.1;
		}
		else if((c == 'e' || c == 'E') && !bExponential && bOk)
		{
			bOk=false;
			bExponential=true;
			iExponential=0;
			if(reader.hasEnd())
				break;
			c=reader.read();
			if(c == '-')
				bExponentialNegative=true;
			else if(c == '+')
				bExponentialNegative=false;
			else if(c>=0x30 && c<=0x39)
			{
				bOk=true;
				bExponential=(bool)(c-0x30);
			}
			else
			{
				bNeedBack=true;
				break;
			}
		}
		else
		{
			bNeedBack=true;
			break;
		}
	}
	if(!bOk)
	{
		char s[64];
		snprintf(s, sizeof(s), "get num error[pos:%u]", (uint32_t)reader.getCur());
		throw TC_Json_Exception(s);
	}
	if(bNeedBack)
		reader.back();
	if(bExponentialNegative)
		iExponential=0-iExponential;

	JsonValueNumPtr p = new JsonValueNum();
	p->isInt=!bFloat;
	if(bFloat)
	{
		double dResult=(iInt+dFloat)*pow(10,iExponential);
		if(bNegative)
			dResult=0-dResult;
		p->value=dResult;
		p->lvalue=dResult;
	}
	else
	{
		if(bNegative)
			iInt =0-iInt ;
		p->lvalue=iInt;
		p->value=iInt;
	}
	return p;
}

//为了提高效率和代码好写就先这么写了
JsonValueBooleanPtr TC_Json::getBoolean(BufferJsonReader & reader,char c)
{
	bool bOk=false;
	bool bValue;
	if(c=='t'||c=='T')
	{
		c=reader.read();
		if(c=='r'||c=='R')
		{
			c=reader.read();
			if(c=='u'||c=='U')
			{
				c=reader.read();
				if(c=='e'||c=='E')
				{
					bValue=true;
					bOk=true;
				}
			}
		}
	}
	else if(c=='f'||c=='F')
	{
		c=reader.read();
		if(c=='a'||c=='A')
		{
			c=reader.read();
			if(c=='l'||c=='L')
			{
				c=reader.read();
				if(c=='s'||c=='S')
				{
					c=reader.read();
					if(c=='e'||c=='E')
					{
						bValue=false;
						bOk=true;
					}
				}
			}
		}
	}

	if(!bOk)
	{
		char s[64];
		snprintf(s, sizeof(s), "get bool error[pos:%u]", (uint32_t)reader.getCur());
		throw TC_Json_Exception(s);
	}

	JsonValueBooleanPtr p = new JsonValueBoolean();
	p->value=bValue;
	return p;
}

JsonValueNullPtr TC_Json::getNull(BufferJsonReader & reader,char c)
{
	JsonValueNullPtr p = new JsonValueNull();
	assert(c=='n' || c=='N');
	bool bOk=false;
	c=reader.read();
	if(c=='u'||c=='U')
	{
		c=reader.read();
		if(c=='l'||c=='L')
		{
			c=reader.read();
			if(c=='l'||c=='L')
			{
				bOk=true;
			}
		}
	}
	if(!bOk)
	{
		char s[64];
		snprintf(s, sizeof(s), "get NULL error[pos:%u]", (uint32_t)reader.getCur());
		throw TC_Json_Exception(s);
	}
	//return NULL;
	return p;
}

uint32_t TC_Json::getHex(BufferJsonReader & reader)
{
	uint32_t iCode=0;
	char c;
	for(int iLoop=0;iLoop<4;iLoop++)
	{
		c=reader.read();
		if(c>='a'&&c<='f')
			iCode=iCode*16+c-'a'+10;
		else if(c>='A'&&c<='F')
			iCode=iCode*16+c-'A'+10;
		else if(c>='0'&&c<='9')
			iCode=iCode*16+c-'0';
		else
		{
			char s[64];
			snprintf(s, sizeof(s), "get string error3(\\u)[pos:%u]", (uint32_t)reader.getCur());
			throw TC_Json_Exception(s);
		}
	}
	return iCode;
}


std::string TC_Json::writeValue(const JsonValuePtr & p, bool withSpace)
{
	std::string ostr;
	writeValue(p, ostr, withSpace);
	return ostr;
}

void TC_Json::writeValue(const JsonValuePtr& p, std::vector<char>& buf, bool withSpace)
{
    std::string ostr;
    writeValue(p, ostr, withSpace);
    buf.assign(ostr.begin(), ostr.end());
}

void TC_Json::writeValue(const JsonValuePtr & p, std::string& ostr, bool withSpace)
{
	if(!p)
	{
		ostr += "null";
		return;
	}
	switch(p->getType())
	{
		case eJsonTypeString :
			writeString(JsonValueStringPtr::dynamicCast(p), ostr);
			break;
		case eJsonTypeNum:
			writeNum(JsonValueNumPtr::dynamicCast(p), ostr);
			break;
		case eJsonTypeObj:
			writeObj(JsonValueObjPtr::dynamicCast(p), ostr, withSpace);
			break;
		case eJsonTypeArray:
		    writeArray(JsonValueArrayPtr::dynamicCast(p), ostr, withSpace);
			break;
		case eJsonTypeBoolean:
			writeBoolean(JsonValueBooleanPtr::dynamicCast(p), ostr);
			break;
		default:
			assert(false);
	}
}


void TC_Json::writeString(const JsonValueStringPtr & p, std::string& ostr)
{
	writeString(p->value, ostr);
}

void TC_Json::writeString(const std::string & s, std::string& ostr)
{
	ostr += "\"";
	std::string::const_iterator it(s.begin()),
		itEnd(s.end());
	for (; it != itEnd; ++it)
	{
		switch(*it)
		{
			case '"':
				ostr += "\\\"";
				break;
			case '\\':
				ostr += "\\\\";
				break;
			case '/':
				ostr += "\\/";
				break;
			case '\b':
				ostr += "\\b";
				break;
			case '\f':
				ostr += "\\f";
				break;
			case '\n':
				ostr += "\\n";
				break;
			case '\r':
				ostr += "\\r";
				break;
			case '\t':
				ostr += "\\t";
				break;
			default:
			{
				if((unsigned char)(*it)<0x20)
				{
					char buf[16];
					snprintf(buf,sizeof(buf),"\\u%04x",(unsigned char)*it);
					ostr += std::string(buf,6);
				}
				else
				{
					ostr.push_back(*it);
				}
				break;
			}
		}
	}
	ostr += "\"";
}


void TC_Json::writeNum(const JsonValueNumPtr & p, std::string& ostr)
{
	std::ostringstream ss;
	if (std::isnan(p->value))
	{
		ss << "null";
	}
	else if (!p->isInt)
	{
		ss << TC_Common::tostr(p->value) ;
	}
	else
	{
		ss << (int64_t)p->lvalue;
	}

	ostr += ss.str();
}


void TC_Json::writeObj(const JsonValueObjPtr & p, std::string& ostr, bool withSpace)
{
    ostr += (withSpace ? "{ " : "{");
	std::unordered_map<std::string,JsonValuePtr>::const_iterator it(p->value.begin()), it_end(p->value.end());
	while (it != it_end)
	{
		writeString(it->first, ostr);
		ostr += (withSpace ? ": " : ":");
		writeValue(it->second, ostr);
		if(++it != it_end)
		{
			ostr += (withSpace ? ", " : ",");
		}
	}
	ostr += (withSpace ? " }" : "}");
}

void TC_Json::writeArray(const JsonValueArrayPtr & p, std::string& ostr, bool withSpace)
{
    ostr += (withSpace ? "[ " : "[");
	std::vector<JsonValuePtr>::const_iterator it(p->value.begin()), it_end(p->value.end());
	while (it != it_end)
	{
		writeValue(*it, ostr);
		if (++it != it_end)
		{
		    ostr += (withSpace ? ", " : ",");
		}
	}
	ostr += (withSpace ? " ]" : "]");
}

void TC_Json::writeBoolean(const JsonValueBooleanPtr & p, std::string& ostr)
{
	if(p->value)
		ostr += "true";
	else
		ostr += "false";
}

JsonValuePtr TC_Json::getValue(const std::string & str)
{
	BufferJsonReader reader;
	reader.setBuffer(str.c_str(),str.length());
	return getValue(reader);
}

JsonValuePtr TC_Json::getValue(const std::vector<char>& buf)
{
	BufferJsonReader reader;
	reader.setBuffer(buf);
	return getValue(reader);
}

//json里面定义的空白字符
bool TC_Json::isspace(char c)
{
	if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
		return true;
	return false;
}

//////////////////////////////////////////////////////
void TC_JsonWriteOstream::writeValue(const JsonValuePtr & p, std::ostream& ostr, bool withSpace)
{
	if(!p)
	{
		ostr << "null";
		return;
	}
	switch(p->getType())
	{
		case eJsonTypeString :
			writeString(JsonValueStringPtr::dynamicCast(p), ostr);
			break;
		case eJsonTypeNum:
			writeNum(JsonValueNumPtr::dynamicCast(p), ostr);
			break;
		case eJsonTypeObj:
			writeObj(JsonValueObjPtr::dynamicCast(p), ostr, withSpace);
			break;
		case eJsonTypeArray:
			writeArray(JsonValueArrayPtr::dynamicCast(p), ostr, withSpace);
			break;
		case eJsonTypeBoolean:
			writeBoolean(JsonValueBooleanPtr::dynamicCast(p), ostr);
			break;
		default:
			assert(false);
	}
}

void TC_JsonWriteOstream::writeString(const JsonValueStringPtr & p, std::ostream& sReturn)
{
	writeString(p->value, sReturn);
}

void TC_JsonWriteOstream::writeString(const std::string & s, std::ostream& sReturn)
{
	sReturn << "\"";
	std::string::const_iterator it(s.begin()),
		itEnd(s.end());
	for (; it != itEnd; ++it)
	{
		switch(*it)
		{
			case '"':
				sReturn << "\\\"";
				break;
			case '\\':
				sReturn << "\\\\";
				break;
			case '/':
				sReturn <<"\\/";
				break;
			case '\b':
				sReturn << "\\b";
				break;
			case '\f':
				sReturn << "\\f";
				break;
			case '\n':
				sReturn << "\\n";
				break;
			case '\r':
				sReturn << "\\r";
				break;
			case '\t':
				sReturn << "\\t";
				break;
			default:
			{
				if((unsigned char)(*it)<0x20)
				{
					char buf[16];
					snprintf(buf,sizeof(buf),"\\u%04x",(unsigned char)*it);
					sReturn << std::string(buf,6);
				}
				else
				{
					sReturn << *it;
				}
				break;
			}
		}
	}
	sReturn << "\"";
}


void TC_JsonWriteOstream::writeNum(const JsonValueNumPtr & p, std::ostream& ostr)
{
	if (!p->isInt)
	{
		ostr << TC_Common::tostr(p->value);
	}
	else
	{
		ostr << (int64_t)p->lvalue;
	}
}


void TC_JsonWriteOstream::writeObj(const JsonValueObjPtr & p, std::ostream& ostr, bool withSpace)
{
	ostr << "{" << (withSpace ? " " : "");
	std::unordered_map<std::string,JsonValuePtr>::const_iterator it(p->value.begin()), it_end(p->value.end());
	while (it != it_end)
	{
		writeString(it->first, ostr);
		ostr << ":" << (withSpace ? " " : "");
		writeValue(it->second, ostr);
		if(++it != it_end)
		{
		    ostr << "," << (withSpace ? " " : "");
		}
	}
	ostr << (withSpace ? " " : "") << "}";
}

void TC_JsonWriteOstream::writeArray(const JsonValueArrayPtr & p, std::ostream& ostr, bool withSpace)
{
    ostr << "[" << (withSpace ? " " : "");
	std::vector<JsonValuePtr>::const_iterator it(p->value.begin()), it_end(p->value.end());
	while (it != it_end)
	{
		writeValue(*it, ostr);
		if (++it != it_end)
		{
		    ostr << "," << (withSpace ? " " : "");
		}
	}
	ostr << (withSpace ? " " : "") << "]";
}

void TC_JsonWriteOstream::writeBoolean(const JsonValueBooleanPtr & p, std::ostream& ostr)
{
	if(p->value)
		ostr << "true";
	else
		ostr << "false";
}

}

