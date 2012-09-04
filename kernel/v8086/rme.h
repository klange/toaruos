/*
 * Realmode Emulator Plugin
 * - By John Hodge (thePowersGang)
 *
 * This code is published under the FreeBSD licence
 * (See the file COPYING for details)
 *
 * ---
 * Core Emulator Include
 */
#ifndef _RME_H_
#define _RME_H_

/**
 * \file rme.h
 * \brief Realmode Emulator Header
 * \author John Hodge (thePowersGang)
 *
 * \section using Using RME
 *
 */


/**
 * \brief Enable the use of size overrides
 * \note Disabling this will speed up emulation, but may cause undefined
 *       behavior with some BIOSes.
 * 
 * If set to -1, size overrides will cause a \#UD
 */
#define USE_SIZE_OVERRIDES	0

/**
 * \brief Size of a memory block
 * \note Feel free to edit this value, just make sure it stays a power
 *       of two.
 */
#define RME_BLOCK_SIZE	0x1000

/**
 * \brief Magic return Instruction Pointer
 */
#define RME_MAGIC_IP	0xFFFF
/**
 * \brief Magic return Code Segment
 */
#define RME_MAGIC_CS	0xFFFF

/**
 * \brief Error codes returned by ::RME_Call and ::RME_CallInt
 */
enum eRME_Errors
{
	RME_ERR_OK,	//!< Exited successfully
	RME_ERR_INVAL,	//!< Bad paramater passed to emulator
	RME_ERR_BADMEM,	//!< Emulator accessed invalid memory
	RME_ERR_UNDEFOPCODE,	//!< Undefined opcode
	RME_ERR_DIVERR,	//!< Divide error
	RME_ERR_BUG,	//!< Bug in the emulator
	
	RME_ERR_LAST	//!< Last Error
};

typedef union uGPR
{
	#if USE_SIZE_OVERRIDES == 1
	uint32_t	D;
	#endif
	uint16_t	W;
	struct {
		uint8_t	L;
		uint8_t	H;
	}	B;
}	tGPR;

/**
 * \brief Emulator state structure
 */
typedef struct sRME_State
{
	//! \brief General Purpose Registers
	//! \{
	tGPR	AX, CX, DX, BX, SP, BP, SI, DI;
	
	//! \}

	//! \brief Segment Registers
	//! \{
	uint16_t	SS;	//!< Stack Segment
	uint16_t	DS;	//!< Data Segment
	uint16_t	ES;	//!< Extra Segment
	//! \}

	//! \brief Program Counter
	//! \{
	uint16_t	CS;	//!< Code Segment
	uint16_t	IP;	//!< Instruction Pointer
	//! \}

	uint16_t	Flags;	//!< State Flags

	/**
	 * \brief Emulator's Memory
	 *
	 * The ~1MiB realmode address space is broken into blocks of
	 * ::RME_BLOCK_SIZE bytes that can each point to different areas
	 * of memory.
	 * NOTE: There is no write protection on these blocks
	 * \note A value of NULL in a block indicates that the block is invalid
	 * \note 0x110000 bytes is all that is accessable using the realmode
	 *       segmentation scheme (true max is 0xFFFF0+0xFFFF = 0x10FFEF)
	 */
	uint8_t	*Memory[0x110000/RME_BLOCK_SIZE];	// 1Mib,64KiB in 256 4 KiB blocks

	/**
	 * \brief High-Level Emulation Callback
	 * \param State	Emulation state at the interrupt
	 * \param IntNum	Interrupt number
	 * \return 1 if the call was handled, 0 if it should be emulated
	 * 
	 * Called on all in-emulator INT calls
	 */
	 int	(*HLECallbacks[256])(struct sRME_State *State, int IntNum);

	 int	InstrNum;	//!< Total executed instructions

	// --- Decoder State ---
	/**
	 * \brief Decoder State
	 * \note Should not be touched except by the emulator
	 */
	struct {
		 int	OverrideSegment;
		 int	bOverrideOperand;
		 int	bOverrideAddress;
		 int	IPOffset;
	}	Decoder;
}	tRME_State;


/**
 * \brief Creates a blank RME instance
 */
extern tRME_State	*RME_CreateState(void);

/**
 * \brief Calls an interrupt
 * \param State	State returned from ::RME_CreateState
 * \param Num	Interrupt number
 */
extern int	RME_CallInt(tRME_State *State, int Num);

/**
 * \brief Executes the emulator until RME_MAGIC_CS:RME_MAGIC_IP is reached
 * \param State	State returned from ::RME_CreateState
 */
extern int	RME_Call(tRME_State *State);

/**
 * \brief Prints contents of the state's registers to debug
 * \param State	State returned from ::RME_CreateState
 */
extern void RME_DumpRegs(tRME_State *State);

/*
 * Definitions specific to the internals of the emulator
 */
#ifdef _RME_C_

/**
 */
enum gpRegs
{
	AL, CL, DL, BL,
	AH, CH, DH, BH
};

enum sRegs
{
	SREG_ES,
	SREG_CS,
	SREG_SS,
	SREG_DS
};

#define OPCODE_RI(name, code)	name##_RI_AL = code|AL,	name##_RI_BL = code|BL,\
	name##_RI_CL = code|CL,	name##_RI_DL = code|DL,\
	name##_RI_AH = code|AH,	name##_RI_BH = code|BH,\
	name##_RI_CH = code|CH,	name##_RI_DH = code|DH,\
	name##_RI_AX = code|AL|8,	name##_RI_BX = code|BL|8,\
	name##_RI_CX = code|CL|8,	name##_RI_DX = code|DL|8,\
	name##_RI_SP = code|AH|8,	name##_RI_BP = code|CH|8,\
	name##_RI_SI = code|DH|8,	name##_RI_DI = code|BH|8

enum opcodes {
	ADD_MR = 0x00,	ADD_MRX = 0x01,
	ADD_RM = 0x02,	ADD_RMX = 0x03,
	ADD_AI = 0x04,	ADD_AIX = 0x05,

	OR_MR = 0x08,	OR_MRX = 0x09,
	OR_RM = 0x0A,	OR_RMX = 0x0B,
	OR_AI = 0x0C,	OR_AIX = 0x0D,

	AND_MR = 0x20,	AND_MRX = 0x21,
	AND_RM = 0x22,	AND_RMX = 0x23,
	AND_AI = 0x24,	AND_AIX = 0x25,

	SUB_MR = 0x28,	SUB_MRX = 0x29,
	SUB_RM = 0x2A,	SUB_RMX = 0x2B,
	SUB_AI = 0x2C,	SUB_AIX = 0x2D,

	XOR_MR = 0x30,	XOR_MRX = 0x31,
	XOR_RM = 0x32,	XOR_RMX = 0x33,
	XOR_AI = 0x34,	XOR_AIX = 0x35,

	CMP_MR = 0x38,	CMP_MRX = 0x39,
	CMP_RM = 0x3A,	CMP_RMX = 0x3B,
	CMP_AI = 0x3C,	CMP_AIX = 0x3D,

	DEC_A = 0x48|AL,	DEC_B = 0x48|BL,
	DEC_C = 0x48|CL,	DEC_D = 0x48|DL,
	DEC_Sp = 0x48|AH,	DEC_Bp = 0x48|CH,
	DEC_Si = 0x48|DH,	DEC_Di = 0x48|BH,

	INC_A = 0x40|AL,	INC_B = 0x40|BL,
	INC_C = 0x40|CL,	INC_D = 0x40|DL,
	INC_Sp = 0x40|AH,	INC_Bp = 0x40|CH,
	INC_Si = 0x40|DH,	INC_Di = 0x40|BH,

	DIV_R = 0xFA,	DIV_RX = 0xFB,
	DIV_M = 0xFA,	DIV_MX = 0xFB,


	INT3 = 0xCC,	INT_I = 0xCD,
	IRET = 0xCF,

	MOV_MoA = 0xA2,	MOV_MoAX = 0xA3,
	MOV_AMo = 0xA0,	MOV_AMoX = 0xA1,
	OPCODE_RI(MOV, 0xB0),
	MOV_MI = 0xC6,	MOV_MIX = 0xC7,
	MOV_MR = 0x88,	MOV_MRX = 0x89,
	MOV_RM = 0x8A,	MOV_RMX = 0x8B,
	MOV_RS = 0x8C,	MOV_SR = 0x8E,
	MOV_MS = 0x8C,	MOV_SM = 0x8E,

	MUL_R = 0xF6,	MUL_RX = 0xF7,
	MUL_M = 0xF6,	MUL_MX = 0xF7,

	NOP = 0x90,
	XCHG_AA = 0x90,	XCHG_AB = 0x90|BL,
	XCHG_AC = 0x90|CL,	XCHG_AD = 0x90|DL,
	XCHG_ASp = 0x90|AH,	XCHG_ABp = 0x90|CH,
	XCHG_ASi = 0x90|DH,	XCHG_ADi = 0x90|BH,
	XCHG_RM = 0x86,

	NOT_R = 0xF6,	NOT_RX = 0xF7,
	NOT_M = 0xF6,	NOT_MX = 0xF7,


	IN_AI = 0xE4,	IN_AIX = 0xE5,
	IN_ADx = 0xEC,	IN_ADxX = 0xED,
	OUT_IA = 0xE6,	OUT_IAX = 0xE7,
	OUT_DxA = 0xEE,	OUT_DxAX = 0xEF,

	POP_AX = 0x58|AL,	POP_BX = 0x58|BL,
	POP_CX = 0x58|CL,	POP_DX = 0x58|DL,
	POP_SP = 0x58|AH,	POP_BP = 0x58|CH,
	POP_SI = 0x58|DH,	POP_DI = 0x58|BH,
	POP_ES = 7|(SREG_ES<<3),
	POP_SS = 7|(SREG_SS<<3),	POP_DS = 7|(SREG_DS<<3),
	POP_MX = 0x8F,
	POPA = 0x61,	POPF = 0x9D,

	PUSH_AX = 0x50|AL,	PUSH_BX = 0x50|BL,
	PUSH_CX = 0x50|CL,	PUSH_DX = 0x50|DL,
	PUSH_SP = 0x50|AH,	PUSH_BP = 0x50|CH,
	PUSH_SI = 0x50|DH,	PUSH_DI = 0x50|BH,
	// PUSH_MX = 0xFF,	// - TODO: Check (maybe 0x87)
	PUSH_ES = 6|(SREG_ES<<3),	PUSH_CS = 6|(SREG_CS<<3),
	PUSH_SS = 6|(SREG_SS<<3),	PUSH_DS = 6|(SREG_DS<<3),
	PUSH_I8 = 0x6A,	PUSH_I = 0x68,
	PUSHA = 0x60,	PUSHF = 0x9C,

	RET_N = 0xC3,	RET_iN = 0xC2,
	RET_F = 0xCB,	RET_iF = 0xCA,

	CALL_MF = 0xFF,	CALL_MN = 0xFF,
	CALL_N = 0xE8,	CALL_F = 0x9A,
	CALL_R = 0xFF,

	JMP_MF = 0xFF,	JMP_N = 0xE9,
	JMP_S = 0xEB,	JMP_F = 0xEA,

	LES = 0xC4,
	LDS = 0xC5,
	LEA = 0x8D,

	CLC = 0xF8,	STC = 0xF9,
	CLI = 0xFA,	STI = 0xFB,
	CLD = 0xFC,	STD = 0xFD,

	TEST_RM = 0x84,	TEST_RMX = 0x85,
	TEST_AI = 0xA8,	TEST_AIX = 0xA9,

	MOVSB = 0xA4,	MOVSW = 0xA5,
	CMPSB = 0xA6,	CMPSW = 0xA7,
	STOSB = 0xAA,	STOSW = 0xAB,
	LODSB = 0xAC,	LODSW = 0xAD,
	SCASB = 0xAE,	SCASW = 0xAF,
	INSB = 0x6C,	INSW = 0x6D,
	OUTSB = 0x6E,	OUTSW = 0x6F,

	// --- Unimplementeds
	FPU_ARITH	= 0xDC,

	// --- Overrides
	OVR_ES = 0x26,
	OVR_CS = 0x2E,
	OVR_SS = 0x36,
	OVR_DS = 0x3E,

	REPNZ = 0xF2,	REP = 0xF3,
	LOOPNZ = 0xE0,	LOOPZ = 0xE1,
	LOOP = 0xE2
};

#endif

#endif
