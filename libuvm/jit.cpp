//#include <cstdarg>
//#include <unordered_map>
#include "runtime.h"

#include "jit.h"

using namespace std;

/* Принимаем что:
 *
 * Количество элементов на стеке всегда = 0:
 *  - перед первой операцией после метки
 *  - после последней операции перед меткой
 *  - перед безусловным переходом (JMP)
 * Перед условным переходом на стеке всегда хранится один 4-байтный элемент, который снимается со стека при переходе
 * Конкретный опкод на конкретной позиции всегда работает с единственным конкретным типом => Для него всегда можно вычислить тип
 * ~~~ПРЫЖОК ОСУЩЕСТВЛЯЕТСЯ ТОЛЬКО НА КОМАНДЫ ЗАГРУЗКИ ЗНАЧЕНИЙ, БЕЗУСЛОВНОГО ПЕРЕХОДА И ВЫЗОВА ФУНКЦИЙ, НО ЭТО НЕТОЧНО~~~
 *
 * Все типы кроме double и (u)long занимают 4 байта
 *
 *
 * byte x byte => NSC
 * short x short =>
 *
 * Возможные баги:
 *  - Обработка значений отличных (меньших) от 4 байт (2, 1 байт) -
 *      возможно, некорректно прописаны переходы в разные режими обработки (не для всех комманд/ошибочно) - перепроверить
 *  - Порядок снятия двух значений со стека для команд, где позиция операнда имеет значение (DIV, SHL...)
 *  - "Грязные" регистры. Непример, для команд div/idiv регистр edx должен равняться нулю чтобы избежать SIGFPE.
 *      При появлении багов обращать внимание на содержимое регистров/памяти, значения констант, и т.д.
 *
 * Дизассемблирование "сырого" 32-битного машинного кода в синтаксис Интел:
 *      objdump -M intel -D -b binary -mi386 <FILE>
 *
 * 64-битное целое хранится в паре регистров edx:eax. В edx - 63..32 биты, в eax - 31..0 биты
 * Отображение 64-битных целых на стеке:
 *      esp -> |31...0| - младшая часть
 *             |63..32| - старшая часть
 *             |......|
 *
 *  <0xXXXX-n>  nop
 *  <0xXXXX>    jmp/jcc ADDR    ;ADDR = 100000000 + <target> - 0xXXXX
 *  <0xXXXX+5>  nop
 */

void pushAddr(addr_t a, vector<byte> &vec);
//void jitCallHelper(jit_function fun, ...) __attribute__((__cdecl__));

#if JIT_LEVEL != JIT_DISABLED

byte* Runtime::jitCompile(Function *f)
{
    log << "JIT: compilling " << f->sign << "...\n";

    vector<byte> x86code;
    x86code.reserve(f->bc_size);
    JITTypeStack type_stack;
    //JCompType type_stack[MAX_STACK_COUNT];
    //int ts_index = 0;
    //vector<JCompType> type_stack;
    //type_stack.reserve(16);
    unordered_map<uint, uint> label_table;
    vector<uint> jump_table;
    jump_table.reserve(16);

    jitGenerateEnter(f, x86code);

    OpCode* code = f->bytecode;

    while(code < f->bytecode + f->bc_size) {
        label_table[(uint)(code - f->bytecode)] = x86code.size();
        switch (*code) {
            case OpCode::ADD: {
                Type type = type_stack.Last();//jitTypeStackPop2(type_stack, ts_index);
                type_stack.Pop();
                switch (type)
                {
                    case Type::UI64:
                    case Type::I64:
                    {
                        // TAKE FIRST NUMBER
                        x86code.push_back(POP_REG(EAX)); // pop lower part
                        x86code.push_back(POP_REG(EDX)); // pop higher part

                        // TAKE SECOND NUMBER
                        x86code.push_back(POP_REG(EBX)); // pop lower part
                        x86code.push_back(POP_REG(ESI)); // pop higher part

                        //add	eax, ebx
                        //adc	edx, esi
                        x86code.push_back(0b11); // ADD..
                        x86code.push_back(MRM(0b11, EAX, EBX));
                        x86code.push_back(0x13); //ADC... (add + CF)
                        x86code.push_back(MRM(0b11, EDX, ESI));

                        x86code.push_back(PUSH_REG(EDX)); // push higher part
                        x86code.push_back(PUSH_REG(EAX)); // push lower part

                        //rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                        break;
                    case Type::UI8:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0b11); // ADD part 1
                        x86code.push_back(MRM(0b11, EAX, EDX)); // ADD part 2
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::CHAR:
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0b11);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFFFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0b11);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                //0x89ABCDEF: [EF|CD|AB|89];
            }
                break;
            case OpCode::ADDF: {
                ///@todo TODO: 64-bit floating point
                rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
            }
                break;
            case OpCode::AND: {
                Type type = type_stack.Last();//jitTypeStackPop2(type_stack, ts_index);
                type_stack.Pop();
                switch (type)
                {
                    case Type::UI64:
                    case Type::I64:
                    {
                        /*
                        pop ebx
                        pop esi

                        pop eax
                        pop edx

                        and	eax, ebx
                        and	edx, esi

                        push edx
                        push eax

                        */
                        // TAKE FIRST NUMBER
                        x86code.push_back(POP_REG(EBX)); // pop lower part
                        x86code.push_back(POP_REG(ESI)); // pop higher part

                        // TAKE SECOND NUMBER
                        x86code.push_back(POP_REG(EAX)); // pop lower part
                        x86code.push_back(POP_REG(EDX)); // pop higher part

                        x86code.push_back(0x23); // AND..
                        x86code.push_back(MRM(0b11, EAX, EBX));
                        x86code.push_back(0x23); // AND..
                        x86code.push_back(MRM(0b11, EDX, ESI));

                        x86code.push_back(PUSH_REG(EDX)); // push higher part
                        x86code.push_back(PUSH_REG(EAX)); // push lower part
                        ///@todo TODO: 64-bit integers
                    }
                        break;
                    case Type::UI8:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x23);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::CHAR:
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x23);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFFFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x23);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::BOOL:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x23);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0x1, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, typeToStr(type) + " with " + OpcodeToStr(*code));
                        break;
                }
            }
                break;
            case OpCode::BREAK:
            {
            }
                break;
            case OpCode::CALL:
            {
                uint faddr = *(uint*)(++code);
                code += 3;
                Function* next = f->module->functions[faddr];

                /*if(next->argc == 0)
                {
                    uint addr = (uint)((code-4) - f->bytecode);
                    jump_table[addr] = x86code.size();
                }*/

                if(!(next->flags & FFlags::RTINTERNAL))
                {
                    if(next->jit_code == nullptr)
                        next->jit_code = jitCompile(next); ///TODO: Inspect
                }
                else if(next->jit_code == nullptr)
                {
                    next->jit_code = (byte*)(void*)findJITInternal(this, next);
                }

#ifndef JIT_NO_FUNC_PRECALL_CHECK
                x86code.push_back(PUSH_CONST_32);           // Push...
                pushAddr((addr_t)next, x86code);  // ...addr of the next function as constant

                //x86code.push_back(0xE8); //CALL...
                x86code.push_back(0xB8); // MOV...
                pushAddr((addr_t)&jitCallHelper, x86code); //...the callHelper                
                x86code.push_back(0xFF); // CALL...
                x86code.push_back(0xD0); // ...eax
                /*****   CLEAN UP STACK (cdecl)   *****/
                x86code.push_back(0x83); //ADD...
                x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                x86code.push_back(0x04); //...4
#endif
                //x86code.push_back(0xE8); //CALL...
                x86code.push_back(0xB8); // MOV...
                pushAddr((addr_t)next->jit_code, x86code);  // ...addr of the next function as constant
                x86code.push_back(0xFF); // CALL...
                x86code.push_back(0xD0); // ...eax

                if(next->argc != 0)
                {
                    x86code.push_back(0x83); //ADD...
                    x86code.push_back(NNN(0b11, 0x0, ESP)); //...to ESP...
                    x86code.push_back(next->argc * 0x04); //...4
                    /// FIXME FIXME FIXME ///      TODO @todo          FIXME        MORE      ADD 64-bit support!!!!!!!!!!!!!!!!!!!!!

                    for(uint i = 0; i < next->argc; ++i)
                        type_stack.Pop();
                }
                if((Type)*next->ret != Type::VOID)
                {
                    x86code.push_back(PUSH_REG(EAX));
                    type_stack.Push(JCompType(next->ret));
                }
            }
                break;
            case OpCode::CONV_F:
            {
                ///@todo TODO: 64-bit floating point
                rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                //stackalloc(4);
                switch((Type)*stack_ptr){
                    case Type::UI8:
                    case Type::UI32: {
                        uint arg = *(uint*)(stack_ptr -= 4);
                        *(double*)stack_ptr = (double)arg;
                        stack_ptr += 8;
                        break;
                    }
                    case Type::I16:
                    case Type::I32: {
                        int arg = *(int*)(stack_ptr -= 4);
                        *(double*)stack_ptr = (double)arg;
                        stack_ptr += 8;
                        break;
                    }
                    case Type::I64:{
                        __int64_t arg = *(__int64_t*)(stack_ptr -= 8);
                        *(double*)stack_ptr = (double)arg;
                        stack_ptr += 8;
                        break;
                    }
                    case Type::UI64:{
                        __uint64_t arg = *(__uint64_t*)(stack_ptr -= 8);
                        *(double*)stack_ptr = (double)arg;
                        stack_ptr += 8;
                        break;
                    }
                    case Type::DOUBLE:
                        break;
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
                *stack_ptr = (byte)Type::DOUBLE;
            }
                break;
            /*:TODO:*/
            case OpCode::CONV_I16:
            {
                Type type = type_stack.Pop();

                switch(type){
                    case Type::UI8: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x0F); // Prefix to...
                        x86code.push_back(0xB6); // MOVZB
                        x86code.push_back(MRM(0b11, AX, AL));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::CHAR: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x98); // CBW
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I32:
                    case Type::UI32: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFFFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I64:
                    case Type::UI64:{
                        /*
                        pop eax
                        pop edx
                        movzx edx ax
                        push edx
                        */
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x0F); // Prefix to...
                        x86code.push_back(0xB7); // MOVZX
                        x86code.push_back(MRM(0b11, EDX, AX));
                        x86code.push_back(PUSH_REG(EDX));
                        ///@todo TODO: 64-bit integers
                        break;
                    }
                    case Type::DOUBLE:{
                        ///@todo TODO: 64-bit floating points
                        rtThrow(NotImplemented, "JIT: 64-bit floating points not implemented");
                        break;
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
                type_stack.Push(Type::I16);
            }
                break;
            case OpCode::CONV_CHR:
            {
                Type type = type_stack.Pop();

                switch(type){
                    case Type::UI8: {
                        break;
                    }
                    case Type::I16:
                    case Type::I32:
                    case Type::UI32: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x25); // AND EAX
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I64:
                    case Type::UI64:{
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x25); // AND EAX
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        ///@todo TODO: 64-bit integers
                        break;
                    }
                    case Type::DOUBLE:{
                        ///@todo TODO: 64-bit floating points
                        rtThrow(NotImplemented, "JIT: 64-bit floating points not implemented");
                        break;
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
                type_stack.Push(Type::CHAR);
            }
                break;
            case OpCode::CONV_I32:
            case OpCode::CONV_UI32:
            {
                Type type = type_stack.Pop();

                switch(type){
                    case Type::UI8: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x0F); // Prefix to...
                        x86code.push_back(0xB6); // MOVZB
                        x86code.push_back(MRM(0b11, EAX, AL));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::CHAR: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x0F); // Prefix to...
                        x86code.push_back(0xBE); // MOVZB
                        x86code.push_back(MRM(0b11, EAX, AL));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I16: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x98); // CWDE - 16bit int to 32bit
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I32:
                    case Type::UI32:
                        break;
                    /*{
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFFFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }*/
                    case Type::I64:
                    case Type::UI64:{
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::DOUBLE:{
                        ///@todo TODO: 64-bit floating points
                        rtThrow(NotImplemented, "JIT: 64-bit floating points not implemented");
                        break;
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
                type_stack.Push(*code == OpCode::CONV_I32 ? Type::I32 : Type::UI32);
            }
                break;
            case OpCode::CONV_UI64:
            case OpCode::CONV_I64:
            {
                Type type = type_stack.Pop();

                switch(type){
                    case Type::UI8: {
                        x86code.push_back(POP_REG(EAX)); /// @todo TODO TEST
                        x86code.push_back(PUSH_CONST_32);
                        pushAddr(0, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::CHAR: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x98);        //CBW
                        x86code.push_back(0x98);        //CWDE
                        x86code.push_back(0x99);        //CDQ
                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I16: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x98);        //CWDE
                        x86code.push_back(0x99);        //CDQ
                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I32: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x99);        //CDQ
                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::UI32: {
                        x86code.push_back(POP_REG(EAX)); /// @todo TODO TEST
                        x86code.push_back(PUSH_CONST_32);
                        pushAddr(0, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    /*{
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFFFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }*/
                    case Type::I64:
                    case Type::UI64:
                        break;
                    case Type::DOUBLE:{
                        ///@todo TODO: 64-bit floating points
                        rtThrow(NotImplemented, "JIT: 64-bit floating points not implemented");
                        break;
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
                type_stack.Push(*code == OpCode::CONV_I64 ? Type::I64 : Type::UI64);
            }
                break;
            case OpCode::CONV_UI8:
            {
                Type type = type_stack.Pop();

                switch(type){
                    case Type::CHAR: {
                        break;
                    }
                    case Type::I16:
                    case Type::I32:
                    case Type::UI32: {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I64:
                    case Type::UI64:{
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::DOUBLE:{
                        ///@todo TODO: 64-bit floating points
                        rtThrow(NotImplemented, "JIT: 64-bit floating points not implemented");
                        break;
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
                type_stack.Push(Type::UI8);
            }
                break;
            case OpCode::DEC:
            {
                Type type = type_stack.Last();
                switch(type){
                    case Type::UI8:
                    case Type::CHAR:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xFE);
                        x86code.push_back(NNN(0b11, 0b001, AL));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x48);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x48);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I64:
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integers
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x83); //ADD
                        x86code.push_back(NNN(0b11, 0b0, EAX)); //...eax...
                        x86code.push_back(0xFF); //... -1
                        x86code.push_back(0x83); //ADC
                        x86code.push_back(NNN(0b11, 0b010, EDX)); //...edx...
                        x86code.push_back(0xFF); // ... -1
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        ///@todo TODO: 64-bit floating point
                        rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
            }
                break;
            case OpCode::DIV: {
                Type type = type_stack.Pop();
                switch (type) {
                    case Type::I64:                        
                    {
                        x86code.push_back(0xB8); // MOV...
                        if(jitConfig.useLibGCC)
                        {
                            if(jitConfig.divdi3Ptr == nullptr)
                                jitConfig.divdi3Ptr = jitFindLibGCCHelper("__divdi3");
                            pushAddr((addr_t)jitConfig.divdi3Ptr, x86code);
                        }
                        else
                            pushAddr((addr_t)&jitI64DivisionHelper, x86code); //...the callHelper
                        x86code.push_back(0xFF); // CALL...
                        x86code.push_back(0xD0); // ...eax
                        /*****   CLEAN UP STACK (cdecl)   *****/
                        x86code.push_back(0x83); //ADD...
                        x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                        x86code.push_back(0x10); //...16
                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI64:
                    {
                        x86code.push_back(0xB8); // MOV...
                        if(jitConfig.useLibGCC)
                        {
                            if(jitConfig.udivdi3Ptr == nullptr)
                                jitConfig.udivdi3Ptr = jitFindLibGCCHelper("__udivdi3");
                            pushAddr((addr_t)jitConfig.udivdi3Ptr, x86code);
                        }
                        else
                            pushAddr((addr_t)&jitUI64DivisionHelper, x86code); //...the callHelper
                        x86code.push_back(0xFF); // CALL...
                        x86code.push_back(0xD0); // ...eax
                        /*****   CLEAN UP STACK (cdecl)   *****/
                        x86code.push_back(0x83); //ADD...
                        x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                        x86code.push_back(0x10); //...16
                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI8:
                    {
                        x86code.push_back(0x31);
                        x86code.push_back(MRM(0b11, EDX, EDX)); // ???
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xF6);
                        x86code.push_back(NNN(0b11, 0b110, ECX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::CHAR:
                    {
                        x86code.push_back(0x31);
                        x86code.push_back(MRM(0b11, EDX, EDX)); // ???
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(CMODE_16); ///@todo: TEST
                        x86code.push_back(0x98); // CBW  - al to ax -- TEST
                        x86code.push_back(0xF6);
                        x86code.push_back(NNN(0b11, 0b111, ECX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I16:
                    {
                        //x86code.push_back(0x31);
                        //x86code.push_back(MRM(0b11, EDX, EDX)); // ???
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x99); // CWD  - ax to dx:ax -- TEST
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b11, 0b111, ECX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFFFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::UI32:
                    {
                        x86code.push_back(0x31);
                        x86code.push_back(MRM(0b11, EDX, EDX)); // ???
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b11, 0b110, ECX));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I32:
                    {
                        //x86code.push_back(0x31);
                        //x86code.push_back(MRM(0b11, EDX, EDX)); // ???
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x99); // CDQ  - eax to edx:eax
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b11, 0b111, ECX));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
                //0x89ABCDEF: [EF|CD|AB|89];
            }
                break;
            case OpCode::DIVF: {
                ///@todo TODO: 64-bit floating point
                rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
            }
                break;
            case OpCode::DUP: {
                ///@todo TODO: 64-bit integer
                ///@todo TODO: 64-bit floating point
                Type type = type_stack.Last();
                type_stack.Push(JCompType(type));
                switch (type) {
                    case Type::BOOL:
                    case Type::CHAR:
                    case Type::UI8:
                    case Type::I16:
                    case Type::UI32:
                    case Type::I32:
                    case Type::UTF8:
                    case Type::ARRAY:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(PUSH_REG(EAX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::I64:
                    case Type::UI64:
                    case Type::DOUBLE:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));

                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));

                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    default:
                        break;
                }
            }
                break;
            case OpCode::EQ: {
                Type type = type_stack.Pop2();
                switch (type) {
                    case Type::UI8:
                    case Type::CHAR:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(0x38);            //CMP
                        x86code.push_back(MRM(0b11, AL, DL)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+0b0100);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, AX, DX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+0b0100);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I64:
                    case Type::UI64:
                    {
                        x86code.push_back(POP_REG(EBX));
                        x86code.push_back(POP_REG(ESI));

                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));

                        x86code.push_back(0x33); //XOR
                        x86code.push_back(MRM(0b11, EAX, EBX));
                        x86code.push_back(0x33); //XOR
                        x86code.push_back(MRM(0b11, EDX, ESI));
                        x86code.push_back(0x0B); //OR
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x85);            //TEST
                        x86code.push_back(MRM(0b11, EAX, EAX));
                        x86code.push_back(0x90+0b0100); //SETE
                        x86code.push_back(NNN(0b11, 0, AL));

                        x86code.push_back(0x0F); // Prefix to...
                        x86code.push_back(0xB6); // MOVZX
                        x86code.push_back(MRM(0b11, EAX, AL));
                        x86code.push_back(PUSH_REG(EAX));


                        ///@todo TODO: 64-bit integer
                    }
                        break;
                    case Type::PTR_NULL:
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, EAX, EDX)); //..
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+0b0100); //SETE
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        ///@todo TODO: 64-bit floating point
                        rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                    }
                        break;
                    case Type::UTF8:
                    {
                        /*
                         * call jitStringECompareHelper
                         * add esp, 8
                         * push eax
                         *
                         */
                        //x86code.push_back(0xE8); //CALL...
                        x86code.push_back(0xB8); // MOV...
                        pushAddr((addr_t)&jitStringECompareHelper, x86code); //...the callHelper                        
                        x86code.push_back(0xFF); // CALL...
                        x86code.push_back(0xD0); // ...eax
                        /*****   CLEAN UP STACK (cdecl)   *****/
                        x86code.push_back(0x83); //ADD...
                        x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                        x86code.push_back(0x08); //...8
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::BOOL:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x25); // TRUNCATE EAX
                        pushAddr(0x1, x86code);

                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x81);
                        x86code.push_back(NNN(0b11, 0b100, EDX)); // TRUNCATE EDX
                        pushAddr(0x1, x86code);

                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, EAX, EDX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+0b0100); //SETE
                        x86code.push_back(NNN(0b11, 0, AL));
                        x86code.push_back(PUSH_REG(EAX));

                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                type_stack.Push(Type::BOOL);
            }
                break;
            /*case OpCode::FREELOC:
            {
                :TODO:
            }*/

            case OpCode::GT: { /// @todo FIXME                                  64 bit FINISHED HERE
                Type type = type_stack.Pop2();
                const uint unsigned_flag = 0b0111; //JA (JNBE)
                const uint signed_flag = 0b1111;   //JG (JNLE)
                switch (type) {
                    case Type::UI8:
                    case Type::CHAR:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(0x38);            //CMP
                        x86code.push_back(MRM(0b11, AL, DL)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+unsigned_flag);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, AX, DX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+signed_flag);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I64:
                    case Type::UI64:
                    {


                        ///@todo TODO: 64-bit integer
                    }
                        break;
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, EAX, EDX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+(type == Type::I32 ? signed_flag : unsigned_flag)); //SETG or SETA
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        ///@todo TODO: 64-bit floating point
                        rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                type_stack.Push(Type::BOOL);
            }
                break;
            case OpCode::GTE: {
                Type type = type_stack.Pop2();
                uint unsigned_flag = 0b0011; //JAE (JNB)
                uint signed_flag = 0b1101;   //JGE (JNL)
                switch (type) {
                    case Type::UI8:
                    case Type::CHAR:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x38);            //CMP
                        x86code.push_back(MRM(0b11, AL, DL)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+unsigned_flag);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I16:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, AX, DX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+signed_flag);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integer
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                        break;
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integer
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                        break;
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, EAX, EDX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+(type == Type::I32 ? signed_flag : unsigned_flag)); //SETG
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        ///@todo TODO: 64-bit floating point
                        rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                type_stack.Push(Type::BOOL);
            }
                break;
            case OpCode::INC:
            {
                Type type = type_stack.Last();
                switch(type){
                    case Type::UI8:
                    case Type::CHAR:
                    case Type::UI32:
                    case Type::I16:
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x40);
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                    case Type::DOUBLE:
                    {
                        ///@todo TODO: 64-bit floating point
                        rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
            }
                break;

            case OpCode::INV:
            {
                Type type = type_stack.Last();
                switch(type){
                    case Type::UI8:
                    case Type::CHAR:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xF6);
                        x86code.push_back(NNN(0b0, 0b010, AL));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b0, 0b010, EAX));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b0, 0b010, AX));
                        x86code.push_back(PUSH_REG(EAX));
                        break;
                    }
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                    case Type::DOUBLE:
                    {
                        ///@todo TODO: 64-bit floating point
                        rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
            }
                break;

            case OpCode::JMP:
            {                
                uint addr = *(uint*)++code;
                //x86code.push_back(0x3E);
                x86code.push_back(0xE9); // JMP near
                jump_table.push_back(x86code.size());   //save relative pointer to label addr
                pushAddr(addr, x86code);    // save label addr
                code += 3;
            }
                break;
            case OpCode::JF:
            case OpCode::JZ:
            {
                x86code.push_back(POP_REG(EAX));
                x86code.push_back(0x85);            //TEST
                x86code.push_back(MRM(0b11, EAX, EAX)); //...

                //x86code.push_back(0x3E);
                x86code.push_back(0x0F);
                x86code.push_back(0x80 + 0b0100); // JZ near
                uint addr = *(uint*)++code;
                jump_table.push_back(x86code.size());
                pushAddr(addr, x86code);
                code += 3;
            }
                break;
            case OpCode::JT:
            case OpCode::JNZ:
            {
                x86code.push_back(POP_REG(EAX));
                x86code.push_back(0x85);            //TEST
                x86code.push_back(MRM(0b11, EAX, EAX)); //...

                //x86code.push_back(0x3E);
                x86code.push_back(0x0F);
                x86code.push_back(0x80 + 0b0101); // JNZ near
                uint addr = *(uint*)++code;
                jump_table.push_back(x86code.size());
                pushAddr(addr, x86code);
                code += 3;
            }
                break;
            case OpCode::JNULL:
            case OpCode::JNNULL:
            {
                rtThrow(NotImplemented);
            }
                break;
            case OpCode::LD_AREF: ///////////////////////FIXME::::::::::::::::::::::::: @@@@@@@@@@@@@@@@@@@@@@@@@test@fix a to kak loh
            {
                type_stack.Pop();
                Type type = type_stack.Last().base;


                // pop eax
                // pop edx
                if(type == Type::CHAR)
                {
                    x86code.push_back(POP_REG(EDX));
                    x86code.push_back(POP_REG(EAX));
                    x86code.push_back(0x83); //ADD...
                    x86code.push_back(NNN(0b11, 000, EAX)); //...to EAX...
                    x86code.push_back(ARRAY_METADATA_SIZE); //... ARR_SIZE
                    x86code.push_back(0b11); //ADD...
                    x86code.push_back(MRM(0b11, EAX, EDX)); //...to EAX EDX
                }
                else
                {
                    x86code.push_back(POP_REG(EDX)); //offset (index) TEST ??
                    //x86code.push_back(POP_REG(EDX)); //addr

                    x86code.push_back(0xB8); //MOV...
                    pushAddr(Sizeof(type), x86code);

                    x86code.push_back(0xF7); //MUL...
                    x86code.push_back(NNN(0b11, 0b100, EDX)); // eax = sizeof(t)*offset || eax = eax*edx
                    x86code.push_back(0x83); //ADD...
                    x86code.push_back(NNN(0b11, 000, EAX)); //...to EAX...
                    x86code.push_back(ARRAY_METADATA_SIZE); //... ARR_SIZE

                    x86code.push_back(POP_REG(EDX));
                    x86code.push_back(0b11); //ADD...
                    x86code.push_back(MRM(0b11, EAX, EDX)); //...to EAX EDX
                }

                x86code.push_back(PUSH_REG(EAX));

                //eax = eax + ARR_SIZE + edx*sizeof(t)

                //ld_aref();
            }
                break;
            case OpCode::LD_BYREF:
            {
                //TODO: TEST
                Type type = type_stack.Last().base;

                x86code.push_back(POP_REG(EAX)); //offset (index)
                if(type == Type::CHAR)
                {
                    x86code.push_back(0x8B); //MOV...
                    x86code.push_back(MRM(0b00, 0b000, EAX));//...eax, [eax]
                    x86code.push_back(0x25); // AND
                    pushAddr(0xFF, x86code);
                }
                else
                {
                    uint masks[] = {0, 0xFF, 0xFFFF};
                    uint size = Sizeof(type);
                    x86code.push_back(0x8B); //MOV...
                    x86code.push_back(MRM(0b00, 0b000, EAX));//...eax, [eax]
                    if(size != 4)
                    {
                        x86code.push_back(0x25); // AND
                        pushAddr(masks[size], x86code);
                    }
                }
                x86code.push_back(PUSH_REG(EAX));// push eax

                type_stack.Push(type);
            }
                break;
            case OpCode::LDARG: {
                // push dword [ebp+(8 + arg_idx*4)]
                //byte idx = 8 + (*(uint*)(++code)) * 4;
                byte idx = (byte)*(uint*)(++code);

                x86code.push_back(0xFF);
                x86code.push_back(NNN(0b01, 0b110, EBP));
                x86code.push_back(ARG_32(idx));
                //ldarg(*(uint*)(++code), f, fargs);
                code += 3; //FIXME: May fail

                type_stack.Push(JCompType(f->args[idx].type));
            }
                break;
            case OpCode::LDARG_0:
                // push dword [ebp+(8 + arg_idx*4)]
                x86code.push_back(0xFF);
                x86code.push_back(NNN(0b01, 0b110, EBP));
                x86code.push_back(ARG_32(0));
                type_stack.Push(JCompType(f->args[0].type));
                break;
            case OpCode::LDARG_1:
                // push dword [ebp+(8 + arg_idx*4)]
                x86code.push_back(0xFF);
                x86code.push_back(NNN(0b01, 0b110, EBP));
                x86code.push_back(ARG_32(1));
                type_stack.Push(JCompType(f->args[1].type));
                break;
            case OpCode::LDARG_2:
                // push dword [ebp+(8 + arg_idx*4)]
                x86code.push_back(0xFF);
                x86code.push_back(MRM(0b01, 0b110, EBP));
                x86code.push_back(ARG_32(2));
                type_stack.Push(JCompType(f->args[2].type));
                break;

            case OpCode::LDFLD: {
                uint idx = *(uint*)(++code);
                jitLdfld(f, idx, x86code, type_stack);
                //ldfld(*(uint*)(++code), f);
                type_stack.Push(JCompType(f->module->globals[idx].type));
                code += 3;
            }
                break;
            case OpCode::LDFLD_0:
                jitLdfld(f, 0, x86code, type_stack);
                //jitTypeStackPush(type_stack, ts_index, JCompType(f->module->globals[0].type));
                break;
            case OpCode::LDFLD_1:
                jitLdfld(f, 1, x86code, type_stack);
                //jitTypeStackPush(type_stack, ts_index, JCompType(f->module->globals[1].type));
                break;
            case OpCode::LDFLD_2:
                jitLdfld(f, 2, x86code, type_stack);
                //jitTypeStackPush(type_stack, ts_index, JCompType(f->module->globals[2].type));
                break;

            case OpCode::LDLOC: {
                // push dword [ebp-(4 + loc_idx*4)]
                uint idx = *(uint*)(++code);
                x86code.push_back(0xFF);
                x86code.push_back(NNN(0b01, 0b110, EBP));
                x86code.push_back(LOC_32(idx)); ///TODO: @todo test this shit
                //ldloc(*(uint*)(++code), f, local_table);
                code += 3;
                type_stack.Push(JCompType(f->locals[idx].type));
            }
                break;
            case OpCode::LDLOC_0:
                x86code.push_back(0xFF);
                x86code.push_back(NNN(0b01, 0b110, EBP));
                x86code.push_back(LOC_32(0));
                type_stack.Push(JCompType(f->locals[0].type));
                break;
            case OpCode::LDLOC_1:
                x86code.push_back(0xFF);
                x86code.push_back(NNN(0b01, 0b110, EBP));
                x86code.push_back(LOC_32(1));
                type_stack.Push(JCompType(f->locals[1].type));
                break;
            case OpCode::LDLOC_2:
                x86code.push_back(0xFF);
                x86code.push_back(NNN(0b01, 0b110, EBP));
                x86code.push_back(LOC_32(2));
                type_stack.Push(JCompType(f->locals[2].type)); //12345678
                break;
            case OpCode::LDELEM:
                jitLdelem(type_stack, x86code); //TODO: make ldelem
                break;
            case OpCode::LD_FALSE:
                jitPushImm32(0, x86code);
                type_stack.Push(Type::BOOL);
                break;
            case OpCode::LD_NULL:
                jitPushImm32(0, x86code);
                type_stack.Push(Type::PTR_NULL);
                break;
            case OpCode::LD_0U:
                jitPushImm32(0, x86code);
                type_stack.Push(Type::UI32);
                break;
            case OpCode::LD_0:
                jitPushImm32(0, x86code);
                type_stack.Push(Type::I32);
                break;
            case OpCode::LD_TRUE:
                jitPushImm32(1, x86code);
                type_stack.Push(Type::BOOL);
                break;
            case OpCode::LD_1U:
                jitPushImm32(1, x86code);
                type_stack.Push(Type::UI32);
                break;
            case OpCode::LD_1:
                jitPushImm32(1, x86code);
                type_stack.Push(Type::I32);
                break;
            case OpCode::LD_2U:
                jitPushImm32(2, x86code);
                type_stack.Push(Type::UI32);
                break;
            case OpCode::LD_2:
                jitPushImm32(2, x86code);
                type_stack.Push(Type::I32);
                break;
            case OpCode::LD_F:
            {
                //TODO: add floating point
                throw "LD_F unsupported";
            }
                break;
            case OpCode::LD_UI8:
            {
                jitPushImm32(*(byte*)(++code), x86code);
                type_stack.Push(Type::UI8);
                code += 3;
                break;
            }

            case OpCode::LD_I16:
            {
                jitPushImm32(*(short*)(++code), x86code);
                type_stack.Push(Type::I16);
                code += 3;
                break;
            }
            case OpCode::LD_CHR:
            {
                jitPushImm32(*(byte*)(++code), x86code);
                type_stack.Push(Type::CHAR);
                code += 3;
            }
                break;

            case OpCode::LD_I32:
            {
                jitPushImm32(*(int*)(++code), x86code);
                type_stack.Push(Type::I32);
                code += 3;
            }
                break;
            case OpCode::LD_UI32:
            {
                jitPushImm32(*(uint*)(++code), x86code);
                type_stack.Push(Type::UI32);
                code += 3;
                break;
            }

            case OpCode::LD_I64:
            {
                throw "LD_I64 unsupported";
                break;
            }

            case OpCode::LD_UI64:
            {
                throw "LD_UI64 unsupported";
                break;
            }
            case OpCode::LD_STR:
            {
                char* ptr = f->module->strings + *(uint*)(++code);
                uint len = strlen(ptr)+1;
                type_stack.Push(JCompType(Type::UTF8));

                //push len
                //push ptr
                //call jitGCAllocStr()
                jitPushImm32(len, x86code);
                jitPushImm32((uint)ptr, x86code);
                //x86code.push_back(0xE8); //CALL...
                x86code.push_back(0xB8); //MOV...
                pushAddr((addr_t)&jitGCAllocStr, x86code); //...string allocator
                x86code.push_back(0xFF); // CALL...
                x86code.push_back(0xD0); // ...eax
                /*****   CLEAN UP STACK (cdecl)   *****/
                x86code.push_back(0x83); //ADD...
                x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                x86code.push_back(0x08); //...8

                x86code.push_back(PUSH_REG(EAX));

                code += 3;
                //f->module->strings
            }
                break;

            case OpCode::LT:
            {
                Type type = type_stack.Pop2();
                const uint unsigned_flag = 0b0010; //JB (JNAE)
                const uint signed_flag = 0b1100;   //JL (JNLE)
                switch (type) {
                    case Type::UI8:
                    case Type::CHAR:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x38);            //CMP
                        x86code.push_back(MRM(0b11, AL, DL)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+unsigned_flag);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I16:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, AX, DX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+signed_flag);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integer
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                        break;
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integer
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                        break;
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, EAX, EDX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+(type == Type::I32 ? signed_flag : unsigned_flag)); //SETL
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        ///@todo TODO: 64-bit floating point
                        rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                type_stack.Push(Type::BOOL);
            }
                break;
            case OpCode::LTE:
            {
                Type type = type_stack.Pop2();
                const uint unsigned_flag = 0b0110;
                const uint signed_flag = 0b1110;
                switch (type) {
                    case Type::UI8:
                    case Type::CHAR:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x38);            //CMP
                        x86code.push_back(MRM(0b11, AL, DL)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+unsigned_flag);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I16:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, AX, DX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+signed_flag);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integer0b1100
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                        break;
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integer
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                        break;
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, EAX, EDX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+(type == Type::I32 ? signed_flag : unsigned_flag)); //SETLE
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        ///@todo TODO: 64-bit floating point
                        rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                type_stack.Push(Type::BOOL);
            }
                break;
            case OpCode::MUL: {
                Type type = type_stack.Pop();
                x86code.push_back(POP_REG(EAX));
                x86code.push_back(POP_REG(EDX));
                switch (type) {
                    case Type::I64:
                    case Type::UI64: /// @todo CHECK TEST
                    {
                        x86code.pop_back();
                        x86code.pop_back();
#ifdef GCC_64_MUL
                        /*
                            pop ebx
                            pop esi

                            pop eax
                            pop edx

                            mov	edi, esi	// ebx - x_l, esi - x_h, eax - y_l, edx - y_h, edi - x_h
                            imul edi, eax       // edx:eax = edi * eax == x_h * y_l
                            mov	ecx, edx
                            imul ecx, ebx
                            add	ecx, edi
                            mul	ebx
                            add	ecx, edx
                            mov	edx, ecx */
                        x86code.push_back(POP_REG(EBX)); // 1 low
                        x86code.push_back(POP_REG(ESI)); // 1 high

                        x86code.push_back(POP_REG(EAX)); // 2 low
                        x86code.push_back(POP_REG(EDX)); // 2 high

                        x86code.push_back(0x8B); // MOV...
                        x86code.push_back(MRM(0b11, EDI, ESI)); //.. esi to edi

                        x86code.push_back(0x0F); // [ext]
                        x86code.push_back(0xAF); // IMUL
                        x86code.push_back(MRM(0b11, EDI, EAX)); //.. edi * eax

                        x86code.push_back(0x8B); // MOV...
                        x86code.push_back(MRM(0b11, ECX, EDX)); //.. edx to ecx

                        x86code.push_back(0x0F); // [ext]
                        x86code.push_back(0xAF); // IMUL
                        x86code.push_back(MRM(0b11, ECX, EBX)); //.. ecx * ebx

                        x86code.push_back(0b11); // ADD
                        x86code.push_back(MRM(0b11, ECX, EDI));

                        x86code.push_back(0xF7); // MUL...
                        x86code.push_back(NNN(0b11, 0b100, EBX)); // eax = eax*ebx

                        x86code.push_back(0b11); // ADD
                        x86code.push_back(MRM(0b11, ECX, EDX));

                        x86code.push_back(0x8B); // MOV...
                        x86code.push_back(MRM(0b11, EDX, ECX)); //.. ecx to edx

                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
#else // SO_64_MUL
                        /*
                            mov edi, val1high
                            mov esi, val1low
                            mov ecx, val2high
                            mov ebx, val2low
                        MUL64_EDIESI_ECXEBX:
                            mov eax, edi
                            mul ebx
                            xch eax, ebx  ; partial product top 32 bits
                            mul esi
                            xch esi, eax ; partial product lower 32 bits
                            add ebx, edx
                            mul ecx
                            add ebx, eax  ; final upper 32 bits
                        ; answer here in EBX:ESI
                        */
                        x86code.push_back(POP_REG(ESI)); // 1 low
                        x86code.push_back(POP_REG(EDI)); // 1 high

                        x86code.push_back(POP_REG(ECX)); // 2 low
                        x86code.push_back(POP_REG(EBX)); // 2 high

                        x86code.push_back(0x8B); // MOV...
                        x86code.push_back(MRM(0b11, EAX, EDI)); //.. edi to eax

                        x86code.push_back(0xF7); // MUL...
                        x86code.push_back(NNN(0b11, 0b100, EBX)); // eax = eax*ebx

                        x86code.push_back(0x93); // XCHG EAX, EBX

                        x86code.push_back(0xF7); // MUL...
                        x86code.push_back(NNN(0b11, 0b100, ESI)); // eax = eax*esi

                        x86code.push_back(0x96); // XCHG EAX, ESI

                        x86code.push_back(0b11);
                        x86code.push_back(MRM(0b11, EBX, EDX));

                        x86code.push_back(0xF7); // MUL...
                        x86code.push_back(NNN(0b11, 0b100, ECX)); // eax = eax*ecx

                        x86code.push_back(0b11);
                        x86code.push_back(MRM(0b11, EBX, EAX));

                        x86code.push_back(PUSH_REG(EBX));
                        x86code.push_back(PUSH_REG(ESI));
#endif
                    }
                    case Type::UI8:
                    {
                        x86code.push_back(0xF6); //IMUL...
                        x86code.push_back(NNN(0b11, 0b100, DL)); // eax = eax*edx TODO: test @todo todo
                    }
                        break;
                    case Type::CHAR:
                    {
                        x86code.push_back(0xF6); //IMUL...
                        x86code.push_back(NNN(0b11, 0b101, DL)); // eax = eax*edx TODO: test @todo todo
                    }
                        break;
                    case Type::I16:
                    {
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0xF7); //IMUL...
                        x86code.push_back(NNN(0b11, 0b101, EDX)); // eax = eax*edx
                        break;
                    }
                    case Type::UI32:
                    {
                        x86code.push_back(0xF7); //MUL...
                        x86code.push_back(NNN(0b11, 0b100, EDX)); // eax = eax*edx
                        break;
                    }
                    case Type::I32:
                    {
                        x86code.push_back(0xF7); //IMUL...
                        x86code.push_back(NNN(0b11, 0b101, EDX)); // eax = eax*edx
                        break;
                    }
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
                x86code.push_back(PUSH_REG(EAX));
            }
                break;
            case OpCode::MULF: {
                ///@todo TODO: 64-bit floating point
                rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
            }
                break;
            case OpCode::NEG: {
                Type type = (Type)*stack_ptr;
                x86code.push_back(POP_REG(EAX));
                switch (type) {
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integer
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integer
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                    case Type::UI8:
                    {
                        x86code.push_back(0xF6); //NEG...
                        x86code.push_back(NNN(0b11, 0b011, AL));
                    }
                    case Type::CHAR:
                    {
                        x86code.push_back(0xF6); //NEG...
                        x86code.push_back(NNN(0b11, 0b011, AL));
                    }
                    case Type::I16:
                    {
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0xF7); //NEG...
                        x86code.push_back(NNN(0b11, 0b011, EAX));
                        break;
                    }
                    case Type::UI32:
                    {
                        x86code.push_back(0xF7); //NEG...
                        x86code.push_back(NNN(0b11, 0b011, EAX));
                        break;
                    }
                    case Type::I32:
                    {
                        x86code.push_back(0xF7); //NEG...
                        x86code.push_back(NNN(0b11, 0b011, EAX));
                        break;
                    }
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                    x86code.push_back(PUSH_REG(EAX));
                }
            }
                break;
            case OpCode::NEQ: {
                Type type = type_stack.Pop2();
                switch (type) {
                    case Type::UI8:
                    case Type::CHAR:
                    {
                        x86code.push_back(0x31); //XOR NULL EAX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x38);            //CMP
                        x86code.push_back(MRM(0b11, AL, DL)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+0b0101);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I16:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, AX, DX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+0b0101);
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integer
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                        break;
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integer
                        rtThrow(NotImplemented, "JIT: 64-bit integer not implemented");
                    }
                        break;
                    case Type::PTR_NULL:
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(0x31); //XOR NULL ECX
                        x86code.push_back(MRM(0b11, ECX, ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, EAX, EDX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+0b0101); //SETE
                        x86code.push_back(NNN(0b11, 0, CL));
                        x86code.push_back(PUSH_REG(ECX));
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        ///@todo TODO: 64-bit floating point
                        rtThrow(NotImplemented, "JIT: 64-bit floating point not implemented");
                    }
                        break;
                    case Type::UTF8:
                    {
                        /*
                         * call jitStringNECompareHelper
                         * add esp, 8
                         * push eax
                         *
                         */
                        //x86code.push_back(0xE8); //CALL...
                        x86code.push_back(0xB8); // MOV...
                        pushAddr((addr_t)&jitStringNECompareHelper, x86code); //...the callHelper
                        x86code.push_back(0xFF); // CALL...
                        x86code.push_back(0xD0); // ...eax
                        /*****   CLEAN UP STACK (cdecl)   *****/
                        x86code.push_back(0x83); //ADD...
                        x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                        x86code.push_back(0x08); //...8
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::BOOL:
                    {
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x25); // TRUNCATE EAX
                        pushAddr(0x1, x86code);

                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(0x81);
                        x86code.push_back(NNN(0b11, 0b100, EDX)); // TRUNCATE EDX
                        pushAddr(0x1, x86code);

                        x86code.push_back(0x39);            //CMP
                        x86code.push_back(MRM(0b11, EAX, EDX)); //...
                        x86code.push_back(0x0F);
                        x86code.push_back(0x90+0b0101); //SETNE
                        x86code.push_back(NNN(0b11, 0, AL));
                        x86code.push_back(PUSH_REG(EAX));

                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                type_stack.Push(Type::BOOL);
            }
                break;
            case OpCode::NEWARR:
            {
                // count already on stack
                byte* type = *(uint*)(++code) + f->module->types; /// TODO: @test
                code += 3;
                jitPushImm32((uint)*type, x86code);

                //x86code.push_back(0xE8); //CALL...
                x86code.push_back(0xB8); //MOV...
                pushAddr((addr_t)&jitGCArrayAllocHelper, x86code); //...the callHelper
                x86code.push_back(0xFF); // CALL...
                x86code.push_back(0xD0); // ...eax
                /*****   CLEAN UP STACK (cdecl)   *****/
                x86code.push_back(0x83); //ADD...
                x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                x86code.push_back(0x08); //...8
                x86code.push_back(PUSH_REG(EAX));

                auto atype = JCompType(type);
                atype.dimens++;

                type_stack.Push(JCompType(type));
                //uint as = *(uint*)(type+1);
            }
                break;
            case OpCode::NOP: {
                x86code.push_back(0x90); // NOP
            }
                break;
            case OpCode::NOT: {
                *(uint*)(stack_ptr - 4) = (uint)!*(bool*)(stack_ptr - 4);
            }
                break;
            case OpCode::OR: {
                Type type = type_stack.Last();//jitTypeStackPop2(type_stack, ts_index);
                type_stack.Pop();
                switch (type)
                {
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                        break;
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                        break;
                    case Type::UI8:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x0B);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::CHAR:
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x0B);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFFFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x0B);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::BOOL:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x0B);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0x1, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, typeToStr(type) + " with " + OpcodeToStr(*code));
                        break;
                }
            }
                break;
            case OpCode::POP: {
                auto t = type_stack.Pop();
                uint sz = SizeOnStack(t);

                x86code.push_back(0x83); //ADD...
                x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                x86code.push_back(sz); //...sz
            }
                break;
            case OpCode::POS: {
                rtThrow(Runtime::NotImplemented);
            }
                break;
            case OpCode::REM: {
                Type type = type_stack.Pop();
                switch (type) {
                    case Type::I64:
                    {
                        x86code.push_back(0xB8); // MOV...
                        pushAddr((addr_t)&jitI64ModHelper, x86code); //...the callHelper
                        x86code.push_back(0xFF); // CALL...
                        x86code.push_back(0xD0); // ...eax
                        /*****   CLEAN UP STACK (cdecl)   *****/
                        x86code.push_back(0x83); //ADD...
                        x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                        x86code.push_back(0x10); //...16
                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI64:
                    {
                        x86code.push_back(0xB8); // MOV...
                        pushAddr((addr_t)&jitUI64ModHelper, x86code); //...the callHelper
                        x86code.push_back(0xFF); // CALL...
                        x86code.push_back(0xD0); // ...eax
                        /*****   CLEAN UP STACK (cdecl)   *****/
                        x86code.push_back(0x83); //ADD...
                        x86code.push_back(NNN(0b11, 000, ESP)); //...to ESP...
                        x86code.push_back(0x10); //...16
                        x86code.push_back(PUSH_REG(EDX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI8:
                    {
                        x86code.push_back(0x31);
                        x86code.push_back(MRM(0b11, EDX, EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b11, 0b110, ECX));

                        x86code.push_back(0x81); // AND EDX by...
                        x86code.push_back(NNN(0b11, 0b100, EDX));
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EDX));
                        break;
                    }
                    case Type::CHAR:
                    {
                        x86code.push_back(0x31);
                        x86code.push_back(MRM(0b11, EDX, EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b11, 0b111, ECX));

                        x86code.push_back(0x81); // AND EDX by...
                        x86code.push_back(NNN(0b11, 0b100, EDX));
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EDX));
                        break;
                    }
                    case Type::I16:
                    {
                        x86code.push_back(0x31);
                        x86code.push_back(MRM(0b11, EDX, EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b11, 0b111, ECX));

                        x86code.push_back(0x81); // AND EDX by...
                        x86code.push_back(NNN(0b11, 0b100, EDX));
                        pushAddr(0xFFFF, x86code);
                        x86code.push_back(PUSH_REG(EDX));
                        break;
                    }
                    case Type::UI32:
                    {
                        x86code.push_back(0x31); // XOR...
                        x86code.push_back(MRM(0b11, EDX, EDX)); //...EDX EDX

                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b11, 0b110, ECX));
                        x86code.push_back(PUSH_REG(EDX));
                        break;
                    }
                    case Type::I32:
                    {
                        x86code.push_back(0x31);
                        x86code.push_back(MRM(0b11, EDX, EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(0xF7);
                        x86code.push_back(NNN(0b11, 0b111, ECX));
                        x86code.push_back(PUSH_REG(EDX));
                        break;
                    }
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
            }
                break;
            case OpCode::REMF: {
                rtThrow(Runtime::NotImplemented);
            }
                break;
            case OpCode::RET: {
                if((Type)*f->ret != Type::VOID)
                    x86code.push_back(POP_REG(EAX));
                jitGenerateLeave(f, x86code);
            }
                break;
            case OpCode::SHL:
            {
                Type type = type_stack.Last();//jitTypeStackPop2(type_stack, ts_index);
                type_stack.Pop();
                switch (type)
                {
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                        break;
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                        break;
                    case Type::CHAR:
                    case Type::UI8:
                    {
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xD3);
                        x86code.push_back(NNN(0b11, 0b100, EAX));
                        x86code.push_back(PUSH_REG(EAX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                    }
                        break;
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0xD3);
                        x86code.push_back(NNN(0b11, 0b100, AX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xD3);
                        x86code.push_back(NNN(0b11, 0b100, EAX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                //0x89ABCDEF: [EF|CD|AB|89];
            }
                break;
            case OpCode::SHR: {
                Type type = type_stack.Last();//jitTypeStackPop2(type_stack, ts_index);
                type_stack.Pop();
                switch (type)
                {
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                        break;
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                        break;
                    case Type::UI8:
                    {
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xD3);
                        x86code.push_back(NNN(0b11, 0b101, EAX));
                        x86code.push_back(PUSH_REG(EAX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                    }
                        break;
                    case Type::CHAR:
                    {
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xD3);
                        x86code.push_back(NNN(0b11, 0b111, EAX));
                        x86code.push_back(PUSH_REG(EAX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                    }
                        break;
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(CMODE_16);
                        x86code.push_back(0xD3);
                        x86code.push_back(NNN(0b11, 0b111, AX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI32:
                    {
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xD3);
                        x86code.push_back(NNN(0b11, 0b101, EAX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(ECX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0xD3);
                        x86code.push_back(NNN(0b11, 0b111, EAX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }


                //0x89ABCDEF: [EF|CD|AB|89];
            }
                break;
            case OpCode::SIZEOF: {
                throw "SIZEOF is not implemented";
            }
                break;
            case OpCode::ST_BYREF:
            {
                Type type = type_stack.Pop2();

                // pop eax
                //
                //

                x86code.push_back(POP_REG(EAX)); //value
                x86code.push_back(POP_REG(EDX)); //addr

                if(type == Type::CHAR || Sizeof(type) == 1)
                {
                    x86code.push_back(0x88); //MOV...
                    x86code.push_back(MRM(0b00, EAX, EDX));
                }
                else if(Sizeof(type) == 2)
                {
                    x86code.push_back(CMODE_16);
                    x86code.push_back(0x89); //MOV...
                    x86code.push_back(MRM(0b00, EAX, EDX));
                }
                else if(Sizeof(type) == 4)
                {
                    x86code.push_back(0x89); //MOV...
                    x86code.push_back(MRM(0b00, EAX, EDX));
                }
            }
                break;
            case OpCode::STARG: {
                type_stack.Pop();
                // push dword [ebp+(8 + arg_idx*4)]
                //byte idx = 8 + (*(uint*)(++code)) * 4;
                byte idx = (byte)*(uint*)(++code);

                x86code.push_back(POP_REG(EAX));

                x86code.push_back(0x89); //MOV...
                x86code.push_back(MRM(0b01, EAX, EBP)); // ... eax, [ebp+
                x86code.push_back(ARG_32(idx)); // +idx]
                code += 3; //FIXME: May fail
            }
                break;
            case OpCode::STARG_0: {
                type_stack.Pop();
                x86code.push_back(POP_REG(EAX));

                x86code.push_back(0x89); //MOV...
                x86code.push_back(MRM(0b01, EAX, EBP));
                x86code.push_back(ARG_32(0));
                code += 3; //FIXME: May fail
            }
                break;
            case OpCode::STARG_1: {
                type_stack.Pop();
                //Type type = type_stack.Pop();
                x86code.push_back(POP_REG(EAX));

                x86code.push_back(0x89); //MOV...
                x86code.push_back(MRM(0b01, EAX, EBP));
                x86code.push_back(ARG_32(1));
                code += 3; //FIXME: May fail
            }
                break;
            case OpCode::STARG_2: {
                type_stack.Pop();
                x86code.push_back(POP_REG(EAX));

                x86code.push_back(0x89); //MOV...
                x86code.push_back(MRM(0b01, EAX, EBP));
                x86code.push_back(ARG_32(2));
                code += 3; //FIXME: May fail
            }
                break;
            case OpCode::STFLD: {
                jitStfld(f, *(uint*)(++code), x86code, type_stack);
                code += 3; //FIXME: May fail
            }
                break;
            case OpCode::STFLD_0: {
                jitStfld(f, 0, x86code, type_stack);
            }
                break;
            case OpCode::STFLD_1: {
                jitStfld(f, 1, x86code, type_stack);
            }
                break;
            case OpCode::STFLD_2: {
                jitStfld(f, 2, x86code, type_stack);
            }
                break;
            case OpCode::STLOC: {
                type_stack.Pop();
                byte idx = (byte)*(uint*)(++code);

                x86code.push_back(POP_REG(EAX));

                x86code.push_back(0x89); //MOV...
                x86code.push_back(MRM(0b01, EAX, EBP)); // ... eax, [ebp+
                x86code.push_back(LOC_32(idx)); // -idx]
                code += 3;
            }
                break;
            case OpCode::STLOC_0: {
                type_stack.Pop();
                x86code.push_back(POP_REG(EAX));

                x86code.push_back(0x89); //MOV...
                x86code.push_back(MRM(0b01, EAX, EBP)); // ... eax, [ebp+
                x86code.push_back(LOC_32(0)); // -idx]
            }
                break;
            case OpCode::STLOC_1: {
                type_stack.Pop();
                x86code.push_back(POP_REG(EAX));

                x86code.push_back(0x89); //MOV...
                x86code.push_back(MRM(0b01, EAX, EBP)); // ... eax, [ebp+
                x86code.push_back(LOC_32(1)); // -idx]
            }
                break;
            case OpCode::STLOC_2: {
                type_stack.Pop();
                x86code.push_back(POP_REG(EAX));

                x86code.push_back(0x89); //MOV...
                x86code.push_back(MRM(0b01, EAX, EBP)); // ... eax, [ebp+
                x86code.push_back(LOC_32(2)); // -idx]
                //jitTypeStackPush(type_stack, ts_index, JCompType(f->locals[2].type));
            }
                break;
            case OpCode::STELEM: {
                jitStelem(type_stack, x86code);
            }
                break;
            case OpCode::SUB:
                {
                    Type type = type_stack.Last();//jitTypeStackPop2(type_stack, ts_index);
                    type_stack.Pop();
                    switch (type)
                    {
                        case Type::UI64: ///@todo TEST
                        case Type::I64:
                        {
                            // TAKE FIRST NUMBER
                            x86code.push_back(POP_REG(EAX)); // pop lower part
                            x86code.push_back(POP_REG(EDX)); // pop higher part

                            // TAKE SECOND NUMBER
                            x86code.push_back(POP_REG(EBX)); // pop lower part
                            x86code.push_back(POP_REG(ESI)); // pop higher part

                            //add	eax, ebx
                            //adc	edx, esi
                            x86code.push_back(0x2B); // SUB...
                            x86code.push_back(MRM(0b11, EAX, EBX));
                            x86code.push_back(0x1B); //SBB... (sub - CF)
                            x86code.push_back(MRM(0b11, EDX, ESI));

                            x86code.push_back(PUSH_REG(EDX)); // push higher part
                            x86code.push_back(PUSH_REG(EAX)); // push lower part
                        }
                            break;
                        case Type::UI8:
                        {
                            x86code.push_back(POP_REG(EDX));
                            x86code.push_back(POP_REG(EAX));
                            x86code.push_back(0x2B); // SUB part 1
                            x86code.push_back(MRM(0b11, EAX, EDX)); // ADD part 2
                            x86code.push_back(0x25); // AND
                            pushAddr(0xFF, x86code);
                            x86code.push_back(PUSH_REG(EAX));
                        }
                            break;
                        case Type::CHAR:
                        case Type::I16:
                        {
                            x86code.push_back(POP_REG(EDX));
                            x86code.push_back(POP_REG(EAX));
                            x86code.push_back(0x2B);
                            x86code.push_back(MRM(0b11, EAX, EDX));
                            x86code.push_back(0x25); // AND
                            pushAddr(0xFFFF, x86code);
                            x86code.push_back(PUSH_REG(EAX));
                        }
                            break;
                        case Type::UI32:
                        case Type::I32:
                        {
                            x86code.push_back(POP_REG(EDX));
                            x86code.push_back(POP_REG(EAX));
                            x86code.push_back(0x2B);
                            x86code.push_back(MRM(0b11, EAX, EDX));
                            x86code.push_back(PUSH_REG(EAX));
                        }
                            break;
                        default:
                            rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                            break;
                    }

                    //0x89ABCDEF: [EF|CD|AB|89];
                }
                break;
            case OpCode::SUBF: {
                rtThrow(Runtime::NotImplemented);
            }
                break;
            case OpCode::XOR: {
                Type type = type_stack.Last();//jitTypeStackPop2(type_stack, ts_index);
                type_stack.Pop();
                switch (type)
                {
                    case Type::UI64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                        break;
                    case Type::I64:
                    {
                        ///@todo TODO: 64-bit integers
                        rtThrow(NotImplemented, "JIT: 64-bit integers not implemented");
                    }
                        break;
                    case Type::UI8:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x33);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::CHAR:
                    case Type::I16:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x33);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0xFFFF, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::UI32:
                    case Type::I32:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x33);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    case Type::BOOL:
                    {
                        x86code.push_back(POP_REG(EDX));
                        x86code.push_back(POP_REG(EAX));
                        x86code.push_back(0x33);
                        x86code.push_back(MRM(0b11, EAX, EDX));
                        x86code.push_back(0x25); // AND
                        pushAddr(0x1, x86code);
                        x86code.push_back(PUSH_REG(EAX));
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, typeToStr(type) + " with " + OpcodeToStr(*code));
                        break;
                }

            }

                break;
            default:
                log.SetType(Log::Error);
                log << "\nUnknown instruction 0x" << hex << (uint)(byte)*code << "\n";
                log << "  address: 0x" << (uint)(code - f->bytecode) << dec << "\n";
                log << "Call stack:\n";
                //printStackTrace("  at: ");
                rtThrow(IllegalOperation);
                break;
        }
        //x86code.push_back(0x90);
        ++code;
    }

    //int pageSize = sysconf(_SC_PAGE_SIZE);

    byte* data = (byte*)mmap(0, x86code.size()+1,
                                  PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == (byte*)-1) {
        rtThrow(JITException, strerror(errno));
        return nullptr;
    }

    memcpy(data, x86code.data(), x86code.size());

    resolveJumps(data, jump_table, label_table);

    ofstream ofs((string("jit_func_") + f->sign + to_string((uint)f->bytecode) + ".bin").c_str());
    ofs.write((char*)x86code.data(), x86code.size());
    ofs.close();
    mprotect(data, x86code.size(), PROT_EXEC);

    x86code.clear();

    //byte* data = x86code.data();
    /*byte* pagedData = (byte*)((uint)data & -pageSize);

    if (mperr < 0) {
        rtThrow(CantExecute, strerror(errno));
        return nullptr;
    }*/
    log << "JIT: " << f->sign << " compiled\n";

    return data;
}

void Runtime::resolveJumps(byte* code, const vector<uint> &jump_table, const unordered_map<uint, uint> &label_table)
{
    for(uint addr : jump_table)
    {
        uint *virt_addr = (uint*)&code[addr];
        uint maddr = ((uint)code + label_table.at(*virt_addr) - ((uint)code + addr + 4));
        *virt_addr = maddr;//(uint)(code + label_table.at(*virt_addr));
        //cerr << *virt_addr << endl;
    }
}

/*JCompType Runtime::jitGetLastPop2(vector<JCompType> &ts)
{
    auto t = ts[ts.size()-1];
    ts.pop_back();
    ts.pop_back();
    return t;
}

JCompType Runtime::jitGetLastPop(vector<JCompType>& ts)
{
    auto t = ts[ts.size()-1];
    ts.pop_back();
    return t;
} * /

void Runtime::jitTypeStackPush(JCompType ts[MAX_STACK_COUNT], int &idx, JCompType &&val)
{
    ts[idx++] = val;
}

JCompType Runtime::jitTypeStackPop(JCompType ts[MAX_STACK_COUNT], int &idx)
{
    return ts[--idx];
}

JCompType Runtime::jitTypeStackPop2(JCompType ts[MAX_STACK_COUNT], int &idx)
{
    auto& t = ts[idx-1];
    idx -= 2;
    return t;
}

JCompType Runtime::jitTypeStackLast(JCompType ts[MAX_STACK_COUNT], int &idx)
{
    return ts[idx-1];
}*/

void pushAddr(addr_t a, vector<byte> &vec) {
    union {
        byte bytes[4];
        addr_t addr;
    } addr2bytes;
    addr2bytes.addr = a;
    vec.push_back(addr2bytes.bytes[0]);
    vec.push_back(addr2bytes.bytes[1]);
    vec.push_back(addr2bytes.bytes[2]);
    vec.push_back(addr2bytes.bytes[3]);
}

void Runtime::jitLdfld(Function *f, uint idx, vector<byte>& x86code, JITTypeStack &ts)
{
    GlobalVar* gv = &f->module->globals[idx];
    uint sz = Sizeof(gv->type);
    if(sz > 4)
        rtThrow(NotImplemented, "double and (u)i64 are not implemented yet");

    //uint addr = 0;
    ///TODO: @test THIS
    uint addr = (uint)gv->addr;
    //memcpy((char*)&addr, &src, sizeof(void*));

    x86code.push_back(0xA1); // MOV [fld], EAX
    pushAddr((addr_t)addr, x86code);
    ts.Push(JCompType(gv->type));

    //x86code.push_back(PUSH_CONST_32);           // Push...
    //pushAddr((addr_t)addr, x86code);    // ...fld
}

void Runtime::jitStfld(Function *f, uint idx, vector<byte>& x86code, JITTypeStack &ts)
{
    ts.Pop();
    GlobalVar* gv = &f->module->globals[idx];
    uint sz = Sizeof(gv->type);
    if(sz > 4)
        rtThrow(NotImplemented, "double and (u)i64 are not implemented yet");

    //uint addr = 0;
    uint addr = (uint)gv->addr;
    //memcpy((char*)&addr, &src, sizeof(void*));

    x86code.push_back(0xA3); // MOV EAX, [fld]
    pushAddr((addr_t)addr, x86code);

    //x86code.push_back(PUSH_CONST_32);           // Push...
    //pushAddr((addr_t)addr, x86code);    // ...fld
}

void Runtime::jitLdelem(JITTypeStack &types, vector<byte>& x86code)
{
    Type type = types.Last();
    //types.push_back(type);

    //TODO: TEST

    if(type == Type::CHAR)
    {
        x86code.push_back(POP_REG(EDX));
        x86code.push_back(POP_REG(EAX));
        x86code.push_back(0x83); //ADD...
        x86code.push_back(NNN(0b11, 000, EAX)); //...to EAX...
        x86code.push_back(ARRAY_METADATA_SIZE); //... ARR_SIZE
        x86code.push_back(0b11); //ADD...
        x86code.push_back(MRM(0b11, EAX, EDX)); //...to EAX EDX

        x86code.push_back(0x8B); //MOV...
        x86code.push_back(MRM(0b00, 0b000, EAX)); ///...eax, [eax] @todo: MAY FAIL TEST @test FIXME TODO
        x86code.push_back(0x25); // AND
        pushAddr(0xFF, x86code);
    }
    else
    {
        x86code.push_back(POP_REG(EAX)); //offset (index)
        //x86code.push_back(POP_REG(EDX)); //addr
        ///TODO: @test
        x86code.push_back(0xBA); // MOV const TO EDX
        pushAddr(Sizeof(type), x86code);


        x86code.push_back(0xF7); //MUL...
        x86code.push_back(NNN(0b11, 0b100, EDX)); // eax = sizeof(t)*offset || eax = eax*edx
        x86code.push_back(0x83); //ADD...
        x86code.push_back(NNN(0b11, 000, EAX)); //...to EAX...
        x86code.push_back(ARRAY_METADATA_SIZE); //... ARR_SIZE

        x86code.push_back(POP_REG(EDX));
        x86code.push_back(0b11); //ADD...
        x86code.push_back(MRM(0b11, EAX, EDX)); //...to EAX EDX

        uint masks[] = {0, 0xFF, 0xFFFF};
        uint size = Sizeof(type);
        x86code.push_back(0x8B); //MOV...
        x86code.push_back(MRM(0b00, 0b000, EAX)); ///...eax, [eax] @todo: MAY FAIL TEST @test FIXME TODO
        if(size != 4)
        {
            x86code.push_back(0x25); // AND
            pushAddr(masks[size], x86code);
        }
    }

    x86code.push_back(PUSH_REG(EAX));// push eax
}

void Runtime::jitStelem(JITTypeStack &types, vector<byte>& x86code)
{
    Type type = types.Pop2();
    types.Pop();
    //jitGetLastPop(types);
    //types.push_back(type.base);

    //TODO: TEST

    if(type == Type::CHAR)
    {
        x86code.push_back(POP_REG(ECX));    // VALUE
        x86code.push_back(POP_REG(EDX));    // INDEX
        x86code.push_back(POP_REG(EAX));    // ARRAY
        x86code.push_back(0x83); //ADD...
        x86code.push_back(NNN(0b11, 000, EAX)); //...to EAX...
        x86code.push_back(ARRAY_METADATA_SIZE); //... ARR_SIZE
        x86code.push_back(0b11); //ADD...
        x86code.push_back(MRM(0b11, EAX, EDX)); //...to EAX EDX

        x86code.push_back(0x88); //MOV...
        x86code.push_back(MRM(0b00, ECX, EAX)); ///...ecx, [eax] @todo: MAY FAIL TEST @test FIXME TODO
    }
    else
    {
        x86code.push_back(POP_REG(ECX));    // VALUE
        x86code.push_back(POP_REG(EAX)); //offset (index)
        //x86code.push_back(POP_REG(EDX)); //addr
        ///TODO: @test
        x86code.push_back(0xBA); // MOV const TO EDX
        pushAddr(Sizeof(type), x86code);

        x86code.push_back(0xF7); //MUL...
        x86code.push_back(NNN(0b11, 0b100, EDX)); // eax = sizeof(t)*offset || eax = eax*edx
        x86code.push_back(0x83); //ADD...
        x86code.push_back(NNN(0b11, 000, EAX)); //...to EAX...
        x86code.push_back(ARRAY_METADATA_SIZE); //... ARR_SIZE

        x86code.push_back(POP_REG(EDX));
        x86code.push_back(0b11); //ADD...
        x86code.push_back(MRM(0b11, EAX, EDX)); //...to EAX EDX

        //uint masks[] = {0, 0xFF, 0xFFFF};
        //uint size = Sizeof(type);

        if(Sizeof(type) == 2)
        {
            x86code.push_back(CMODE_16);
        }
        x86code.push_back(0x89); //MOV...
        x86code.push_back(MRM(0b00, ECX, EAX)); ///...ecx, [eax] @todo: MAY FAIL TEST @test FIXME TODO
    }

    //x86code.push_back(PUSH_REG(EAX));// push eax
}

void Runtime::jitGenerateEnter(Function *f, vector<byte>& x86asm)
{
    // push ebp
    x86asm.push_back(PUSH_REG(EBP));
    // mov ebp, esp
    x86asm.push_back(0x89);
    x86asm.push_back(MRM(0b11, ESP, EBP));

    if(f->locals_size > 0)
    {
        //TODO: add long valuse;
        byte size = f->locals_size * 4; //suppose types > 4 bytes not supported
        x86asm.push_back(0x83); // SUB
        x86asm.push_back(NNN(0b11, 0b101, ESP));
        x86asm.push_back(size);
    }
}

void Runtime::jitGenerateLeave(Function *f, vector<byte>& x86asm)
{
    // mov esp, ebp
    x86asm.push_back(0x89);
    x86asm.push_back(MRM(0b11, EBP, ESP));
    x86asm.push_back(POP_REG(EBP));

    x86asm.push_back(0xC3); // RET NEAR
}

void Runtime::jitPushImm32(uint imm, vector<byte>& x86asm)
{
    x86asm.push_back(PUSH_CONST_32);
    pushAddr((addr_t)imm, x86asm);    // ...fld
}

void *Runtime::jitFindLibGCCHelper(const char *sign)
{
    //typedef __uint64_t(*longMathFp)(__uint64_t, __uint64_t);
    auto proc = dlopen(nullptr, RTLD_LAZY);

    void* fun = dlsym(proc, sign);
    char* error = dlerror();
    if(error != nullptr)
    {
        //cerr << "Error while searching libGCC function: " << endl << error << endl;
        rtThrow(Runtime::JITException, error);
    }
    return fun;
}

/* HELPERS */
byte* jitGCAllocStr(byte* ptr, uint len)
{
    auto self = Runtime::Instance;
    byte* addr = self->memoryManager.Allocate(Type::UTF8, len + Runtime::ARRAY_METADATA_SIZE); // ACHTUNG

    strcpy((char*)addr + Runtime::ARRAY_METADATA_SIZE, (const char*)ptr);
    *addr = (byte)Type::UTF8;
    *(uint*)(addr+1) = len;

    return addr;
}

byte* jitGCArrayAllocHelper(uint type, uint count)
{
    auto self = Runtime::Instance;
    auto t = (Type)type;
    uint size;
    switch(t)
    {
        //case Type::ARRAY:
        //    break;
        case Type::CLASS:
            break;
        default:
            size = count * Runtime::Sizeof(t) + Runtime::ARRAY_METADATA_SIZE;
            auto ptr = self->memoryManager.Allocate(size); // ACHTUNG
            *ptr = (byte)t;
            *(uint*)(ptr+1) = count;
            return ptr;
    }
    return nullptr;
}

uint jitStringECompareHelper(const char *str1, const char *str2)
{
    return !strcmp(str1, str2);
}

uint jitStringNECompareHelper(const char *str1, const char *str2)
{
    return strcmp(str1, str2);
}

__uint64_t jitUI64DivisionHelper(__uint64_t p1, __uint64_t p2)
{
    return p1 / p2;
}

__int64_t jitI64DivisionHelper(__int64_t p1, __int64_t p2)
{
    return p1 / p2;
}

__uint64_t jitUI64ModHelper(__uint64_t p1, __uint64_t p2)
{
    return p1 % p2;
}

__int64_t jitI64ModHelper(__int64_t p1, __int64_t p2)
{
    return p1 % p2;
}

#endif
int jitCheckSetPriority(void *fun_ptr)
{
    if(((Function*)fun_ptr)->jit_code != nullptr)
        return 1;

    ///@todo: TODO: ADD SETTING PRIORITY

    return 0;
}

int jitIsCompiled(void *fun_ptr)
{
    return ((Function*)fun_ptr)->jit_code != nullptr;
}


/*
    NOP,                //0x0
    DUP,
    BAND,
    BOR,
    ADD,
    ADDF,
    SUB,
    SUBF,
    MUL,
    MULF,
    DIV,
    DIVF,
    REM,
    REMF,
    CONV_UI8,
    CONV_I16,          //15
    CONV_CHR,
    CONV_I32,
    CONV_UI32,
    CONV_I64,
    CONV_UI64,
    CONV_F,
* /
uint opNop(OpCode *op, vector<byte*> asjit)
{
    //asjit.push_back(0x90);
    return 1;
}
uint opDup(OpCode *op, vector<byte*> asjit)
{
    //asjit.push_back(0x58);
    return 1;
}
*/
