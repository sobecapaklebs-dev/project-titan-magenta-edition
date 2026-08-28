#include <ultra64.h>
#include <bondtypes.h>
#include "cheat_buttons.h"
#include "chr.h"
#include "chr_b.h"
#include "chrobjdata.h"
#include "objecthandler.h"

s32 load_body_head_if_not_loaded(s32 model)
{
    if (c_item_entries[model].header->RootNode == 0)
    {
        fileLoad(c_item_entries[model].header, c_item_entries[model].filename);
        return 1;
    }
    return 0;
}


/**
 * Address 0x7F0232E8 (VERSION_US)
 * Address 0x7F0235D8 (other version)
 * Not a very descriptive name for a function. What it does is load Bond's model or those
 * for spawning guards. It is not used for guards that spawn at level loading.
*/
struct Model *makeonebody(s32 body, s32 head, struct ModelFileHeader *bodyHeader, struct ModelFileHeader *headHeader, s32 sunglasses, struct Model *model)
{
    f32 scale;
    f32 pov;
    bool can_attach_head = FALSE;
#ifdef NATIVE_PORT
    ModelNode *opcode;   /* actually a ModelNode* — Opcode is the first field */
#else
    s32 opcode;
#endif
    union ModelRwData *node;

    scale = c_item_entries[body].scale * 0.10000001f;
    opcode = 0;
    pov = c_item_entries[body].pov;

    if (
#ifdef BUGFIX_R1
    cheatIsActive(CHEAT_DK_MODE) && not_in_us_7F0209EC(body, head)
#else
    cheatIsActive(CHEAT_DK_MODE)
#endif
    )
    {
        scale *= 0.8f;
    }

    if (bodyHeader->RootNode == 0)
    {
#ifdef NATIVE_PORT
        bodyHeader->debugName = c_item_entries[body].filename;
        (void)0; /* bodyHeader->RootNode NULL — fileLoad needed */
#endif
        fileLoad(bodyHeader, c_item_entries[body].filename);
    }
#ifdef NATIVE_PORT
    else if (bodyHeader->debugName == NULL)
    {
        bodyHeader->debugName = c_item_entries[body].filename;
    }
#endif

    modelCalculateRwDataLen(bodyHeader);

    if ((c_item_entries[body].hasHead == 0) && (head >= 0))
    {
#ifdef NATIVE_PORT
        /* Early body/head assembly happens before some converted node maps are
         * fully reliable, so use bounds-only lookup for the canonical slot. */
        opcode = modelGetSwitchNodeBounded(bodyHeader, 4);  /* Switches[4] is the head-attach node */
#else
        opcode = &bodyHeader->Switches[4]->Opcode;
#endif
        can_attach_head = opcode != 0;

        if (can_attach_head)
        {
            if (headHeader->RootNode == 0)
            {
#ifdef NATIVE_PORT
                headHeader->debugName = c_item_entries[head].filename;
                (void)0; /* headHeader->RootNode NULL — fileLoad needed */
#endif
                fileLoad(headHeader, c_item_entries[head].filename);
#ifdef XBLADEBUG
    #error fix XBLADEBUG
      //sprintf("makeonebody: no head attachment for body number %d!\n",lVar3);
#endif
            }
#ifdef NATIVE_PORT
            else if (headHeader->debugName == NULL)
            {
                headHeader->debugName = c_item_entries[head].filename;
            }
#endif

            modelCalculateRwDataLen(headHeader);

            bodyHeader->numRecords += headHeader->numRecords;
        }
    }

    if (model == 0)
    {
        model = get_aircraft_obj_instance_controller(bodyHeader);
    }
    #ifdef DEBUG
    assert(chrsub->inst.savesize>=bodyobj->savesize); //bodyHeader = chrsub, model = bodyobj
    #endif
#ifdef XBLADEBUG
    #error fix XBLADEBUG
  //
  //        assertPrint_8291E690
  //                  (".\\ported\\chrlv.cpp",0xc4,
  //                   "Assertion failed: chrsub->inst.savesize>=bodyobj->savesize");
  //        }
  //
  //
#endif

    if (model != 0)
    {
        modelSetScale(model, scale);
        sub_GAME_7F06CE84(model, pov);

        if ((headHeader != 0) && (c_item_entries[body].hasHead == 0))
        {
            if (can_attach_head)
            {
                bodyHeader->numRecords -= headHeader->numRecords;
                modelAttachHead(model, opcode, headHeader);
            }

            if (can_attach_head && (sunglasses == 0) && ((s32) headHeader->numSwitches > 0))
            {
                ModelNode *head_switch0;

#ifdef NATIVE_PORT
                head_switch0 = modelGetSwitchNodeBounded(headHeader, 0);
#else
                head_switch0 = headHeader->Switches[0];
#endif

                if (head_switch0 != 0)
                {
                    node = modelGetNodeRwData(model, head_switch0);
                    node->Switch.visible = 0;
                }
            }
        }
    }

    return model;
}

//sub_GAME_7F0234A8
Model *setup_chr_instance(int body,int head,ModelFileHeader *body_header, ModelFileHeader *head_header,int sunglasses)
{
  return makeonebody(body,head,body_header,head_header,sunglasses,0x0);
}
