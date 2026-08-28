# Project Titan: Magenta Edition

> Um sistema experimental para PlayStation 2 que reúne shell, runtime nativo, SDK, serviços de plataforma, temas, rede e suporte a aplicações homebrew.

**Situação da primeira release pública:** release candidate com **instalação Semi-autônoma / DEV1 suportada**. O instalador autônomo baseado em Free McBoot continua no source como trabalho experimental e fica **desativado na mídia da primeira release**.

O Project Titan: Magenta Edition explora até onde o PlayStation 2 pode ser levado como plataforma homebrew moderna, mantendo o hardware real como autoridade final.

## Principais recursos

- Shell inspirado na experiência Orbis, com perfis, Settings, notificações, pastas e transições consistentes.
- Runtime de aplicações Titan e formato de pacote nativo.
- Titan SDK / Game SDK para aplicações e ports da comunidade.
- Integração ISO / ELF / UL por Titan-Neutrino e Titan UL.
- VMC por perfil.
- Theme Engine com cenas estáticas e animadas.
- `pikaVR` para residência/pressão de VRAM e texturas.
- `VlombisCore` para scheduling, storage, áudio, rede e cache.
- Package Installer e infraestrutura de atualização.
- USB, Ethernet e suporte experimental a Wi-Fi.
- Trabalho de acessórios/controladores modernos.
- Source da integração autônoma FMCB preservada para uma release futura.

## Instalação suportada na primeira release

```text
Semi-autônoma / Matrix-Modbo DEV1
    -> mcX:/BOOT/BOOT.ELF
    -> Titan Bootloader
    -> /kernel.elf no USB
    -> Project Titan
```

`BOOT.ELF` é deliberadamente escrito em maiúsculas. A mídia inicial não expõe a opção Autônoma.

A integração Free McBoot **não foi removida do source**. Ela permanece experimental e deverá ser habilitada em uma versão posterior depois de uma matriz maior de testes físicos, MagicGate/KELF, rollback e cold boot.

Consulte [Status da release](docs/RELEASE_STATUS.md) e [Instalação](docs/INSTALLATION.md).

## Estado de hardware

O candidato atual foi exercitado em PlayStation 2 físico no caminho semi-autônomo principal, incluindo conclusão do instalador, cold boot por DEV1, scan de aplicações, transações do Package Installer, aplicações Titan Native e a apresentação atual da tela Welcome.

Isso não significa que todo controle, adaptador de rede, jogo, modo de vídeo ou revisão de console esteja exaustivamente validado.

## Aviso importante de redistribuição

O repositório público deve conter apenas source, documentação e assets que possam ser redistribuídos.

Não publique ROMs/ISOs comerciais, BIOS, software proprietário da Sony, firmware sem licença de redistribuição, packages privados que contenham conteúdo comercial, credenciais, logs pessoais ou dumps de hardware.

Consulte [Licenciamento](docs/LICENSING.md).

### Asset Magenta Melody

O export público do source não redistribui
`runtime/TITAN/audio/magenta_melody.wav`. O candidato privado validado usou uma
fonte de música fornecida pelo usuário e, portanto, não podemos presumir direito
de redistribuição pública. O runtime aceita a ausência dessa música; builds
privadas podem fornecer um WAV PCM compatível.

## Documentação

- [Status da release](docs/RELEASE_STATUS.md)
- [Compilação](docs/BUILDING.md)
- [Instalação e deploy USB](docs/INSTALLATION.md)
- [Arquitetura](docs/ARCHITECTURE.md)
- [Validação em hardware](docs/HARDWARE_TESTING.md)
- [Problemas conhecidos](docs/KNOWN_ISSUES.md)
- [Checklist de release](docs/RELEASE_CHECKLIST.md)
- [Como contribuir](CONTRIBUTING.md)
- [Segurança](SECURITY.md)
- [Licenciamento](docs/LICENSING.md)

## Licença

O código original do Project Titan é distribuído sob a [Licença MIT](LICENSE), exceto onde arquivos ou componentes indiquem outra licença. Componentes de terceiros mantêm suas licenças originais; em especial, o FCEUmm é GPL-2.0 e distribuições do Project NES ligadas a ele seguem a fronteira de combined work GPL documentada. Consulte [Licenciamento](docs/LICENSING.md).

---

Project Titan é um projeto homebrew independente e não é afiliado nem endossado pela Sony Interactive Entertainment.
