/*
 * Project Titan Magenta - AR9271 passive scanner radio core.
 *
 * This is a deliberately narrow AR9271/2.4GHz/20MHz live radio port for the
 * standalone Titan tester.  It now covers receive, management transmit and the
 * EEPROM-derived AR9271 4k TX-power path needed before Auth/Association.
 * The sequencing and register model follow Linux ath9k_htc.
 */
#include <types.h>
#include <sysclib.h>
#include <thbase.h>
#include "ar9271_radio.h"
#include "ar9271_wmi_ids.h"
#include "ar9271_regs.h"
#include "ar9271_initvals.h"
#include "titan_flightrec.h"
#include "titan_ps2_mac.h"


#define HTC_M_STA 1
#define HTC_M_MONITOR 8
#define MODE_11NG 1
#define EEPROM_BYTES 376
#define EEPROM_WORDS 188
#define EEPROM_START_WORD 64
#define REG_BATCH 14
#define RMW_BATCH 8

#define EEP_MODAL 52
#define EEP_OFF_VERSION 4
#define EEP_OFF_RXMASK 18
#define EEP_OFF_TXMASK 19
#define EEP_SPURCHANS (EEP_MODAL + 48)

static u32 be32(const u8 *p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }
static void pbe16(u8 *p,u16 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static void pbe32(u8 *p,u32 v){p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;}
static u16 le16(const u8 *p){return (u16)p[0]|((u16)p[1]<<8);}
static u32 le32(const u8 *p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static int min_i(int a,int b){return a<b?a:b;}
/* EEPROM byte helpers are defined below the TX-power port; declare them here. */
static u8 eu8(TitanAr9271Radio *a,int o);
static u16 ele16(TitanAr9271Radio *a,int o);


static int cmd(TitanAr9271Radio *a,u16 id,const void *p,int n,void *r,int cap,int *rn)
{
    int rc;
    a->last_wmi_cmd=id;
    rc=a->io.wmi_cmd(a->io.ctx,id,p,n,r,cap,rn);
    a->last_wmi_rc=rc;
    return rc;
}

static int reg_read(TitanAr9271Radio *a,u32 addr,u32 *val)
{
    u8 q[4],r[16]; int n=0;
    a->last_reg=addr;
    pbe32(q,addr);
    if(cmd(a,WMI_REG_READ_CMDID,q,4,r,sizeof(r),&n)<0 || n<4) return -1;
    *val=be32(r); a->last_reg_val=*val; return 0;
}

static int reg_read_multi(TitanAr9271Radio *a,const u32 *addr,int cnt,u32 *vals)
{
    u8 q[64],r[64]; int i,n=0;
    if(cnt<=0 || cnt>8) return -1;
    for(i=0;i<cnt;i++) pbe32(q+i*4,addr[i]);
    if(cmd(a,WMI_REG_READ_CMDID,q,cnt*4,r,sizeof(r),&n)<0 || n<cnt*4) return -1;
    for(i=0;i<cnt;i++) vals[i]=be32(r+i*4);
    return 0;
}

static int reg_write(TitanAr9271Radio *a,u32 addr,u32 val)
{
    u8 q[8]; int rn=0; a->last_reg=addr; a->last_reg_val=val; pbe32(q,addr);pbe32(q+4,val);
    return cmd(a,WMI_REG_WRITE_CMDID,q,8,NULL,0,&rn);
}

static int reg_write_batch(TitanAr9271Radio *a,const u32 *addr,const u32 *val,int cnt)
{
    u8 q[REG_BATCH*8]; int i,rn=0;
    if(cnt<=0 || cnt>REG_BATCH) return -1;
    a->last_reg=addr[cnt-1]; a->last_reg_val=val[cnt-1];
    for(i=0;i<cnt;i++){pbe32(q+i*8,addr[i]);pbe32(q+i*8+4,val[i]);}
    return cmd(a,WMI_REG_WRITE_CMDID,q,cnt*8,NULL,0,&rn);
}

static int reg_rmw(TitanAr9271Radio *a,u32 addr,u32 set,u32 clr)
{
    u8 q[12]; int rn=0; a->last_reg=addr; a->last_reg_val=set; pbe32(q,addr);pbe32(q+4,set);pbe32(q+8,clr);
    return cmd(a,WMI_REG_RMW_CMDID,q,12,NULL,0,&rn);
}

static int reg_rmw_batch(TitanAr9271Radio *a,const u32 *addr,const u32 *set,const u32 *clr,int cnt)
{
    u8 q[RMW_BATCH*12]; int i,rn=0;
    if(cnt<=0 || cnt>RMW_BATCH) return -1;
    for(i=0;i<cnt;i++){pbe32(q+i*12,addr[i]);pbe32(q+i*12+4,set[i]);pbe32(q+i*12+8,clr[i]);}
    return cmd(a,WMI_REG_RMW_CMDID,q,cnt*12,NULL,0,&rn);
}

static int rmw_field(TitanAr9271Radio *a,u32 reg,u32 mask,u32 value)
{return reg_rmw(a,reg,SM(value,mask),mask);}

static int wait_reg(TitanAr9271Radio *a,u32 reg,u32 mask,u32 want,int loops)
{
    int i;u32 v=0;
    for(i=0;i<loops;i++){
        if(reg_read(a,reg,&v)<0){
            titan_fr_record(TITAN_FR_EV_REG_POLL,1,-1,reg,mask,want,v,NULL,0,(u16)i);return -1;
        }
        if((v&mask)==want){
            titan_fr_record(TITAN_FR_EV_REG_POLL,0,0,reg,mask,want,v,NULL,0,(u16)(i+1));return 0;
        }
        DelayThread(10);
    }
    titan_fr_record(TITAN_FR_EV_REG_POLL,2,-2,reg,mask,want,v,NULL,0,(u16)loops);
    return -2;
}


static void fr_stage(TitanAr9271Radio *a,u8 stage)
{
    a->init_stage=stage;
    titan_fr_record(TITAN_FR_EV_RADIO_STAGE,0,0,(u32)stage,a->last_reg,(u32)a->last_wmi_cmd,(u32)a->last_wmi_rc,NULL,0,0);
}
/* ---- EEPROM ------------------------------------------------------------ */
static int eeprom_fill(TitanAr9271Radio *a)
{
    int word=0,i,j; u32 addr[8],vals[8];
    while(word<EEPROM_WORDS){
        int n=min_i(8,EEPROM_WORDS-word);
        for(i=0;i<n;i++) addr[i]=AR5416_EEPROM_OFFSET+((EEPROM_START_WORD+word+i)<<AR5416_EEPROM_S);
        if(reg_read_multi(a,addr,n,vals)<0)return -1;
        for(j=0;j<n;j++){
            a->eeprom[(word+j)*2]=(u8)(vals[j]&0xff);
            a->eeprom[(word+j)*2+1]=(u8)((vals[j]>>8)&0xff);
        }
        word+=n;
    }
    return 0;
}

static int eeprom_single_word(TitanAr9271Radio *a,u32 off,u16 *out)
{
    u32 v;
    if(reg_read(a,AR5416_EEPROM_OFFSET+(off<<AR5416_EEPROM_S),&v)<0)return -1;
    if(wait_reg(a,AR_EEPROM_STATUS_DATA,AR_EEPROM_STATUS_DATA_BUSY|AR_EEPROM_STATUS_DATA_PROT_ACCESS,0,1000)<0)return -1;
    if(reg_read(a,AR_EEPROM_STATUS_DATA,&v)<0)return -1;
    *out=(u16)(v&AR_EEPROM_STATUS_DATA_VAL);return 0;
}

static int eeprom_init(TitanAr9271Radio *a)
{
    u16 magic,length,version,checksum=0,w; int i,words;
    if(eeprom_fill(a)<0)return -1;
    if(eeprom_single_word(a,AR5416_EEPROM_MAGIC_OFFSET,&magic)<0)return -2;
    if(magic!=AR5416_EEPROM_MAGIC){
        u16 swapped=(u16)((magic>>8)|(magic<<8));
        if(swapped!=AR5416_EEPROM_MAGIC)return -3;
        for(i=0;i<EEPROM_WORDS;i++){
            u8 t=a->eeprom[i*2];a->eeprom[i*2]=a->eeprom[i*2+1];a->eeprom[i*2+1]=t;
        }
    }
    length=le16(a->eeprom); words=min_i(length/2,EEPROM_WORDS);
    for(i=0;i<words;i++){w=le16(a->eeprom+i*2);checksum^=w;}
    if(checksum!=0xffff)return -4;
    version=le16(a->eeprom+4);
    if(((version&AR5416_EEP_VER_MAJOR_MASK)>>AR5416_EEP_VER_MAJOR_SHIFT)!=AR5416_EEP_VER)return -5;
    if((version&AR5416_EEP_VER_MINOR_MASK)<AR5416_EEP_NO_BACK_VER)return -6;
    a->rxchainmask=a->eeprom[EEP_OFF_RXMASK];
    a->txchainmask=a->eeprom[EEP_OFF_TXMASK];
    memcpy(a->macaddr,a->eeprom+12,6);
    {
        static const u8 ps2_bios_mac[6]={TITAN_PS2_BIOS_MAC_BYTES};
        /* V10.8 LINUX-GOLD: one authoritative station identity. Override
         * the AR9271 EEPROM address before reset_opmode()/STA_ID/VIF/node
         * setup so every layer observes exactly the PS2 BIOS MAC. */
        memcpy(a->macaddr,ps2_bios_mac,sizeof(ps2_bios_mac));
    }
    return 0;
}

/* ---- AR9271 4k EEPROM TX power ---------------------------------------
 * V10.13.0: live port of the ath9k 4k power path used by AR9271.
 * This deliberately computes from this adapter's EEPROM and the current
 * channel instead of replaying register writes captured on another run.
 * Functional source: Linux ath9k eeprom_4k.c/eeprom.c and wifit3 AR9271 v2.
 */
#define EEP_OFF_ANTGAIN      (EEP_MODAL + 8)
#define EEP_OFF_XPDGAIN      (EEP_MODAL + 20)
#define EEP_OFF_PDOVERLAP    (EEP_MODAL + 24)
#define EEP_CALFREQPIER2G    120
#define EEP_CALPIERDATA2G    123
#define EEP_CALTARGET_CCK    183
#define EEP_CALTARGET_2G     198
#define EEP_CALTARGET_HT20   213
#define EEP_CTLINDEX         267
#define EEP_CTLDATA          279
#define TITAN_NO_CTL         0xffu
#define TITAN_MAX_COMBINED_POWER 254
#define TITAN_RATE_COUNT 36

enum Titan5416Rate {
    TR_6M=0,TR_9M,TR_12M,TR_18M,TR_24M,TR_36M,TR_48M,TR_54M,
    TR_1L,TR_2L,TR_2S,TR_5L,TR_5S,TR_11L,TR_11S,TR_XR,
    TR_HT20_0,TR_HT20_1,TR_HT20_2,TR_HT20_3,TR_HT20_4,TR_HT20_5,TR_HT20_6,TR_HT20_7,
    TR_HT40_0,TR_HT40_1,TR_HT40_2,TR_HT40_3,TR_HT40_4,TR_HT40_5,TR_HT40_6,TR_HT40_7,
    TR_DUP_CCK,TR_DUP_OFDM,TR_EXT_CCK,TR_EXT_OFDM
};

static int txp_freq(int channel){return channel==14?2484:2407+channel*5;}
static int txp_fbin(int freq){return (freq-2300)&0xff;}
static int txp_fbin_freq(int b){return b==(int)AR5416_BCHAN_UNUSED?b:2300+b;}
static int txp_min(int a,int b){return a<b?a:b;}
static int txp_max(int a,int b){return a>b?a:b;}
static int txp_interp(int target,int sl,int sr,int tl,int tr)
{
    if(sr==sl)return tl;
    return ((target-sl)*tr+(sr-target)*tl)/(sr-sl);
}

static void txp_lower_upper(int target,const u8 *list,int n,int *match,int *lo,int *hi)
{
    int i;
    *match=0;*lo=0;*hi=0;
    if(n<=0)return;
    if(target<=list[0]){*match=1;return;}
    if(target>=list[n-1]){*match=1;*lo=*hi=n-1;return;}
    for(i=0;i<n-1;i++){
        if(list[i]==target){*match=1;*lo=*hi=i;return;}
        if(target<list[i+1]){*lo=i;*hi=i+1;return;}
    }
}

static int txp_fill_vpd(int pmin,int pmax,const u8 *pwr,const u8 *vpd,int num,u8 *out,int cap)
{
    int curr=pmin,n=0;
    while(curr<=pmax && n<cap){
        int m,l,r,k;
        txp_lower_upper(curr,pwr,num,&m,&l,&r);
        (void)m;
        if(r<1)r=1;
        if(l==num-1)l=num-2;
        if(pwr[l]==pwr[r])k=vpd[l];
        else k=((curr-pwr[l])*vpd[r]+(pwr[r]-curr)*vpd[l])/(pwr[r]-pwr[l]);
        out[n++]=(u8)k;curr+=2;
    }
    return n;
}

static void txp_target_leg(TitanAr9271Radio *a,int base,int freq,u8 out[4])
{
    int i,match=-1,low=-1;
    if(freq<=txp_fbin_freq(eu8(a,base)))match=0;
    else{
        for(i=0;i<3;i++){
            int b=eu8(a,base+i*5),f;
            if(b==AR5416_BCHAN_UNUSED)break;
            f=txp_fbin_freq(b);
            if(freq==f){match=i;break;}
            if(freq<f && i>0 && freq>txp_fbin_freq(eu8(a,base+(i-1)*5))){low=i-1;break;}
        }
        if(match<0 && low<0)match=(i>0)?i-1:0;
    }
    if(match>=0){for(i=0;i<4;i++)out[i]=eu8(a,base+match*5+1+i);return;}
    {
        int fl=txp_fbin_freq(eu8(a,base+low*5)),fr=txp_fbin_freq(eu8(a,base+(low+1)*5));
        for(i=0;i<4;i++)out[i]=(u8)txp_interp(freq,fl,fr,eu8(a,base+low*5+1+i),eu8(a,base+(low+1)*5+1+i));
    }
}

static void txp_target_ht(TitanAr9271Radio *a,int base,int freq,u8 out[8])
{
    int i,match=-1,low=-1;
    if(freq<=txp_fbin_freq(eu8(a,base)))match=0;
    else{
        for(i=0;i<3;i++){
            int b=eu8(a,base+i*9),f;
            if(b==AR5416_BCHAN_UNUSED)break;
            f=txp_fbin_freq(b);
            if(freq==f){match=i;break;}
            if(freq<f && i>0 && freq>txp_fbin_freq(eu8(a,base+(i-1)*9))){low=i-1;break;}
        }
        if(match<0 && low<0)match=(i>0)?i-1:0;
    }
    if(match>=0){for(i=0;i<8;i++)out[i]=eu8(a,base+match*9+1+i);return;}
    {
        int fl=txp_fbin_freq(eu8(a,base+low*9)),fr=txp_fbin_freq(eu8(a,base+(low+1)*9));
        for(i=0;i<8;i++)out[i]=(u8)txp_interp(freq,fl,fr,eu8(a,base+low*9+1+i),eu8(a,base+(low+1)*9+1+i));
    }
}

static int txp_ctl_edge(TitanAr9271Radio *a,int ctl,int freq)
{
    int k,twice=(int)MAX_RATE_POWER,base=EEP_CTLDATA+ctl*8;
    for(k=0;k<4;k++){
        int b=eu8(a,base+k*2),c=eu8(a,base+k*2+1),f;
        if(b==AR5416_BCHAN_UNUSED)break;
        f=txp_fbin_freq(b);
        if(freq==f){twice=c&0x3f;break;}
        if(k>0 && freq<f){
            int pb=eu8(a,base+(k-1)*2),pc=eu8(a,base+(k-1)*2+1);
            if(txp_fbin_freq(pb)<freq && (pc&0xc0))twice=pc&0x3f;
            break;
        }
    }
    return twice;
}

static void txp_rate_table(TitanAr9271Radio *a,int channel,int power_limit,int rates[TITAN_RATE_COUNT])
{
    static const int modes[3]={CTL_11B,CTL_11G,CTL_2GHT20};
    u8 cck[4],ofdm[4],ht[8];int freq=txp_freq(channel),i,m;
    int ant=eu8(a,EEP_OFF_ANTGAIN);int scaled=power_limit-ant;
    if(scaled<0)scaled=0;
    if(scaled>(int)MAX_RATE_POWER)scaled=(int)MAX_RATE_POWER;
    memset(rates,0,sizeof(int)*TITAN_RATE_COUNT);
    txp_target_leg(a,EEP_CALTARGET_CCK,freq,cck);
    txp_target_leg(a,EEP_CALTARGET_2G,freq,ofdm);
    txp_target_ht(a,EEP_CALTARGET_HT20,freq,ht);
    for(m=0;m<3;m++){
        int maxedge=MAX_RATE_POWER;
        for(i=0;i<12;i++){
            int ci=eu8(a,EEP_CTLINDEX+i),base;
            if(ci==0)break;
            base=((int)TITAN_NO_CTL & ~(int)CTL_MODE_M)|(modes[m]&CTL_MODE_M);
            if(base==ci || base==(int)((ci&CTL_MODE_M)|SD_NO_CTL)){
                int edge=txp_ctl_edge(a,i,freq);
                if((((int)TITAN_NO_CTL)&~(int)CTL_MODE_M)==SD_NO_CTL)maxedge=txp_min(maxedge,edge);
                else{maxedge=edge;break;}
            }
        }
        maxedge=txp_min(maxedge,scaled);
        if(modes[m]==CTL_11B)for(i=0;i<4;i++)cck[i]=(u8)txp_min(cck[i],maxedge);
        else if(modes[m]==CTL_11G)for(i=0;i<4;i++)ofdm[i]=(u8)txp_min(ofdm[i],maxedge);
        else for(i=0;i<8;i++)ht[i]=(u8)txp_min(ht[i],maxedge);
    }
    rates[TR_6M]=rates[TR_9M]=rates[TR_12M]=rates[TR_18M]=rates[TR_24M]=ofdm[0];
    rates[TR_36M]=ofdm[1];rates[TR_48M]=ofdm[2];rates[TR_54M]=ofdm[3];rates[TR_XR]=ofdm[0];
    for(i=0;i<8;i++)rates[TR_HT20_0+i]=ht[i];
    rates[TR_1L]=cck[0];rates[TR_2L]=rates[TR_2S]=cck[1];
    rates[TR_5L]=rates[TR_5S]=cck[2];rates[TR_11L]=rates[TR_11S]=cck[3];
}

static int txp_gain_pdadc(TitanAr9271Radio *a,int channel,int overlap,int nxpd,u16 bound[4],u8 pdadc[128])
{
    u8 bchan[3],vpd_i[4][64],pwrL[5],pwrR[5],vpdL[5],vpdR[5],vl[64],vr[64];
    int minp[4]={0,0,0,0},maxp[4]={0,0,0,0};
    int freq=txp_freq(channel),sf=txp_fbin(freq),np=0,match,lo,hi,i,j,k=0;
    for(i=0;i<3;i++){bchan[i]=eu8(a,EEP_CALFREQPIER2G+i);if(bchan[i]==AR5416_BCHAN_UNUSED)break;np++;}
    if(np<=0||nxpd<=0)return -1;
    memset(vpd_i,0,sizeof(vpd_i));memset(pdadc,0,128);memset(bound,0,4*sizeof(u16));
    txp_lower_upper(sf,bchan,np,&match,&lo,&hi);
    for(i=0;i<nxpd;i++){
        if(match){
            int base=EEP_CALPIERDATA2G+lo*20;
            for(j=0;j<5;j++){pwrL[j]=eu8(a,base+i*5+j);vpdL[j]=eu8(a,base+10+i*5+j);}
            minp[i]=pwrL[0];maxp[i]=pwrL[4];
            (void)txp_fill_vpd(minp[i],maxp[i],pwrL,vpdL,5,vpd_i[i],64);
        }else{
            int bl=EEP_CALPIERDATA2G+lo*20,br=EEP_CALPIERDATA2G+hi*20,n;
            for(j=0;j<5;j++){
                pwrL[j]=eu8(a,bl+i*5+j);vpdL[j]=eu8(a,bl+10+i*5+j);
                pwrR[j]=eu8(a,br+i*5+j);vpdR[j]=eu8(a,br+10+i*5+j);
            }
            minp[i]=txp_max(pwrL[0],pwrR[0]);maxp[i]=txp_min(pwrL[4],pwrR[4]);
            memset(vl,0,sizeof(vl));memset(vr,0,sizeof(vr));
            txp_fill_vpd(minp[i],maxp[i],pwrL,vpdL,5,vl,64);
            txp_fill_vpd(minp[i],maxp[i],pwrR,vpdR,5,vr,64);
            n=(maxp[i]-minp[i])/2+1;
            for(j=0;j<n && j<64;j++)vpd_i[i][j]=(u8)txp_interp(sf,bchan[lo],bchan[hi],vl[j],vr[j]);
        }
    }
    for(i=0;i<nxpd;i++){
        int ss,step,size,tgt,maxidx,tmp;
        if(i==nxpd-1)bound[i]=(u16)(maxp[i]/2);else bound[i]=(u16)((maxp[i]+minp[i+1])/4);
        if(bound[i]>MAX_RATE_POWER)bound[i]=MAX_RATE_POWER;
        if(i==0)ss=-(minp[i]/2);else ss=((int)bound[i-1]-(minp[i]/2))-overlap+1;
        step=(int)vpd_i[i][1]-(int)vpd_i[i][0];if(step<1)step=1;
        while(ss<0 && k<127){tmp=(int)vpd_i[i][0]+ss*step;pdadc[k++]=(u8)(tmp<0?0:tmp);ss++;}
        size=(maxp[i]-minp[i])/2+1;if(size<2)return -2;
        tgt=(int)bound[i]+overlap-(minp[i]/2);maxidx=tgt<size?tgt:size;
        while(ss<maxidx && k<127){if(ss<0)ss=0;pdadc[k++]=vpd_i[i][ss];ss++;}
        step=(int)vpd_i[i][size-1]-(int)vpd_i[i][size-2];if(step<1)step=1;
        if(tgt>=maxidx)while(ss<=tgt && k<127){tmp=(int)vpd_i[i][size-1]+(ss-maxidx+1)*step;pdadc[k++]=(u8)(tmp>255?255:tmp);ss++;}
    }
    for(i=nxpd;i<4;i++)bound[i]=58;
    if(k==0)return -3;
    while(k<128){pdadc[k]=pdadc[k-1];k++;}
    return 0;
}

static int txp_cal_table(TitanAr9271Radio *a,int channel)
{
    u8 xmask=eu8(a,EEP_OFF_XPDGAIN),xval[2]={0,0},pdadc[128];u16 bound[4];
    int nx=0,i,j,overlap;u32 v,ad[14],vv[14];int n;
    if((ele16(a,EEP_OFF_VERSION)&AR5416_EEP_VER_MINOR_MASK)>=AR5416_EEP_MINOR_VER_2)overlap=eu8(a,EEP_OFF_PDOVERLAP);
    else{if(reg_read(a,AR_PHY_TPCRG5,&v)<0)return -1;overlap=MS(v,AR_PHY_TPCRG5_PD_GAIN_OVERLAP);}
    for(i=1;i<=4;i++)if((xmask>>(4-i))&1){if(nx>=2)break;xval[nx++]=(u8)(4-i);}
    if(nx<=0)return -2;
    if(rmw_field(a,AR_PHY_TPCRG1,AR_PHY_TPCRG1_NUM_PD_GAIN,(u32)((nx-1)&3))<0)return -3;
    if(rmw_field(a,AR_PHY_TPCRG1,AR_PHY_TPCRG1_PD_GAIN_1,xval[0])<0)return -4;
    if(rmw_field(a,AR_PHY_TPCRG1,AR_PHY_TPCRG1_PD_GAIN_2,xval[1])<0)return -5;
    if(rmw_field(a,AR_PHY_TPCRG1,AR_PHY_TPCRG1_PD_GAIN_3,0)<0)return -6;
    if(!(a->txchainmask&1))return 0;
    if(txp_gain_pdadc(a,channel,overlap,nx,bound,pdadc)<0)return -7;
    ad[0]=AR_PHY_TPCRG5;vv[0]=SM(overlap,AR_PHY_TPCRG5_PD_GAIN_OVERLAP)|SM(bound[0],AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1)|SM(bound[1],AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2)|SM(bound[2],AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3)|SM(bound[3],AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4);
    n=1;
    for(j=0;j<32;j++){
        u32 reg=AR_PHY_BASE+(672u<<2)+(u32)j*4u;
        u32 val=(u32)pdadc[j*4]|((u32)pdadc[j*4+1]<<8)|((u32)pdadc[j*4+2]<<16)|((u32)pdadc[j*4+3]<<24);
        ad[n]=reg;vv[n]=val;n++;
        if(n==14){if(reg_write_batch(a,ad,vv,n)<0)return -8;n=0;}
    }
    if(n && reg_write_batch(a,ad,vv,n)<0)return -9;
    return 0;
}

static u32 txp_pack(int r,int shift){return ((u32)r&0x3fu)<<shift;}

static int txpower_apply(TitanAr9271Radio *a,int channel,int limit)
{
    int rates[TITAN_RATE_COUNT],i,maxp=0;u32 ad[7],vv[7];
    if(!a||channel<1||channel>13)return -1;
    if(limit<0)limit=0;
    if(limit>TITAN_MAX_COMBINED_POWER)limit=TITAN_MAX_COMBINED_POWER;
    txp_rate_table(a,channel,limit,rates);
    if(txp_cal_table(a,channel)<0)return -2;
    for(i=0;i<TITAN_RATE_COUNT;i++){
        if(rates[i]>(int)MAX_RATE_POWER)rates[i]=(int)MAX_RATE_POWER;
        if(rates[i]>maxp)maxp=rates[i];
        rates[i]+=10;
    }
    ad[0]=AR_PHY_POWER_TX_RATE1;vv[0]=txp_pack(rates[TR_18M],24)|txp_pack(rates[TR_12M],16)|txp_pack(rates[TR_9M],8)|txp_pack(rates[TR_6M],0);
    ad[1]=AR_PHY_POWER_TX_RATE2;vv[1]=txp_pack(rates[TR_54M],24)|txp_pack(rates[TR_48M],16)|txp_pack(rates[TR_36M],8)|txp_pack(rates[TR_24M],0);
    ad[2]=AR_PHY_POWER_TX_RATE3;vv[2]=txp_pack(rates[TR_2S],24)|txp_pack(rates[TR_2L],16)|txp_pack(rates[TR_XR],8)|txp_pack(rates[TR_1L],0);
    ad[3]=AR_PHY_POWER_TX_RATE4;vv[3]=txp_pack(rates[TR_11S],24)|txp_pack(rates[TR_11L],16)|txp_pack(rates[TR_5S],8)|txp_pack(rates[TR_5L],0);
    ad[4]=AR_PHY_POWER_TX_RATE5;vv[4]=txp_pack(rates[TR_HT20_3],24)|txp_pack(rates[TR_HT20_2],16)|txp_pack(rates[TR_HT20_1],8)|txp_pack(rates[TR_HT20_0],0);
    ad[5]=AR_PHY_POWER_TX_RATE6;vv[5]=txp_pack(rates[TR_HT20_7],24)|txp_pack(rates[TR_HT20_6],16)|txp_pack(rates[TR_HT20_5],8)|txp_pack(rates[TR_HT20_4],0);
    ad[6]=AR_PHY_POWER_TX_RATE_MAX;vv[6]=MAX_RATE_POWER;
    if(reg_write_batch(a,ad,vv,7)<0)return -3;
    a->txpower_limit=(u16)limit;a->txpower_max=(u16)maxp;if(a->txpower_applies!=0xffffu)++a->txpower_applies;
    return 0;
}


/* ---- core reset -------------------------------------------------------- */
static int read_revision(TitanAr9271Radio *a)
{
    u32 srev,val;
    if(reg_read(a,AR_SREV,&srev)<0 || srev==0xffffffff)return -1;
    val=srev&AR_SREV_ID;
    if(val==0xff){a->mac_version=(srev&AR_SREV_VERSION2)>>AR_SREV_TYPE2_S;a->mac_rev=(srev&AR_SREV_REVISION2)>>AR_SREV_REVISION2_S;}
    else{a->mac_version=(val&AR_SREV_VERSION)>>AR_SREV_VERSION_S;a->mac_rev=val&AR_SREV_REVISION;}
    return a->mac_version==AR_SREV_VERSION_9271?0:-2;
}

static int set_reset(TitanAr9271Radio *a,int type)
{
    u32 tmp,rst;
    if(reg_write(a,AR_RTC_FORCE_WAKE,AR_RTC_FORCE_WAKE_EN|AR_RTC_FORCE_WAKE_ON_INT)<0)return -1;
    if(!a->reset_power_on)type=ATH9K_RESET_POWER_ON;
    if(type==ATH9K_RESET_POWER_ON){
        u32 wa[3]={AR_RTC_FORCE_WAKE,AR_RC,AR_RTC_RESET};
        u32 vv[3]={AR_RTC_FORCE_WAKE_EN|AR_RTC_FORCE_WAKE_ON_INT,AR_RC_AHB,0};
        if(reg_write_batch(a,wa,vv,3)<0)return -2;
        if(reg_write(a,AR_RC,0)<0 || reg_write(a,AR_RTC_RESET,AR_RTC_RESET_EN)<0)return -3;
        if(wait_reg(a,AR_RTC_STATUS,AR_RTC_STATUS_M,AR_RTC_STATUS_ON,1200)<0)return -4;
        a->reset_power_on=1;
        type=ATH9K_RESET_WARM;
    }
    if(reg_read(a,AR_INTR_SYNC_CAUSE,&tmp)<0)return -5;
    tmp&=AR_INTR_SYNC_LOCAL_TIMEOUT|AR_INTR_SYNC_RADM_CPL_TIMEOUT;
    if(tmp){
        if(reg_write(a,AR_INTR_SYNC_ENABLE,0)<0)return -6;
        if(reg_write(a,AR_RC,AR_RC_HOSTIF|AR_RC_AHB)<0)return -7;
    }else if(reg_write(a,AR_RC,AR_RC_AHB)<0)return -8;
    rst=AR_RTC_RC_MAC_WARM;if(type==ATH9K_RESET_COLD)rst|=AR_RTC_RC_MAC_COLD;
    if(reg_write(a,AR_RTC_RC,rst)<0 || reg_write(a,AR_RTC_RC,0)<0)return -9;
    if(wait_reg(a,AR_RTC_RC,AR_RTC_RC_M,0,1200)<0)return -10;
    return reg_write(a,AR_RC,0);
}

static int power_awake(TitanAr9271Radio *a)
{
    u32 v;int i;
    if(reg_read(a,AR_RTC_STATUS,&v)<0)return -1;
    if((v&AR_RTC_STATUS_M)==AR_RTC_STATUS_SHUTDOWN && set_reset(a,ATH9K_RESET_POWER_ON)<0)return -2;
    if(reg_rmw(a,AR_RTC_FORCE_WAKE,AR_RTC_FORCE_WAKE_EN,0)<0)return -3;
    for(i=0;i<300;i++){
        if(reg_read(a,AR_RTC_STATUS,&v)<0)return -4;
        if((v&AR_RTC_STATUS_M)==AR_RTC_STATUS_ON)break;
        reg_rmw(a,AR_RTC_FORCE_WAKE,AR_RTC_FORCE_WAKE_EN,0);
        DelayThread(50);
    }
    if(i==300)return -5;
    return reg_rmw(a,AR_STA_ID1,0,AR_STA_ID1_PWR_SAV);
}

static int init_pll(TitanAr9271Radio *a,int channel)
{
    u32 pll=SM(5,AR_RTC_9160_PLL_REFDIV)|SM(0x2c,AR_RTC_9160_PLL_DIV);
    (void)channel;
    if(reg_write(a,AR_RTC_PLL_CONTROL,pll)<0)return -1;
    if(reg_write(a,AR9271_CORE_CLOCK,AR9271_CORE_CLOCK_VAL)<0)return -2;
    return reg_write(a,AR_RTC_SLEEP_CLK,AR_RTC_FORCE_DERIVED_CLK);
}

static int get_tsf(TitanAr9271Radio *a,u64 *tsf)
{
    u32 hi1,hi2,lo;int i;
    if(reg_read(a,AR_TSF_U32,&hi1)<0)return -1;
    for(i=0;i<16;i++){
        if(reg_read(a,AR_TSF_L32,&lo)<0 || reg_read(a,AR_TSF_U32,&hi2)<0)return -1;
        if(hi1==hi2){*tsf=((u64)hi1<<32)|lo;return 0;}hi1=hi2;
    }
    *tsf=((u64)hi1<<32)|lo;return 0;
}

static int reset_begin(TitanAr9271Radio *a,int channel)
{
    u32 qtxe,cr;int type=ATH9K_RESET_WARM;
    if(reg_read(a,AR_DEF_ANTENNA,&a->save_def_antenna)<0)return -1;
    if(!a->save_def_antenna)a->save_def_antenna=1;
    if(reg_read(a,AR_STA_ID1,&a->mac_sta_id1)<0)return -2;
    a->mac_sta_id1&=AR_STA_ID1_BASE_RATE_11B;
    if(get_tsf(a,&a->tsf)<0)return -3;
    if(reg_read(a,AR_CFG_LED,&a->save_led_state)<0)return -4;
    a->save_led_state&=AR_CFG_LED_SAVE_MASK;
    if(reg_write(a,AR_PHY_ACTIVE,AR_PHY_ACTIVE_DIS)<0)return -5;
    if(a->htc_reset_init && reg_write(a,AR9271_RESET_POWER_DOWN_CONTROL,AR9271_RADIO_RF_RST)<0)return -6;
    if(reg_read(a,AR_Q_TXE,&qtxe)<0 || reg_read(a,AR_CR,&cr)<0)return -7;
    if(qtxe || (cr&AR_CR_RXE))type=ATH9K_RESET_COLD;
    if(set_reset(a,type)<0)return -8;
    if(init_pll(a,channel)<0)return -9;
    if(a->htc_reset_init){a->htc_reset_init=0;if(reg_write(a,AR9271_RESET_POWER_DOWN_CONTROL,AR9271_GATE_MAC_CTL)<0)return -10;}
    if(reg_write(a,AR_TSF_L32,(u32)a->tsf)<0 || reg_write(a,AR_TSF_U32,(u32)(a->tsf>>32))<0)return -11;
    return reg_rmw(a,AR_GPIO_INPUT_EN_VAL,AR_GPIO_JTAG_DISABLE,0);
}

/* ---- INI / PHY --------------------------------------------------------- */
static int write_ini5(TitanAr9271Radio *a,const TitanIni5 *t,unsigned int n,int col)
{
    u32 ad[REG_BATCH],vv[REG_BATCH]; int cnt=0;unsigned int i;
    for(i=0;i<n;i++){
        ad[cnt]=t[i].v[0];vv[cnt]=t[i].v[col];cnt++;
        if(cnt==REG_BATCH){if(reg_write_batch(a,ad,vv,cnt)<0)return -1;cnt=0;}
    }
    if(cnt && reg_write_batch(a,ad,vv,cnt)<0)return -1;
    return 0;
}
static int write_ini2(TitanAr9271Radio *a,const TitanIni2 *t,unsigned int n)
{
    u32 ad[REG_BATCH],vv[REG_BATCH];int cnt=0;unsigned int i;
    for(i=0;i<n;i++){
        ad[cnt]=t[i].reg;vv[cnt]=t[i].val;cnt++;
        if(cnt==REG_BATCH){if(reg_write_batch(a,ad,vv,cnt)<0)return -1;cnt=0;}
    }
    if(cnt && reg_write_batch(a,ad,vv,cnt)<0)return -1;
    return 0;
}

static int set_channel_regs(TitanAr9271Radio *a)
{
    u32 turbo;u32 ad[4],vv[4];
    if(reg_read(a,AR_PHY_TURBO,&turbo)<0)return -1;
    turbo&=AR_PHY_FC_ENABLE_DAC_FIFO;
    ad[0]=AR_PHY_TURBO;vv[0]=AR_PHY_FC_HT_EN|AR_PHY_FC_SHORT_GI_40|AR_PHY_FC_SINGLE_HT_LTF1|AR_PHY_FC_WALSH|turbo;
    ad[1]=AR_2040_MODE;vv[1]=0;
    ad[2]=AR_GTXTO;vv[2]=25u<<AR_GTXTO_TIMEOUT_LIMIT_S;
    ad[3]=AR_CST;vv[3]=0xFu<<AR_CST_TIMEOUT_LIMIT_S;
    return reg_write_batch(a,ad,vv,4);
}

static int init_chain_masks(TitanAr9271Radio *a)
{
    u32 ad[3]={AR_PHY_RX_CHAINMASK,AR_PHY_CAL_CHAINMASK,AR_SELFGEN_MASK};
    u32 vv[3]={a->rxchainmask,a->rxchainmask,a->txchainmask};
    return reg_write_batch(a,ad,vv,3);
}

static int process_ini(TitanAr9271Radio *a)
{
    int txhi=a->eeprom[31]==AR5416_EEP_TXGAIN_HIGH_POWER;
    if(reg_write(a,AR_PHY(0),7)<0)return -1;
    if(reg_write(a,AR_PHY_ADC_SERIAL_CTL,AR_PHY_SEL_EXTERNAL_RADIO)<0)return -2;
    if(reg_write(a,AR_PHY_ADC_SERIAL_CTL,AR_PHY_SEL_INTERNAL_ADDAC)<0)return -3;
    if(write_ini5(a,titan_modes_9271,titan_modes_9271_count,4)<0)return -4;
    if(write_ini5(a,txhi?titan_txgain_high_9271:titan_txgain_normal_9271,
                  txhi?titan_txgain_high_9271_count:titan_txgain_normal_9271_count,4)<0)return -5;
    if(write_ini2(a,titan_common_9271,titan_common_9271_count)<0)return -6;
    if(reg_rmw(a,AR_DIAG_SW,AR_DIAG_RX_DIS|AR_DIAG_RX_ABORT,0)<0)return -7;
    {u32 v;if(reg_read(a,AR_PCU_MISC_MODE2,&v)<0)return -8;v&=~AR_ADHOC_MCAST_KEYID_ENABLE;v|=AR_PCU_MISC_MODE2_CFP_IGNORE;if(reg_write(a,AR_PCU_MISC_MODE2,v)<0)return -9;}
    if(set_channel_regs(a)<0)return -10;
    return init_chain_masks(a);
}

static int set_rfmode(TitanAr9271Radio *a){return reg_write(a,AR_PHY_MODE,AR_PHY_MODE_DYNAMIC);}

static int set_delta_slope(TitanAr9271Radio *a,int channel)
{
    int freq=2407+channel*5;u32 coef=0x64000000u/(u32)freq;int e;u32 man,exp,c;
    for(e=31;e>0;e--)if((coef>>e)&1)break;
    exp=(u32)(14-(e-(int)COEF_SCALE_S));c=coef+(1u<<(COEF_SCALE_S-exp-1));man=c>>(COEF_SCALE_S-exp);
    if(rmw_field(a,AR_PHY_TIMING3,AR_PHY_TIMING3_DSC_MAN,man)<0 || rmw_field(a,AR_PHY_TIMING3,AR_PHY_TIMING3_DSC_EXP,exp-16)<0)return -1;
    coef=(9u*coef)/10u;
    for(e=31;e>0;e--)if((coef>>e)&1)break;
    exp=(u32)(14-(e-(int)COEF_SCALE_S));c=coef+(1u<<(COEF_SCALE_S-exp-1));man=c>>(COEF_SCALE_S-exp);
    if(rmw_field(a,AR_PHY_HALFGI,AR_PHY_HALFGI_DSC_MAN,man)<0 || rmw_field(a,AR_PHY_HALFGI,AR_PHY_HALFGI_DSC_EXP,exp-16)<0)return -2;
    return 0;
}

static int rf_set_freq(TitanAr9271Radio *a,int channel)
{
    int freq=2407+channel*5;u32 v,tx;
    if(channel==14)freq=2484;
    if(reg_read(a,AR_PHY_SYNTH_CONTROL,&v)<0 || reg_read(a,AR_PHY_CCK_TX_CTRL,&tx)<0)return -1;
    v&=0xc0000000u;
    if(freq==2484)tx|=AR_PHY_CCK_TX_CTRL_JAPAN;else tx&=~AR_PHY_CCK_TX_CTRL_JAPAN;
    if(reg_write(a,AR_PHY_CCK_TX_CTRL,tx)<0)return -2;
    v|=(1u<<29)|(1u<<28)|CHANSEL_2G(freq);
    return reg_write(a,AR_PHY_SYNTH_CONTROL,v);
}

/* ---- EEPROM modal / board values -------------------------------------- */
static u8 eu8(TitanAr9271Radio *a,int o){return a->eeprom[o];}
static u16 ele16(TitanAr9271Radio *a,int o){return le16(a->eeprom+o);}
static u32 ele32(TitanAr9271Radio *a,int o){return le32(a->eeprom+o);}

static int set_gain(TitanAr9271Radio *a)
{
    u32 ad[RMW_BATCH],ss[RMW_BATCH],cc[RMW_BATCH];int n=0,offi;u32 txatt=23;
    ad[n]=AR_PHY_SWITCH_CHAIN_0;ss[n]=ele32(a,EEP_MODAL);cc[n]=0;n++;
    ad[n]=AR_PHY_TIMING_CTRL4_0;ss[n]=SM(eu8(a,EEP_MODAL+22),AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF)|SM(eu8(a,EEP_MODAL+23),AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF);cc[n]=AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF|AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF;n++;
    if(reg_rmw_batch(a,ad,ss,cc,n)<0)return -1;
    if((ele16(a,EEP_OFF_VERSION)&AR5416_EEP_VER_MINOR_MASK)>=AR5416_EEP_MINOR_VER_3)txatt=eu8(a,EEP_MODAL+10);
    for(offi=0;offi<2;offi++){
        u32 off=offi?CHAIN_BLOCK1_OFFSET:0;
        if((ele16(a,EEP_OFF_VERSION)&AR5416_EEP_VER_MINOR_MASK)>=AR5416_EEP_MINOR_VER_3){
            if(rmw_field(a,AR_PHY_GAIN_2GHZ+off,AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,eu8(a,EEP_MODAL+32))<0)return -2;
            if(rmw_field(a,AR_PHY_GAIN_2GHZ+off,AR_PHY_GAIN_2GHZ_XATTEN1_DB,eu8(a,EEP_MODAL+31))<0)return -3;
            if(rmw_field(a,AR_PHY_GAIN_2GHZ+off,AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN,eu8(a,EEP_MODAL+35))<0)return -4;
            if(rmw_field(a,AR_PHY_GAIN_2GHZ+off,AR_PHY_GAIN_2GHZ_XATTEN2_DB,eu8(a,EEP_MODAL+34))<0)return -5;
        }
        if(rmw_field(a,AR_PHY_RXGAIN+off,AR9280_PHY_RXGAIN_TXRX_ATTEN,txatt)<0)return -6;
        if(rmw_field(a,AR_PHY_RXGAIN+off,AR9280_PHY_RXGAIN_TXRX_MARGIN,eu8(a,EEP_MODAL+11))<0)return -7;
    }
    return 0;
}

static int set_ant_div(TitanAr9271Radio *a)
{
    u8 c1=(eu8(a,EEP_MODAL+39)>>4)&0xf,c2=(eu8(a,EEP_MODAL+41)>>4)&0xf;u32 v;
    if(reg_read(a,AR_PHY_MULTICHAIN_GAIN_CTL,&v)<0)return -1;
    v&=~AR_PHY_9285_ANT_DIV_CTL_ALL;
    v|=SM(c1,AR_PHY_9285_ANT_DIV_CTL)|SM(c2,AR_PHY_9285_ANT_DIV_ALT_LNACONF)|SM(c2>>2,AR_PHY_9285_ANT_DIV_MAIN_LNACONF)|SM(c1>>1,AR_PHY_9285_ANT_DIV_ALT_GAINTB)|SM(c1>>2,AR_PHY_9285_ANT_DIV_MAIN_GAINTB);
    if(reg_write(a,AR_PHY_MULTICHAIN_GAIN_CTL,v)<0 || reg_read(a,AR_PHY_MULTICHAIN_GAIN_CTL,&v)<0)return -2;
    if(reg_read(a,AR_PHY_CCK_DETECT,&v)<0)return -3;
    v=(v&~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV)|SM(c1>>3,AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
    if(reg_write(a,AR_PHY_CCK_DETECT,v)<0 || reg_read(a,AR_PHY_CCK_DETECT,&v)<0)return -4;
    return 0;
}

static int analog_bias(TitanAr9271Radio *a)
{
    u8 b77=eu8(a,EEP_MODAL+25),b78=eu8(a,EEP_MODAL+26),b88=eu8(a,EEP_MODAL+36),b90=eu8(a,EEP_MODAL+38);
    u8 ob0=b77&15,ob1=(b77>>4)&15,ob2=b90&15,db1=b78&15,db2=b88&15;
    int ver=eu8(a,EEP_MODAL+37);
    if(ver==1)ob2=ob1; else if(ver==0){ob1=ob0;ob2=ob0;db2=db1;}
    if(rmw_field(a,AR9285_AN_RF2G3,AR9271_AN_RF2G3_OB_cck,ob0)<0)return -1;
    if(rmw_field(a,AR9285_AN_RF2G3,AR9271_AN_RF2G3_OB_psk,ob1)<0)return -2;
    if(rmw_field(a,AR9285_AN_RF2G3,AR9271_AN_RF2G3_OB_qam,ob2)<0)return -3;
    if(rmw_field(a,AR9285_AN_RF2G3,AR9271_AN_RF2G3_DB_1,db1)<0)return -4;
    return rmw_field(a,AR9285_AN_RF2G4,AR9271_AN_RF2G4_DB_2,db2);
}

static int set_board(TitanAr9271Radio *a)
{
    int rev=ele16(a,EEP_OFF_VERSION)&AR5416_EEP_VER_MINOR_MASK;
    if(reg_write(a,AR_PHY_SWITCH_COM,ele32(a,EEP_MODAL+4))<0)return -1;
    if(set_gain(a)<0)return -2;
    if(eu8(a,EEP_MODAL+37)>=3 && set_ant_div(a)<0)return -3;
    if(analog_bias(a)<0)return -4;
    if(rmw_field(a,AR_PHY_SETTLING,AR_PHY_SETTLING_SWITCH,eu8(a,EEP_MODAL+9))<0)return -5;
    if(rmw_field(a,AR_PHY_DESIRED_SZ,AR_PHY_DESIRED_SZ_ADC,eu8(a,EEP_MODAL+12))<0)return -6;
    if(reg_rmw(a,AR_PHY_RF_CTL4,
               SM(eu8(a,EEP_MODAL+15),AR_PHY_RF_CTL4_TX_END_XPAA_OFF)|SM(eu8(a,EEP_MODAL+15),AR_PHY_RF_CTL4_TX_END_XPAB_OFF)|SM(eu8(a,EEP_MODAL+17),AR_PHY_RF_CTL4_FRAME_XPAA_ON)|SM(eu8(a,EEP_MODAL+17),AR_PHY_RF_CTL4_FRAME_XPAB_ON),0)<0)return -7;
    if(rmw_field(a,AR_PHY_RF_CTL3,AR_PHY_TX_END_TO_A2_RX_ON,eu8(a,EEP_MODAL+16))<0)return -8;
    if(rmw_field(a,AR_PHY_CCA,AR9280_PHY_CCA_THRESH62,eu8(a,EEP_MODAL+18))<0)return -9;
    if(rmw_field(a,AR_PHY_EXT_CCA0,AR_PHY_EXT_CCA0_THRESH62,eu8(a,EEP_MODAL+18))<0)return -10;
    if(rev>=(int)AR5416_EEP_MINOR_VER_2){
        if(rmw_field(a,AR_PHY_RF_CTL2,AR_PHY_TX_END_DATA_START,eu8(a,EEP_MODAL+28))<0)return -11;
        if(rmw_field(a,AR_PHY_RF_CTL2,AR_PHY_TX_END_PA_ON,eu8(a,EEP_MODAL+29))<0)return -12;
    }
    return 0;
}

static int spur_mitigate(TitanAr9271Radio *a,int channel)
{
    int i,freq=2407+channel*5;u16 cur; if(channel==14)freq=2484;
    if(reg_rmw(a,AR_PHY_FORCE_CLKEN_CCK,0,AR_PHY_FORCE_CLKEN_CCK_MRC_MUX)<0)return -1;
    for(i=0;i<5;i++){
        cur=ele16(a,EEP_SPURCHANS+i*4);
        if(cur==AR_NO_SPUR)return 0;
        {int bb=(int)(cur/10)+(int)AR_BASE_FREQ_2GHZ-freq;int bound=(int)AR_SPUR_FEEQ_BOUND_HT20;if(bb>-bound && bb<bound)return -2;}
    }
    return 0;
}

/* ---- calibration ------------------------------------------------------- */
static int cl_cal(TitanAr9271Radio *a)
{
    if(reg_rmw(a,AR_PHY_CL_CAL_CTL,AR_PHY_CL_CAL_ENABLE,0)<0)return -1;
    if(reg_rmw(a,AR_PHY_ADC_CTL,0,AR_PHY_ADC_CTL_OFF_PWDADC)<0)return -2;
    if(reg_rmw(a,AR_PHY_AGC_CONTROL,AR_PHY_AGC_CONTROL_FLTR_CAL,0)<0)return -3;
    if(reg_rmw(a,AR_PHY_TPCRG1,AR_PHY_TPCRG1_PD_CAL_ENABLE,0)<0)return -4;
    if(reg_rmw(a,AR_PHY_AGC_CONTROL,AR_PHY_AGC_CONTROL_CAL,0)<0)return -5;
    if(wait_reg(a,AR_PHY_AGC_CONTROL,AR_PHY_AGC_CONTROL_CAL,0,3000)<0)return -6;
    if(reg_rmw(a,AR_PHY_ADC_CTL,AR_PHY_ADC_CTL_OFF_PWDADC,0)<0)return -7;
    if(reg_rmw(a,AR_PHY_CL_CAL_CTL,0,AR_PHY_CL_CAL_ENABLE)<0)return -8;
    return reg_rmw(a,AR_PHY_AGC_CONTROL,0,AR_PHY_AGC_CONTROL_FLTR_CAL);
}

static int pa_cal(TitanAr9271Radio *a)
{
    static const u32 regs[8]={AR9285_AN_TOP3,AR9285_AN_RXTXBB1,AR9285_AN_RF2G1,AR9285_AN_RF2G2,AR9285_AN_TOP2,AR9285_AN_RF2G8,AR9285_AN_RF2G7,AR9285_AN_RF2G3};
    u32 saved[8],ad[RMW_BATCH],ss[RMW_BATCH],cc[RMW_BATCH],v;int n=0,i;
    if(reg_read_multi(a,regs,8,saved)<0)return -1;
#define ADD_RMW(R,S,C) do{ad[n]=(R);ss[n]=(S);cc[n]=(C);if(++n==RMW_BATCH){if(reg_rmw_batch(a,ad,ss,cc,n)<0)return -2;n=0;}}while(0)
    ADD_RMW(AR9285_AN_RF2G6,0,1u<<0);ADD_RMW(0x9808,1u<<27,0);ADD_RMW(AR9285_AN_TOP3,AR9285_AN_TOP3_PWDDAC,0);ADD_RMW(AR9285_AN_RXTXBB1,AR9285_AN_RXTXBB1_PDRXTXBB1,0);
    ADD_RMW(AR9285_AN_RXTXBB1,AR9285_AN_RXTXBB1_PDV2I,0);ADD_RMW(AR9285_AN_RXTXBB1,AR9285_AN_RXTXBB1_PDDACIF,0);ADD_RMW(AR9285_AN_RF2G2,0,AR9285_AN_RF2G2_OFFCAL);ADD_RMW(AR9285_AN_RF2G7,0,AR9285_AN_RF2G7_PWDDB);
    ADD_RMW(AR9285_AN_RF2G1,0,AR9285_AN_RF2G1_ENPACAL);ADD_RMW(AR9285_AN_RF2G1,0,AR9285_AN_RF2G1_PDPADRV1);if(n && reg_rmw_batch(a,ad,ss,cc,n)<0)return -3;
#undef ADD_RMW
    if(reg_write(a,AR9285_AN_TOP2,0xCA0358A0)<0 || rmw_field(a,AR9285_AN_RF2G6,AR9271_AN_RF2G6_OFFS,0)<0)return -4;
    for(i=6;i>0;i--){
        if(reg_read(a,AR9285_AN_RF2G6,&v)<0)return -5;
        v|=1u<<(20+i);
        if(reg_write(a,AR9285_AN_RF2G6,v)<0)return -6;
        v&=~(1u<<(20+i));
        {u32 t;if(reg_read(a,AR9285_AN_RF2G9,&t)<0)return -7;v|=MS(t,AR9285_AN_RXTXBB1_SPARE9)<<(20+i);}if(reg_write(a,AR9285_AN_RF2G6,v)<0)return -8;
    }
    if(reg_rmw(a,AR9285_AN_RF2G6,1u,0)<0 || reg_rmw(a,0x9808,0,1u<<27)<0)return -9;
    {u32 vv[8];for(i=0;i<8;i++)vv[i]=saved[i];if(reg_write_batch(a,regs,vv,8)<0)return -10;}
    return 0;
}

static int load_nf(TitanAr9271Radio *a)
{
    u32 ctl;int i,done=0;
    if(reg_read(a,AR_PHY_AGC_CONTROL,&ctl)<0)return -1;
    if(reg_rmw(a,AR_PHY_CCA,((((u32)(s32)-118)<<1)&0x1ff),0x1ff)<0)return -2;
    if((ctl&AR_PHY_AGC_CONTROL_NF) && reg_rmw(a,AR_PHY_AGC_CONTROL,0,AR_PHY_AGC_CONTROL_NF)<0)return -3;
    if(reg_rmw(a,AR_PHY_AGC_CONTROL,0,AR_PHY_AGC_CONTROL_ENABLE_NF)<0)return -4;
    if(reg_rmw(a,AR_PHY_AGC_CONTROL,0,AR_PHY_AGC_CONTROL_NO_UPDATE_NF)<0)return -5;
    if(reg_rmw(a,AR_PHY_AGC_CONTROL,AR_PHY_AGC_CONTROL_NF,0)<0)return -6;
    /* On Linux this is a very large MMIO poll budget.  On the PS2 every register
       read is a WMI round-trip over 12 Mbit/s USB, so replaying 22k polls can take
       ages.  Keep a bounded hardware-time sample window instead; in normal AR9271
       operation the NF bit clears long before this limit. */
    for(i=0;i<256;i++){
        u32 v;
        if(reg_read(a,AR_PHY_AGC_CONTROL,&v)<0)return -7;
        if(!(v&AR_PHY_AGC_CONTROL_NF)){done=1;break;}
        DelayThread(250);
    }
    if(!done)return -8;
    /* Restore a previously running NF calibration before returning to normal maxCCA power. */
    if(ctl&AR_PHY_AGC_CONTROL_NF){
        if((ctl&AR_PHY_AGC_CONTROL_ENABLE_NF) && reg_rmw(a,AR_PHY_AGC_CONTROL,AR_PHY_AGC_CONTROL_ENABLE_NF,0)<0)return -9;
        if((ctl&AR_PHY_AGC_CONTROL_NO_UPDATE_NF) && reg_rmw(a,AR_PHY_AGC_CONTROL,AR_PHY_AGC_CONTROL_NO_UPDATE_NF,0)<0)return -10;
        if(reg_rmw(a,AR_PHY_AGC_CONTROL,AR_PHY_AGC_CONTROL_NF,0)<0)return -11;
    }
    if(reg_rmw(a,AR_PHY_CCA,((((u32)(s32)-50)<<1)&0x1ff),0x1ff)<0)return -12;
    return 0;
}
static int start_nf(TitanAr9271Radio *a)
{
    if(reg_rmw(a,AR_PHY_AGC_CONTROL,AR_PHY_AGC_CONTROL_ENABLE_NF,0)<0)return -1;
    if(reg_rmw(a,AR_PHY_AGC_CONTROL,0,AR_PHY_AGC_CONTROL_NO_UPDATE_NF)<0)return -2;
    return reg_rmw(a,AR_PHY_AGC_CONTROL,AR_PHY_AGC_CONTROL_NF,0);
}
static int init_cal(TitanAr9271Radio *a)
{
    if(cl_cal(a)<0)return -1;
    if(pa_cal(a)<0)return -2;
    if(load_nf(a)<0)return -3;
    if(start_nf(a)<0)return -4;
    if(rmw_field(a,AR_PHY_TIMING_CTRL4_0,AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX,PER_MAX_LOG_COUNT)<0)return -5;
    if(reg_write(a,AR_PHY_CALMODE,AR_PHY_CALMODE_IQ)<0)return -6;
    return reg_rmw(a,AR_PHY_TIMING_CTRL4_0,AR_PHY_TIMING_CTRL4_DO_CAL,0);
}

/* ---- MAC/RX ------------------------------------------------------------ */
static int reset_opmode(TitanAr9271Radio *a)
{
    u32 id1;u32 ad[8],vv[8];int n=0;
    if(reg_rmw(a,AR_STA_ID1,a->mac_sta_id1|AR_STA_ID1_RTS_USE_DEF|a->sta_id1_defaults,(~AR_STA_ID1_SADH_MASK)&0xffffffffu)<0)return -1;
    ad[n]=AR_STA_ID0;vv[n]=le32(a->macaddr);n++;
    if(reg_read(a,AR_STA_ID1,&id1)<0)return -2;
    id1=(id1&~AR_STA_ID1_SADH_MASK)|((u32)a->macaddr[4]|((u32)a->macaddr[5]<<8));
    ad[n]=AR_STA_ID1;vv[n]=id1;n++;
    ad[n]=AR_BSSMSKL;vv[n]=0xffffffff;n++;ad[n]=AR_BSSMSKU;vv[n]=0xffff;n++;
    ad[n]=AR_DEF_ANTENNA;vv[n]=a->save_def_antenna;n++;ad[n]=AR_BSS_ID0;vv[n]=0;n++;ad[n]=AR_BSS_ID1;vv[n]=0;n++;ad[n]=AR_ISR;vv[n]=0xffffffff;n++;
    if(reg_write_batch(a,ad,vv,n)<0)return -3;
    if(reg_write(a,AR_RSSI_THR,INIT_RSSI_THR)<0)return -4;
    /* Monitor state: no STA/AP indication; ath9k keeps key-search mode enabled. */
    if(reg_rmw(a,AR_CFG,0,AR_CFG_AP_ADHOC_INDICATION)<0)return -5;
    return reg_rmw(a,AR_STA_ID1,AR_STA_ID1_KSRCH_MODE,AR_STA_ID1_STA_AP|AR_STA_ID1_ADHOC);
}

/* V10.14.1 UL1411: reproduce the post-add-interface hardware phase that
 * current Linux ath9k_htc performs immediately after WMI_VAP_CREATE + the
 * local WMI_NODE_CREATE used for pre-association management TX.
 *
 * Linux ath9k_htc_add_interface() calls, in this order:
 *   ath9k_htc_set_mac_bssid_mask(priv, vif);
 *   ath9k_htc_set_opmode(priv);
 * before the subsequent channel reset.  FR1410 skipped that phase and went
 * directly from NODE_CREATE to DISABLE_INTR/DRAIN/STOP/reset.  The fresh
 * successful Linux USBMON control makes this the first structural lifecycle
 * divergence that is both observable and semantically attributable.
 *
 * Single-VIF station semantics:
 *   - hardware MAC = station MAC
 *   - BSSID mask = ff:ff:ff:ff:ff:ff
 *   - opmode = station (clear AP/adhoc indication, enable key-search mode)
 */
int titan_ar9271_radio_post_vif_station(TitanAr9271Radio *a)
{
    u32 id1;
    const u32 rmw_addr[2]={AR_CFG,AR_STA_ID1};
    const u32 rmw_set[2]={0,AR_STA_ID1_KSRCH_MODE};
    const u32 rmw_clr[2]={AR_CFG_AP_ADHOC_INDICATION,
                          AR_STA_ID1_STA_AP|AR_STA_ID1_ADHOC};
    if(!a)return -1;

    /* ath_hw_setbssidmask(common), specialized for one station VIF.
     * Preserve the WMI shape visible in the fresh Linux-success USBMON:
     * WRITE STA_ID0; READ/WRITE STA_ID1; WRITE BSSMSKL/BSSMSKU. */
    if(reg_write(a,AR_STA_ID0,le32(a->macaddr))<0)return -2;
    if(reg_read(a,AR_STA_ID1,&id1)<0)return -3;
    id1=(id1&~AR_STA_ID1_SADH_MASK)|((u32)a->macaddr[4]|((u32)a->macaddr[5]<<8));
    if(reg_write(a,AR_STA_ID1,id1)<0)return -4;
    if(reg_write(a,AR_BSSMSKL,0xffffffffu)<0)return -5;
    if(reg_write(a,AR_BSSMSKU,0x0000ffffu)<0)return -6;

    /* ath9k_hw_set_operating_mode(..., NL80211_IFTYPE_STATION).
     * Linux buffers these two RMWs into one WMI_REG_RMW command. */
    if(reg_rmw_batch(a,rmw_addr,rmw_set,rmw_clr,2)<0)return -7;
    a->opmode=HTC_M_STA;
    return 0;
}

static int interrupt_masks(TitanAr9271Radio *a)
{
    u32 imr=AR_IMR_TXERR|AR_IMR_TXURN|AR_IMR_RXERR|AR_IMR_RXORN|AR_IMR_BCNMISC|AR_IMR_RXINTM|AR_IMR_RXMINTR|AR_IMR_TXOK;
    u32 ad[5]={AR_IMR,AR_IMR_S2,AR_INTR_SYNC_CAUSE,AR_INTR_SYNC_ENABLE,AR_INTR_SYNC_MASK};
    u32 vv[5];a->imrs2_reg|=AR_IMR_S2_GTT;vv[0]=imr;vv[1]=a->imrs2_reg;vv[2]=0xffffffff;vv[3]=AR_INTR_SYNC_DEFAULT;vv[4]=0;
    return reg_write_batch(a,ad,vv,5);
}

static int init_global_settings(TitanAr9271Radio *a)
{
    const u32 clockrate = 44;
    const u32 sifstime = 10;
    const u32 slottime = 9;
    u32 eifs_reg, usec_reg, rx_lat, tx_lat;
    u32 acktimeout = slottime + sifstime + (64 - sifstime - slottime);
    u32 ctstimeout = slottime + sifstime + (48 - sifstime - slottime);
    u32 ad[3], vv[3];

    if (reg_rmw(a, AR_PCU_MISC, AR_PCU_MIC_NEW_LOC_ENA, 0) < 0) return -1;
    if (reg_read(a, AR_D_GBL_IFS_EIFS, &eifs_reg) < 0) return -2;
    if (reg_read(a, AR_USEC, &usec_reg) < 0) return -3;
    rx_lat = MS(usec_reg, AR_USEC_RX_LAT);
    tx_lat = MS(usec_reg, AR_USEC_TX_LAT);

    ad[0] = AR_D_GBL_IFS_SIFS; vv[0] = (sifstime - 2) * clockrate;
    ad[1] = AR_D_GBL_IFS_SLOT; vv[1] = slottime * clockrate;
    ad[2] = AR_D_GBL_IFS_EIFS; vv[2] = (eifs_reg / clockrate) * clockrate;
    if (reg_write_batch(a, ad, vv, 3) < 0) return -4;
    if (rmw_field(a, AR_TIME_OUT, AR_TIME_OUT_ACK, acktimeout * clockrate) < 0) return -5;
    if (rmw_field(a, AR_TIME_OUT, AR_TIME_OUT_CTS, ctstimeout * clockrate) < 0) return -6;
    if (reg_rmw(a, AR_USEC,
                (clockrate - 1) | SM(rx_lat, AR_USEC_RX_LAT) | SM(tx_lat, AR_USEC_TX_LAT),
                AR_USEC_TX_LAT | AR_USEC_RX_LAT | AR_USEC_USEC) < 0) return -7;
    return 0;
}

static int init_qos(TitanAr9271Radio *a)
{
    u32 ad[8], vv[8];
    ad[0]=AR_MIC_QOS_CONTROL; vv[0]=0x100aau;
    ad[1]=AR_MIC_QOS_SELECT; vv[1]=0x3210u;
    ad[2]=AR_QOS_NO_ACK; vv[2]=SM(2,AR_QOS_NO_ACK_TWO_BIT)|SM(5,AR_QOS_NO_ACK_BIT_OFF)|SM(0,AR_QOS_NO_ACK_BYTE_OFF);
    ad[3]=AR_TXOP_X; vv[3]=AR_TXOP_X_VAL;
    ad[4]=AR_TXOP_0_3; vv[4]=0xffffffffu;
    ad[5]=AR_TXOP_4_7; vv[5]=0xffffffffu;
    ad[6]=AR_TXOP_8_11; vv[6]=0xffffffffu;
    ad[7]=AR_TXOP_12_15; vv[7]=0xffffffffu;
    return reg_write_batch(a, ad, vv, 8);
}

static int dma_init(TitanAr9271Radio *a)
{
    if(reg_rmw(a,AR_STA_ID1,AR_STA_ID1_PRESERVE_SEQNUM,0)<0)return -1;
    if(reg_rmw(a,AR_AHB_MODE,AR_AHB_PREFETCH_RD_EN,0)<0)return -2;
    if(reg_rmw(a,AR_TXCFG,AR_TXCFG_DMASZ_128B,AR_TXCFG_DMASZ_MASK)<0)return -3;
    if(rmw_field(a,AR_TXCFG,AR_FTRIG,AR_FTRIG_256B>>AR_FTRIG_S)<0)return -4;
    if(reg_rmw(a,AR_RXCFG,AR_RXCFG_DMASZ_128B,AR_RXCFG_DMASZ_MASK)<0)return -5;
    if(reg_write(a,AR_RXFIFO_CFG,0x200)<0 || reg_write(a,AR_OBS,8)<0)return -6;
    if(rmw_field(a,AR_RIMT,AR_RIMT_LAST,250)<0 || rmw_field(a,AR_RIMT,AR_RIMT_FIRST,700)<0)return -7;
    return 0;
}

static int ani_restart(TitanAr9271Radio *a)
{
    u32 ad[4]={AR_PHY_ERR_1,AR_PHY_ERR_2,AR_PHY_ERR_MASK_1,AR_PHY_ERR_MASK_2};
    u32 vv[4]={0,0,AR_PHY_ERR_OFDM_TIMING,AR_PHY_ERR_CCK_TIMING};
    return reg_write_batch(a,ad,vv,4);
}
static int mib_enable(TitanAr9271Radio *a)
{
    u32 rr[5]={AR_RTS_OK,AR_RTS_FAIL,AR_ACK_FAIL,AR_FCS_FAIL,AR_BEACON_CNT},vv[5];u32 ad[5],wr[5];
    if(reg_read_multi(a,rr,5,vv)<0)return -1;
    ad[0]=AR_FILT_OFDM;wr[0]=0;ad[1]=AR_FILT_CCK;wr[1]=0;ad[2]=AR_MIBC;wr[2]=(~(AR_MIBC_COW|AR_MIBC_FMC|AR_MIBC_CMC|AR_MIBC_MCS))&0xf;ad[3]=AR_PHY_ERR_MASK_1;wr[3]=AR_PHY_ERR_OFDM_TIMING;ad[4]=AR_PHY_ERR_MASK_2;wr[4]=AR_PHY_ERR_CCK_TIMING;
    return reg_write_batch(a,ad,wr,5);
}
static int set_rx_filter(TitanAr9271Radio *a,u32 bits)
{
    u32 ad[2]={AR_RX_FILTER,AR_PHY_ERR},vv[2]={bits,0};
    if(reg_write_batch(a,ad,vv,2)<0)return -1;
    return reg_rmw(a,AR_RXCFG,0,AR_RXCFG_ZLFDMA);
}
static int host_rx_initial(TitanAr9271Radio *a)
{
    /* PCAP: ath9k_htc immediately after WMI_START_RECV, before VAP_CREATE,
       programs UCAST|MCAST|BCAST|MYBEACON = 0x0207. */
    u32 flags=ATH9K_RX_FILTER_UCAST|ATH9K_RX_FILTER_MCAST|ATH9K_RX_FILTER_BCAST|
              ATH9K_RX_FILTER_MYBEACON;
    if(reg_write(a,AR_CR,AR_CR_RXE)<0)return -1;
    if(set_rx_filter(a,flags)<0)return -2;
    if(reg_write(a,AR_MCAST_FIL0,0xffffffff)<0 || reg_write(a,AR_MCAST_FIL1,0xffffffff)<0)return -3;
    if(mib_enable(a)<0 || ani_restart(a)<0)return -4;
    return reg_rmw(a,AR_DIAG_SW,0,AR_DIAG_RX_DIS|AR_DIAG_RX_ABORT);
}

static int host_rx_station(TitanAr9271Radio *a)
{
    /* V10.10 LINUX-TRACE-1TO1: the successful Linux trace programs 0x1607 on
       the final channel reset after the station VAP/local node already exist.
       The extra bits accept compressed/uncompressed BlockAck/BAR traffic. */
    u32 flags=ATH9K_RX_FILTER_UCAST|ATH9K_RX_FILTER_MCAST|ATH9K_RX_FILTER_BCAST|
              ATH9K_RX_FILTER_MYBEACON|ATH9K_RX_FILTER_COMP_BAR|
              ATH9K_RX_FILTER_UNCOMP_BA_BAR;
    if(reg_write(a,AR_CR,AR_CR_RXE)<0)return -1;
    if(set_rx_filter(a,flags)<0)return -2;
    if(reg_write(a,AR_MCAST_FIL0,0xffffffff)<0 || reg_write(a,AR_MCAST_FIL1,0xffffffff)<0)return -3;
    if(mib_enable(a)<0 || ani_restart(a)<0)return -4;
    return reg_rmw(a,AR_DIAG_SW,0,AR_DIAG_RX_DIS|AR_DIAG_RX_ABORT);
}

static int host_rx_monitor(TitanAr9271Radio *a)
{
    u32 flags=ATH9K_RX_FILTER_UCAST|ATH9K_RX_FILTER_MCAST|ATH9K_RX_FILTER_BCAST|
              ATH9K_RX_FILTER_PROM|ATH9K_RX_FILTER_CONTROL|ATH9K_RX_FILTER_BEACON|
              ATH9K_RX_FILTER_PSPOLL|ATH9K_RX_FILTER_MCAST_BCAST_ALL;
    if(reg_write(a,AR_CR,AR_CR_RXE)<0)return -1;
    if(set_rx_filter(a,flags)<0)return -2;
    if(reg_write(a,AR_MCAST_FIL0,0xffffffff)<0 || reg_write(a,AR_MCAST_FIL1,0xffffffff)<0)return -3;
    if(mib_enable(a)<0 || ani_restart(a)<0)return -4;
    return reg_rmw(a,AR_DIAG_SW,0,AR_DIAG_RX_DIS|AR_DIAG_RX_ABORT);
}

static int reset_tail(TitanAr9271Radio *a)
{
    if(reg_write(a,AR_CFG_LED,a->save_led_state|AR_CFG_SCLK_32KHZ)<0)return -1;
    return reg_write(a,AR_CFG,AR_CFG_SWRB|AR_CFG_SWTB);
}


/* ---- bring-up pieces that ath9k runs before/inside ath9k_hw_reset ------ */
static int init_mfp(TitanAr9271Radio *a)
{
    /* AR9271 is on the 9280_20+ path: mask mutable FC bits from CCMP AAD. */
    return rmw_field(a, AR_AES_MUTE_MASK1, AR_AES_MUTE_MASK1_FC_MGMT,
                     AR_AES_MUTE_MASK1_FC_MGMT_VAL);
}

static int gpio_set(TitanAr9271Radio *a, unsigned int gpio, int val)
{
    u32 hwval;
    if (gpio >= 16) return -1;
    /* AR9271 GPIO output polarity is inverted in ath9k. */
    hwval = val ? 0u : 1u;
    return reg_rmw(a, AR_GPIO_IN_OUT, hwval << gpio, 1u << gpio);
}

static int led_init(TitanAr9271Radio *a)
{
    const unsigned int gpio = ATH_LED_PIN_9271;
    const unsigned int oe_shift = gpio << 1;
    const unsigned int mux_shift = (gpio % 6u) * 5u;
    if (reg_rmw(a, AR_GPIO_OE_OUT,
                AR_GPIO_OE_OUT_DRV_ALL << oe_shift,
                AR_GPIO_OE_OUT_DRV << oe_shift) < 0) return -1;
    if (reg_rmw(a, AR_GPIO_OUTPUT_MUX3,
                AR_GPIO_OUTPUT_MUX_AS_OUTPUT << mux_shift,
                0x1fu << mux_shift) < 0) return -2;
    return gpio_set(a, gpio, 1); /* off, active-low */
}

static int key_cache_clear(TitanAr9271Radio *a)
{
    u32 ad[8], vv[8];
    int i, j;
    for (i = 0; i < (int)AR_KEYTABLE_SIZE; ++i) {
        u32 base = AR_KEYTABLE(i);
        for (j = 0; j < 8; ++j) { ad[j] = base + (u32)(j * 4); vv[j] = 0; }
        vv[5] = AR_KEYTABLE_TYPE_CLR;
        if (reg_write_batch(a, ad, vv, 8) < 0) return -1;
    }
    return 0;
}

static int reset_one_txq(TitanAr9271Radio *a, int q, int qtype)
{
    u32 cwmin = (qtype == TXQ_BEACON) ? 1u : INIT_CWMIN;
    u32 cwmax = (qtype == TXQ_BEACON) ? 1u : INIT_CWMAX;
    u32 aifs  = (qtype == TXQ_BEACON) ? 1u : INIT_AIFS;
    u32 ad[5], vv[5];
    ad[0] = AR_DLCL_IFS(q);
    vv[0] = SM(cwmin, AR_D_LCL_IFS_CWMIN) | SM(cwmax, AR_D_LCL_IFS_CWMAX) |
            SM(aifs, AR_D_LCL_IFS_AIFS);
    ad[1] = AR_DRETRY_LIMIT(q);
    vv[1] = SM(INIT_SSH_RETRY, AR_D_RETRY_LIMIT_STA_SH) |
            SM(INIT_SLG_RETRY, AR_D_RETRY_LIMIT_STA_LG) |
            SM(INIT_SH_RETRY, AR_D_RETRY_LIMIT_FR_SH);
    ad[2] = AR_QMISC(q); vv[2] = AR_Q_MISC_DCU_EARLY_TERM_REQ;
    ad[3] = AR_DMISC(q); vv[3] = AR_D_MISC_CW_BKOFF_EN | AR_D_MISC_FRAG_WAIT_EN | 0x2u;
    ad[4] = AR_DCHNTIME(q); vv[4] = 0;
    if (reg_write_batch(a, ad, vv, 5) < 0) return -1;
    if (qtype == TXQ_BEACON) {
        if (reg_rmw(a, AR_QMISC(q),
                    AR_Q_MISC_FSP_DBA_GATED | AR_Q_MISC_BEACON_USE | AR_Q_MISC_CBR_INCR_DIS1,
                    0) < 0) return -2;
        if (reg_rmw(a, AR_DMISC(q),
                    (AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL << AR_D_MISC_ARB_LOCKOUT_CNTRL_S) |
                    AR_D_MISC_BEACON_USE | AR_D_MISC_POST_FR_BKOFF_DIS, 0) < 0) return -3;
    } else if (qtype == TXQ_CAB) {
        if (reg_rmw(a, AR_QMISC(q),
                    AR_Q_MISC_FSP_DBA_GATED | AR_Q_MISC_CBR_INCR_DIS1 | AR_Q_MISC_CBR_INCR_DIS0,
                    0) < 0) return -4;
        if (reg_rmw(a, AR_DMISC(q),
                    AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL << AR_D_MISC_ARB_LOCKOUT_CNTRL_S, 0) < 0) return -5;
    }
    return 0;
}

static int init_tx_queues(TitanAr9271Radio *a)
{
    u32 ad[10], vv[10];
    u32 txdesc_mask = (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<8);
    int i;
    for (i = 0; i < 10; ++i) { ad[i] = AR_DQCUMASK(i); vv[i] = 1u << i; }
    if (reg_write_batch(a, ad, vv, 10) < 0) return -1;
    if (reset_one_txq(a, 0, TXQ_DATA) < 0) return -2;
    if (reset_one_txq(a, 1, TXQ_DATA) < 0) return -3;
    if (reset_one_txq(a, 2, TXQ_DATA) < 0) return -4;
    if (reset_one_txq(a, 3, TXQ_DATA) < 0) return -5;
    if (reset_one_txq(a, 8, TXQ_CAB) < 0) return -6;
    if (reset_one_txq(a, 9, TXQ_BEACON) < 0) return -7;
    /* HTC data/CAB queues request TXDESC + TXEOL interrupts. */
    if (reg_write(a, AR_IMR_S0, SM(txdesc_mask, AR_IMR_S0_QCU_TXDESC)) < 0) return -8;
    if (reg_write(a, AR_IMR_S1, SM(txdesc_mask, AR_IMR_S1_QCU_TXEOL)) < 0) return -9;
    return 0;
}

static int reset_body(TitanAr9271Radio *a,int channel)
{
    u32 dummy;
    if(process_ini(a)<0)return -1;
    if(set_rfmode(a)<0)return -2;
    if(init_mfp(a)<0)return -3;
    if(set_delta_slope(a,channel)<0)return -4;
    if(spur_mitigate(a,channel)<0)return -5;
    if(set_board(a)<0)return -6;
    if(reset_opmode(a)<0)return -7;
    if(rf_set_freq(a,channel)<0)return -8;
    if(init_tx_queues(a)<0)return -9;
    if(interrupt_masks(a)<0)return -10;
    /* ANI cache: these are intentionally read-only snapshots of the INI baseline. */
    if(reg_read(a,AR_PHY_SFCORR,&dummy)<0 || reg_read(a,AR_PHY_SFCORR_LOW,&dummy)<0 ||
       reg_read(a,AR_PHY_SFCORR_EXT,&dummy)<0 || reg_read(a,AR_PHY_FIND_SIG,&dummy)<0 ||
       reg_read(a,AR_PHY_FIND_SIG_LOW,&dummy)<0 || reg_read(a,AR_PHY_TIMING5,&dummy)<0 ||
       reg_read(a,AR_PHY_EXT_CCA,&dummy)<0)return -11;
    if(init_qos(a)<0)return -12;
    if(init_global_settings(a)<0)return -13;
    if(dma_init(a)<0)return -14;
    if(reg_read(a,AR_PHY_RX_DELAY,&dummy)<0 || reg_write(a,AR_PHY_ACTIVE,AR_PHY_ACTIVE_EN)<0)return -15;
    if(init_cal(a)<0)return -16;
    return reset_tail(a);
}

static int rf_claim(TitanAr9271Radio *a)
{
    u32 ad[9],vv[9],v;int i;u8 rev,major;
    if(reg_write(a,AR_PHY(0),7)<0)return -1;
    ad[0]=AR_PHY(0x36);vv[0]=0x00007058;for(i=0;i<8;i++){ad[i+1]=AR_PHY(0x20);vv[i+1]=0x00010000;}
    if(reg_write_batch(a,ad,vv,9)<0)return -2;
    if(reg_read(a,AR_PHY(256),&v)<0)return -3;
    rev=(u8)((v>>24)&0xff);rev=(u8)(((rev&0xf0)>>4)|((rev&0x0f)<<4));
    {u8 x=rev,y=0;for(i=0;i<8;i++){y=(u8)((y<<1)|(x&1));x>>=1;}rev=y;}
    major=rev&AR_RADIO_SREV_MAJOR;if(major==0)major=AR_RAD5133_SREV_MAJOR;
    if(major!=AR_RAD5133_SREV_MAJOR && major!=AR_RAD5122_SREV_MAJOR && major!=AR_RAD2133_SREV_MAJOR && major!=AR_RAD2122_SREV_MAJOR)return -4;
    return 0;
}

int titan_ar9271_radio_init(TitanAr9271Radio *a,const TitanRadioIo *io)
{
    u8 mode[2]={0,MODE_11NG};int rn=0,rc;
#define INIT_STEP(ST, EXPR) do { \
    fr_stage(a,(u8)(ST)); a->init_rc=0; \
    rc=(EXPR); \
    if(rc<0){a->init_rc=rc;return rc;} \
} while(0)
    memset(a,0,sizeof(*a));a->io=*io;a->htc_reset_init=1;a->rxchainmask=1;a->txchainmask=1;a->sta_id1_defaults=AR_STA_ID1_DEFAULTS;
    INIT_STEP(1,read_revision(a));
    INIT_STEP(2,set_reset(a,ATH9K_RESET_POWER_ON));
    INIT_STEP(3,power_awake(a));
    fr_stage(a,4);
    rc=reg_read(a,AR_PHY_CHIP_ID,&a->phy_rev);
    if(rc<0){a->init_rc=-104;return -104;}
    INIT_STEP(5,rf_claim(a));
    INIT_STEP(6,eeprom_init(a));

    /* Preserve V10.13.0 behavior for this diagnostic build.  The purpose of
     * B1311 is to make the first failing live stage and raw WMI/register
     * operation unambiguous before changing another radio algorithm. */
    fr_stage(a,7); rc=ani_restart(a); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,8); rc=mib_enable(a); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,9); rc=key_cache_clear(a); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,10); rc=set_reset(a,ATH9K_RESET_COLD); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,11); rc=init_pll(a,1); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,12); rc=led_init(a); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,13); rc=cmd(a,WMI_FLUSH_RECV_CMDID,NULL,0,NULL,0,&rn); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,14); rc=reset_begin(a,1); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,15); rc=reset_body(a,1); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,16); rc=txpower_apply(a,1,40); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,17); rc=cmd(a,WMI_SET_MODE_CMDID,mode,2,NULL,0,&rn); if(rc<0){a->init_rc=rc;return rc;}
    fr_stage(a,18); rc=cmd(a,WMI_ATH_INIT_CMDID,NULL,0,NULL,0,&rn); if(rc<0){a->init_rc=rc;return rc;}
    a->current_channel=1;
    fr_stage(a,19);a->init_rc=0;
#undef INIT_STEP
    return 0;
}

/* Reproduce the successful Linux ath9k_htc restart phase captured immediately
 * before station VAP_CREATE/NODE_CREATE.  This is intentionally distinct from
 * the later set-channel reset: Linux does
 *   DISABLE/DRAIN/STOP -> FLUSH -> reset -> SET_MODE -> ATH_INIT -> START_RECV
 *   -> pre-VAP RX(0x0207) -> TARGET_IC_UPDATE -> VAP/NODE creation,
 * and only then performs the final channel reset used for association. */
int titan_ar9271_radio_restart_station_base(TitanAr9271Radio *a, int channel)
{
    int rn=0;
    u8 mode[2]={0,MODE_11NG};
    u8 cap[8];
    if(!a || channel<1 || channel>13)return -1;
    a->opmode=HTC_M_STA;
    if(cmd(a,WMI_DISABLE_INTR_CMDID,NULL,0,NULL,0,&rn)<0)return -2;
    if(cmd(a,WMI_DRAIN_TXQ_ALL_CMDID,NULL,0,NULL,0,&rn)<0)return -3;
    if(cmd(a,WMI_STOP_RECV_CMDID,NULL,0,NULL,0,&rn)<0)return -4;
    if(cmd(a,WMI_FLUSH_RECV_CMDID,NULL,0,NULL,0,&rn)<0)return -5;
    if(reset_begin(a,channel)<0)return -6;
    if(reset_body(a,channel)<0)return -7;
    if(txpower_apply(a,channel,40)<0)return -13;
    if(cmd(a,WMI_SET_MODE_CMDID,mode,2,NULL,0,&rn)<0)return -8;
    if(cmd(a,WMI_ATH_INIT_CMDID,NULL,0,NULL,0,&rn)<0)return -9;
    if(cmd(a,WMI_START_RECV_CMDID,NULL,0,NULL,0,&rn)<0)return -10;
    if(host_rx_initial(a)<0)return -11;
    memset(cap,0,sizeof(cap));pbe32(cap,0xffff);cap[4]=0xff;cap[5]=0;cap[6]=a->txchainmask;
    if(cmd(a,WMI_TARGET_IC_UPDATE,cap,8,NULL,0,&rn)<0)return -12;
    a->current_channel=(u32)channel;
    return 0;
}

int titan_ar9271_radio_start_monitor(TitanAr9271Radio *a)
{
    u8 vap[12],node[22],cap[8];int rn=0;

    /* ath9k_htc_start(): target RX first, while HW is still in the normal pre-monitor state. */
    if(cmd(a,WMI_START_RECV_CMDID,NULL,0,NULL,0,&rn)<0)return -1;
    if(host_rx_initial(a)<0)return -2;
    memset(cap,0,sizeof(cap));pbe32(cap,0xffff);cap[4]=0xff;cap[5]=0;cap[6]=a->txchainmask;
    if(cmd(a,WMI_TARGET_IC_UPDATE,cap,8,NULL,0,&rn)<0)return -3;

    /* mac80211 monitor vif creation, matching ath9k_htc_add_monitor_interface. */
    if(gpio_set(a,ATH_LED_PIN_9271,0)<0)return -4;
    memset(vap,0,sizeof(vap));
    vap[0]=0; vap[1]=HTC_M_MONITOR; memcpy(vap+2,a->macaddr,6); vap[8]=0; pbe16(vap+9,0); vap[11]=0;
    if(cmd(a,WMI_VAP_CREATE_CMDID,vap,12,NULL,0,&rn)<0)return -5;
    memset(node,0,sizeof(node));
    memcpy(node,a->macaddr,6); /* bssid remains 00:00:00:00:00:00 */
    node[12]=0; node[13]=0; node[14]=1; pbe16(node+15,0); pbe16(node+17,0); pbe16(node+19,0xffff); node[21]=0;
    if(cmd(a,WMI_NODE_CREATE_CMDID,node,22,NULL,0,&rn)<0)return -6;

    /* ath9k_htc_set_channel() literal ordering for the initial monitor tune. */
    if(cmd(a,WMI_DISABLE_INTR_CMDID,NULL,0,NULL,0,&rn)<0)return -7;
    if(cmd(a,WMI_DRAIN_TXQ_ALL_CMDID,NULL,0,NULL,0,&rn)<0)return -8;
    if(cmd(a,WMI_STOP_RECV_CMDID,NULL,0,NULL,0,&rn)<0)return -9;

    /* Full ath9k_hw_reset path. Do not use the FASTSCAN approximation here. */
    if(reset_begin(a,1)<0)return -10;
    if(reset_body(a,1)<0)return -11;
    if(txpower_apply(a,1,40)<0)return -17;

    if(cmd(a,WMI_START_RECV_CMDID,NULL,0,NULL,0,&rn)<0)return -12;
    if(host_rx_monitor(a)<0)return -13;
    {u8 mode[2]={0,MODE_11NG};if(cmd(a,WMI_SET_MODE_CMDID,mode,2,NULL,0,&rn)<0)return -14;}
    if(cmd(a,WMI_ENABLE_INTR_CMDID,NULL,0,NULL,0,&rn)<0)return -15;

    /* Equivalent of the subsequent configure_filter() after monitor state becomes active. */
    if(host_rx_monitor(a)<0)return -16;
    a->current_channel=1;
    return 0;
}

int titan_ar9271_radio_set_channel(TitanAr9271Radio *a,int channel)
{
    int rn=0;
    if(channel<1 || channel>13)return -1;
    if(cmd(a,WMI_DISABLE_INTR_CMDID,NULL,0,NULL,0,&rn)<0)return -2;
    if(cmd(a,WMI_DRAIN_TXQ_ALL_CMDID,NULL,0,NULL,0,&rn)<0)return -3;
    if(cmd(a,WMI_STOP_RECV_CMDID,NULL,0,NULL,0,&rn)<0)return -4;

    /* Reliability-first port of ath9k_htc_set_channel: full reset on each hop. */
    if(reset_begin(a,channel)<0)return -5;
    if(reset_body(a,channel)<0)return -6;
    if(txpower_apply(a,channel,40)<0)return -11;
    if(cmd(a,WMI_START_RECV_CMDID,NULL,0,NULL,0,&rn)<0)return -7;
    if(host_rx_monitor(a)<0)return -8;
    {u8 mode[2]={0,MODE_11NG};if(cmd(a,WMI_SET_MODE_CMDID,mode,2,NULL,0,&rn)<0)return -9;}
    if(cmd(a,WMI_ENABLE_INTR_CMDID,NULL,0,NULL,0,&rn)<0)return -10;
    a->current_channel=(u32)channel;
    return 0;
}

int titan_ar9271_radio_rx_kick(TitanAr9271Radio *a)
{
    int rn = 0;
    if (!a) return -1;
    if (cmd(a, WMI_DISABLE_INTR_CMDID, NULL, 0, NULL, 0, &rn) < 0) return -2;
    if (cmd(a, WMI_STOP_RECV_CMDID, NULL, 0, NULL, 0, &rn) < 0) return -3;
    if (reg_rmw(a, AR_DIAG_SW, 0, AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT) < 0) return -4;
    if (cmd(a, WMI_START_RECV_CMDID, NULL, 0, NULL, 0, &rn) < 0) return -5;
    if (host_rx_monitor(a) < 0) return -6;
    if (cmd(a, WMI_ENABLE_INTR_CMDID, NULL, 0, NULL, 0, &rn) < 0) return -7;
    return 0;
}


/* Station-mode transition used by the standalone online tester.
 * This mirrors the ath9k_htc channel/start ordering but deliberately keeps
 * power-save/HT disabled until the WPA2 + IPv4 proof is complete. */
int titan_ar9271_radio_start_station(TitanAr9271Radio *a, int channel)
{
    int rn = 0;
    u8 mode[2] = {0, MODE_11NG};
    if (!a || channel < 1 || channel > 13) return -1;
    a->opmode = HTC_M_STA;
    if (cmd(a, WMI_DISABLE_INTR_CMDID, NULL, 0, NULL, 0, &rn) < 0) return -2;
    if (cmd(a, WMI_DRAIN_TXQ_ALL_CMDID, NULL, 0, NULL, 0, &rn) < 0) return -3;
    if (cmd(a, WMI_STOP_RECV_CMDID, NULL, 0, NULL, 0, &rn) < 0) return -4;
    if (reset_begin(a, channel) < 0) return -5;
    if (reset_body(a, channel) < 0) return -6;
    if (txpower_apply(a, channel, 40) < 0) return -11;
    if (cmd(a, WMI_START_RECV_CMDID, NULL, 0, NULL, 0, &rn) < 0) return -7;
    if (host_rx_station(a) < 0) return -8;
    if (cmd(a, WMI_SET_MODE_CMDID, mode, 2, NULL, 0, &rn) < 0) return -9;
    if (cmd(a, WMI_ENABLE_INTR_CMDID, NULL, 0, NULL, 0, &rn) < 0) return -10;
    a->current_channel = (u32)channel;
    return 0;
}

int titan_ar9271_radio_set_promisc(TitanAr9271Radio *a)
{
    if(!a)return -1;
    return host_rx_monitor(a);
}

int titan_ar9271_radio_set_station_rx(TitanAr9271Radio *a)
{
    if(!a)return -1;
    return host_rx_station(a);
}

int titan_ar9271_radio_set_assoc(TitanAr9271Radio *a, const u8 bssid[6], u16 aid)
{
    u32 lo, hi;
    if (!a || !bssid) return -1;
    lo = le32(bssid);
    hi = (u32)le16(bssid + 4) | (((u32)aid & 0x3fffu) << AR_BSS_ID1_AID_S);
    if (reg_write(a, AR_BSS_ID0, lo) < 0) return -2;
    if (reg_write(a, AR_BSS_ID1, hi) < 0) return -3;
    /* Re-apply the station filter after the BSSID/AID becomes valid so the
       hardware can use MYBEACON filtering like the Linux station path. */
    if (host_rx_station(a) < 0) return -4;
    return 0;
}

/* Port of ath_hw_set_keycache_entry()/ath_hw_keysetmac() for AES-CCMP.
 * AR9xxx key-cache memory requires the exact paired write ordering below. */
int titan_ar9271_radio_set_ccmp_key(TitanAr9271Radio *a, u8 entry,
                                    const u8 key[16], const u8 *mac)
{
    u32 key0, key1, key2, key3, key4, mac_lo = 0, mac_hi = 0;
    u32 unicast = AR_KEYTABLE_VALID;
    if (!a || !key || entry >= AR_KEYTABLE_SIZE) return -1;

    key0 = le32(key + 0);
    key1 = (u32)le16(key + 4);
    key2 = le32(key + 6);
    key3 = (u32)le16(key + 10);
    key4 = le32(key + 12);

    if (mac) {
        mac_lo = le32(mac);
        mac_hi = (u32)le16(mac + 4);
        if (mac[0] & 1u) unicast = 0; /* multicast/group key */
        mac_lo >>= 1;
        mac_lo |= (mac_hi & 1u) << 31;
        mac_hi >>= 1;
    } else {
        /* Linux ath_hw_keysetmac() semantics: a NULL MAC leaves MAC0/MAC1
         * at zero while AR_KEYTABLE_VALID remains set. In STA mode this is
         * how the default/group key slots (0..3) are programmed. */
        mac_lo = 0;
        mac_hi = 0;
    }

    /* Match Linux ath_hw_set_keycache_entry()/ath_hw_keysetmac(): the key
       words + type are one buffered register-write group, then MAC0/MAC1 are
       written as the second group.  WMI_REG_WRITE accepts an array of register
       writes, which is the target-side equivalent of ath9k's buffered flush. */
    {
        const u32 ad[6] = {
            AR_KEYTABLE_KEY0(entry), AR_KEYTABLE_KEY1(entry),
            AR_KEYTABLE_KEY2(entry), AR_KEYTABLE_KEY3(entry),
            AR_KEYTABLE_KEY4(entry), AR_KEYTABLE_TYPE(entry)
        };
        const u32 vv[6] = { key0, key1, key2, key3, key4, AR_KEYTABLE_TYPE_CCM };
        if (reg_write_batch(a, ad, vv, 6) < 0) return -2;
    }
    {
        const u32 ad[2] = { AR_KEYTABLE_MAC0(entry), AR_KEYTABLE_MAC1(entry) };
        const u32 vv[2] = { mac_lo, mac_hi | unicast };
        if (reg_write_batch(a, ad, vv, 2) < 0) return -3;
    }
    return 0;
}


/* Receive-side TKIP group key using the same AR9xxx key-cache layout as
 * Linux ath9k. The caller provides the local-oriented TKIP layout
 * TK || local-TX-MIC || local-RX-MIC. The hardware has AR_PCU_MIC_NEW_LOC_ENA enabled by
 * init_global_settings(), so the temporal key occupies entry N and the
 * combined Michael MIC entry occupies N+64. For a STA group key, Linux uses
 * the RX MIC key for both halves because the key is receive-only on the air. */
int titan_ar9271_radio_set_tkip_group_key(TitanAr9271Radio *a, u8 entry,
                                          const u8 key[32])
{
    const u8 *tk, *rxmic;
    u32 key0,key1,key2,key3,key4,mic0,mic1,mic2,mic3,mic4;
    u8 micentry;
    if(!a||!key)return -1;
    if(entry>=64u || (u16)entry+64u>=AR_KEYTABLE_SIZE)return -2;
    tk=key;rxmic=key+24;micentry=(u8)(entry+64u);
    key0=le32(tk+0);key1=(u32)le16(tk+4);key2=le32(tk+6);
    key3=(u32)le16(tk+10);key4=le32(tk+12);
    mic0=le32(rxmic+0);mic2=le32(rxmic+4);
    mic1=(u32)le16(rxmic+2);mic3=(u32)le16(rxmic+0);mic4=le32(rxmic+4);

    /* Exact batching observed after M4 in the successful Linux cold-start:
     * seq3937..3942 write TKIP KEY0..TYPE as six individual WMI_REG_WRITE
     * commands; seq3943 writes MAC0/MAC1 together; seq3944 writes the six MIC
     * words; seq3945 writes MIC MAC0/MAC1 and restores TKIP KEY0/KEY1 in one
     * four-register batch.  Values remain live PTK/GTK-derived material. */
    { const u32 ad[1]={AR_KEYTABLE_KEY0(entry)}; const u32 vv[1]={~key0};
      if(reg_write_batch(a,ad,vv,1)<0)return -3; }
    { const u32 ad[1]={AR_KEYTABLE_KEY1(entry)}; const u32 vv[1]={~key1};
      if(reg_write_batch(a,ad,vv,1)<0)return -4; }
    { const u32 ad[1]={AR_KEYTABLE_KEY2(entry)}; const u32 vv[1]={key2};
      if(reg_write_batch(a,ad,vv,1)<0)return -5; }
    { const u32 ad[1]={AR_KEYTABLE_KEY3(entry)}; const u32 vv[1]={key3};
      if(reg_write_batch(a,ad,vv,1)<0)return -6; }
    { const u32 ad[1]={AR_KEYTABLE_KEY4(entry)}; const u32 vv[1]={key4};
      if(reg_write_batch(a,ad,vv,1)<0)return -7; }
    { const u32 ad[1]={AR_KEYTABLE_TYPE(entry)}; const u32 vv[1]={AR_KEYTABLE_TYPE_TKIP};
      if(reg_write_batch(a,ad,vv,1)<0)return -8; }
    { const u32 ad[2]={AR_KEYTABLE_MAC0(entry),AR_KEYTABLE_MAC1(entry)};
      const u32 vv[2]={0,AR_KEYTABLE_VALID};
      if(reg_write_batch(a,ad,vv,2)<0)return -9; }
    { const u32 ad[6]={AR_KEYTABLE_KEY0(micentry),AR_KEYTABLE_KEY1(micentry),
                       AR_KEYTABLE_KEY2(micentry),AR_KEYTABLE_KEY3(micentry),
                       AR_KEYTABLE_KEY4(micentry),AR_KEYTABLE_TYPE(micentry)};
      const u32 vv[6]={mic0,mic1,mic2,mic3,mic4,AR_KEYTABLE_TYPE_CLR};
      if(reg_write_batch(a,ad,vv,6)<0)return -10; }
    { const u32 ad[4]={AR_KEYTABLE_MAC0(micentry),AR_KEYTABLE_MAC1(micentry),
                       AR_KEYTABLE_KEY0(entry),AR_KEYTABLE_KEY1(entry)};
      const u32 vv[4]={0,0,key0,key1};
      if(reg_write_batch(a,ad,vv,4)<0)return -11; }
    return 0;
}

int titan_ar9271_radio_diag(TitanAr9271Radio *a, TitanAr9271RadioDiag *d)
{
    if(!a || !d)return -1;
    memset(d,0,sizeof(*d));
    if(reg_read(a,AR_RTC_STATUS,&d->rtc_status)<0)return -2;
    if(reg_read(a,AR_PHY_ACTIVE,&d->phy_active)<0)return -3;
    if(reg_read(a,AR_CR,&d->mac_cr)<0)return -4;
    if(reg_read(a,AR_RX_FILTER,&d->rx_filter)<0)return -5;
    if(reg_read(a,AR_DIAG_SW,&d->diag_sw)<0)return -6;
    if(reg_read(a,AR_PHY_SYNTH_CONTROL,&d->synth_control)<0)return -7;
    if(reg_read(a,AR_PHY_AGC_CONTROL,&d->agc_control)<0)return -8;
    return 0;
}
