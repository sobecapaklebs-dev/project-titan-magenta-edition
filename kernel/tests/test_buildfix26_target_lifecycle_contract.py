from pathlib import Path
import sys
k=Path(sys.argv[1])
g=(k/'src/services/titan_game_runtime.c').read_text()
v=(k/'src/graphics/titan_video.c').read_text()
net=(k/'src/services/titan_network.c').read_text()
st=(k/'src/services/titan_storage_scheduler.c').read_text()
req=[
 ('TitanAudio_StopMusic()',g),('TitanStorageScheduler_SetTitanGameExclusiveMode(1)',g),
 ('TitanVideo_BeginGameMode',g),('TitanGameRuntime_ReleaseAllAllocations()',g),
 ('TitanVideo_EndGameMode()',g),('TitanStorageScheduler_SetTitanGameExclusiveMode(0)',g),
 ('TitanAudio_StartMusic()',g),('TitanWaveVu1_RebindVideoResources()',v),
 ('TitanPikaVr_ResetTextures()',v),('TitanVideo_RestoreFramebufferConfig',v),
 ('TitanNetwork_GetStatus',net),('TitanStorageScheduler_IsTitanGameWorkerParked',st)
]
for token,text in req: assert token in text, token
print('titan-target-lifecycle-contract: PASS')
