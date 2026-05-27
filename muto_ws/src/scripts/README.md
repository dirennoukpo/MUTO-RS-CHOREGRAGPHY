# scripts

## Description
Workspace utility scripts. This folder currently contains an ONNX diagnostic tool used to validate a policy model before it is consumed by `muto_policy`.

## Key Files
- [check_onnx.py](check_onnx.py): inspects ONNX model inputs and outputs and runs a simple inference check.

## Usage / Examples
Run the diagnostic on a local model:

```bash
python3 scripts/check_onnx.py muto_ws/src/muto_description/config/muto_walk_policy.onnx
```

The script requires `onnxruntime` and `numpy` in the active Python environment.

## Technical Notes
- The script checks compatibility with `muto_policy_node` using 24 inputs and 18 outputs as the expected dimensions.
- It also prints the real tensor names, which is useful when debugging ONNX exports from different training pipelines.
