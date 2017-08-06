#ifndef TOKEN_H
#define TOKEN_H
#include <string>
#include <cstring>
#include <iostream>
#include "common.h"
using namespace std;

#define ICUR (inc(cur_pos))
//#define ICUR (inc(temp_pos))
/*
class mstring : public string
{
    char operator [](size_t n)
    {
        if(n >= this->length())
        {
            cerr << "Wrong index!" << endkl
        }
    }
};*/

class Token
{
    static const int EOL_REACHED = 0;
    static const int UNKNOWN_ESCAPE = 1;
    bool firstTime = true;
    string src, last, cur;
    uint cur_pos, last_pos; //I wanted to use string::iterator but it was a bug
                            //that was modifing all iterators after adding *cur_pos to result string after some chars

public:
    enum TokenType {
        String, Digit, Delim, Bool, Identifier, EOL, UNKNOWN
    };

    Token(string src) {
        this->src = src + " ";

        cur_pos = 0;

        last = cur = Next();
        last_pos = 0;
    }
    
    bool operator ==(string str) {
        return this->ToString() == str;
    }
    
    bool operator !=(string str) {
        return this->ToString() != str;
    }
    
    Token& operator>>(string& str) {
        str = this->Next();
        return *this;
    }
    
    string PushBack() {
        cur_pos = last_pos;
        cur = last;
        return cur;
    }

    string ToString() {
        return cur;
    }

    string Last() {
        return last;
    }

    string Next() {
        string res = "";
        try {
            last_pos = cur_pos;
            last = cur;
            res = cur = next();
        } catch (int ex) {
            switch(ex) {
            case EOL_REACHED:
                type = EOL;
                res = "";
                break;
            case UNKNOWN_ESCAPE:
                type = UNKNOWN;
                res = "";
                break;
            }
        }
        firstTime = false;
        return res;
    }

    string NextWhileNot(char ch, bool skip=false) {
        string res = "";
        res.reserve(src.length()+1);
        try {
            last_pos = cur_pos;
            last = cur;
            while(src[cur_pos] != ch  && cur_pos != src.length() && cur_pos < src.length()) {
                res += src[cur_pos];
                ICUR;
            }
            if(skip) ICUR;
            cur = res;
        } catch (int ex) {
            if(ex == EOL_REACHED) {
                type = EOL;
                res = "";
            }
        }
        return res;
    }

    Token &NextToken() {
        this->Next();
        return *this;
    }

    TokenType Type() {
        return type;
    }

private:
    TokenType type;

    string next() {
        //auto temp_pos = cur_pos;
        //const auto temp_last = last_pos;
        string result = "";
        bool isCyr = false;

        if(src[cur_pos] == '/' &&  cur_pos < src.length()-1 && src[cur_pos + 1] == '/') {
            ICUR;
            ICUR;
            while(src[cur_pos] != '\n') ICUR;
        }

        while(strchr(" \t\r\n", src[cur_pos])) ICUR;

        //cout << src[cur_pos] << endl;
        if(firstTime && src[cur_pos] == '.') {
            result += src[cur_pos];
            ICUR;
            while(isalpha(src[cur_pos]) || isdigit(src[cur_pos]) || (strchr("-_@$", src[cur_pos]) != NULL)) {
                result += src[cur_pos];
                ICUR;
            }
        }
        else if(isalpha(src[cur_pos]) || src[cur_pos] == '_' || (isCyr = isCyrillicAlpha())) {
            result += src[cur_pos];
            if(isCyr)
            {
                ICUR;
                result += src[cur_pos];
            }
            ICUR;
            while(isalpha(src[cur_pos]) || isdigit(src[cur_pos])
                  || (strchr("_@$", src[cur_pos]) != NULL) || (isCyr = isCyrillicAlpha())) {
                //cout << "Char: " << src[cur_pos] << endl;
                if(src[cur_pos] == '\0') break;
                result.push_back(src[cur_pos]);
                if(isCyr)
                {
                    ICUR;
                    result.push_back(src[cur_pos]);
                }
                ICUR;
            }
            if(result == "true" || result == "false")
                type = Bool;
            else
                type = Identifier;
        }
        else if(src[cur_pos] == '0' && cur_pos < src.length()-1 && src[cur_pos+1] == 'x')
        {
            //cout << result << endl;
            type = Digit;
            result += "0x";
            ICUR;
            ICUR;


            while (isdigit(src[cur_pos]) || (toupper(src[cur_pos]) >= 'A' && toupper(src[cur_pos]) <= 'F')) {
                result += src[cur_pos];
                ICUR;
            }
        }
        else if(isdigit(src[cur_pos]) ||
                (src[cur_pos] == '-' && cur_pos < src.length()-1 && isdigit(src[cur_pos+1])))
        {
            type = Digit;
            result += src[cur_pos];
            ICUR;

            bool hasDot = false;
            while ([&](){
                   if(src[cur_pos] == '.' && !hasDot) {
                        hasDot = true;
                        return true;
                   }
                   else if(isdigit(src[cur_pos])) return true;
                   return false;
                }()) {
                result += src[cur_pos];
                ICUR;
            }
        }
        else if(src[cur_pos] == '"') {
            type = String;
            ICUR;
            while(src[cur_pos] != '"') {
                if(src[cur_pos] == '\\') {
                    ICUR;
                    switch (src[cur_pos]) {
                        case '\\':
                            result += '\\';
                            break;
                        case '\'':
                            result += '\'';
                            break;
                        case '"':
                            result += '"';
                            break;
                        case 'a':
                            result += '\a';
                            break;
                        case 'b':
                            result += '\b';
                            break;
                        case 'v':
                            result += '\v';
                            break;
                        case 'f':
                            result += '\f';
                            break;
                        case 'n':
                            result += '\n';
                            break;
                        case 'r':
                            result += '\r';
                            break;
                        case 't':
                            result += '\t';
                            break;
                        case '0':
                            result += '\0';
                            break;
                        default:
                            throw UNKNOWN_ESCAPE;
                    }
                }
                else result += src[cur_pos];
                ICUR;
            }
            ICUR;
        }
        else if(src[cur_pos] == '-' && cur_pos < src.length()-1 &&  src[cur_pos + 1] == '>') {
            ICUR;
            ICUR;
            type = Delim;
            result = "->";
        }
        else if(strchr(".,+-=(){}[]|\!@#%^*&~`<>:", src[cur_pos])) {
            result = src[cur_pos];
            type = Delim;
            ICUR;
        }
        //cur_pos = temp_pos;
        //last_pos = temp_last;
        return result;
    }

    void inc(uint& iter) {
        ++iter;
        if(iter >= src.length()) throw EOL_REACHED;
    }

    bool isCyrillicAlpha()
    {
        //int i = (byte)src[cur_pos];
        //cout << i;
        if((byte)src[cur_pos] != 208 && (byte)src[cur_pos] != 209)
            return false;
        if(cur_pos + 1 ==src.length())
            return false;
        byte ch = (byte)src[cur_pos + 1];
        if(ch >= (byte)'а' && ch <= (byte)'п')
            return true;
        if(ch >= (byte)'р' && ch <= (byte)'я')
            return true;
        if(ch >= (byte)'А' && ch <= (byte)'Я')
            return true;
        return false;
    }
};

#undef ICUR

#endif // TOKEN_H
