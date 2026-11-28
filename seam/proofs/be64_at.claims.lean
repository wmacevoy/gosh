import be64_atOwed

/-! The proofs about be64_at: a DRAFT by draft.py. Nothing here is trusted, and nothing here says
what is owed: that is `be64_at.Owed`, generated from the class. Each statement was tried by one
automatic proof; a statement it did not close is `sorry`, marked LEFT, which the audit refuses. -/

set_option maxHeartbeats 400000

namespace be64_at
namespace Claims

theorem new_safe : be64_at.new.isSome := by
  intros; simp_all [be64_at.Inv, be64_at.raw, be64_at.new, be64_at.call, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def] <;> (try split) <;> (try split at *) <;> grind

theorem new_inv : ∀ (me : be64_at), be64_at.new = some me → be64_at.Inv me := by
  intros; simp_all [be64_at.Inv, be64_at.raw, be64_at.new, be64_at.call, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def, Prooc.copied, Prooc.filled, Prooc.copiedOwn] <;> (try split) <;> (try split at *) <;> grind

theorem call_safe : ∀ (me : be64_at) (text : Prooc.Bytes) («at» : BitVec 64), be64_at.Inv me → (be64_at.call me text «at»).isSome := by
  intro me text «at» _
  unfold be64_at.call
  by_cases hc : («at» > Prooc.Bytes.size text) ∨ ((Prooc.Bytes.size text) - «at» < 8#64)
  · simp [hc]
  · rw [if_neg hc]
    have hs := Prooc.Bytes.size_toNat text
    have hn : «at».toNat + 8 ≤ text.bytes.size := by bv_omega
    have hr : ∀ k, k < 8 → text.read («at» + BitVec.ofNat 64 k).toNat = some (text.byte («at».toNat + k)) := by
      intro k hk
      have hv : («at» + BitVec.ofNat 64 k).toNat = «at».toNat + k := by bv_omega
      rw [hv]
      exact Prooc.Bytes.read_of_lt text (by omega)
    have h0 : text.read «at».toNat = some (text.byte «at».toNat) :=
      Prooc.Bytes.read_of_lt text (by omega)
    rw [h0, hr 1 (by omega), hr 2 (by omega), hr 3 (by omega), hr 4 (by omega),
      hr 5 (by omega), hr 6 (by omega), hr 7 (by omega)]
    simp

/-- Everything owed. -/
theorem owed : be64_at.Owed :=
  { new_safe, new_inv, call_safe }

end Claims
end be64_at
