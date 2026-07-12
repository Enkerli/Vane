# Traceability Model

## Core principle

Every interesting sound should become a named preset with notes. Every render should be reproducible from a preset, a test suite, and an engine version.

## Preset genealogy

Presets should form a lineage graph.

Example:

```yaml
breathy_001
  -> breathy_002: lower reed stiffness
  -> breathy_003: higher noise, darker bell
      -> subtone_001: lower pressure threshold
```

## Required preset metadata

- `id`
- `modelName`
- `modelVersion`
- `createdAt`
- `author`
- `derivedFrom`
- `description`
- `reason`
- `gitCommit`
- `parameters`
- `observations`
- `tags`

## Subjective tags

Use tags to create a preset genome. Initially these are manually assigned.

Suggested tags:

```text
warm
bright
breathy
stable
unstable
responsive
reedy
growly
airy
dark
flexible
subtone
chirpy
synthetic
sax-ish
clarinet-ish
bottle-ish
```

## Render provenance

Each analysis report should include:

- source preset path
- preset id
- model version
- git commit
- test suite id
- test case id
- render sample rate
- random seed
- parameter hash
- output file path
- descriptor file path

## Experiment log

Use one markdown file per day or session. Keep it short but explicit.

Template:

```markdown
# Experiment YYYY-MM-DD

## Goal

## Starting point

## Changes tried

## Results

## Interesting presets saved

## Problems

## Next steps
```
