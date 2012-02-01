/*
 * Realmode Emulator Plugin
 * - By John Hodge (thePowersGang)
 *
 * This code is published under the FreeBSD licence
 * (See the file COPYING for details)
 *
 * ---
 * Core Emulator
 */
#define _RME_C_
#include <system.h>
#include <types.h>

#define outb outportb
#define outw outports
#define outl outportl
#define inb  inportb
#define inw  inports
#define inl  inportl

typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int8_t   Sint8;
typedef int16_t  Sint16;
typedef int32_t  Sint32;
typedef int32_t  intptr_t;

#include "rme.h"

// Settings
#define DEBUG	0	// Enable debug?
#define RME_DO_NULL_CHECK	1
#define ERR_OUTPUT	1	// Enable using printf on an error?
#define	printf	printf	// Formatted print function
#define	outB(state,port,val)	(outb(port,val),0)	// Write 1 byte to an IO Port
#define	outW(state,port,val)	(outw(port,val),0)	// Write 2 bytes to an IO Port
#define	outD(state,port,val)	(outl(port,val),0)	// Write 4 bytes to an IO Port
#define	inB(state,port,dst)	(*(dst)=inb((port)),0)	// Read 1 byte from an IO Port
#define	inW(state,port,dst)	(*(dst)=inw((port)),0)	// Read 2 bytes from an IO Port
#define	inD(state,port,dst)	(*(dst)=inl((port)),0)	// Read 4 bytes from an IO Port

// -- Per Compiler macros
#define	WARN_UNUSED_RET	__attribute__((warn_unused_result))

// === CONSTANTS ===
#define FLAG_DEFAULT	0x2
/**
 * \brief FLAGS Register Values
 * \{
 */
#define FLAG_CF	0x001	//!< Carry Flag
#define FLAG_PF	0x004	//!< Pairity Flag
#define FLAG_AF	0x010	//!< Adjust Flag
#define FLAG_ZF	0x040	//!< Zero Flag
#define FLAG_SF	0x080	//!< Sign Flag
#define FLAG_TF	0x100	//!< Trap Flag (for single stepping)
#define FLAG_IF	0x200	//!< Interrupt Flag
#define FLAG_DF	0x400	//!< Direction Flag
#define FLAG_OF	0x800	//!< Overflow Flag
/**
 * \}
 */

/**
 * \name Exception Handlers
 * \{
 */
#define	RME_Int_Expt_DivideError(State)	(RME_ERR_DIVERR)
/**
 * \}
 */

// --- Stack Primiatives ---
#define PUSH(v)	do{\
	ret=RME_Int_Write16(State,State->SS,State->SP.W-=2,(v));\
	if(ret)return ret;\
	}while(0)
#define POP(dst)	do{\
	uint16_t v;\
	ret=RME_Int_Read16(State,State->SS,State->SP.W,&v);\
	if(ret)return ret;\
	State->SP.W+=2;\
	(dst)=v;\
	}while(0)

// --- Debug Macro ---
#if DEBUG
# define DEBUG_S(...)	kprintf(__VA_ARGS__)
#else
# define DEBUG_S(...)
#endif
#if ERR_OUTPUT
# define ERROR_S(...)	kprintf(__VA_ARGS__)
#else
# define ERROR_S(...)
#endif

// === MACRO VOODOO ===
#define XCHG(a,b)	do{uint32_t t=(a);(a)=(b);(b)=(t);}while(0)
// --- Case Helpers
#define CASE4(b)	case(b):case((b)+1):case((b)+2):case((b)+3)
#define CASE8(b)	CASE4(b):CASE4((b)+4)
#define CASE16(b)	CASE4(b):CASE4((b)+4):CASE4((b)+8):CASE4((b)+12)
#define CASE4K(b,k)	case(b):case((b)+1*(k)):case((b)+2*(k)):case((b)+3*(k))
#define CASE8K(b,k)	CASE4K(b,k):CASE4K((b)+4*(k),k)
#define CASE16K(b,k)	CASE4K(b,k):CASE4K((b)+4*(k),k):CASE4K((b)+8*(k),k):CASE4K((b)+12*(k),k)
// --- Operation helpers
#define PAIRITY8(v)	((((v)>>7)&1)^(((v)>>6)&1)^(((v)>>5)&1)^(((v)>>4)&1)^(((v)>>3)&1)^(((v)>>2)&1)^(((v)>>1)&1)^((v)&1))
#define PAIRITY16(v)	(PAIRITY8(v) ^ PAIRITY8(v>>8))
#define SET_PF(State,v,w) do{\
	if(w==8)	State->Flags |= PAIRITY8(v) ? 0 : FLAG_PF;\
	else if(w==16)	State->Flags |= PAIRITY16(v) ? 0 : FLAG_PF;\
	else	State->Flags |= (PAIRITY16(v) ^ PAIRITY16((v)>>16)) ? 0 : FLAG_PF;\
	}while(0)
#define SET_COMM_FLAGS(State,v,w) do{\
	State->Flags &= ~(FLAG_ZF|FLAG_SF|FLAG_CF);\
	State->Flags |= ((v) == 0) ? FLAG_ZF : 0;\
	State->Flags |= ((v) >> ((w)-1)) ? FLAG_SF : 0;\
	SET_PF(State, (v), (w));\
	}while(0)

// --- OPERATIONS ---
// Logical TEST (Set flags according to AND)
#define RME_Int_DoTest(State, to, from, width)	do{\
	int v = (to) & (from);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,v,(width));\
	}while(0)
#define RME_Int_DoRol(State, to, from, width)	do{\
	(to) = ((to) << (from)) | ((to) >> ((width)-(from)));\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	State->Flags |= ((to) >> ((width)-1)) ? FLAG_CF : 0;\
	}while(0)
#define RME_Int_DoRor(State, to, from, width)	do{\
	(to) = ((to) >> (from)) | ((to) << ((width)-(from)));\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	State->Flags |= ((to) >> ((width)-1)) ? FLAG_CF : 0;\
	}while(0)
#define RME_Int_DoShl(State, to, from, width)	do{\
	(to) <<= (from);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	State->Flags |= ((to) >> ((width)-1)) ? FLAG_CF : 0;\
	}while(0)
#define RME_Int_DoShr(State, to, from, width)	do{\
	(to) >>= (from);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	State->Flags |= ((to) & 1) ? FLAG_CF : 0;\
	}while(0)

// --- Memory Helpers
/**
 * \brief Read an unsigned byte from the instruction stream
 * Reads 1 byte as an unsigned integer from CS:IP and increases IP by 1.
 */
#define READ_INSTR8(dst)	do{int r;uint8_t v;\
	r=RME_Int_Read8(State,State->CS,State->IP+State->Decoder.IPOffset,&v);\
	if(r)	return r;\
	State->Decoder.IPOffset++;\
	(dst) = v;\
	}while(0)
/**
 * \brief Read a signed byte from the instruction stream
 * Reads 1 byte as an signed integer from CS:IP and increases IP by 1.
 */
#define READ_INSTR8S(dst)	do{int r;int8_t v;\
	r=RME_Int_Read8(State,State->CS,State->IP+State->Decoder.IPOffset,(uint8_t*)&v);\
	if(r)	return r;\
	State->Decoder.IPOffset++;\
	(dst) = v;\
	}while(0)
/**
 * \brief Read a word from the instruction stream
 * Reads 2 bytes as an unsigned integer from CS:IP and increases IP by 2.
 */
#define READ_INSTR16(dst)	do{int r;uint16_t v;\
	r=RME_Int_Read16(State,State->CS,State->IP+State->Decoder.IPOffset,&v);\
	if(r)	return r;\
	State->Decoder.IPOffset+=2;\
	(dst) = v;\
	}while(0)
/**
 * \brief Read a word from the instruction stream
 * Reads 4 bytes as an unsigned integer from CS:IP and increases IP by 4.
 */
#define READ_INSTR32(dst)	do{int r;uint32_t v;\
	r=RME_Int_Read32(State,State->CS,State->IP+State->Decoder.IPOffset,&v);\
	if(r)	return r;\
	State->Decoder.IPOffset+=4;\
	(dst) = v;\
	}while(0)
/**
 * \brief Get a segment with overrides
 */
#define GET_SEGMENT(State,_def)	(Seg(State, (State->Decoder.OverrideSegment==-1?(_def):State->Decoder.OverrideSegment)))

// === PROTOTYPES ===
tRME_State	*RME_CreateState(void);
void	RME_DumpRegs(tRME_State *State);
 int	RME_CallInt(tRME_State *State, int Num);
 int	RME_Call(tRME_State *State);
static int	RME_Int_DoOpcode(tRME_State *State);

static inline WARN_UNUSED_RET int	RME_Int_GetPtr(tRME_State *State, uint16_t Seg, uint16_t Ofs, void **Ptr);
static inline WARN_UNUSED_RET int	RME_Int_Read8(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint8_t *Dst);
static inline WARN_UNUSED_RET int	RME_Int_Read16(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint16_t *Dst);
static inline WARN_UNUSED_RET int	RME_Int_Read32(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint32_t *Dst);
static inline WARN_UNUSED_RET int	RME_Int_Write8(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint8_t Val);
static inline WARN_UNUSED_RET int	RME_Int_Write16(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint16_t Val);
static inline WARN_UNUSED_RET int	RME_Int_Write32(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint32_t Val);

static inline WARN_UNUSED_RET int	RME_Int_DoArithOp8(int Num, tRME_State *State, uint8_t *Dest, uint8_t Src);
static inline WARN_UNUSED_RET int	RME_Int_DoArithOp16(int Num, tRME_State *State, uint16_t *Dest, uint16_t Src);
static inline WARN_UNUSED_RET int	RME_Int_DoArithOp32(int Num, tRME_State *State, uint32_t *Dest, uint32_t Src);

static WARN_UNUSED_RET int	RME_Int_ParseModRM(tRME_State *State, uint8_t **to, uint8_t **from) __attribute__((warn_unused_result));
static WARN_UNUSED_RET int	RME_Int_ParseModRMX(tRME_State *State, uint16_t **to, uint16_t **from) __attribute__((warn_unused_result));
static inline uint16_t	*Seg(tRME_State *State, int code);
static inline uint8_t	*RegB(tRME_State *State, int num);
static inline uint16_t	*RegW(tRME_State *State, int num);
static inline WARN_UNUSED_RET int	RME_Int_DoCondJMP(tRME_State *State, uint8_t type, uint16_t offset, const char *name);

// === GLOBALS ===
#if DEBUG
static const char *casArithOps[] = {"ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"};
static const char *casLogicOps[] = {"ROL", "ROR", "RCL", "RCR", "SHL", "SHR", "L6-", "L7-"};
#endif

// === CODE ===
/**
 * \brief Creates a blank RME State
 */
tRME_State *RME_CreateState(void)
{
	tRME_State	*state = calloc(sizeof(tRME_State), 1);

	if(state == NULL)	return NULL;

	// Initial Stack
	state->Flags = FLAG_DEFAULT;

	// Stub CS/IP
	state->CS = 0xF000;
	state->IP = 0xFFF0;

	return state;
}

/**
 * \brief Dump Realmode Registers
 */
void RME_DumpRegs(tRME_State *State)
{
	DEBUG_S("\n");
	#if USE_SIZE_OVERRIDES == 1
	DEBUG_S("EAX %x  ECX %x  EDX %x  EBX %x\n",
		State->AX.D, State->CX.D, State->DX.D, State->BX.D);
	DEBUG_S("ESP %x  EBP %x  ESI %x  EDI %x\n",
		State->SP.D, State->BP.D, State->SI.D, State->DI.D);
	#else
	DEBUG_S("AX %x  CX %x  DX %x  BX %x\n",
		State->AX.W, State->CX.W, State->DX.W, State->BX.W);
	DEBUG_S("SP %x  BP %x  SI %x  DI %x\n",
		State->SP.W, State->BP.W, State->SI.W, State->DI.W);
	#endif
	DEBUG_S("SS %x  DS %x  ES %x\n",
		State->SS, State->DS, State->ES);
	DEBUG_S("CS:IP = 0x%x:%x\n", State->CS, State->IP);
	DEBUG_S("Flags = %x\n", State->Flags);
}

/**
 * \brief Run Realmode interrupt
 */
int RME_CallInt(tRME_State *State, int Num)
{
	 int	ret;
	DEBUG_S("RM_Int: Calling Int 0x%x\n", Num);

	if(Num < 0 || Num > 0xFF) {
		ERROR_S("WARNING: %d is not a valid interrupt number", Num);
		return RME_ERR_INVAL;
	}

	ret = RME_Int_Read16(State, 0, Num*4, &State->IP);
	if(ret)	return ret;
	ret = RME_Int_Read16(State, 0, Num*4+2, &State->CS);
	if(ret)	return ret;

	PUSH(State->Flags);
	PUSH(RME_MAGIC_CS);
	PUSH(RME_MAGIC_IP);

	return RME_Call(State);
}

/**
 * \brief Call a realmode function (a jump to a magic location is used as the return)
 */
int RME_Call(tRME_State *State)
{
	 int	ret;
	for(;;)
	{
		#if DEBUG >= 2
		RME_DumpRegs(State);
		#endif
		if(State->IP == RME_MAGIC_IP && State->CS == RME_MAGIC_CS)
			return 0;
		ret = RME_Int_DoOpcode(State);
		if(ret)	return ret;
	}
}

/**
 * \brief Processes a single instruction
 */
int RME_Int_DoOpcode(tRME_State *State)
{
	uint16_t	pt2, pt1 = 0;	// Spare Words, used for values read from memory
	uint16_t	seg;	// Segment value
	#if USE_SIZE_OVERRIDES == 1
	uint32_t	dword;	// Spare Double Word
	#endif
	// Destination/Source pointers (word sized)
	union {
		uint32_t	*D;
		uint16_t	*W;
	}	to, from;
	// Destination/Source pointers (byte sized)
	 uint8_t	*toB, *fromB;
	 uint8_t	repType = 0;	// Repeat flag
	 uint8_t	opcode, byte2;	// Current opcode and second byte
	 int	ret;	// Return value from functions
	// Initial CPU State
	uint16_t	startIP, startCS;

	startIP = State->IP;
	startCS = State->CS;

	State->Decoder.OverrideSegment = -1;
	State->Decoder.bOverrideOperand = 0;
	State->Decoder.bOverrideAddress = 0;
	State->Decoder.IPOffset = 0;
	State->InstrNum ++;

	DEBUG_S("(%d) [0x%x] %x:%x ", State->InstrNum, State->CS*16+State->IP, State->CS, State->IP);

decode:
	READ_INSTR8( opcode );
	switch( opcode )
	{

	// Prefixes
	// - Segment Overrides
	case OVR_CS:
		DEBUG_S("<CS> ");
		State->Decoder.OverrideSegment = SREG_CS;
		goto decode;
	case OVR_SS:
		DEBUG_S("<SS> ");
		State->Decoder.OverrideSegment = SREG_SS;
		goto decode;
	case OVR_DS:
		DEBUG_S("<DS> ");
		State->Decoder.OverrideSegment = SREG_DS;
		goto decode;
	case OVR_ES:
		DEBUG_S("<ES> ");
		State->Decoder.OverrideSegment = SREG_ES;
		goto decode;
	
	#if USE_SIZE_OVERRIDES == 1
	case 0x66:	// Operand Size Override
		DEBUG_S("<OPER> ");
		State->Decoder.bOverrideOperand = 1;
		goto decode;
	case 0x67:	// Memory Size Override
		DEBUG_S("<ADDR> ");
		State->Decoder.bOverrideAddress = 1;
		return RME_ERR_UNDEFOPCODE;
	#elif USE_SIZE_OVERRIDES == 0
	case 0x66:	// Operand Size Override
	case 0x67:	// Memory Size Override
		goto decode;
	#endif
	case 0x69:
	case 0x6D:
		goto decode;

	// Repeat Prefix
	case REP:	DEBUG_S("REP ");
		repType = REP;
		goto decode;
	case REPNZ:	DEBUG_S("REPNZ ");
		repType = REPNZ;
		goto decode;

	case 0x37:
		DEBUG_S("AAA");
		break;

	// <op> MR - Memory from Register
	CASE8K(0x00, 0x8):
		DEBUG_S("%s (MR)", casArithOps[opcode >> 3]);
		ret = RME_Int_ParseModRM(State, &fromB, &toB);
		if(ret)	return ret;
		ret = RME_Int_DoArithOp8( opcode >> 3, State, toB, *fromB );
		if(ret)	return ret;
		break;
	// <op> MRX - Memory from Register (Word)
	CASE8K(0x01, 0x8):
		DEBUG_S("%s (MRX)", casArithOps[opcode >> 3]);
		ret = RME_Int_ParseModRMX(State, &from.W, &to.W);
		if(ret)	return ret;
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			ret = RME_Int_DoArithOp32( opcode >> 3, State, to.D, *from.D );
		else
		#endif
			ret = RME_Int_DoArithOp16( opcode >> 3, State, to.W, *from.W );
		if(ret)	return ret;
		break;

	// <op> RM - Register from Memory
	CASE8K(0x02, 0x8):
		DEBUG_S("%s (RM)", casArithOps[opcode >> 3]);
		ret = RME_Int_ParseModRM(State, &toB, &fromB);
		if(ret)	return ret;
		ret = RME_Int_DoArithOp8( opcode >> 3, State, toB, *fromB );
		if(ret)	return ret;
		break;
	// <op> RMX - Register from Memory (Word)
	CASE8K(0x03, 0x8):
		DEBUG_S("%s (RM)", casArithOps[opcode >> 3]);
		ret = RME_Int_ParseModRMX(State, &to.W, &from.W);
		if(ret)	return ret;
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			ret = RME_Int_DoArithOp32( opcode >> 3, State, to.D, *from.D );
		else
		#endif
			ret = RME_Int_DoArithOp16( opcode >> 3, State, to.W, *from.W );
		if(ret)	return ret;
		break;

	// <op> AI - Accumulator from Immedate
	CASE8K(0x04, 8):
		READ_INSTR8( pt2 );
		DEBUG_S("%s (AI) AL 0x%x", casArithOps[opcode >> 3], pt2);
		ret = RME_Int_DoArithOp8( opcode >> 3, State, &State->AX.B.L, pt2 );
		if(ret)	return ret;
		break;

	// <op> AIX - Accumulator from Immedate (Word)
	CASE8K(0x05, 8):
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand) {
			READ_INSTR32( dword );
			DEBUG_S("%s (AIX) EAX 0x%x", casArithOps[opcode >> 3], dword);
			ret = RME_Int_DoArithOp32( opcode >> 3, State, &State->AX.D, dword );
		} else {
		#endif
			READ_INSTR16( pt2 );
			DEBUG_S("%s (AIX) AX 0x%x", casArithOps[opcode >> 3], pt2);
			ret = RME_Int_DoArithOp16( opcode >> 3, State, &State->AX.W, pt2 );
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		if(ret)	return ret;
		break;

	// <op> RI - Register from Immediate
	case 0x80:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		DEBUG_S("%s (RI)", casArithOps[(byte2 >> 3) & 7]);
		ret = RME_Int_ParseModRM(State, NULL, &toB);
		if(ret)	return ret;
		READ_INSTR8( pt2 );
		DEBUG_S(" 0x%x", pt2);
		ret = RME_Int_DoArithOp8( (byte2 >> 3) & 7, State, toB, pt2 );
		if(ret)	return ret;
		break;
	// <op> RIX - Register from Immediate (Word)
	case 0x81:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		DEBUG_S("%s (RIX)", casArithOps[(byte2 >> 3) & 7]);
		ret = RME_Int_ParseModRMX(State, NULL, &to.W);	// Get Register Value
		if(ret)	return ret;
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand) {
			READ_INSTR32( dword );
			DEBUG_S(" 0x%x", dword);
			ret = RME_Int_DoArithOp32( (byte2 >> 3) & 7, State, to.D, dword );
		} else {
		#endif
			READ_INSTR16( pt2 );
			DEBUG_S(" 0x%x", pt2);
			ret = RME_Int_DoArithOp16( (byte2 >> 3) & 7, State, to.W, pt2 );
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		if(ret)	return ret;
		break;
	
	// 0x82 MAY be a valid instruction, with the same effect as 0x80 (<op> RI)
	// NOPE - 0x82 is a #UD
	
	// <op> RI8X - Register from Imm8 (Word)
	case 0x83:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		DEBUG_S("%s (RI8X)", casArithOps[(byte2 >> 3) & 7]);
		ret = RME_Int_ParseModRMX(State, NULL, &to.W);	//Get Register Value
		if(ret)	return ret;
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand) {
			READ_INSTR8S( dword );
			DEBUG_S(" 0x%x", dword);
			ret = RME_Int_DoArithOp32( (byte2 >> 3) & 7, State, to.D, dword );
		} else {
		#endif
			READ_INSTR8S( pt2 );
			DEBUG_S(" 0x%x", pt2);
			ret = RME_Int_DoArithOp16( (byte2 >> 3) & 7, State, to.W, pt2 );
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		if(ret)	return ret;
		break;

	// ==== Logic Functions (Shifts) ===
	#define RME_Int_DoLogicOp( num, State, to, from, width )	do{\
		switch( (num) ) {\
		case 1: RME_Int_DoRor(State, (to), (from), (width));	break;\
		case 4:	RME_Int_DoShl(State, (to), (from), (width));	break;\
		case 5:	RME_Int_DoShr(State, (to), (from), (width));	break;\
		default: /*ERROR_S(" - DoLogicOp Undef %d\n", (num));*/ return RME_ERR_UNDEFOPCODE;\
		}}while(0)
	// <op> RI8 - Register by Imm8
	case 0xC0:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		DEBUG_S("%s (RI8)", casLogicOps[(byte2 >> 3) & 7]);
		ret = RME_Int_ParseModRM(State, NULL, &toB);
		if(ret)	return ret;
		READ_INSTR8( pt2 );
		DEBUG_S(" 0x%x", pt2);
		RME_Int_DoLogicOp( (byte2 >> 3) & 7, State, *toB, pt2, 8 );
		break;
	// <op> RI8X - Register by Imm8 (Word)
	case 0xC1:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		DEBUG_S("%s (RI8X)", casLogicOps[(byte2 >> 3) & 7]);
		ret = RME_Int_ParseModRMX(State, NULL, &to.W);
		if(ret)	return ret;
		READ_INSTR8( pt2 );
		DEBUG_S(" 0x%x", pt2);
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			RME_Int_DoLogicOp( (byte2 >> 3) & 7, State, *to.D, pt2, 32 );
		else
		#endif
			RME_Int_DoLogicOp( (byte2 >> 3) & 7, State, *to.W, pt2, 16 );
		break;
	// <op> R1 - Register by 1
	case 0xD0:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		DEBUG_S("%s (R1)", casLogicOps[(byte2 >> 3) & 7]);
		ret = RME_Int_ParseModRM(State, NULL, &toB);
		if(ret)	return ret;
		DEBUG_S(" 1");
		RME_Int_DoLogicOp( (byte2 >> 3) & 7, State, *toB, 1, 8 );
		break;
	// <op> R1X - Register by 1 (Word)
	case 0xD1:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		DEBUG_S("%s (R1X)", casLogicOps[(byte2 >> 3) & 7]);
		ret = RME_Int_ParseModRMX(State, NULL, &to.W);
		if(ret)	return ret;
		DEBUG_S(" 1");
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			RME_Int_DoLogicOp( (byte2 >> 3) & 7, State, *to.D, 1, 32 );
		else
		#endif
			RME_Int_DoLogicOp( (byte2 >> 3) & 7, State, *to.W, 1, 16 );
		break;
	// <op> RCl - Register by CL
	case 0xD2:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		DEBUG_S("%s (RCl)", casLogicOps[(byte2 >> 3) & 7]);
		ret = RME_Int_ParseModRM(State, NULL, &toB);
		if(ret)	return ret;
		DEBUG_S(" CL");
		RME_Int_DoLogicOp( (byte2 >> 3) & 7, State, *toB, State->CX.B.L, 8 );
		break;
	// <op> RClX - Register by CL (Word)
	case 0xD3:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		DEBUG_S("%s (RClX)", casLogicOps[(byte2 >> 3) & 7]);
		ret = RME_Int_ParseModRMX(State, NULL, &to.W);
		if(ret)	return ret;
		DEBUG_S(" CL");
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			RME_Int_DoLogicOp( (byte2 >> 3) & 7, State, *to.D, State->CX.B.L, 32 );
		else
		#endif
			RME_Int_DoLogicOp( (byte2 >> 3) & 7, State, *to.W, State->CX.B.L, 16 );
		break;

	// ==== Misc ALU ====
	// <op> RI - Register by Immediate
	case 0xF6:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		switch( (byte2>>3) & 7 )
		{
		case 0:	// TEST r/m8, Imm8
			DEBUG_S("TEST (MI)");
			ret = RME_Int_ParseModRM(State, NULL, &toB);
			if(ret)	return ret;
			READ_INSTR8(pt2);
			DEBUG_S(" 0x%x", pt2);
			RME_Int_DoTest(State, *toB, pt2, 8);
			break;
		// Undefined Opcode
		case 1:	DEBUG_S("0xF6 /1 Undefined\n");	return RME_ERR_UNDEFOPCODE;
		// NOT r/m8
		case 2:	DEBUG_S("NOT (M)");
			ret = RME_Int_ParseModRM(State, NULL, &toB);
			if(ret)	return ret;
			*toB = ~*toB;
			break;
		// NEG r/m8
		case 3:	DEBUG_S("NEG (M)");
			ret = RME_Int_ParseModRM(State, NULL, &toB);
			if(ret)	return ret;
			*toB = -*toB;
			State->Flags &= ~FLAG_OF;
			State->Flags |= (*toB == 0) ? FLAG_CF : 0;
			SET_COMM_FLAGS(State, *toB, 8);
			break;
		case 4:	// MUL AX = AL * r/m8
			ERROR_S("MUL (MI) AL");
			ret = RME_Int_ParseModRM(State, NULL, &fromB);
			if(ret)	return ret;
			pt2 = (Uint16)State->AX.B.L * (*fromB);
			State->AX.W = pt2;
			// TODO: Flags
			break;
		case 5:	// IMUL AX = AL * r/m8 (signed)
			DEBUG_S("IMUL (MI) AL");
			ret = RME_Int_ParseModRM(State, NULL, &fromB);
			if(ret)	return ret;
			pt2 = (Sint16)(Sint8)State->AX.B.L * (Sint8)*fromB;
			State->AX.W = pt2;
			// TODO: Flags
			break;
		case 6:	// DIV AX, r/m8 (unsigned)
			DEBUG_S("DIV (MI) AX");
			ret = RME_Int_ParseModRM(State, NULL, &fromB);
			if(ret)	return ret;
			if(*fromB == 0)	return RME_Int_Expt_DivideError(State);
			pt2 = State->AX.W / *fromB;
			if(pt2 > 0xFF)	return RME_Int_Expt_DivideError(State);
			pt2 |= (State->AX.W - pt2 * (*fromB)) << 8;
			State->AX.W = pt2;
			break;
		case 7:	// IDIV AX, r/m8 (signed)
			ERROR_S("0xF6 /7 - IDIV AL, r/m8 unimplemented\n");
			return RME_ERR_UNDEFOPCODE;
		}
		break;
	// <op> RIX - Register by Immediate (Word)
	case 0xF7:
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		switch( (byte2>>3) & 7 )
		{
		case 0:	// TEST r/m16, Imm16
			DEBUG_S("TEST (RIX)");
			ret = RME_Int_ParseModRMX(State, NULL, &to.W);
			if(ret)	return ret;
			#if USE_SIZE_OVERRIDES == 1
			if(State->Decoder.bOverrideOperand)
			{
				READ_INSTR32(dword);
				DEBUG_S(" 0x%x", dword);
				RME_Int_DoTest(State, *to.D, dword, 32);
			} else {
			#endif
				READ_INSTR16(pt2);
				DEBUG_S(" 0x%x", pt2);
				RME_Int_DoTest(State, *to.W, pt2, 16);
			#if USE_SIZE_OVERRIDES == 1
			}
			#endif
			break;
		// Undefined Opcode
		case 1:
			DEBUG_S("0xF7 /1 Undefined\n");
			return RME_ERR_UNDEFOPCODE;
		// NOT r/m16
		case 2:	DEBUG_S("NOT (MX)");
			ret = RME_Int_ParseModRMX(State, NULL, &to.W);
			if(!ret)	return ret;
			#if USE_SIZE_OVERRIDES == 1
			if(State->Decoder.bOverrideOperand)
			{
				*to.D = ~*to.D;
			} else {
			#endif
				*to.W = ~*to.W;
			#if USE_SIZE_OVERRIDES == 1
			}
			#endif
			// TODO: Flags
			break;
		// NEG r/m16
		case 3:	DEBUG_S("NEG (MX)");
			ret = RME_Int_ParseModRMX(State, NULL, &to.W);
			if(!ret)	return ret;
			#if USE_SIZE_OVERRIDES == 1
			if(State->Decoder.bOverrideOperand)
			{
				*to.D = -*to.D;
				State->Flags &= ~FLAG_OF;
				State->Flags |= (*to.D == 0) ? FLAG_CF : 0;
				SET_COMM_FLAGS(State, *to.D, 32);
			}
			else {
			#endif
				*to.W = -*to.W;
				State->Flags &= ~FLAG_OF;
				State->Flags |= (*to.W == 0) ? FLAG_CF : 0;
				SET_COMM_FLAGS(State, *to.W, 16);
			#if USE_SIZE_OVERRIDES == 1
			}
			#endif
			break;
		case 4:	// MUL AX, r/m16
			{
			uint32_t	dword;
			DEBUG_S("MUL (RIX) AX");
			
			#if USE_SIZE_OVERRIDES == 1
			// TODO: Handle Size Overrides
			//#warning "Implement MUL (RIX) 32-bit"
			if(State->Decoder.bOverrideOperand)	return RME_ERR_UNDEFOPCODE;
			#endif
			
			ret = RME_Int_ParseModRMX(State, NULL, &from.W);
			if(ret)	return ret;
			#if DEBUG >= 2
			DEBUG_S(" (0x%x)", *from.W);
			#endif
			
			dword = State->AX.W * *from.W;
			if(dword == State->AX.W)
				State->Flags &= ~(FLAG_CF|FLAG_OF);
			else
				State->Flags |= FLAG_CF|FLAG_OF;
			// TODO: More flags?
			State->DX.W = dword >> 16;
			State->AX.W = dword & 0xFFFF;
			}
			break;
		case 5:	// IMUL AX, r/m16
			{
			uint32_t	dword;
			DEBUG_S("IMUL (RIX) AX");
			
			#if USE_SIZE_OVERRIDES == 1
			// TODO: Handle Size Overrides
			//#warning "Implement IMUL (RIX) 32-bit"
			if(State->Decoder.bOverrideOperand)	return RME_ERR_UNDEFOPCODE;
			#endif
			
			ret = RME_Int_ParseModRMX(State, NULL, &from.W);
			if(ret)	return ret;
			dword = (int16_t)State->AX.W * (int16_t)*from.W;
			DEBUG_S(" %x * %x = %x", State->AX.W, *from.W, dword);
			if((int32_t)dword == (int16_t)State->AX.W)
				State->Flags &= ~(FLAG_CF|FLAG_OF);
			else
				State->Flags |= FLAG_CF|FLAG_OF;
			// TODO: More flags?
			State->DX.W = dword >> 16;
			State->AX.W = dword & 0xFFFF;
			}
			break;
		case 6:	// DIV DX:AX, r/m16
			#if USE_SIZE_OVERRIDES == 1
			if(State->Decoder.bOverrideOperand)
				DEBUG_S("DIV (RIX) EDX:EAX");
			else
			#endif
				DEBUG_S("DIV (RIX) DX:AX");
			
			ret = RME_Int_ParseModRMX(State, NULL, &from.W);
			if(ret)	return ret;
			
			#if USE_SIZE_OVERRIDES == 1
			if(State->Decoder.bOverrideOperand) {
				uint64_t	qword, qword2;
				if( *from.D == 0 )	return RME_Int_Expt_DivideError(State);
				#if DEBUG >= 2
				DEBUG_S(" /= 0x%x", *from.D);
				#endif
				qword = ((uint64_t)State->DX.D << 32) | State->AX.D;
				//qword2 = qword / *from.D;
				if(qword2 > 0xFFFFFFFF)	return RME_Int_Expt_DivideError(State);
				State->AX.D = qword2;
				State->DX.D = qword - qword2 * (*from.D);
			}
			else
			#endif
			{
				uint32_t	dword, dword2;			
				if( *from.W == 0 )	return RME_Int_Expt_DivideError(State);
				#if DEBUG >= 2
				DEBUG_S(" /= 0x%x", *from.W);
				#endif
				dword = (State->DX.W << 16) | State->AX.W;
				dword2 = dword / *from.W;
				if(dword2 > 0xFFFF)	return RME_Int_Expt_DivideError(State);
				State->AX.W = dword2;
				State->DX.W = dword - dword2 * (*from.W);
				//#warning "Implement Flags for DIV (RIX)"
			}
			break;
		case 7:	// IDIV DX:AX, r/m16
			//#warning "TODO: IDIV RIX"
			ERROR_S("0xF7 /7 - IDIV DX:AX, r/m16 unimplemented\n");
			return RME_ERR_UNDEFOPCODE;
		default:
			ERROR_S("0xF7 /%x unknown\n", (byte2>>3) & 7);
			return RME_ERR_UNDEFOPCODE;
		}
		break;

	// ==== Unary ALU ===
	// <op> R
	case 0xFE:	// Register
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		switch( (byte2>>3) & 7 )
		{
		case 0:
			DEBUG_S("INC (R)");
			ret = RME_Int_ParseModRM(State, NULL, &toB);	//Get Register Value
			if(ret)	return ret;
			(*toB) ++;
			State->Flags &= ~(FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF);
			SET_COMM_FLAGS(State, *toB, 8);
			State->Flags |= (State->Flags & FLAG_ZF) ? FLAG_OF : 0;
			break;
		case 1:
			DEBUG_S("DEC (R)");
			ret = RME_Int_ParseModRM(State, NULL, &toB);	//Get Register Value
			if(ret)	return ret;
			(*toB) --;
			State->Flags &= ~(FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF);
			SET_COMM_FLAGS(State, *toB, 8);
			State->Flags |= (*toB == 0xFF) ? FLAG_OF : 0;
			break;
		default:
			DEBUG_S("0xFE /%x unknown", (byte2>>3) & 7);
			return RME_ERR_UNDEFOPCODE;
		}
		break;

	// <op> RX
	case 0xFF:	// Register (Word)
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		switch( (byte2>>3) & 7 )
		{
		// Increment
		case 0:
			DEBUG_S("INC (RX)");
			ret = RME_Int_ParseModRMX(State, NULL, &to.W);	//Get Register Value
			if(ret)	return ret;
			
			State->Flags &= ~(FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF);
			#if USE_SIZE_OVERRIDES == 1
			if(State->Decoder.bOverrideOperand) {
				(*to.D) ++;
				SET_COMM_FLAGS(State, *to.D, 32);
			} else {
			#endif
				(*to.W) ++;
				SET_COMM_FLAGS(State, *to.W, 16);
			#if USE_SIZE_OVERRIDES == 1
			}
			#endif
			State->Flags |= (State->Flags & FLAG_ZF) ? FLAG_OF : 0;
			break;
		// Decrement
		case 1:
			DEBUG_S("DEC (RX)");
			ret = RME_Int_ParseModRMX(State, NULL, &to.W);	//Get Register Value
			if(ret)	return ret;
			
			State->Flags &= ~(FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF);
			#if USE_SIZE_OVERRIDES == 1
			if(State->Decoder.bOverrideOperand) {
				(*to.D) --;
				SET_COMM_FLAGS(State, *to.D, 32);
				State->Flags |= (*to.D == 0xFFFFFFFF) ? FLAG_OF : 0;
			} else {
			#endif
				(*to.W) --;
				SET_COMM_FLAGS(State, *to.W, 16);
				State->Flags |= (*to.W == 0xFFFF) ? FLAG_OF : 0;
			#if USE_SIZE_OVERRIDES == 1
			}
			#endif
			break;
		case 2:
			DEBUG_S("CALL (RX) NEAR");
			ret = RME_Int_ParseModRMX(State, NULL, &to.W);	//Get Register Value
			if(ret)	return ret;
			PUSH( State->IP + State->Decoder.IPOffset );
			DEBUG_S(" (0x%x)", *to.W);
			State->IP = *to.W;
			goto ret;
		case 3:
			ERROR_S("CALL (MX) FAR --NI--\n");
			return RME_ERR_UNDEFOPCODE;
		case 4:
			DEBUG_S("JMP (RX) NEAR");
			ret = RME_Int_ParseModRMX(State, NULL, &to.W);	//Get Register Value
			if(ret)	return ret;
			DEBUG_S(" (0x%x)", *to.W);
			State->IP = *to.W;
			goto ret;
		case 5:
			ERROR_S("JMP (MX) FAR --NI--\n");
			return RME_ERR_UNDEFOPCODE;
		case 6:
			DEBUG_S("PUSH (RX)");
			ret = RME_Int_ParseModRMX(State, NULL, &to.W);	//Get Register Value
			if(ret)	return ret;
			PUSH( *to.W );
			break;
		case 7:
			ERROR_S("0xFF /7 - Undefined\n");
			return RME_ERR_UNDEFOPCODE;
		}
		break;

	// --- TEST ---
	case TEST_RM:	DEBUG_S("TEST (RM)");	//Test Register
		ret = RME_Int_ParseModRM(State, &toB, &fromB);
		if(ret)	return ret;
		RME_Int_DoTest(State, *toB, *fromB, 8);
		break;
	case TEST_RMX:	DEBUG_S("TEST (RMX)");	//Test Register Extended
		ret = RME_Int_ParseModRMX(State, &to.W, &from.W);
		if(ret)	return ret;
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			RME_Int_DoTest(State, *to.D, *from.D, 32);
		else
		#endif
			RME_Int_DoTest(State, *to.W, *from.W, 16);
		break;
	case TEST_AI:	DEBUG_S("TEST (AI)");
		READ_INSTR8( pt2 );
		RME_Int_DoTest(State, State->AX.B.L, pt2, 8);
		break;
	case TEST_AIX:	DEBUG_S("TEST (AIX)");
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand) {
			READ_INSTR32( dword );
			DEBUG_S(" EAX 0x%x", dword);
			RME_Int_DoTest(State, State->AX.D, dword, 32);
		} else {
		#endif
			READ_INSTR16( pt2 );
			DEBUG_S(" AX 0x%x", pt2);
			RME_Int_DoTest(State, State->AX.W, pt2, 16);
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		break;

	// Flag Control
	case CLC:	DEBUG_S("CLC");	State->Flags &= ~FLAG_CF;	break;
	case STC:	DEBUG_S("STC");	State->Flags |= FLAG_CF;	break;
	case CLI:	DEBUG_S("CLI");
		State->Flags &= ~FLAG_IF;
		//if(State->bReflectIF)	__asm__ __volatile__ ("cli");
		break;
	case STI:	DEBUG_S("STI");
		State->Flags |= FLAG_IF;
		//if(State->bReflectIF)	__asm__ __volatile__ ("sti");
		break;
	case CLD:	DEBUG_S("CLD");	State->Flags &= ~FLAG_DF;	break;
	case STD:	DEBUG_S("STD");	State->Flags |= FLAG_DF;	break;

	// INC Register
	CASE8(INC_A):
		DEBUG_S("INC");
		to.W = RegW(State, opcode&7);
		State->Flags &= ~(FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF);
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand) {
			(*to.D) ++;
			SET_COMM_FLAGS(State, *to.D, 32);
		} else {
		#endif
			(*to.W) ++;
			SET_COMM_FLAGS(State, *to.W, 16);
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		State->Flags |= (State->Flags & FLAG_ZF) ? FLAG_OF : 0;
		break;
	// DEC Register
	CASE8(DEC_A):
		DEBUG_S("DEC");
		to.W = RegW(State, opcode&7);
		State->Flags &= ~(FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF);
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand) {
			(*to.D) --;
			SET_COMM_FLAGS(State, *to.D, 32);
			State->Flags |= (*to.D == 0xFFFFFFFF) ? FLAG_OF : 0;
		} else {
		#endif
			(*to.W) --;
			SET_COMM_FLAGS(State, *to.W, 16);
			State->Flags |= (*to.W == 0xFFFF) ? FLAG_OF : 0;
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		break;

	// ==== Port IO ====
	// IN <port>, A
	case IN_AI:	// Imm8, AL
		READ_INSTR8( pt2 );
		DEBUG_S("IN (AI) 0x%x AL", pt2);
		ret = inB( State, pt2, &State->AX.B.L );
		if(ret)	return ret;
		break;
	case IN_AIX:	// Imm8, AX
		READ_INSTR8( pt2 );
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand ) {
			DEBUG_S("IN (AIX) 0x%x EAX", pt2);
			ret = inD( State, pt2, &State->AX.D );
		} else {
		#endif
			DEBUG_S("IN (AIX) 0x%x AX", pt2);
			ret = inW( State, pt2, &State->AX.W );
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		if(ret)	return ret;
		break;
	case IN_ADx:	// DX, AL
		DEBUG_S("IN (ADx) DX AL");
		ret = inB(State, State->DX.W, &State->AX.B.L);
		if(ret)	return ret;
		break;
	case IN_ADxX:	// DX, AX
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand ) {
			DEBUG_S("IN (ADxX) DX EAX");
			ret = inD(State, State->DX.W, &State->AX.D);
		} else {
		#endif
			DEBUG_S("IN (ADxX) DX AX");
			ret = inW(State, State->DX.W, &State->AX.W);
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		if(ret)	return ret;
		break;
	
	// OUT <port>, A
	case OUT_IA:	// Imm8, AL
		READ_INSTR8( pt2 );
		DEBUG_S("OUT (IA) 0x%x AL", pt2);
		ret = outB( State, pt2, State->AX.B.L );
		if(ret)	return ret;
		break;
	case OUT_IAX:	// Imm8, AX
		READ_INSTR8( pt2 );
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand ) {
			DEBUG_S("OUT (IAX) 0x%x EAX", pt2);
			ret = outD( State, pt2, State->AX.D );
		} else {
		#endif
			DEBUG_S("OUT (IAX) 0x%x AX", pt2);
			ret = outW( State, pt2, State->AX.W );
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		if(ret)	return ret;
		break;
	case OUT_DxA:	// DX, AL
		DEBUG_S("OUT (DxA) DX AL");
		ret = outB( State, State->DX.W, State->AX.B.L );
		if(ret)	return ret;
		break;
	case OUT_DxAX:	// DX, AX
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand ) {
			DEBUG_S("OUT (DxAX) DX EAX");
			ret = outD( State, State->DX.W, State->AX.D );
		} else {
		#endif
			DEBUG_S("OUT (DxAX) DX AX");
			ret = outW( State, State->DX.W, State->AX.W );
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		if(ret)	return ret;
		break;

	// ==== Software Interrupts ====
	case INT3:
		DEBUG_S("INT 3");
		// High-Level Emulation Call
		if( State->HLECallbacks[3] ) {
			State->HLECallbacks[3](State, 3);
			break;
		}
		// Full emulation then
		ret = RME_Int_Read16(State, 0, 3*4, &pt1);	// Offset
		if(ret)	return ret;
		ret = RME_Int_Read16(State, 0, 3*4+2, &pt2);	// Segment
		if(ret)	return ret;
		PUSH( State->Flags );
		PUSH( State->CS );
		PUSH( State->IP );
		State->IP = pt1;
		State->CS = pt2;
		goto ret;	// Don't change IP after the instruction
	case INT_I:
		READ_INSTR8( byte2 );
		DEBUG_S("INT 0x%x", byte2);
		// High-Level Emulation Call
		if( State->HLECallbacks[byte2] ) {
			State->HLECallbacks[byte2](State, byte2);
			break;
		}
		// Full emulation then
		ret = RME_Int_Read16(State, 0, (int)byte2*4, &pt1);	// Offset
		if(ret)	return ret;
		ret = RME_Int_Read16(State, 0, (int)byte2*4+2, &pt2);	// Segment
		if(ret)	return ret;
		
		if(pt1 == 0 && pt2 == 0) {
			ERROR_S(" Caught attempt to execute IVT pointing to 0000:0000");
			return RME_ERR_BADMEM;
		}
		
		PUSH( State->Flags );
		PUSH( State->CS );
		PUSH( State->IP );
		State->IP = pt1;
		State->CS = pt2;
		goto ret;	// Don't change IP after the instruction
	case IRET:	DEBUG_S("IRET");
		POP( State->IP );
		POP( State->CS );
		POP( State->Flags );
		goto ret;	// Don't change IP after the instruction

	// ==== MOV ====
	case MOV_MoA:	// Store AL at Memory Offset
		DEBUG_S("MOV (MoA)");
		seg = *GET_SEGMENT(State, SREG_DS);
		READ_INSTR16( pt2 );
		DEBUG_S(":0x%x AL", pt2);
		ret = RME_Int_Write8(State, seg, pt2, State->AX.W & 0xFF);
		if(ret)	return ret;
		break;
	case MOV_MoAX:	// Store AX at Memory Offset
		DEBUG_S("MOV (MoAX)");
		seg = *GET_SEGMENT(State, SREG_DS);
		READ_INSTR16( pt2 );
		DEBUG_S(":0x%x", pt2);
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand) {
			DEBUG_S(" EAX");
			ret = RME_Int_Write32(State, seg, pt2, State->AX.D);
		} else {
		#endif
			DEBUG_S(" AX");
			ret = RME_Int_Write16(State, seg, pt2, State->AX.W);
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		if(ret)	return ret;
		break;
	case MOV_AMo:	// Memory Offset to AL
		DEBUG_S("MOV (AMo) AL");
		seg = *GET_SEGMENT(State, SREG_DS);
		READ_INSTR16( pt2 );
		DEBUG_S(":0x%x", pt2);
		ret = RME_Int_Read8(State, seg, pt2, (uint8_t*)&State->AX.W);
		if(ret)	return ret;
		break;
	case MOV_AMoX:	// Memory Offset to AX
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			DEBUG_S("MOV (AMoX) EAX");
		else
		#endif
			DEBUG_S("MOV (AMoX) AX");
		seg = *GET_SEGMENT(State, SREG_DS);
		READ_INSTR16( pt2 );
		DEBUG_S(":0x%x", pt2);
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			ret = RME_Int_Read32(State, State->DS, pt2, &State->AX.D);
		else
		#endif
			ret = RME_Int_Read16(State, State->DS, pt2, &State->AX.W);
		if(ret)	return ret;
		break;
	case MOV_MI:	// Store Immediate at Memory
		DEBUG_S("MOV (MI)");
		ret = RME_Int_ParseModRM(State, NULL, &toB);
		if(ret)	return ret;
		READ_INSTR8( pt2 );
		DEBUG_S(" 0x%x", pt2);
		*toB = pt2;
		break;
	case MOV_MIX:	// Store Immediate at Memory
		DEBUG_S("MOV (RIX)");
		ret = RME_Int_ParseModRMX(State, NULL, &to.W);
		if(ret)	return ret;
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand) {
			READ_INSTR32( dword );
			DEBUG_S(" 0x%x", dword);
			*to.D = dword;
		} else {
		#endif
			READ_INSTR16( pt2 );
			DEBUG_S(" 0x%x", pt2);
			*to.W = pt2;
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		break;
	case MOV_RM:	DEBUG_S("MOV (RM)");
		ret = RME_Int_ParseModRM(State, &toB, &fromB);
		if(ret)	return ret;
		*toB = *fromB;
		break;
	case MOV_RMX:	DEBUG_S("MOV (RMX)");
		ret = RME_Int_ParseModRMX(State, &to.W, &from.W);
		if(ret)	return ret;
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			*to.D = *from.D;
		else
		#endif
			*to.W = *from.W;
		break;
	case MOV_MR:	DEBUG_S("MOV (RM) REV");
		ret = RME_Int_ParseModRM(State, &fromB, &toB);
		if(ret)	return ret;
		*toB = *fromB;
		break;
	case MOV_MRX:	DEBUG_S("MOV (RMX) REV");
		ret = RME_Int_ParseModRMX(State, &from.W, &to.W);
		if(ret)	return ret;
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			*to.D = *from.D;
		else
		#endif
			*to.W = *from.W;
		break;

	case MOV_RI_AL:	case MOV_RI_CL:
	case MOV_RI_DL:	case MOV_RI_BL:
	case MOV_RI_AH:	case MOV_RI_CH:
	case MOV_RI_DH:	case MOV_RI_BH:
		DEBUG_S("MOV (RI)");
		toB = RegB(State, opcode&7);
		READ_INSTR8( pt2 );
		DEBUG_S(" 0x%x", pt2);
		*toB = pt2;
		break;

	case MOV_RI_AX:	case MOV_RI_CX:
	case MOV_RI_DX:	case MOV_RI_BX:
	case MOV_RI_SP:	case MOV_RI_BP:
	case MOV_RI_SI:	case MOV_RI_DI:
		DEBUG_S("MOV (RIX)");
		to.W = RegW(State, opcode&7);
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand) {
			READ_INSTR16( dword );
			DEBUG_S(" 0x%x", dword);
			*to.D = dword;
		} else {
		#endif
			READ_INSTR16( pt2 );
			DEBUG_S(" 0x%x", pt2);
			*to.W = pt2;
		#if USE_SIZE_OVERRIDES == 1
		}
		#endif
		break;

	// Segment Registers
	case MOV_RS:
		DEBUG_S("MOV (RS)");
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		from.W = Seg(State, (byte2>>3)&7);
		ret = RME_Int_ParseModRMX(State, NULL, &to.W);
		if(ret)	return ret;
		*to.W = *from.W;
		break;

	case MOV_SR:
		DEBUG_S("MOV (SR)");
		READ_INSTR8( byte2 );	State->Decoder.IPOffset --;
		to.W = Seg(State, (byte2>>3)&7);
		ret = RME_Int_ParseModRMX(State, NULL, &from.W);
		if(ret)	return ret;
		*to.W = *from.W;
		break;


	// === JMP Family ===
	case JMP_S:	// Short Jump
		READ_INSTR8S( pt2 );
		DEBUG_S("JMP (S) .+0x%x", pt2);
		State->IP += pt2;
		//if(pt2 == 0xFFFE)	return RME_ERR_BUG;
		break;
	case JMP_N:	// Near Jump
		READ_INSTR16( pt2 );
		DEBUG_S("JMP (N) .+0x%x", pt2 );
		State->IP += pt2;
		//if(pt2 == 0xFFFE)	return RME_ERR_BUG;
		break;
	case JMP_F:	// Far Jump
		READ_INSTR16( pt1 );
		READ_INSTR16( pt2 );
		DEBUG_S("JMP FAR %x:%x", pt2, pt1);
		State->CS = pt2;	State->IP = pt1;
		goto ret;

	// === XCHG Family ===
	case XCHG_AA:	// NOP 0x90
		DEBUG_S("NOP");
		break;
	case XCHG_AC:
	case XCHG_AD:	case XCHG_AB:
	case XCHG_ASp:	case XCHG_ABp:
	case XCHG_ASi:	case XCHG_ADi:
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			DEBUG_S("XCHG EAX");
		else
		#endif
			DEBUG_S("XCHG AX");
		from.W = RegW(State, opcode&7);
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			XCHG(State->AX.D, *from.D);
		else
		#endif
			XCHG(State->AX.W, *from.W);
		break;

	case XCHG_RM:
		DEBUG_S("XCHG (RM)");
		ret = RME_Int_ParseModRMX(State, &to.W, &from.W);
		if(ret)	return ret;
		// Note: Check for BX-BX for a breakpoint?
		#if USE_SIZE_OVERRIDES == 1
		if(State->Decoder.bOverrideOperand)
			XCHG(*to.D, *from.D);
		else
		#endif
			XCHG(*to.W, *from.W);
		break;

	// PUSH Family
	case PUSHF:
		DEBUG_S("PUSHF");
		PUSH(State->Flags);
		break;
	case PUSHA:
		DEBUG_S("PUSHA");
		pt2 = State->SP.W;
		PUSH(State->AX.W);	PUSH(State->CX.W);
		PUSH(State->DX.W);	PUSH(State->BX.W);
		PUSH(pt2);	PUSH(State->BP.W);
		PUSH(State->SI.W);	PUSH(State->DI.W);
		break;
	case PUSH_AX:	DEBUG_S("PUSH AX");	PUSH(State->AX.W);	break;
	case PUSH_BX:	DEBUG_S("PUSH BX");	PUSH(State->BX.W);	break;
	case PUSH_CX:	DEBUG_S("PUSH CX");	PUSH(State->CX.W);	break;
	case PUSH_DX:	DEBUG_S("PUSH DX");	PUSH(State->DX.W);	break;
	case PUSH_SP:	DEBUG_S("PUSH Sp");	pt2 = State->SP.W; PUSH(pt2);	break;
	case PUSH_BP:	DEBUG_S("PUSH BP");	PUSH(State->BP.W);	break;
	case PUSH_SI:	DEBUG_S("PUSH SI");	PUSH(State->SI.W);	break;
	case PUSH_DI:	DEBUG_S("PUSH DI");	PUSH(State->DI.W);	break;
	case PUSH_ES:	DEBUG_S("PUSH ES");	PUSH(State->ES);	break;
	case PUSH_CS:	DEBUG_S("PUSH CS");	PUSH(State->CS);	break;
	case PUSH_SS:	DEBUG_S("PUSH SS");	PUSH(State->SS);	break;
	case PUSH_DS:	DEBUG_S("PUSH DS");	PUSH(State->DS);	break;
	case PUSH_I8:
		READ_INSTR8( pt2 );
		DEBUG_S("PUSH (I8) 0x%x", pt2);
		PUSH(pt2);
		break;
	case PUSH_I:
		READ_INSTR16( pt2 );
		DEBUG_S("PUSH (I) 0x%x", pt2);
		PUSH(pt2);
		break;

	//POP Family
	case POPF:
		DEBUG_S("POPF");
		POP(State->Flags);
		break;
	case POPA:
		DEBUG_S("POPA");
		POP(State->DI.W);	POP(State->SI.W);
		POP(State->BP.W);	State->SP.W += 2;
		POP(State->BX.W);	POP(State->DX.W);
		POP(State->CX.W);	POP(State->AX.W);
		break;
	case POP_AX:	DEBUG_S("POP AX");	POP(State->AX.W);	break;
	case POP_CX:	DEBUG_S("POP CX");	POP(State->CX.W);	break;
	case POP_DX:	DEBUG_S("POP DX");	POP(State->DX.W);	break;
	case POP_BX:	DEBUG_S("POP BX");	POP(State->BX.W);	break;
	// The POP macro is indirect, so no need to do anthing special
	case POP_SP:	DEBUG_S("POP SP");	POP(State->SP.W);	break;
	case POP_BP:	DEBUG_S("POP BP");	POP(State->BP.W);	break;
	case POP_SI:	DEBUG_S("POP SI");	POP(State->SI.W);	break;
	case POP_DI:	DEBUG_S("POP DI");	POP(State->DI.W);	break;
	case POP_ES:	DEBUG_S("POP ES");	POP(State->ES);	break;
	case POP_SS:	DEBUG_S("POP SS");	POP(State->SS);	break;
	case POP_DS:	DEBUG_S("POP DS");	POP(State->DS);	break;

	case POP_MX:
		DEBUG_S("POP (MX)");
		ret = RME_Int_ParseModRMX(State, NULL, &to.W);
		if(ret)	return ret;
		POP(*to.W);
		break;

	//  === CALL Family ===
	case CALL_N:
		READ_INSTR16( pt2 );
		DEBUG_S("CALL (N) .+0x%x", pt2);
		State->IP += State->Decoder.IPOffset;
		PUSH(State->IP);
		State->IP += pt2;
		goto ret;
	case CALL_F:
		READ_INSTR16( pt1 );
		READ_INSTR16( pt2 );
		DEBUG_S("CALL (F) %x:%x", pt2, pt1);
		PUSH(State->CS);
		PUSH(State->IP + State->Decoder.IPOffset);
		State->CS = pt2;
		State->IP = pt1;
		goto ret;
	case RET_N:
		DEBUG_S("RET (N)");
		POP(State->IP);
		goto ret;
	case RET_F:
		DEBUG_S("RET (F)");
		POP(State->IP);
		POP(State->CS);
		goto ret;

	// -- String Operations --
	// Move
	case MOVSB:	DEBUG_S("MOVS");
		DEBUG_S(" DS:[SI]");	// TODO: Address Overrides
		DEBUG_S(" ES:[DI]");
		if( repType == REP ) {
			DEBUG_S(" (0x%x times)", State->CX.W);
			if( State->CX.W == 0 ) {	repType = 0;	break;	}
		}
		do {
			uint8_t	tmp;
			ret = RME_Int_Read8(State, State->DS, State->SI.W, &tmp);
			if(ret)	return ret;
			ret = RME_Int_Write8(State, State->ES, State->DI.W, tmp);
			if(ret)	return ret;
			if(State->Flags & FLAG_DF) {
				State->SI.W --;
				State->DI.W --;
			}
			else {
				State->DI.W ++;
				State->SI.W ++;
			}
		} while(repType && --State->CX.W);
		repType = 0;
		break;
	case MOVSW:	DEBUG_S("MOVSW");
		DEBUG_S(" DS:[SI]");	// TODO: Address Overrides
		DEBUG_S(" ES:[DI]");
		if( repType == REP ) {
			DEBUG_S(" (0x%x times)", State->CX.W);
			if( State->CX.W == 0 ) {	repType = 0;	break;	}
		}
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand )
		{
			 int	step = 4;
			if(State->Flags & FLAG_DF)	step = -step;
			do {
				uint32_t	tmp;
				ret = RME_Int_Read32(State, State->DS, State->SI.W, &tmp);
				if(ret)	return ret;
				ret = RME_Int_Write32(State, State->ES, State->DI.W, tmp);
				if(ret)	return ret;
				State->DI.W += step;
				State->SI.W += step;
			} while(repType == REP && --State->CX.W);
		}
		else
		#endif
		{
			 int	step = 2;
			if(State->Flags & FLAG_DF)	step = -step;
			do {
				uint16_t	tmp;
				ret = RME_Int_Read16(State, State->DS, State->SI.W, &tmp);
				if(ret)	return ret;
				ret = RME_Int_Write16(State, State->ES, State->DI.W, tmp);
				if(ret)	return ret;
				State->DI.W += step;
				State->SI.W += step;
			} while(repType && --State->CX.W);
		}
		repType = 0;
		break;
	
	// Compare String
	case CMPSB:	DEBUG_S("CMPSB");
		DEBUG_S(" ES:[DI]");
		DEBUG_S(" DS:[SI]");	// TODO: Address Override
		if(repType)		DEBUG_S(" (limit 0x%x)", State->CX.W);
		// Check for initial CX of zero
		if(repType && State->CX.W == 0) { repType = 0; break; }
		// Do the operation
		do {
			uint8_t	byte1, byte2;
			 int	v;
			ret = RME_Int_Read8(State, State->DS, State->SI.W, &byte1);
			if(ret)	return ret;
			ret = RME_Int_Read8(State, State->ES, State->DI.W, &byte2);
			if(ret)	return ret;
			
			#if DEBUG >= 2
			DEBUG_S(" %x==%x", byte1, byte2);
			#endif
			
			// Increment
			if(State->Flags & FLAG_DF) {
				State->SI.W --;
				State->DI.W --;
			}
			else {
				State->SI.W ++;
				State->DI.W ++;
			}
			
			// Do Comparison
			v = byte1 - byte2;
			State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);
			SET_COMM_FLAGS(State, v, 8);
			State->Flags |= (v < 0) ? FLAG_OF|FLAG_CF : 0;
			
			if(repType == REP && !(State->Flags & FLAG_ZF) )
				break;
			if(repType == REPNZ && (State->Flags & FLAG_ZF) )
				break;
		} while(repType && --State->CX.W);
		repType = 0;
		break;
	case CMPSW:	DEBUG_S("CMPSW");
		ERROR_S(" TODO: Implement CMPSW");
		return RME_ERR_UNDEFOPCODE;
	
	// Store String
	case STOSB:	DEBUG_S("STOSB");
		DEBUG_S(" ES:[DI]");	// TODO: Address Override
		DEBUG_S(" AL");
		if( repType == REP ) {
			DEBUG_S(" (0x%x times)", State->CX.W);
			if( State->CX.W == 0 ) {	repType = 0;	break;	}
		}
		do {
			ret = RME_Int_Write8(State, State->ES, State->DI.W, State->AX.B.L);
			if(ret)	return ret;
			if(State->Flags & FLAG_DF)
				State->DI.W --;
			else
				State->DI.W ++;
		} while(repType == REP && --State->CX.W);
		repType = 0;
		break;
	case STOSW:	DEBUG_S("STOSW");
		DEBUG_S(" ES:[DI]");	// TODO: Address Override
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand )
			DEBUG_S(" EAX");
		else
		#endif
			DEBUG_S(" AX");
		
		if( repType == REP ) {
			DEBUG_S(" (0x%x times)", State->CX.W);
			if( State->CX.W == 0 ) {	repType = 0;	break;	}
		}
		
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand )
		{
			 int	step = 4;
			if(State->Flags & FLAG_DF)	step = -step;
			do {
				ret = RME_Int_Write32(State, State->ES, State->DI.W, State->AX.D);
				if(ret)	return ret;
				State->DI.W += step;
			} while(repType == REP && --State->CX.W);
		}
		else
		#endif
		{
			 int	step = 2;
			if(State->Flags & FLAG_DF)	step = -step;
			do {
				ret = RME_Int_Write16(State, State->ES, State->DI.W, State->AX.W);
				if(ret)	return ret;
				State->DI.W += step;
			} while(repType == REP && --State->CX.W);
		}
		repType = 0;
		break;
	
	// Load String
	case LODSB:	DEBUG_S("LODS AL");
		DEBUG_S(" DS:[SI]");	// TODO: Address Overrides
		if( repType == REP ) {
			DEBUG_S(" (0x%x times)", State->CX.W);
			if( State->CX.W == 0 ) {	repType = 0;	break;	}
		}
		do {
			ret = RME_Int_Read8(State, State->DS, State->SI.W, &State->AX.B.L);
			if(ret)	return ret;
			if(State->Flags & FLAG_DF)
				State->SI.W --;
			else
				State->SI.W ++;
		} while(repType == REP && --State->CX.W);
		repType = 0;
		break;
	case LODSW:	DEBUG_S("LODS");
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand )
			DEBUG_S(" EAX");
		else
		#endif
			DEBUG_S(" AX");
		DEBUG_S(" DS:[SI]");	// TODO: Address Overrides
		
		if( repType == REP ) {
			DEBUG_S(" (0x%x times)", State->CX.W);
			if( State->CX.W == 0 ) {	repType = 0;	break;	}
		}
		
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand )
		{
			 int	step = 4;
			if(State->Flags & FLAG_DF)	step = -step;
			do {
				ret = RME_Int_Read32(State, State->DS, State->SI.W, &State->AX.D);
				if(ret)	return ret;
				State->SI.W += step;
			} while(repType == REP && --State->CX.W);
		}
		else
		#endif
		{
			 int	step = 2;
			if(State->Flags & FLAG_DF)	step = -step;
			do {
				ret = RME_Int_Read16(State, State->DS, State->SI.W, &State->AX.W);
				if(ret)	return ret;
				State->SI.W += step;
			} while(repType == REP && --State->CX.W);
		}
		repType = 0;
		break;

	// === Misc ===
	// LES	- Load ES:r(16,32) with m16:m(16,32)
	case LES:
		DEBUG_S("LES");
		ret = RME_Int_ParseModRMX(State, &to.W, &from.W);
		if(ret)	return ret;
		State->ES = *from.W;
		from.W = (void*)((intptr_t)from.W + 2);
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand )
			*to.D = *from.D;
		else
		#endif
			*to.W = *from.W;
		break;
	// LDS	- Load DS:r(16,32) with m16:m(16,32)
	case LDS:
		DEBUG_S("LDS");
		ret = RME_Int_ParseModRMX(State, &to.W, &from.W);
		if(ret)	return ret;
		State->DS = *from.W;
		from.W = (void*)((intptr_t)from.W + 2);
		#if USE_SIZE_OVERRIDES == 1
		if( State->Decoder.bOverrideOperand )
			*to.D = *from.D;
		else
		#endif
			*to.W = *from.W;
		break;
	
	// Load effective address
	case LEA:
		DEBUG_S("LEA");
		// LEA parses the address itself, because it needs the
		// emulated address, not the true address.
		// Hence, it cannot use RME_Int_ParseModRM/W
		READ_INSTR8(byte2);
		switch(byte2 >> 6)
		{
		case 0:	// No Offset
			pt1 = 0;
			break;
		case 1:	// 8-bit signed
			READ_INSTR8S(pt1);
			break;
		case 2:	// 16-bit
			READ_INSTR16(pt1);
			break;
		case 3:	// Register -- ERROR!!!
			return RME_ERR_UNDEFOPCODE;
		default:
			return RME_ERR_BUG;
		}

		to.W = RegW( State, (byte2>>3)&7 );

		switch(byte2 & 7)
		{
		case 0:
			DEBUG_S(" DS:[BX+SI+0x%x]", pt1);
			pt1 += State->BX.W + State->SI.W;
			break;
		case 1:
			DEBUG_S(" DS:[BX+DI+0x%x]", pt1);
			pt1 += State->BX.W + State->DI.W;
			break;
		case 2:
			DEBUG_S(" SS:[BP+SI+0x%x]", pt1);
			pt1 += State->BP.W + State->SI.W;
			break;
		case 3:
			DEBUG_S(" SS:[BP+DI+0x%x]", pt1);
			pt1 += State->BP.W + State->DI.W;
			break;
		case 4:
			DEBUG_S(" DS:[SI+0x%x]", pt1);
			pt1 += State->SI.W;
			break;
		case 5:
			DEBUG_S(" DS:[DI+0x%x]", pt1);
			pt1 += State->DI.W;
			break;
		case 6:
			if( (byte2 >> 6) == 0 ) {
				READ_INSTR16(pt1);
				DEBUG_S(" DS:[0x%x]", pt1);
			} else {
				DEBUG_S(" SS:[BP+0x%x]", pt1);
				pt1 += State->BP.W;
			}
			break;
		case 7:
			DEBUG_S(" DS:[BX+0x%x]", pt1);
			pt1 += State->BX.W;
			break;
		}
		*to.W = pt1;
		break;

	// -- Loops --
	case LOOP:	DEBUG_S("LOOP ");
		READ_INSTR8S( pt2 );
		DEBUG_S(".+0x%x", pt2);
		State->CX.W --;
		if(State->CX.W != 0)
			State->IP += pt2;
		break;
	case LOOPNZ:	DEBUG_S("LOOPNZ ");
		READ_INSTR8S( pt2 );
		DEBUG_S(".+0x%x", pt2);
		State->CX.W --;
		if(State->CX.W != 0 && !(State->Flags & FLAG_ZF))
			State->IP += pt2;
		break;
	case LOOPZ:	DEBUG_S("LOOPZ ");
		READ_INSTR8S( pt2 );
		DEBUG_S(".+0x%x", pt2);
		State->CX.W --;
		if(State->CX.W != 0 && State->Flags & FLAG_ZF)
			State->IP += pt2;
		break;

	// Short Jumps
	CASE16(0x70):
		READ_INSTR8S( pt2 );
		ret = RME_Int_DoCondJMP(State, opcode & 0xF, pt2, "(S)");
		if(ret)	return ret;
		break;


	// -- Two Byte Opcodes --
	case 0x0F:
		READ_INSTR8( byte2 );

		switch(byte2)
		{
		//--- Near Jump --- (1000cccc)
		CASE16(0x80):
			READ_INSTR16( pt2 );
			ret = RME_Int_DoCondJMP(State, byte2&0xF, pt2, "(N)");
			if(ret)	return ret;
			break;

		default:
			ERROR_S("0x0F 0x%x unknown\n", byte2);
			return RME_ERR_UNDEFOPCODE;
		}
		break;

	default:
		ERROR_S("Unknown Opcode 0x%x at 0x%x\n", opcode, State->IP);
		return RME_ERR_UNDEFOPCODE;
	}

	// repType is cleared if it is used, so if it's not used, it's invalid
	if(repType)
	{
		DEBUG_S("Prefix 0x%x used with wrong opcode 0x%x\n", repType, opcode);
		return RME_ERR_UNDEFOPCODE;
	}

	State->IP += State->Decoder.IPOffset;

ret:
	#if DEBUG
	{
		uint16_t	i = startIP;
		uint8_t	byte;
		 int	j = State->Decoder.IPOffset;

		DEBUG_S("\t;");
		while(i < 0x10000 && j--) {
			ret = RME_Int_Read8(State, startCS, i, &byte);
			DEBUG_S(" %x", byte);
			i ++;
		}
		if(j > 0)
		{
			while(j--) {
				ret = RME_Int_Read8(State, startCS, i, &byte);
				DEBUG_S(" %x", byte);
				i ++;
			}
		}
	}
	#endif
	DEBUG_S("\n");
	return 0;
}

/**
 * \brief Convert an eumulated Segment:Offset address into a host pointer
 * \param State	Emulator State
 * \param Seg	Segment
 * \param Offset	Offset
 * \param Ptr	Pointer to a void* where the final pointer will be stored
 */
static inline int RME_Int_GetPtr(tRME_State *State, uint16_t Seg, uint16_t Ofs, void* *Ptr)
{
	uint32_t	addr = Seg * 16 + Ofs;
	#if RME_DO_NULL_CHECK
	# if RME_ALLOW_ZERO_TO_BE_NULL
	if(addr/RME_BLOCK_SIZE && State->Memory[addr/RME_BLOCK_SIZE] == NULL)
	# else
	if(State->Memory[addr/RME_BLOCK_SIZE] == NULL)
	# endif
		return RME_ERR_BADMEM;
	#endif
	*Ptr = &State->Memory[addr/RME_BLOCK_SIZE][addr%RME_BLOCK_SIZE];
	return 0;
}

static inline int RME_Int_Read8(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint8_t *Dst)
{
	void	*ptr;
	 int	ret;
	ret = RME_Int_GetPtr(State, Seg, Ofs, (void**)&ptr);
	if(ret)	return ret;
	*Dst = *(uint8_t*)ptr;
	return 0;
}

static inline int RME_Int_Read16(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint16_t *Dst)
{
	void	*ptr;
	 int	ret;
	ret = RME_Int_GetPtr(State, Seg, Ofs, (void**)&ptr);
	if(ret)	return ret;
	*Dst = *(uint16_t*)ptr;
	return 0;
}

static inline int RME_Int_Read32(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint32_t *Dst)
{
	void	*ptr;
	 int	ret;
	ret = RME_Int_GetPtr(State, Seg, Ofs, (void**)&ptr);
	if(ret)	return ret;
	*Dst = *(uint32_t*)ptr;
	return 0;
}

static inline int RME_Int_Write8(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint8_t Val)
{
	void	*ptr;
	 int	ret;
	ret = RME_Int_GetPtr(State, Seg, Ofs, (void**)&ptr);
	if(ret)	return ret;
	*(uint8_t*)ptr = Val;
	return 0;
}

static inline int RME_Int_Write16(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint16_t Val)
{
	void	*ptr;
	 int	ret;
	ret = RME_Int_GetPtr(State, Seg, Ofs, (void**)&ptr);
	if(ret)	return ret;
	*(uint16_t*)ptr = Val;
	return 0;
}

static inline int RME_Int_Write32(tRME_State *State, uint16_t Seg, uint16_t Ofs, uint32_t Val)
{
	void	*ptr;
	 int	ret;
	ret = RME_Int_GetPtr(State, Seg, Ofs, (void**)&ptr);
	if(ret)	return ret;
	*(uint32_t*)ptr = Val;
	return 0;
}

/**
 * \brief 0 - Add
 * \todo Set AF
 */
#define RME_Int_DoAdd(State, to, from, width)	do{\
	(to) += (from);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	State->Flags |= ((to) < (from)) ? FLAG_OF|FLAG_CF : 0;\
	}while(0)
/**
 * \brief 1 - Bitwise OR
 */
#define RME_Int_DoOr(State, to, from, width)	do{\
	(to) |= (from);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	}while(0)
/**
 * \brief 2 - Add with Carry
 */
#define RME_Int_DoAdc(State, to, from, width)	do{\
	(to) += (from) + ((State->Flags&FLAG_CF)?1:0);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	State->Flags |= ((to) < (from)) ? FLAG_OF|FLAG_CF : 0;\
	}while(0)
/**
 * \brief 3 - Subtract with borrow
 */
#define RME_Int_DoSbb(State, to, from, width)	do{\
	int v = (to) - (from) + ((State->Flags&FLAG_CF)?1:0);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	State->Flags |= ((to)<(from) || (from)==(uint32_t)((1<<((width)-1)-1)|(1<<((width)-1)))) ? FLAG_CF : 0;\
	State->Flags |= (((((to) ^ (from)) & ((to) ^ (v))) & (1<<((width)-1))) != 0) ? FLAG_OF : 0;\
	(to) = v;\
	}while(0)
// 4: Bitwise AND
#define RME_Int_DoAnd(State, to, from, width)	do{\
	(to) &= (from);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	}while(0)
// 5: Subtract
#define RME_Int_DoSub(State, to, from, width)	do{\
	int v = (to) - (from);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	State->Flags |= ((to)<(from)) ? FLAG_CF : 0;\
	State->Flags |= (((((to) ^ (from)) & ((to) ^ (v))) & (1<<((width)-1))) != 0) ? FLAG_OF : 0;\
	(to) = v;\
	}while(0)
// 6: Bitwise XOR
#define RME_Int_DoXor(State, to, from, width)	do{\
	(to) ^= (from);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,(to),(width));\
	}while(0)
// 7: Compare (Set flags according to SUB)
#define RME_Int_DoCmp(State, to, from, width)	do{\
	int v = (to)-(from);\
	State->Flags &= ~(FLAG_PF|FLAG_ZF|FLAG_SF|FLAG_OF|FLAG_CF);\
	SET_COMM_FLAGS(State,v,(width));\
	State->Flags |= ((to)<(from)) ? FLAG_CF : 0;\
	State->Flags |= (((((to) ^ (from)) & ((to) ^ (v))) & (1<<((width)-1))) != 0) ? FLAG_OF : 0;\
	}while(0)

/**
 * \brief Delegates an Arithmatic Operation to the required helper
 */
#define RME_Int_DoArithOp(num, State, to, from, width)	do{\
	switch( (num) ) {\
	case 0:	RME_Int_DoAdd(State, (to), (from), (width));	break;\
	case 1:	RME_Int_DoOr (State, (to), (from), (width));	break;\
	case 2:	RME_Int_DoAdc(State, (to), (from), (width));	break;\
	case 3:	RME_Int_DoSbb(State, (to), (from), (width));	break;\
	case 4:	RME_Int_DoAnd(State, (to), (from), (width));	break;\
	case 5:	RME_Int_DoSub(State, (to), (from), (width));	break;\
	case 6:	RME_Int_DoXor(State, (to), (from), (width));	break;\
	case 7:	RME_Int_DoCmp(State, (to), (from), (width));	break;\
	default: DEBUG_S(" - Undef DoArithOP %d\n", (num));	return RME_ERR_BUG;\
	}}while(0)
/**
 * \brief Do an arithmatic operation on an 8-bit integer
 */
static inline int RME_Int_DoArithOp8(int Num, tRME_State *State, uint8_t *Dest, uint8_t Src)
{
	RME_Int_DoArithOp(Num, State, *Dest, Src, 8);
	return 0;
}
/**
 * \brief Do an arithmatic operation on a 16-bit integer
 */
static inline int RME_Int_DoArithOp16(int Num, tRME_State *State, uint16_t *Dest, uint16_t Src)
{
	RME_Int_DoArithOp(Num, State, *Dest, Src, 16);
	return 0;
}
/**
 * \brief Do an arithmatic operation on a 32-bit integer
 */
static inline int RME_Int_DoArithOp32(int Num, tRME_State *State, uint32_t *Dest, uint32_t Src)
{
	RME_Int_DoArithOp(Num, State, *Dest, Src, 32);
	return 0;
}

/**
 * \brief Returns a pointer to the specified byte register
 * \param State	Emulator State
 * \param num	Register Number
 */
static inline uint8_t *RegB(tRME_State *State, int num)
{
	switch(num)
	{
	case 0:	DEBUG_S(" AL");	return &State->AX.B.L;
	case 1:	DEBUG_S(" CL");	return &State->CX.B.L;
	case 2:	DEBUG_S(" DL");	return &State->DX.B.L;
	case 3:	DEBUG_S(" BL");	return &State->BX.B.L;
	case 4:	DEBUG_S(" AH");	return &State->AX.B.H;
	case 5:	DEBUG_S(" CH");	return &State->CX.B.H;
	case 6:	DEBUG_S(" DH");	return &State->DX.B.H;
	case 7:	DEBUG_S(" BH");	return &State->BX.B.H;
	}
	return 0;
}

/**
 * \brief Returns a pointer to the specified word register
 * \param State	Emulator State
 * \param num	Register Number
 */
static inline uint16_t *RegW(tRME_State *State, int num)
{
	#if USE_SIZE_OVERRIDES == 1
	if(State->Decoder.bOverrideOperand) {
		switch(num)
		{
		case 0:	DEBUG_S(" EAX");	return &State->AX.W;
		case 1:	DEBUG_S(" ECX");	return &State->CX.W;
		case 2:	DEBUG_S(" EDX");	return &State->DX.W;
		case 3:	DEBUG_S(" EBX");	return &State->BX.W;
		case 4:	DEBUG_S(" ESP");	return &State->SP.W;
		case 5:	DEBUG_S(" EBP");	return &State->BP.W;
		case 6:	DEBUG_S(" ESI");	return &State->SI.W;
		case 7:	DEBUG_S(" EDI");	return &State->DI.W;
		}
	} else {
	#endif
		switch(num)
		{
		case 0:	DEBUG_S(" AX");	return &State->AX.W;
		case 1:	DEBUG_S(" CX");	return &State->CX.W;
		case 2:	DEBUG_S(" DX");	return &State->DX.W;
		case 3:	DEBUG_S(" BX");	return &State->BX.W;
		case 4:	DEBUG_S(" SP");	return &State->SP.W;
		case 5:	DEBUG_S(" BP");	return &State->BP.W;
		case 6:	DEBUG_S(" SI");	return &State->SI.W;
		case 7:	DEBUG_S(" DI");	return &State->DI.W;
		}
	#if USE_SIZE_OVERRIDES == 1
	}
	#endif
	return 0;
}

/**
 * \brief Returns a pointer to the specified segment register
 * \param State	Emulator State
 * \param code	Segment register Number (0 to 3)
 */
static inline uint16_t *Seg(tRME_State *State, int code)
{
	switch(code) {
	case 0:	DEBUG_S(" ES");	return &State->ES;
	case 1:	DEBUG_S(" CS");	return &State->CS;
	case 2:	DEBUG_S(" SS");	return &State->SS;
	case 3:	DEBUG_S(" DS");	return &State->DS;
	default:
		DEBUG_S("ERROR - Invalid value passed to Seg(). (%d is not a segment)", code);
	}
	return NULL;
}

/**
 * \brief Performs a memory addressing function
 * \param State	Emulator State
 * \param mmm	Function ID (mmm field from ModR/M byte)
 * \param disp	Displacement
 * \param ptr	Destination for final pointer
 */
static int DoFunc(tRME_State *State, int mmm, int16_t disp, void *ptr)
{
	uint32_t	addr;
	uint16_t	seg;

	switch(mmm){
	case 2:	case 3:	case 6:
		seg = SREG_SS;
		break;
	default:
		seg = SREG_DS;
		break;
	}

	if(State->Decoder.OverrideSegment != -1)
		seg = State->Decoder.OverrideSegment;

	seg = *Seg(State, seg);

	switch(mmm)
	{
	case -1:	// R/M == 6 when Mod == 0
		READ_INSTR16( disp );
		DEBUG_S(":[0x%x]", disp);
		addr = disp;
		break;

	case 0:
		DEBUG_S(":[BX+SI+0x%x]", disp);
		addr = State->BX.W + State->SI.W + disp;
		break;
	case 1:
		DEBUG_S(":[BX+DI+0x%x]", disp);
		addr = State->BX.W + State->DI.W + disp;
		break;
	case 2:
		DEBUG_S(":[BP+SI+0x%x]", disp);
		addr = State->BP.W + State->SI.W + disp;
		break;
	case 3:
		DEBUG_S(":[BP+DI+0x%x]", disp);
		addr = State->BP.W + State->DI.W + disp;
		break;
	case 4:
		DEBUG_S(":[SI+0x%x]", disp);
		addr = State->SI.W + disp;
		break;
	case 5:
		DEBUG_S(":[DI+0x%x]", disp);
		addr = State->DI.W + disp;
		break;
	case 6:
		DEBUG_S(":[BP+0x%x]", disp);
		addr = State->BP.W + disp;
		break;
	case 7:
		DEBUG_S(":[BX+0x%x]", disp);
		addr = State->BX.W + disp;
		break;
	default:
		return RME_ERR_BUG;
	}
	return RME_Int_GetPtr(State, seg, addr, ptr);
}

/**
 * \brief Parses the ModR/M byte as a 8-bit value
 * \param State	Emulator State
 * \param to	R field destination (ignored if NULL)
 * \param from	M field destination (ignored if NULL)
 */
int RME_Int_ParseModRM(tRME_State *State, uint8_t **to, uint8_t **from)
{
	uint8_t	d;
	uint16_t	ofs;
	 int	ret;

	READ_INSTR8(d);
	switch(d >> 6)
	{
	case 0:	//No Offset
		if(to) *to = RegB( State, (d>>3) & 7 );
		if(from) {
			if((d & 7) == 6)
				ret = DoFunc( State, -1, 0, from );
			else
				ret = DoFunc( State, d & 7, 0, from );
			if(ret)	return ret;
		}
		return 0;
	case 1:	//8 Bit
		if(to) *to = RegB( State, (d>>3) & 7 );
		if(from) {
			READ_INSTR8S( ofs );
			ret = DoFunc( State, d & 7, ofs, from);
			if(ret)	return ret;
		}
		return 0;
	case 2:	//16 Bit
		if(to) *to = RegB( State, (d>>3) & 7 );
		if(from) {
			READ_INSTR16( ofs );
			ret = DoFunc( State, d & 7, ofs, from );
			if(ret)	return ret;
		}
		return 0;
	case 3:	//Regs Only
		if(to) *to = RegB( State, (d>>3) & 7 );
		if(from) *from = RegB( State, d & 7 );
		return 0;
	}
	return 0;
}

/**
 * \brief Parses the ModR/M byte as a 16-bit value
 * \param State	Emulator State
 * \param to	R field destination (ignored if NULL)
 * \param from	M field destination (ignored if NULL)
 */
int RME_Int_ParseModRMX(tRME_State *State, uint16_t **to, uint16_t **from)
{
	uint8_t	d;
	uint16_t	ofs;
	 int	ret;

	READ_INSTR8(d);
	switch(d >> 6)
	{
	case 0:	//No Offset
		if(to)	*to = RegW( State, (d>>3) & 7 );
		if(from) {
			if( (d & 7) == 6 )
				ret = DoFunc( State, -1, 0, from );
			else
				ret = DoFunc( State, d & 7, 0, from );
			if(ret)	return ret;
		}
		return 0;
	case 1:	//8 Bit
		if(to) *to = RegW( State, (d>>3) & 7 );
		if(from) {
			READ_INSTR8S( ofs );
			ret = DoFunc( State, d & 7, ofs, from );
			if(ret)	return ret;

		}
		return 0;
	case 2:	//16 Bit
		if(to) *to = RegW( State, (d>>3) & 7 );
		if(from) {
			READ_INSTR16( ofs );
			ret = DoFunc( State, d & 7, ofs, from );
			if(ret)	return ret;
		}
		return 0;
	case 3:	//Regs Only
		if(to) *to = RegW( State, (d>>3) & 7 );
		if(from) *from = RegW( State, d & 7 );
		return 0;
	}
	return 0;
}

/**
 * \brief Does a conditional jump
 * \param State	Current emulator state
 * \param type	Jump Type (0-15)
 * \param offset	Offset to add to IP if jump succeeds
 * \param name	Jump class name (Short or Near)
 */
static inline int RME_Int_DoCondJMP(tRME_State *State, uint8_t type, uint16_t offset, const char *name)
{
	DEBUG_S("J");
	switch(type) {
	case 0x0:	DEBUG_S("O");	// Overflow
		if(State->Flags & FLAG_OF)	State->IP += offset;
		break;
	case 0x1:	DEBUG_S("NO");	// No Overflow
		if(!(State->Flags & FLAG_OF))	State->IP += offset;
		break;
	case 0x2:	DEBUG_S("C");	// Carry
		if(State->Flags & FLAG_CF)	State->IP += offset;
		break;
	case 0x3:	DEBUG_S("NC");	// No Carry
		if(!(State->Flags & FLAG_CF))	State->IP += offset;
		break;
	case 0x4:	DEBUG_S("Z");	// Equal
		if(State->Flags & FLAG_ZF)	State->IP += offset;
		break;
	case 0x5:	DEBUG_S("NZ");	// Not Equal
		if(!(State->Flags & FLAG_ZF))	State->IP += offset;
		break;
	case 0x6:	DEBUG_S("BE");	// Below or Equal
		if(State->Flags & FLAG_CF || State->Flags & FLAG_ZF)	State->IP += offset;
		break;
	case 0x7:	DEBUG_S("A");	// Above
		if( !(State->Flags & FLAG_CF) && !(State->Flags & FLAG_ZF))
			State->IP += offset;
		break;
	case 0x8:	DEBUG_S("S");	// Sign
		if( State->Flags & FLAG_SF )
			State->IP += offset;
		break;
	case 0x9:	DEBUG_S("NS");	// Not Sign
		if( !(State->Flags & FLAG_SF) )
			State->IP += offset;
		break;
	case 0xA:	DEBUG_S("PE");	// Pairity Even
		if( State->Flags & FLAG_PF )
			State->IP += offset;
		break;
	case 0xB:	DEBUG_S("PO");	// Pairity Odd
		if( !(State->Flags & FLAG_PF) )
			State->IP += offset;
		break;
	case 0xC:	DEBUG_S("L");	// Less
		if( !!(State->Flags & FLAG_SF) != !!(State->Flags & FLAG_OF) )
			State->IP += offset;
		break;
	case 0xD:	DEBUG_S("GE");	// Greater or Equal
		if( !!(State->Flags & FLAG_SF) == !!(State->Flags & FLAG_OF) )
			State->IP += offset;
		break;
	case 0xE:	DEBUG_S("LE");	// Less or Equal
		if( State->Flags & FLAG_ZF || !!(State->Flags & FLAG_SF) != !!(State->Flags & FLAG_OF) )
			State->IP += offset;
		break;
	case 0xF:	DEBUG_S("G");	// Greater
		if( !(State->Flags & FLAG_ZF) || !!(State->Flags & FLAG_SF) == !!(State->Flags & FLAG_OF) )
			State->IP += offset;
		break;

	default:
		DEBUG_S(" 0x%x", type);
		return RME_ERR_BUG;
	}
	DEBUG_S(" %s .+0x%x", name, offset);
	return 0;
}
