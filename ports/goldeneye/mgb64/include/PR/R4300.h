#ifndef __R4300_H__
#define __R4300_H__

/*
 * Clean-room R4300 compatibility constants. These values describe the MIPS
 * R4300 register layout, exception vectors, cache/TLB fields, and address
 * conversion ABI used by N64 matching-target code.
 */

#include <PR/ultratypes.h>

#define KUBASE 0
#define KUSIZE 0x80000000
#define K0BASE 0x80000000
#define K0SIZE 0x20000000
#define K1BASE 0xA0000000
#define K1SIZE 0x20000000
#define K2BASE 0xC0000000
#define K2SIZE 0x20000000

#define SIZE_EXCVEC 0x80
#define UT_VEC K0BASE
#define R_VEC (K1BASE + 0x1fc00000)
#define XUT_VEC (K0BASE + 0x80)
#define ECC_VEC (K0BASE + 0x100)
#define E_VEC (K0BASE + 0x180)

#ifdef _LANGUAGE_ASSEMBLY

#ifdef NATIVE_PORT
#ifndef K0_TO_K1
#define K0_TO_K1(x) ((uintptr_t)(x))
#endif
#ifndef K1_TO_K0
#define K1_TO_K0(x) ((uintptr_t)(x))
#endif
#ifndef K0_TO_PHYS
#define K0_TO_PHYS(x) ((uintptr_t)(x))
#endif
#ifndef K1_TO_PHYS
#define K1_TO_PHYS(x) ((uintptr_t)(x))
#endif
#ifndef KDM_TO_PHYS
#define KDM_TO_PHYS(x) ((uintptr_t)(x))
#endif
#else
#define K0_TO_K1(x) ((x) | 0xA0000000)
#define K1_TO_K0(x) ((x) & 0x9FFFFFFF)
#define K0_TO_PHYS(x) ((x) & 0x1FFFFFFF)
#define K1_TO_PHYS(x) ((x) & 0x1FFFFFFF)
#define KDM_TO_PHYS(x) ((x) & 0x1FFFFFFF)
#endif
#ifndef PHYS_TO_K0
#define PHYS_TO_K0(x) ((x) | 0x80000000)
#endif
#ifndef PHYS_TO_K1
#define PHYS_TO_K1(x) ((x) | 0xA0000000)
#endif

#else

#ifdef NATIVE_PORT
#ifndef K0_TO_K1
#define K0_TO_K1(x) ((uintptr_t)(x))
#endif
#ifndef K1_TO_K0
#define K1_TO_K0(x) ((uintptr_t)(x))
#endif
#ifndef K0_TO_PHYS
#define K0_TO_PHYS(x) ((uintptr_t)(x))
#endif
#ifndef K1_TO_PHYS
#define K1_TO_PHYS(x) ((uintptr_t)(x))
#endif
#ifndef KDM_TO_PHYS
#define KDM_TO_PHYS(x) ((uintptr_t)(x))
#endif
#ifndef PHYS_TO_K0
#define PHYS_TO_K0(x) ((uintptr_t)(x))
#endif
#ifndef PHYS_TO_K1
#define PHYS_TO_K1(x) ((uintptr_t)(x))
#endif
#else
#define K0_TO_K1(x) ((u32)(x) | 0xA0000000)
#define K1_TO_K0(x) ((u32)(x) & 0x9FFFFFFF)
#define K0_TO_PHYS(x) ((u32)(x) & 0x1FFFFFFF)
#define K1_TO_PHYS(x) ((u32)(x) & 0x1FFFFFFF)
#define KDM_TO_PHYS(x) ((u32)(x) & 0x1FFFFFFF)
#define PHYS_TO_K0(x) ((u32)(x) | 0x80000000)
#define PHYS_TO_K1(x) ((u32)(x) | 0xA0000000)
#endif

#endif

#define IS_KSEG0(x) ((u32)(x) >= K0BASE && (u32)(x) < K1BASE)
#define IS_KSEG1(x) ((u32)(x) >= K1BASE && (u32)(x) < K2BASE)
#define IS_KSEGDM(x) ((u32)(x) >= K0BASE && (u32)(x) < K2BASE)
#define IS_KSEG2(x) ((u32)(x) >= K2BASE && (u32)(x) < KPTE_SHDUBASE)
#define IS_KPTESEG(x) ((u32)(x) >= KPTE_SHDUBASE)
#define IS_KUSEG(x) ((u32)(x) < K0BASE)

#define NTLBENTRIES 31

#define TLBHI_VPN2MASK 0xffffe000
#define TLBHI_VPN2SHIFT 13
#define TLBHI_PIDMASK 0xff
#define TLBHI_PIDSHIFT 0
#define TLBHI_NPID 255

#define TLBLO_PFNMASK 0x3fffffc0
#define TLBLO_PFNSHIFT 6
#define TLBLO_CACHMASK 0x38
#define TLBLO_CACHSHIFT 3
#define TLBLO_UNCACHED 0x10
#define TLBLO_NONCOHRNT 0x18
#define TLBLO_EXLWR 0x28
#define TLBLO_D 0x4
#define TLBLO_V 0x2
#define TLBLO_G 0x1

#define TLBINX_PROBE 0x80000000
#define TLBINX_INXMASK 0x3f
#define TLBINX_INXSHIFT 0

#define TLBRAND_RANDMASK 0x3f
#define TLBRAND_RANDSHIFT 0

#define TLBWIRED_WIREDMASK 0x3f

#define TLBCTXT_BASEMASK 0xff800000
#define TLBCTXT_BASESHIFT 23
#define TLBCTXT_BASEBITS 9
#define TLBCTXT_VPNMASK 0x7ffff0
#define TLBCTXT_VPNSHIFT 4

#define TLBPGMASK_4K 0x0
#define TLBPGMASK_16K 0x6000
#define TLBPGMASK_64K 0x1e000

#define SR_CUMASK 0xf0000000
#define SR_CU3 0x80000000
#define SR_CU2 0x40000000
#define SR_CU1 0x20000000
#define SR_CU0 0x10000000
#define SR_RP 0x08000000
#define SR_FR 0x04000000
#define SR_RE 0x02000000
#define SR_ITS 0x01000000
#define SR_BEV 0x00400000
#define SR_TS 0x00200000
#define SR_SR 0x00100000
#define SR_CH 0x00040000
#define SR_CE 0x00020000
#define SR_DE 0x00010000
#define SR_IMASK 0x0000ff00
#define SR_IMASK8 0x00000000
#define SR_IMASK7 0x00008000
#define SR_IMASK6 0x0000c000
#define SR_IMASK5 0x0000e000
#define SR_IMASK4 0x0000f000
#define SR_IMASK3 0x0000f800
#define SR_IMASK2 0x0000fc00
#define SR_IMASK1 0x0000fe00
#define SR_IMASK0 0x0000ff00
#define SR_IBIT8 0x00008000
#define SR_IBIT7 0x00004000
#define SR_IBIT6 0x00002000
#define SR_IBIT5 0x00001000
#define SR_IBIT4 0x00000800
#define SR_IBIT3 0x00000400
#define SR_IBIT2 0x00000200
#define SR_IBIT1 0x00000100
#define SR_IMASKSHIFT 8
#define SR_KX 0x00000080
#define SR_SX 0x00000040
#define SR_UX 0x00000020
#define SR_KSU_MASK 0x00000018
#define SR_KSU_USR 0x00000010
#define SR_KSU_SUP 0x00000008
#define SR_KSU_KER 0x00000000
#define SR_ERL 0x00000004
#define SR_EXL 0x00000002
#define SR_IE 0x00000001

#define CAUSE_BD 0x80000000
#define CAUSE_CEMASK 0x30000000
#define CAUSE_CESHIFT 28
#define CAUSE_IP8 0x00008000
#define CAUSE_IP7 0x00004000
#define CAUSE_IP6 0x00002000
#define CAUSE_IP5 0x00001000
#define CAUSE_IP4 0x00000800
#define CAUSE_IP3 0x00000400
#define CAUSE_SW2 0x00000200
#define CAUSE_SW1 0x00000100
#define CAUSE_IPMASK 0x0000FF00
#define CAUSE_IPSHIFT 8
#define CAUSE_EXCMASK 0x0000007C
#define CAUSE_EXCSHIFT 2

#define EXC_CODE(x) ((x) << 2)
#define EXC_INT EXC_CODE(0)
#define EXC_MOD EXC_CODE(1)
#define EXC_RMISS EXC_CODE(2)
#define EXC_WMISS EXC_CODE(3)
#define EXC_RADE EXC_CODE(4)
#define EXC_WADE EXC_CODE(5)
#define EXC_IBE EXC_CODE(6)
#define EXC_DBE EXC_CODE(7)
#define EXC_SYSCALL EXC_CODE(8)
#define EXC_BREAK EXC_CODE(9)
#define EXC_II EXC_CODE(10)
#define EXC_CPU EXC_CODE(11)
#define EXC_OV EXC_CODE(12)
#define EXC_TRAP EXC_CODE(13)
#define EXC_VCEI EXC_CODE(14)
#define EXC_FPE EXC_CODE(15)
#define EXC_WATCH EXC_CODE(23)
#define EXC_VCED EXC_CODE(31)

#define C0_IMPMASK 0xff00
#define C0_IMPSHIFT 8
#define C0_REVMASK 0xff
#define C0_MAJREVMASK 0xf0
#define C0_MAJREVSHIFT 4
#define C0_MINREVMASK 0xf

#define C0_READI 0x1
#define C0_WRITEI 0x2
#define C0_WRITER 0x6
#define C0_PROBE 0x8
#define C0_RFE 0x10

#define CACH_PI 0x0
#define CACH_PD 0x1
#define CACH_SI 0x2
#define CACH_SD 0x3
#define C_IINV 0x0
#define C_IWBINV 0x0
#define C_ILT 0x4
#define C_IST 0x8
#define C_CDX 0xc
#define C_HINV 0x10
#define C_HWBINV 0x14
#define C_FILL 0x14
#define C_HWB 0x18
#define C_HSV 0x1c

#define ICACHE_SIZE 0x4000
#define ICACHE_LINESIZE 32
#define ICACHE_LINEMASK (ICACHE_LINESIZE - 1)
#define DCACHE_SIZE 0x2000
#define DCACHE_LINESIZE 16
#define DCACHE_LINEMASK (DCACHE_LINESIZE - 1)

#define CONFIG_CM 0x80000000
#define CONFIG_EC 0x70000000
#define CONFIG_EC_1_1 0x6
#define CONFIG_EC_3_2 0x7
#define CONFIG_EC_2_1 0x0
#define CONFIG_EC_3_1 0x1
#define CONFIG_EP 0x0f000000
#define CONFIG_SB 0x00c00000
#define CONFIG_SS 0x00200000
#define CONFIG_SW 0x00100000
#define CONFIG_EW 0x000c0000
#define CONFIG_SC 0x00020000
#define CONFIG_SM 0x00010000
#define CONFIG_BE 0x00008000
#define CONFIG_EM 0x00004000
#define CONFIG_EB 0x00002000
#define CONFIG_IC 0x00000e00
#define CONFIG_DC 0x000001c0
#define CONFIG_IB 0x00000020
#define CONFIG_DB 0x00000010
#define CONFIG_CU 0x00000008
#define CONFIG_K0 0x00000007
#define CONFIG_UNCACHED 0x00000002
#define CONFIG_NONCOHRNT 0x00000003
#define CONFIG_COHRNT_EXLWR 0x00000005
#define CONFIG_SB_SHFT 22
#define CONFIG_IC_SHFT 9
#define CONFIG_DC_SHFT 6
#define CONFIG_BE_SHFT 15

#define SADDRMASK 0xFFFFE000
#define SVINDEXMASK 0x00000380
#define SSTATEMASK 0x00001c00
#define SINVALID 0x00000000
#define SCLEANEXCL 0x00001000
#define SDIRTYEXCL 0x00001400
#define SECC_MASK 0x0000007f
#define SADDR_SHIFT 4
#define PADDRMASK 0xFFFFFF00
#define PADDR_SHIFT 4
#define PSTATEMASK 0x00C0
#define PINVALID 0x0000
#define PCLEANEXCL 0x0080
#define PDIRTYEXCL 0x00C0
#define PPARITY_MASK 0x0001

#define CACHERR_ER 0x80000000
#define CACHERR_EC 0x40000000
#define CACHERR_ED 0x20000000
#define CACHERR_ET 0x10000000
#define CACHERR_ES 0x08000000
#define CACHERR_EE 0x04000000
#define CACHERR_EB 0x02000000
#define CACHERR_EI 0x01000000
#define CACHERR_SIDX_MASK 0x003ffff8
#define CACHERR_PIDX_MASK 0x00000007
#define CACHERR_PIDX_SHIFT 12

#define WATCHLO_WTRAP 0x00000001
#define WATCHLO_RTRAP 0x00000002
#define WATCHLO_ADDRMASK 0xfffffff8
#define WATCHLO_VALIDMASK 0xfffffffb
#define WATCHHI_VALIDMASK 0x0000000f

#ifdef _LANGUAGE_ASSEMBLY
#define C0_INX $0
#define C0_RAND $1
#define C0_ENTRYLO0 $2
#define C0_ENTRYLO1 $3
#define C0_CONTEXT $4
#define C0_PAGEMASK $5
#define C0_WIRED $6
#define C0_BADVADDR $8
#define C0_COUNT $9
#define C0_ENTRYHI $10
#define C0_SR $12
#define C0_CAUSE $13
#define C0_EPC $14
#define C0_PRID $15
#define C0_COMPARE $11
#define C0_CONFIG $16
#define C0_LLADDR $17
#define C0_WATCHLO $18
#define C0_WATCHHI $19
#define C0_ECC $26
#define C0_CACHE_ERR $27
#define C0_TAGLO $28
#define C0_TAGHI $29
#define C0_ERROR_EPC $30
#else
#define C0_INX 0
#define C0_RAND 1
#define C0_ENTRYLO0 2
#define C0_ENTRYLO1 3
#define C0_CONTEXT 4
#define C0_PAGEMASK 5
#define C0_WIRED 6
#define C0_BADVADDR 8
#define C0_COUNT 9
#define C0_ENTRYHI 10
#define C0_SR 12
#define C0_CAUSE 13
#define C0_EPC 14
#define C0_PRID 15
#define C0_COMPARE 11
#define C0_CONFIG 16
#define C0_LLADDR 17
#define C0_WATCHLO 18
#define C0_WATCHHI 19
#define C0_ECC 26
#define C0_CACHE_ERR 27
#define C0_TAGLO 28
#define C0_TAGHI 29
#define C0_ERROR_EPC 30
#endif

#define FPCSR_FS 0x01000000
#define FPCSR_C 0x00800000
#define FPCSR_CE 0x00020000
#define FPCSR_CV 0x00010000
#define FPCSR_CZ 0x00008000
#define FPCSR_CO 0x00004000
#define FPCSR_CU 0x00002000
#define FPCSR_CI 0x00001000
#define FPCSR_EV 0x00000800
#define FPCSR_EZ 0x00000400
#define FPCSR_EO 0x00000200
#define FPCSR_EU 0x00000100
#define FPCSR_EI 0x00000080
#define FPCSR_FV 0x00000040
#define FPCSR_FZ 0x00000020
#define FPCSR_FO 0x00000010
#define FPCSR_FU 0x00000008
#define FPCSR_FI 0x00000004
#define FPCSR_RM_MASK 0x00000003
#define FPCSR_RM_RN 0x00000000
#define FPCSR_RM_RZ 0x00000001
#define FPCSR_RM_RP 0x00000002
#define FPCSR_RM_RM 0x00000003

#endif
