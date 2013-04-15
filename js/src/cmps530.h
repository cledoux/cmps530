#include <iostream>

#include "jsprvtd.h"
#include "jsscript.h"
#include <map>
// For SrcNoteType
#include "frontend/BytecodeEmitter.h"

using namespace js;


typedef enum LoopType {
    JSLOOP_FOR,
    JSLOOP_WHILE,
    JSLOOP_FORIN
} LoopType;

struct Loop {
    LoopType looptype;
    jsbytecode *loophead;   // Loop body head
    jsbytecode *loopentry;  // condition check (cond), update already done 
    jsbytecode *update;     // condition update.
                                // for loop only
                                // while loop: unknown
                                // for-in loop: nextiter immediately after loophead
    jsbytecode *tail;
};

struct Instruction {
    unsigned ofs;
    unsigned line;
    unsigned pc;
    unsigned delta;
    SrcNoteType type;
    Loop loopdata; // If type is loop

    Instruction(unsigned p_ofs, unsigned p_line, unsigned p_pc, unsigned p_delta, 
                SrcNoteType p_type) : ofs(p_ofs), line(p_line), pc(p_pc), delta(p_delta),
                type(p_type) {}
};

typedef std::map<unsigned, Instruction*> instr_map;
typedef std::map<unsigned, Instruction*>::iterator instr_map_iter;

class ScriptNotes {
    JSContext *cx;
    JSScript *script;
    jsbytecode *original_pc;
    instr_map instruction_notes; // The disassembled notes
    //Intruction * instr;
                                                       // Indexed by pc
  public:
    ScriptNotes(JSContext *c, JSScript *s, jsbytecode *p) : cx(c), script(s), original_pc(p)
    {
        //"ofs", "line", "pc", "delta", "desc", "args");
        // offset variable == pc.  
        // beginning pointer - Current pointer == offset printout
        //        (sn)             (notes)
        unsigned offset = 0;
        unsigned colspan = 0;
        unsigned lineno = script->lineno;
        jssrcnote *notes = script->notes();
        unsigned switchTableEnd = 0, switchTableStart = 0;
        // Main decoding loop.  Each iteration corresponds to decoding a 
        // single note.
        for (jssrcnote *sn = notes; !SN_IS_TERMINATOR(sn); sn = SN_NEXT(sn)) {
            unsigned delta = SN_DELTA(sn);
            offset += delta;
            SrcNoteType type = (SrcNoteType) SN_TYPE(sn);
            const char *name = js_SrcNoteSpec[type].name;
            Instruction *instr;
            if (type == SRC_LABEL) {
                /* Check if the source note is for a switch case. */
                if (switchTableStart <= offset && offset < switchTableEnd)
                    name = "case";
            }
            //Sprint(sp, "%3u: %4u %5u [%4u] %-8s", unsigned(sn - notes), lineno, offset, delta, name);
            switch (type) {
              case SRC_COLSPAN:
                colspan = js_GetSrcNoteOffset(sn, 0);
                if (colspan >= SN_COLSPAN_DOMAIN / 2)
                    colspan -= SN_COLSPAN_DOMAIN;
                //Sprint(sp, "%d", colspan);
                break;
              case SRC_SETLINE:
                lineno = js_GetSrcNoteOffset(sn, 0);
                //Sprint(sp, " lineno %u", lineno);
                break;
              case SRC_NEWLINE:
                ++lineno;
                break;
              case SRC_FOR:
                instr = new Instruction( unsigned(sn - notes), lineno, offset, delta, type);
                Loop thisloop;
                thisloop.looptype = JSLOOP_FOR;
                thisloop.loophead = original_pc + offset + 6;
                thisloop.loopentry = original_pc + offset + unsigned(js_GetSrcNoteOffset(sn,0)) + 1; //cond + 1
                thisloop.update = original_pc + offset + unsigned(js_GetSrcNoteOffset(sn, 1));
                thisloop.tail = original_pc + offset + unsigned(js_GetSrcNoteOffset(sn, 2));
                instr->loopdata = thisloop;
                this->instruction_notes[offset] = instr;
                //Sprint(sp, " cond %u update %u tail %u",
                       //unsigned(js_GetSrcNoteOffset(sn, 0)),
                       //unsigned(js_GetSrcNoteOffset(sn, 1)),
                       //unsigned(js_GetSrcNoteOffset(sn, 2)));
                break;
              case SRC_IF_ELSE:
                //Sprint(sp, " else %u elseif %u",
                       //unsigned(js_GetSrcNoteOffset(sn, 0)),
                       //unsigned(js_GetSrcNoteOffset(sn, 1)));
                break;
              case SRC_COND:
              case SRC_WHILE:
              case SRC_PCBASE:
              case SRC_PCDELTA:
              case SRC_DECL:
              case SRC_BRACE:
                //Sprint(sp, " offset %u", unsigned(js_GetSrcNoteOffset(sn, 0)));
                break;
              case SRC_LABEL:
              case SRC_LABELBRACE:
              case SRC_BREAK2LABEL:
              case SRC_CONT2LABEL: {
                uint32_t index = js_GetSrcNoteOffset(sn, 0);
                JSAtom *atom = script->getAtom(index);
                //Sprint(sp, " atom %u (", index);
                size_t len = PutEscapedString(NULL, 0, atom, '\0');
                //if (char *buf = sp->reserve(len)) {
                //    PutEscapedString(buf, len + 1, atom, 0);
                //    buf[len] = 0;
                // }
                //Sprint(sp, ")");
                break;
              }
              case SRC_FUNCDEF: {
                uint32_t index = js_GetSrcNoteOffset(sn, 0);
                JSObject *obj = script->getObject(index);
                JSFunction *fun = obj->toFunction();
                JSString *str = JS_DecompileFunction(cx, fun, JS_DONT_PRETTY_PRINT);
                JSAutoByteString bytes;
                //if (!str || !bytes.encode(cx, str))
                //    ReportException(cx);
                //Sprint(sp, " function %u (%s)", index, !!bytes ? bytes.ptr() : "N/A");
                break;
              }
              case SRC_SWITCH: {
                JSOp op = JSOp(script->code[offset]);
                if (op == JSOP_GOTO)
                    break;
                //Sprint(sp, " length %u", unsigned(js_GetSrcNoteOffset(sn, 0)));
                unsigned caseOff = (unsigned) js_GetSrcNoteOffset(sn, 1);
                if (caseOff)
                    //Sprint(sp, " first case offset %u", caseOff);
                //UpdateSwitchTableBounds(cx, script, offset, &switchTableStart, &switchTableEnd);
                break;
              }
              case SRC_CATCH:
                delta = (unsigned) js_GetSrcNoteOffset(sn, 0);
                //if (delta) {
                    //if (script->main()[offset] == JSOP_LEAVEBLOCK)
                        //Sprint(sp, " stack depth %u", delta);
                    //else
                        //Sprint(sp, " guard delta %u", delta);
                //}
                break;
              default:;
            }
            //Sprint(sp, "\n");
        }
    }

    /* Return an iterator to the instruction at the pc */
    Instruction * getInstruction(unsigned pc) {
        if (instruction_notes.count(pc)) {
            return instruction_notes[pc];
        } else {
            return NULL;
        }
    }
    
    void print() {
        for (instr_map_iter it = instruction_notes.begin(); it != instruction_notes.end(); ++it){
            printf("%3u: %4u %5u [%4u] %-8s", it->second->ofs, it->second->line, 
                                                it->second->pc, it->second->delta, 
                                                js_SrcNoteSpec[it->second->type].name);
            if (it->second->type == SRC_FOR) {
                printf(" head %d entry/cond %d update %d tail %d\n",
                        int( it->second->loopdata.loophead - original_pc),
                        int( it->second->loopdata.loopentry - original_pc),
                        int( it->second->loopdata.update - original_pc),
                        int( it->second->loopdata.tail - original_pc));
            } else { printf("\n"); }
        }
    }
};
