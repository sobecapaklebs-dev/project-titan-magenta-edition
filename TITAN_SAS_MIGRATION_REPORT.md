# Titan SAS Migration Report — BuildFix24

Regression fixture comparison against BuildFix23 semantics:

- sas_before: 5
- sas_after / compatible_count: 5
- compliant_count: 2
- compatible_noncompliant_count: 3
- classification_changes: 0 SAS-compatible apps removed; one metadata precedence correction (`app.cfg` -> canonical `title.cfg`)
- false_positives_fixed: compliance is no longer implied by app.cfg, unknown prefix, or subfolder layout
- false_negatives_fixed: none demonstrated by this fixture

The compatibility catalog remains stable; BuildFix24 adds a stricter normative axis rather than deleting compatible applications.
