# RC6 Titan UL Engine parallel language build correction

> Superseded by RC7. Making `languages` depend on `download_lng` did not order
> the other prerequisites of `languages`; GNU Make still scheduled the YAML
> targets concurrently with the checkout under `-j8`.

## Failure reproduced from the PS2DEV build log

The RC5 reproducible build passed the Titan Neutrino Engine point fixed in RC5
and reached the Titan UL Engine. Under `make -j8`, `download_lng` cloned
`lng_src` while the independent `languages` prerequisite simultaneously
invoked `lang_compiler.py`. Translation files including `Croatian.yml`,
`Arabic.yml`, `Albanian.yml`, `Cebuano.yml`, `Bulgarian.yml`, and `Czech.yml`
were requested before their checkout completed.

## Root cause

The upstream `all` target listed `download_lng` and `languages` as independent
prerequisites. GNU Make was therefore allowed to run them concurrently. The
language rules require files produced by `download_lng`, but that dependency
was not represented in the build graph.

## Correction

The `languages` target now depends explicitly on `download_lng`. GNU Make must
complete the language checkout before scheduling any translation update or
`.lng` generation, while retaining parallelism for the remaining Titan UL
Engine build.

## Validation boundary

Host validation, extracted-source validation, the build-order source contract,
and byte-level visual/asset comparison are performed for this package. The
provided log confirms the RC5 Neutrino correction passed its former failure
point, but a complete RC6 PS2DEV/PS2SDK MIPS build and physical PS2 test remain
required before release approval.
