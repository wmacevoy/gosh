import NonceOwed

/-! The proofs about Nonce: a DRAFT by draft.py. Nothing here is trusted, and nothing here says
what is owed: that is `Nonce.Owed`, generated from the class. Each statement was tried by one
automatic proof; a statement it did not close is `sorry`, marked LEFT, which the audit refuses. -/

set_option maxHeartbeats 400000

namespace Nonce
namespace Claims

theorem new_val_safe : ∀ (val : BitVec 64), (Nonce.new_val val).isSome := by
  intros; simp_all [Nonce.Inv, Nonce.raw, Nonce.new_val, Nonce.new_s_bytes, Nonce.val, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def] <;> (try split) <;> (try split at *) <;> grind

theorem new_val_inv : ∀ (val : BitVec 64) (me : Nonce), Nonce.new_val val = some me → Nonce.Inv me := by
  intros; simp_all [Nonce.Inv, Nonce.raw, Nonce.new_val, Nonce.new_s_bytes, Nonce.val, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def, Prooc.copied, Prooc.filled, Prooc.copiedOwn] <;> (try split) <;> (try split at *) <;> grind

theorem new_s_bytes_safe : ∀ (s_bytes : Prooc.Bytes), (Nonce.new_s_bytes s_bytes).isSome := by
  intros; simp_all [Nonce.Inv, Nonce.raw, Nonce.new_val, Nonce.new_s_bytes, Nonce.val, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def] <;> (try split) <;> (try split at *) <;> grind

theorem new_s_bytes_inv : ∀ (s_bytes : Prooc.Bytes) (me : Nonce), Nonce.new_s_bytes s_bytes = some (.gave me) → Nonce.Inv me := by
  intros; simp_all [Nonce.Inv, Nonce.raw, Nonce.new_val, Nonce.new_s_bytes, Nonce.val, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def, Prooc.copied, Prooc.filled, Prooc.copiedOwn] <;> (try split) <;> (try split at *) <;> grind

theorem val_safe : ∀ (me : Nonce), Nonce.Inv me → (Nonce.val me).isSome := by
  intros; simp_all [Nonce.Inv, Nonce.raw, Nonce.new_val, Nonce.new_s_bytes, Nonce.val, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def] <;> (try split) <;> (try split at *) <;> grind

theorem val_same : ∀ (me : Nonce) (me' : Nonce) (r : BitVec 64), Nonce.val me = some (me', r) → me' = me := by
  intros; simp_all [Nonce.Inv, Nonce.raw, Nonce.new_val, Nonce.new_s_bytes, Nonce.val, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def, Prooc.copied, Prooc.filled, Prooc.copiedOwn] <;> (try split) <;> (try split at *) <;> grind

/-- Everything owed. -/
theorem owed : Nonce.Owed :=
  { new_val_safe, new_val_inv, new_s_bytes_safe, new_s_bytes_inv, val_safe, val_same }

end Claims
end Nonce
