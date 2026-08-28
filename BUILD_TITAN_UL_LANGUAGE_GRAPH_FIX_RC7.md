# RC7 Titan UL Engine language dependency graph correction

## Failure confirmed by the RC6 PS2DEV log

The RC6 build passed the former Titan Neutrino Engine `DelayThread()` failure
and entered the Titan UL Engine. With `make -j8`, `download_lng.sh` was still
cloning `lng_src` while `lang_compiler.py` attempted to open translation files
such as `Albanian.yml`, `Arabic.yml`, `Bulgarian.yml`, `Cebuano.yml`, and
`Croatian.yml`.

## Root cause

RC6 changed the aggregate target to:

```make
languages: download_lng ...translation targets...
```

GNU Make is allowed to update all prerequisites of one target in parallel.
Their textual order does not impose execution order, so the translation targets
did not wait for the checkout.

## Correction

The static rule for every translation YAML and the English template rule now
carry an order-only prerequisite on `download_lng`:

```make
$(TRANSLATIONS_YML): %.yml: ... | download_lng
$(ENGLISH_TEMPLATE_YML): ... | download_lng
```

This represents the real producer/consumer edge in the graph. The checkout must
finish before a language file can be updated or compiled, while unrelated Titan
UL Engine targets remain parallel.

## Validation boundary

The host suite, Make database inspection, a parallel dry run, extracted ZIP
validation, and byte-level comparisons are performed for RC7. A complete
PS2DEV/PS2SDK MIPS build and physical PS2 test are not available in this
environment and remain required before release approval.
