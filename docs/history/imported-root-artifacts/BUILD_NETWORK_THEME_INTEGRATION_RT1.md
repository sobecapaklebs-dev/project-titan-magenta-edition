# Project Titan Magenta Edition — Network + Flow Themes Integration RT1

Base congelada: USB BDM Core BuildFix6 / Installer POC v2.0.14 BuildFix6.

## Escopo autorizado

- Titan Network Core comum para Wi-Fi e RJ45.
- Integração residente do AR9271 baseada no tester TT1426 validado em hardware.
- RJ45 via PS2DEV9/NetMan/SMAP e ps2ip.
- Configurações de rede em Settings.
- Flow Wine/Cyan/Twilight alinhados ao motion model corrigido do Scarlet Flow.
- Default, Scarlet Flow, Flow Wine, Flow Cyan e Flow Twilight disponíveis como temas internos, sem PKG.
- Instalação de temas `.tthm` pela tile "Adicionar tema".
- Exclusão de temas externos; temas internos não podem ser apagados.
- "Atualizacao do software do sistema" movida para a primeira linha da guia Sistema.

## Rede

O IOP mantém um único USBD residente. USB mass storage continua na cadeia BDM oficial e o AR9271 apenas registra seu driver sobre o USBD já existente; o módulo Wi-Fi não reseta o IOP e não carrega uma segunda instância de USBD.

O AR9271 foi portado a partir da linha V10.14.16 TT1426. SSID/PMK deixaram de ser build-time: o EE deriva PBKDF2-HMAC-SHA1 da senha e envia somente SSID+PMK ao serviço IOP. A preferência, SSID e PMK persistem em `mass:/TITAN/settings/network.cfg`; senha em texto claro não é persistida.

O caminho NetMan do AR9271 registra a interface `WFI`. O callback TX é notification-only e desperta um worker dedicado; qualquer espera USB acontece fora do callback. A interface é entregue ao ps2ip após o caminho TT1426 ficar online. RJ45 usa PS2DEV9 + NetMan + SMAP.

A capacidade do flight recorder residente foi reduzida de laboratório para 1024 registros + 32 KiB de blob. Esta redução afeta somente telemetria diagnóstica, não rádio/HTC/WMI/WPA/DHCP/crypto.

A imagem AR9271 instrumentada não está embutida no fonte. O build exige:

```
TITAN_AR9271_FW=/caminho/para/htc_9271-1.4.0-titan-data-crypto-tt1424.fw
```

## Temas

Os cinco temas internos são registrados diretamente pelo TitanTheme. As variantes Flow usam a mesma cinemática orgânica/snake e parâmetros corrigidos do Scarlet Flow, preservando paletas e seeds próprios.

`mass:/TITAN/themes` continua reservado a temas instalados externamente. A tile "Adicionar tema" abre o instalador filtrado para `.tthm`; temas externos podem ser excluídos pela tela de temas. Duplicatas externas de IDs internos são ignoradas.

## TINS

Este RT1 deliberadamente não reconstrói `TITAN_SYSTEM.TINS`. Primeiro devem ser validados no PS2 físico: coexistência BDM+AR9271, sockets ps2ip via Wi-Fi, RJ45 e a UI nova. Depois disso o TINS oficial será regenerado com o bootloader BF6 congelado e o kernel integrado.
