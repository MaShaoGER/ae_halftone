#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "AE_General.r"
#include "PluginConfig.h"
resource 'PiPL' (16000) {
  {
    Kind { AEEffect },
    Name { PATTERN_DISPLAY_NAME },
    Category { PATTERN_CATEGORY },
    CodeMacARM64 { "EffectMain" },
    AE_PiPL_Version { 2, 0 },
    AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
    AE_Effect_Version { PATTERN_VERSION },
    AE_Effect_Info_Flags { 0 },
    AE_Effect_Global_OutFlags { PATTERN_OUT_FLAGS },
    AE_Effect_Global_OutFlags_2 { PATTERN_OUT_FLAGS2 },
    AE_Effect_Match_Name { "com.mashao.OrderedPatterns" },
    AE_Reserved_Info { 0 }
  }
};
