# TaskPin PaddleOCR runtime attribution

TaskPin's local OCR uses **PaddleOCR PP-OCRv5 mobile detection and recognition
models**. RapidOCR 3.10.0 is the inference wrapper; ONNX Runtime 1.32.0 executes
the converted models on the CPU. RapidOCR is not a replacement model family.
The optional angle classifier is disabled. No PaddlePaddle Python package is
distributed, and the unused PP-OCRv6/classifier artifacts in RapidOCR's wheel
are omitted from this runtime.

The model weights are copyright Baidu and/or the applicable PaddleOCR rights
holders. The PaddleOCR project and these PaddleOCR-derived models are provided
under Apache License 2.0. RapidOCR's conversion/packaging work is separately
copyright the RapidOCR Authors and licensed under Apache License 2.0.
The full license is included as `PaddleOCR-Apache-2.0.txt`.

The ONNX artifacts were converted from official PaddleOCR models by RapidOCR.
Conversion changes the model representation; it does not transfer ownership
of the upstream weights. TaskPin redistributes the hash-verified ONNX files
without changing their contents.

| Artifact | Upstream model | SHA-256 |
| --- | --- | --- |
| `ch_PP-OCRv5_det_mobile.onnx` | `PP-OCRv5_mobile_det` | `4d97c44a20d30a81aad087d6a396b08f786c4635742afc391f6621f5c6ae78ae` |
| `ch_PP-OCRv5_rec_mobile.onnx` | `PP-OCRv5_mobile_rec` | `5825fc7ebf84ae7a412be049820b4d86d77620f204a041697b0494669b1742c5` |

Model source URLs and immutable artifact hashes are in
`TaskPinQt/ocr/runtime-lock.json`. The source registry is RapidOCR 3.10.0,
Git commit `38ed48d802cfb9f91d24153258427beef2b3d50c`:

- [RapidOCR model registry](https://github.com/RapidAI/RapidOCR/blob/38ed48d802cfb9f91d24153258427beef2b3d50c/rapidocr/default_models.yaml)
- [PaddleOCR 3.0.3 source and license](https://github.com/PaddlePaddle/PaddleOCR/tree/v3.0.3)
- [Official detection model](https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_det)
- [Official recognition model](https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_rec)

The included PaddleOCR license text was copied without modification from the
official `paddleocr-3.0.3-py3-none-any.whl` PyPI distribution,
SHA-256 `f1c89d0803d2ee24d76b1ee07a26c02f2c7e5d5fd4d0f51538d0520c726808a2`.
No additional model-specific NOTICE file was included in that distribution.
RapidOCR's own LICENSE and MODEL_LICENSES.md are also preserved from its wheel;
the model table in its notice describes its defaults, which TaskPin does not use.

## Runtime dependencies

The runtime includes an unmodified CPython 3.12.10 embeddable distribution,
including its PSF and third-party license notices in the root `LICENSE.txt`.
Every Python dependency and transitive dependency is fixed by URL, version, and
SHA-256 in `runtime-lock.json` and `requirements-win-x64.lock`. Wheel license,
copyright, and third-party notice files remain in their original package
locations and are copied into `licenses/Python-Packages/` for convenience.
`licenses/INDEX.json` lists packages and license locations.
The FlatBuffers 25.12.19 wheel omits its standalone license file, so the builder
also includes the unmodified upstream `v25.12.19/LICENSE` snapshot, SHA-256
`cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`.
Its source URL and destination are recorded in `licenseSupplements` in the lock.
The original copyright notices in its Python source files remain intact.

Important bundled components include ONNX Runtime (MIT), RapidOCR (Apache-2.0),
OpenCV (Apache-2.0 and third-party notices), NumPy (BSD-3-Clause and bundled
OpenBLAS/other notices), Pillow (MIT-CMU and bundled codec notices), Shapely
(BSD-3-Clause) and its dynamically linked GEOS (LGPL-2.1-or-later), requests
(Apache-2.0), certifi (MPL-2.0), and tqdm (MPL-2.0/MIT). The complete notices
shipped by each pinned wheel govern its bundled components. Upstream GEOS
source is available at <https://libgeos.org/usage/download/>. Its license and
source links are preserved in Shapely's included `LICENSE_GEOS` notice; the
runtime keeps the GEOS shared libraries separately replaceable.

The four app-local Microsoft Visual C++ desktop runtime DLLs are copied
without modification from Microsoft's official Visual C++ 2015-2022 x64
Redistributable 14.44.35211.0. The builder extracts its CAB payload without
executing the EXE/MSI or installing system components. English and Simplified
Chinese license RTFs from that exact package accompany the DLLs under
`licenses/Microsoft-VC-Runtime/`. The download and individual DLL hashes are
locked in the manifest. These Microsoft components retain their own license;
they are not relicensed under TaskPin's Apache license.

PaddleOCR, PaddlePaddle, RapidOCR, Microsoft, and other third-party names and
trademarks identify their respective works and do not imply endorsement.
