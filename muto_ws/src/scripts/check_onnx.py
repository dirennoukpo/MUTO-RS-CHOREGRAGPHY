#!/usr/bin/env python3
"""
check_onnx.py — Diagnostic du modèle ONNX avant déploiement sur Muto RS

Usage :
  python3 check_onnx.py /chemin/vers/modele.onnx
  python3 check_onnx.py ~/muto_ws/install/muto_description/share/muto_description/config/test_policy.onnx

Ce script vérifie :
  - les vrais noms des tenseurs (input/output)
  - les dimensions attendues
  - la compatibilité avec muto_policy_node (INPUT_SIZE=24, OUTPUT=18)
"""

import sys
import os

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 check_onnx.py <chemin_modele.onnx>")
        sys.exit(1)

    model_path = sys.argv[1]

    if not os.path.exists(model_path):
        print(f"❌ Fichier introuvable : {model_path}")
        sys.exit(1)

    try:
        import onnxruntime as ort
    except ImportError:
        print("❌ onnxruntime non installé.")
        print("   Installez avec : pip3 install onnxruntime")
        sys.exit(1)

    print(f"\n{'='*60}")
    print(f"  Modèle : {model_path}")
    print(f"  Taille : {os.path.getsize(model_path) / 1024:.1f} Ko")
    print(f"{'='*60}\n")

    sess = ort.InferenceSession(model_path)

    print("── ENTRÉES ──────────────────────────────────────────────────")
    for inp in sess.get_inputs():
        print(f"  Nom   : '{inp.name}'")
        print(f"  Shape : {inp.shape}")
        print(f"  Type  : {inp.type}")
        print()

    print("── SORTIES ──────────────────────────────────────────────────")
    for out in sess.get_outputs():
        print(f"  Nom   : '{out.name}'")
        print(f"  Shape : {out.shape}")
        print(f"  Type  : {out.type}")
        print()

    # ── Vérification de compatibilité ────────────────────────────────────────
    INPUT_SIZE = 24   # NUM_JOINTS(18) + NUM_IMU(6)
    NUM_JOINTS = 18

    print("── COMPATIBILITÉ muto_policy_node ───────────────────────────")
    inputs  = sess.get_inputs()
    outputs = sess.get_outputs()

    ok = True

    if inputs:
        in_shape = inputs[0].shape
        in_size = in_shape[-1] if in_shape else None
        if in_size is not None and in_size != INPUT_SIZE:
            print(f"  ⚠️  ENTRÉE : modèle attend {in_size} valeurs, "
                  f"code attend {INPUT_SIZE}")
            print(f"     → Ajustez INPUT_SIZE, NUM_JOINTS ou NUM_IMU_CHANNELS")
            ok = False
        else:
            print(f"  ✓  Entrée : {in_size} dims (attendu {INPUT_SIZE}) OK")

        # Afficher le nom à utiliser dans le C++
        print(f"  → Nom tenseur entrée dans le C++ : déjà lu dynamiquement ✓")

    if outputs:
        out_shape = outputs[0].shape
        out_size = out_shape[-1] if out_shape else None
        if out_size is not None and out_size != NUM_JOINTS:
            print(f"  ⚠️  SORTIE : modèle produit {out_size} valeurs, "
                  f"code attend {NUM_JOINTS} joints")
            ok = False
        else:
            print(f"  ✓  Sortie : {out_size} joints (attendu {NUM_JOINTS}) OK")

    print()
    if ok:
        print("✅ Modèle compatible avec muto_policy_node !")
    else:
        print("❌ Incompatibilités détectées — adaptez les constantes dans")
        print("   muto_policy_node.cpp avant de lancer.")
    print()

    # ── Test d'inférence rapide ───────────────────────────────────────────────
    import numpy as np
    print("── TEST D'INFÉRENCE (entrée zéro) ───────────────────────────")
    try:
        in_name  = inputs[0].name
        in_shape_concrete = [1 if (s is None or s == 'batch' or str(s) == 'None')
                             else s for s in inputs[0].shape]
        dummy = np.zeros(in_shape_concrete, dtype=np.float32)
        result = sess.run(None, {in_name: dummy})
        print(f"  ✓ Inférence OK — sortie shape : {result[0].shape}")
        print(f"  ✓ Sortie (5 premières valeurs) : {result[0].flatten()[:5]}")
    except Exception as e:
        print(f"  ❌ Erreur lors du test d'inférence : {e}")


if __name__ == "__main__":
    main()
