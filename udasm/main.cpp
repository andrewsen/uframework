#include <iostream>
#include "../uasm/token.h"

using namespace std;

int main()
{
    string code[2];
    code[0] = "\
            private i32 main()\
                .locals\
                0 : double\
                1 : double\
                2 : double\
                .end-locals\
            {\
                ld_f 3.14 \
                stloc_0\
                ld_i32 10 \
                neg\
                conv_f\
                stloc_1\
                ldloc_2\
                ld_i32 3\
                ld_i32 5 \
                ld_2\
                conv_ui32\
                call [math.sem] pow(double, ui32) \
                conv_f\
                call [math.sem] mul(double, double) \
                stloc_2\
                ldloc_2\
                call [::vm.internal] print(double) \
                ld_str \"\n\"\
                call [::vm.internal] print(string) \
                ret\
            }";

    Token toks1(code[0]);
    while(toks1.Type() != Token::EOL)
    {
        toks1.Next();
        string t = toks1.ToString();
        cout << "Token from 1: " << t << endl;
    }
    cin.get();
}

