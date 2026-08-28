#include <ultra64.h>
#include <bondgame.h>
#include <memp.h>
#include "lvl_text.h"
#include "ob.h"
#ifdef NATIVE_PORT
#include "assets/obseg/text/LtitleE.h"
#endif

// bss
//CODE.bss:8008C640
#ifdef NATIVE_PORT
static u8 *langGetNativeTitleFallback(s32 slotID)
{
    u32 text_id = (u16)slotID;

    if ((text_id >> 10) != LTITLE) {
        return NULL;
    }

    switch (text_id & 0x03FF) {
        case TITLE_STR_227_LF: return (u8 *)"\n";
        case TITLE_STR_228_THEACTORS: return (u8 *)"The Actors\n";
        case TITLE_STR_229_STARRING: return (u8 *)"Starring\n";
        case TITLE_STR_230_ALSOFEATURING: return (u8 *)"Also Featuring\n";
        case TITLE_STR_231_GUESTSTAR: return (u8 *)"Guest Star\n";
        case TITLE_STR_232_007: return (u8 *)"007\n";
        case TITLE_STR_233_JAMESBOND: return (u8 *)"James Bond";
        case TITLE_STR_234_NATALYASIMONOVA: return (u8 *)"Natalya Simonova\n";
        case TITLE_STR_235_006: return (u8 *)"006\n";
        case TITLE_STR_236_ALECTREVELYAN: return (u8 *)"Alec Trevelyan\n";
        case TITLE_STR_237_JANUSOPPERATIVE: return (u8 *)"Janus Operative\n";
        case TITLE_STR_238_XENIAONPTOPP: return (u8 *)"Xenia Onatopp\n";
        case TITLE_STR_239_GENERAL: return (u8 *)"General\n";
        case TITLE_STR_240_ARKADYOURUMOV: return (u8 *)"Arkady Ourumov\n";
        case TITLE_STR_241_BORISGRISHENKO: return (u8 *)"Boris Grishenko\n";
        case TITLE_STR_242_EXKGBAGENT: return (u8 *)"Ex KGB Agent\n";
        case TITLE_STR_243_VELENTINZUKOVSKY: return (u8 *)"Valentin Zukovsky\n";
        case TITLE_STR_244_DEFENSEMINISTER: return (u8 *)"Defense Minister\n";
        case TITLE_STR_245_DIMITRIMISHKIN: return (u8 *)"Dimitri Mishkin\n";
        case TITLE_STR_246_MAYDAY: return (u8 *)"Mayday\n";
        case TITLE_STR_247_JAWS: return (u8 *)"Jaws\n";
        case TITLE_STR_248_ODDJOB: return (u8 *)"Oddjob\n";
        case TITLE_STR_249_BERONSAMEDI: return (u8 *)"Baron Samedi\n";
        case TITLE_STR_250_JUNGLECOMMANDO: return (u8 *)"Jungle Commando\n";
        case TITLE_STR_251_STPETERSBURGGUARD: return (u8 *)"St. Petersburg Guard\n";
        case TITLE_STR_252_RUSSIANINFANTRY: return (u8 *)"Russian Infantry\n";
        case TITLE_STR_253_RUSSIANSOLDIER: return (u8 *)"Russian Soldier\n";
        case TITLE_STR_254_JANUSMARINE: return (u8 *)"Janus Marine\n";
        case TITLE_STR_255_JANUSSPECIALFORCES: return (u8 *)"Janus Special Forces\n";
        case TITLE_STR_256_RUSSIANCOMMANDANT: return (u8 *)"Russian Commandant\n";
        case TITLE_STR_257_NAVALOFFICER: return (u8 *)"Naval Officer\n";
        case TITLE_STR_258_SIBERIANGUARD: return (u8 *)"Siberian Guard\n";
        case TITLE_STR_259_ARCTICCOMMANDO: return (u8 *)"Arctic Commando\n";
        case TITLE_STR_260_SIBERIANSPECIALFORCES: return (u8 *)"Siberian Special Forces\n";
        case TITLE_STR_261_MOOKRAKERELITE: return (u8 *)"Moonraker Elite\n";
        case TITLE_STR_262_HELICOPTERPILOT: return (u8 *)"Helicopter Pilot\n";
        case TITLE_STR_263_SCIENTIST: return (u8 *)"Scientist\n";
        case TITLE_STR_264_CIVILIAN: return (u8 *)"Civilian\n";
        default: return NULL;
    }
}

uintptr_t g_LangBanks[45];
#else
s32 g_LangBanks[45];
#endif


//CODE.bss:8008C6F4
struct jpncharpixels* g_JpnCharCachePixels;
//CODE.bss:8008C6F8 canonically  jloaddata
struct  jpncacheitem *g_JpnCacheCacheItems;


#ifdef LANG_JP
s32 j_text_trigger = 1;
#else
/**
 * EU .data 80041150
*/
s32 j_text_trigger = 0;
#endif


#if defined(LANG_US) || defined(LANG_JP)

void *LnameX_lookuptable[45][2] = {
    {NULL, NULL},                    /* Null (unused) */
    {"LameE", "LameJ"},              /* Library (multi) */
    {"LarchE", "LarchJ"},            /* Archives */
    {"LarkE", "LarkJ"},              /* Facility */
    {"LashE", "LashJ"},              /* Stack (multi) */
    {"LaztE", "LaztJ"},              /* Aztec */
    {"LcatE", "LcatJ"},              /* Citadel (multi) */
    {"LcaveE", "LcaveJ"},            /* Caverns */
    {"LarecE", "LarecJ"},            /* Control */
    {"LcradE", "LcradJ"},            /* Cradle */
    {"LcrypE", "LcrypJ"},            /* Egypt */
    {"LdamE", "LdamJ"},              /* Dam */
    {"LdepoE", "LdepoJ"},            /* Depot */
    {"LdestE", "LdestJ"},            /* Frigate */
    {"LdishE", "LdishJ"},            /* Temple (multi) */
    {"LearE", "LearJ"},              /* Ear (unused) */
    {"LeldE", "LeldJ"},              /* Eld (unused) */
    {"LimpE", "LimpJ"},              /* Basement (multi) */
    {"LjunE", "LjunJ"},              /* Jungle */
    {"LleeE", "LleeJ"},              /* Lee (unused) */
    {"LlenE", "LlenJ"},              /* Cuba */
    {"LlipE", "LlipJ"},              /* Lip (unused) */
    {"LlueE", "LlueJ"},              /* Lue (unused) */
    {"LoatE", "LoatJ"},              /* Cave (multi) */
    {"LpamE", "LpamJ"},              /* Pam (unused) */
    {"LpeteE", "LpeteJ"},            /* Streets */
    {"LrefE", "LrefJ"},              /* Complex (multi) */
    {"LritE", "LritJ"},              /* Rit (unused) */
    {"LrunE", "LrunJ"},              /* Runway */
    {"LsevbE", "LsevbJ"},            /* Bunker 2 */
    {"LsevE", "LsevJ"},              /* Bunker 1 */
    {"LsevxE", "LsevxJ"},            /* Surface 1 */
    {"LsevxbE", "LsevxbJ"},          /* Surface 2 */
    {"LshoE", "LshoJ"},              /* Shooting Range (unused) */
    {"LsiloE", "LsiloJ"},            /* Silo */
    {"LstatE", "LstatJ"},            /* Statue */
    {"LtraE", "LtraJ"},              /* Train */
    {"LwaxE", "LwaxJ"},              /* Wax (unused) */
    {"LgunE", "LgunJ"},              /* Guns */
    {"LtitleE", "LtitleJ"},          /* Stage and menu titles */
    {"LmpmenuE", "LmpmenuJ"},        /* Multi menus */
    {"LpropobjE", "LpropobjJ"},      /* In-game pickups */
    {"LmpweaponsE", "LmpweaponsJ"},  /* Multi weapon select */
    {"LoptionsE", "LoptionsJ"},      /* Solo in-game menus */
    {"LmiscE", "LmiscJ"}};           /* Cheat options */

#endif

#if defined(LANG_EU)
void *LnameX_lookuptable[45][2] = {
    {NULL, NULL},                    /* Null (unused) */
    {"LameP", "LameJ"},              /* Library (multi) */
    {"LarchP", "LarchJ"},            /* Archives */
    {"LarkP", "LarkJ"},              /* Facility */
    {"LashP", "LashJ"},              /* Stack (multi) */
    {"LaztP", "LaztJ"},              /* Aztec */
    {"LcatP", "LcatJ"},              /* Citadel (multi) */
    {"LcaveP", "LcaveJ"},            /* Caverns */
    {"LarecP", "LarecJ"},            /* Control */
    {"LcradP", "LcradJ"},            /* Cradle */
    {"LcrypP", "LcrypJ"},            /* Egypt */
    {"LdamP", "LdamJ"},              /* Dam */
    {"LdepoP", "LdepoJ"},            /* Depot */
    {"LdestP", "LdestJ"},            /* Frigate */
    {"LdishP", "LdishJ"},            /* Temple (multi) */
    {"LearP", "LearJ"},              /* Ear (unused) */
    {"LeldP", "LeldJ"},              /* Eld (unused) */
    {"LimpP", "LimpJ"},              /* Basement (multi) */
    {"LjunP", "LjunJ"},              /* Jungle */
    {"LleeP", "LleeJ"},              /* Lee (unused) */
    {"LlenP", "LlenJ"},              /* Cuba */
    {"LlipP", "LlipJ"},              /* Lip (unused) */
    {"LlueP", "LlueJ"},              /* Lue (unused) */
    {"LoatP", "LoatJ"},              /* Cave (multi) */
    {"LpamP", "LpamJ"},              /* Pam (unused) */
    {"LpeteP", "LpeteJ"},            /* Streets */
    {"LrefP", "LrefJ"},              /* Complex (multi) */
    {"LritP", "LritJ"},              /* Rit (unused) */
    {"LrunP", "LrunJ"},              /* Runway */
    {"LsevbP", "LsevbJ"},            /* Bunker 2 */
    {"LsevP", "LsevJ"},              /* Bunker 1 */
    {"LsevxP", "LsevxJ"},            /* Surface 1 */
    {"LsevxbP", "LsevxbJ"},          /* Surface 2 */
    {"LshoP", "LshoJ"},              /* Shooting Range (unused) */
    {"LsiloP", "LsiloJ"},            /* Silo */
    {"LstatP", "LstatJ"},            /* Statue */
    {"LtraP", "LtraJ"},              /* Train */
    {"LwaxP", "LwaxJ"},              /* Wax (unused) */
    {"LgunP", "LgunJ"},              /* Guns */
    {"LtitleP", "LtitleJ"},          /* Stage and menu titles */
    {"LmpmenuP", "LmpmenuJ"},        /* Multi menus */
    {"LpropobjP", "LpropobjJ"},      /* In-game pickups */
    {"LmpweaponsP", "LmpweaponsJ"},  /* Multi weapon select */
    {"LoptionsP", "LoptionsJ"},      /* Solo in-game menus */
    {"LmiscP", "LmiscJ"}};           /* Cheat options */
#endif

// cannonically gettextloadnum
LEVELID langGetLangBankIndexFromStagenum(LEVELID level)
{
    enum TEXTBANK_LEVEL_INDEX return_id;

    switch(level)
    {
        case LEVELID_DAM:
            return_id = LDAM;
            break;
        case LEVELID_FACILITY:
            return_id = LARK;
            break;
        case LEVELID_RUNWAY:
            return_id = LRUN;
            break;
        case LEVELID_SURFACE:
            return_id = LSEVX;
            break;
        case LEVELID_BUNKER1:
            return_id = LSEV;
            break;
        case LEVELID_SILO:
            return_id = LSILO;
            break;
        case LEVELID_FRIGATE:
            return_id = LDEST;
            break;
        case LEVELID_SURFACE2:
            return_id = LSEVXB;
            break;
        case LEVELID_BUNKER2:
            return_id = LSEVB;
            break;
        case LEVELID_STATUE:
            return_id = LSTAT;
            break;
        case LEVELID_ARCHIVES:
            return_id = LARCH;
            break;
        case LEVELID_STREETS:
            return_id = LPETE;
            break;
        case LEVELID_DEPOT:
            return_id = LDEPO;
            break;
        case LEVELID_TRAIN:
            return_id = LTRA;
            break;
        case LEVELID_JUNGLE:
            return_id = LJUN;
            break;
        case LEVELID_CONTROL:
            return_id = LAREC;
            break;
        case LEVELID_CAVERNS:
            return_id = LCAVE;
            break;
        case LEVELID_CRADLE:
            return_id = LCRAD;
            break;
        case LEVELID_AZTEC:
            return_id = LAZT;
            break;
        case LEVELID_EGYPT:
            return_id = LCRYP;
            break;
        case LEVELID_TEMPLE:
            return_id = LDISH;
            break;
        case LEVELID_COMPLEX:
            return_id = LREF;
            break;
        case LEVELID_LIBRARY:
            return_id = LAME;
            break;
        case LEVELID_BASEMENT:
            return_id = LIMP;
            break;
        case LEVELID_STACK:
            return_id = LASH;
            break;
        case LEVELID_CAVES:
            return_id = LOAT;
            break;
        case LEVELID_CUBA:
            return_id = LLEN;
            break;
#ifdef NATIVE_PORT
        /* MP-only / unused levels not in original switch */
        case LEVELID_CITADEL:
            return_id = LCAT;
            break;
        case LEVELID_SHO:
            return_id = LSHO;
            break;
        case LEVELID_ELD:
            return_id = LELD;
            break;
        case LEVELID_LUE:
            return_id = LLUE;
            break;
        case LEVELID_RIT:
            return_id = LRIT;
            break;
        case LEVELID_EAR:
            return_id = LEAR;
            break;
        case LEVELID_LEE:
            return_id = LLEE;
            break;
        case LEVELID_LIP:
            return_id = LLIP;
            break;
        case LEVELID_WAX:
            return_id = LWAX;
            break;
        case LEVELID_PAM:
            return_id = LPAM;
            break;
#endif
        default:
        {
            #ifdef DEBUG
                osSyncPrintf("gettextloadnum: level %d unknown. (HANG now.)\n",level);
            #endif
#ifdef NATIVE_PORT
            fprintf(stderr, "[LANG] langGetLangBankIndexFromStagenum: unknown level %d, returning 0\n", level);
            return_id = 0;
#else
            /* infinite loop on invalid text bank */
            while(1) {};
#endif
        }
    }

    return (LEVELID)return_id;
}


void langInit(void) {
    s32 i;

    if (j_text_trigger) {
        g_JpnCharCachePixels = mempAllocBytesInBank(0x2E80, MEMPOOL_PERMANENT);
        g_JpnCacheCacheItems = mempAllocBytesInBank(0x100, MEMPOOL_PERMANENT);
        for(i = 0;i != 124;i++) {
            g_JpnCacheCacheItems[i].ttl =0;
            g_JpnCacheCacheItems[i].codepoint =-1;
        }
    }

	for (i = 0; i < 45; i++) {
		g_LangBanks[i] = 0;
	}
    g_LangBanks[LGUN] = (uintptr_t)_fileNameLoadToBank(LnameX_lookuptable[LGUN][j_text_trigger], FILELOADMETHOD_DEFAULT, 0x100, MEMPOOL_PERMANENT);
    g_LangBanks[LTITLE] = (uintptr_t)_fileNameLoadToBank(LnameX_lookuptable[LTITLE][j_text_trigger], FILELOADMETHOD_DEFAULT, 0x100, MEMPOOL_PERMANENT);
    g_LangBanks[LMPMENU] = (uintptr_t)_fileNameLoadToBank(LnameX_lookuptable[LMPMENU][j_text_trigger], FILELOADMETHOD_DEFAULT, 0x100, MEMPOOL_PERMANENT);
    g_LangBanks[LPROPOBJ] = (uintptr_t)_fileNameLoadToBank(LnameX_lookuptable[LPROPOBJ][j_text_trigger], FILELOADMETHOD_DEFAULT, 0x100, MEMPOOL_PERMANENT);
    g_LangBanks[LMPWEAPONS] = (uintptr_t)_fileNameLoadToBank(LnameX_lookuptable[LMPWEAPONS][j_text_trigger], FILELOADMETHOD_DEFAULT, 0x100, MEMPOOL_PERMANENT);
    g_LangBanks[LOPTIONS] = (uintptr_t)_fileNameLoadToBank(LnameX_lookuptable[LOPTIONS][j_text_trigger], FILELOADMETHOD_DEFAULT, 0x100, MEMPOOL_PERMANENT);
    g_LangBanks[LMISC] = (uintptr_t)_fileNameLoadToBank(LnameX_lookuptable[LMISC][j_text_trigger], FILELOADMETHOD_DEFAULT, 0x100, MEMPOOL_PERMANENT);
}


//assert here reveals this file is language.c
void langTick(void)
{
    s32 i;

    if (j_text_trigger)
    {
        for (i = 0; i < 0x7c; i++)
        {
		    if (g_JpnCacheCacheItems[i].ttl)
            {
			    g_JpnCacheCacheItems[i].ttl--;
		    }
            #ifdef DEBUG
            assert(jloaddata[i].timeleft<=3);
            #endif
		}
    }
}


extern u32 _efontchardataSegmentRomStart;
extern u32 _jfontchardataSegmentRomStart;
void romCopy(void *target, void *source, u32 size);

struct jpncharpixels *langGetJpnCharPixels(s32 codepoint)
{
	s32 i;
	s32 freeindexsingle = -1;
	s32 freeindexmulti = -1;
	s32 multibyte = 0;


	if (codepoint & 0x2000) {
		multibyte = 1;
	}


#define SHIFTAMOUNT 1
#define TMUL 8

	for (i = 0; i < 0x7C; i++) {
		if (!multibyte && (codepoint >> SHIFTAMOUNT) == g_JpnCacheCacheItems[i].codepoint) {
			break;
		}

		if (multibyte && i + 1 < 0x7C
				&& (codepoint >> SHIFTAMOUNT) == g_JpnCacheCacheItems[i].codepoint
				&& (codepoint >> SHIFTAMOUNT) == g_JpnCacheCacheItems[i + 1].codepoint) {
			break;
		}

		if (g_JpnCacheCacheItems[i].ttl == 0) {
			freeindexsingle = i;
		}

		if (g_JpnCacheCacheItems[i].ttl == 0 && g_JpnCacheCacheItems[i + 1].ttl == 0 && i + 1 < 0x7C) {
			freeindexmulti = i;
		}
	}

	if (i < 0x7C) {
		if (!multibyte) {
			g_JpnCacheCacheItems[i].ttl = 2;

			return &g_JpnCharCachePixels[i * TMUL];
		} else {
			g_JpnCacheCacheItems[i + 0].ttl = 2;
			g_JpnCacheCacheItems[i + 1].ttl = 2;

			return &g_JpnCharCachePixels[TMUL * i];
		}
	}


	if (!multibyte && freeindexsingle >= 0) {
		g_JpnCacheCacheItems[freeindexsingle].ttl = 2;
		g_JpnCacheCacheItems[freeindexsingle].codepoint = codepoint >> 1;

#ifdef NATIVE_PORT
		romCopy(&g_JpnCharCachePixels[freeindexsingle * 8], (void *)((uintptr_t)ROM_OFFSET(_jfontchardataSegmentRomStart) + (codepoint >> SHIFTAMOUNT) * 0x60), 0x60);
#else
		romCopy(&g_JpnCharCachePixels[freeindexsingle * 8], (romptr_t) &_jfontchardataSegmentRomStart + (codepoint >> SHIFTAMOUNT) * 0x60, 0x60);
#endif

		return &g_JpnCharCachePixels[freeindexsingle * 8];
	}

	if (multibyte && freeindexmulti >= 0) {
		g_JpnCacheCacheItems[freeindexmulti + 0].ttl = 2;
		g_JpnCacheCacheItems[freeindexmulti + 1].ttl = 2;
		g_JpnCacheCacheItems[freeindexmulti + 0].codepoint = codepoint >> 1;
		g_JpnCacheCacheItems[freeindexmulti + 1].codepoint = codepoint >> 1;

#ifdef NATIVE_PORT
		romCopy(&g_JpnCharCachePixels[freeindexmulti * 8], (void *)((uintptr_t)ROM_OFFSET(_efontchardataSegmentRomStart) + ((codepoint & 0x1fff) >> SHIFTAMOUNT) * 0x80), 0x80);
#else
		romCopy(&g_JpnCharCachePixels[freeindexmulti * 8], (romptr_t) &_efontchardataSegmentRomStart + ((codepoint & 0x1fff) >> SHIFTAMOUNT) * 0x80, 0x80);
#endif

		return &g_JpnCharCachePixels[freeindexmulti * 8];
	}

	return &g_JpnCharCachePixels[0];
}


void langLoadToAddr(u32 id)
{
    g_LangBanks[id] = (uintptr_t)_fileNameLoadToBank(LnameX_lookuptable[id][j_text_trigger],1,0x100,MEMPOOL_STAGE);
}


void langLoadToBank(int id,u8 *target,int size)
{
    g_LangBanks[id] = (uintptr_t)_fileNameLoadToAddr(LnameX_lookuptable[id][j_text_trigger],1,target,size);
}


void langClearBank(s32 textBank) {
    g_LangBanks[textBank] = 0;
}

/**
 * Get pointer of a string based on language of game (E/J)
 * @param slotID: UniqueID of string (a combination of Bank ID and string index)
 * @return char* string.
 */
u8 * langGet(s32 slotID)
{
#ifdef NATIVE_PORT
    /* Text bank file: array of BE u32 offsets, followed by text data.
     * Each offset is relative to the start of the bank file. */
    u32 text_id = (u16)slotID;
    u32 textbank_id = text_id >> 10;
    u32 textslot_id = text_id & 0x03FF;
    u32 *textbank_ptr = (u32 *)(uintptr_t)g_LangBanks[textbank_id];
    u32 textslot_offset;
    if (textbank_ptr == NULL) return langGetNativeTitleFallback(slotID);

    /* Byte-swap the offset from big-endian */
    textslot_offset = __builtin_bswap32(textbank_ptr[textslot_id]);
    if (textslot_offset != 0) {
        return (u8 *)((uintptr_t)textbank_ptr + textslot_offset);
    }

    /* Some native title-bank builds have zero offsets for the actor-credit
     * strings even though the ROM/authored tables include them. Preserve the
     * original display-cast sequence while keeping all non-title misses null. */
    return langGetNativeTitleFallback(slotID);
#else
    u32 * textbank_ptr = g_LangBanks[slotID >> 10]; /* get the text file bank ID index the text ptr table */
    u32 textslot_offset = textbank_ptr[slotID & 0x03FF]; /* load the textbank ptr table then get the slot's offset */

    u32 output_slot = textslot_offset; /* add the text slot offset to the base ptr to get the ptr to text file's slot */
    output_slot += (u32)textbank_ptr;
    #ifdef DEBUG
    return (textslot_offset != 0) ? (u8 *)output_slot : "Sorry, string not loaded.";
    #endif
    return (textslot_offset != 0) ? (u8*)output_slot : NULL;
#endif
}
