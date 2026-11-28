import SessionOwed

/-! The proofs about Session: a DRAFT by draft.py. Nothing here is trusted, and nothing here says
what is owed: that is `Session.Owed`, generated from the class. Each statement was tried by one
automatic proof; a statement it did not close is `sorry`, marked LEFT, which the audit refuses. -/

set_option maxHeartbeats 400000

namespace Session
namespace Claims

theorem decrypt_safe : ∀ (me : Session) (world : Nat → BitVec 64) (str : Prooc.Bytes), Session.Inv me → 0#64 ≤ Prooc.Bytes.size str ∧ Prooc.Bytes.size str ≤ 2048#64 → (Session.decrypt me world str).isSome := by
  -- by hand (the agent, 2026-11-28): neither the automatic proof nor Claude Haiku closed it; what it
  -- needs is the length as a number below 2048, and `len - 8` as its value before the int arithmetic
  intro me world str _ ⟨_, h2⟩
  have hs := Prooc.Bytes.size_toNat str
  have hle : str.bytes.size ≤ 2048 := by rw [BitVec.le_def] at h2; simp at h2; omega
  unfold Session.decrypt
  by_cases h24 : str.bytes.size < 24
  · have : Prooc.Bytes.size str < 24#64 := by rw [BitVec.lt_def]; simp; omega
    simp [this]
  · have hn : ¬ Prooc.Bytes.size str < 24#64 := by rw [BitVec.lt_def]; simp; omega
    have hbl : ((Prooc.Bytes.size str - 8#64).setWidth 32).toNat = str.bytes.size - 8 := by
      rw [BitVec.toNat_setWidth, BitVec.toNat_sub_of_le (by rw [BitVec.le_def]; simp; omega)]; simp; omega
    generalize hb : (Prooc.Bytes.size str - 8#64).setWidth 32 = b at hbl
    simp [hn, hb, Prooc.ssub, Prooc.Bytes.sub, Nonce.new_s_bytes, Nonce.raw, Prooc.fill, Prooc.copyIn, Prooc.copyOwn,
      Prooc.outside, Prooc.span, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def, BitVec.slt, BitVec.ssubOverflow,
      BitVec.toInt, BitVec.toNat_signExtend] <;> (try split) <;> (try split at *) <;> (try omega) <;> grind

theorem decrypt_inv : ∀ (me : Session) (world : Nat → BitVec 64) (str : Prooc.Bytes) (me' : Session) (r : Prooc.Outcome (Unit)), Session.Inv me → 0#64 ≤ Prooc.Bytes.size str ∧ Prooc.Bytes.size str ≤ 2048#64 → Session.decrypt me world str = some (me', r) → r ≠ Prooc.Outcome.ended → Session.Inv me' := by
  intros; simp_all [Session.Inv, Session.raw, Session.decrypt, bounded_of.Inv, bounded_of.raw, bounded_of.new_pre, bounded_of.new, bounded_of.number, Nonce.Inv, Nonce.raw, Nonce.new_val, Nonce.new_s_bytes, Nonce.val, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def, Prooc.copied, Prooc.filled, Prooc.copiedOwn] <;> (try split) <;> (try split at *) <;> grind

/-- Everything owed. -/
theorem owed : Session.Owed :=
  { decrypt_safe, decrypt_inv }

end Claims
end Session
