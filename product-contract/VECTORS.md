# Conformance vector format

Each capability's `test-vectors.json` is normative Product Contract evidence
(CONTRACT-EVIDENCE-001). This page defines how every native implementation,
including each Linux edition, reads and checks those vectors. Expected values
come from the contract text, never from one implementation (ROOT-PROHIBIT-001).
The structure is validated by `schemas/test-vectors.schema.json` and the
contract linter.

## File structure

| Field | Meaning |
|---|---|
| `schema_version` | `2` |
| `capability` | Owning capability ID; MUST equal the directory name |
| `operation` | Default operation for vectors that do not name their own |
| `comparison` | `exact` or `subset` (see below) |
| `input_defaults` | Optional values merged under every vector's `input` |
| `requirement_ids` | Requirements the file as a whole evidences |
| `notes` | Normative explanation of operations and input fields |
| `vectors[]` | Ordered vectors |

Each vector has a stable `id` (unique in the file and never reused for a
different meaning), an optional `operation`, the `requirements` it evidences,
an `input` object, and an `expected` object.

## Executing a vector

1. Merge `input_defaults` under `input`: objects merge key by key and the
   vector's value wins.
2. Invoke the named operation through the platform's headless test seam with
   the merged input. The seam MUST exercise the same production code path the
   application uses, with only the injected clock, random bytes, file access,
   permission answers, display topology, and action outcomes substituted.
3. Compare the returned JSON with `expected`.

An operation a platform cannot perform is a failed vector unless an accepted
disposition covers the requirement. Reporting "not implemented" is never a
pass.

## Comparison

- `exact`: the returned object MUST equal `expected` exactly, with no extra keys.
- `subset`: every key in an `expected` object MUST exist in the returned object
  with a matching value; extra returned keys are allowed. Arrays MUST have the
  same length and match element by element under the same rule.
- Strings compare as exact UTF-8 byte sequences after JSON decoding. JSON
  `\u` escapes denote Unicode scalar values.
- Integers MUST be parsed and compared as signed 64-bit values. Non-integer
  numbers compare exactly after decoding to IEEE 754 binary64.
- `null` matches only `null`. A key whose expected value is `null` MUST be
  present with the value `null`.

## Changing vectors

Adding a vector that follows existing normative text is a patch-level contract
change. Changing an expected value, or adding a vector that decides behavior
the text leaves open, changes the contract and requires the product owner
under ROOT-REVIEW-001.
