import be16_atOwed

/-! The proofs about be16_at: a DRAFT by draft.py. Nothing here is trusted, and nothing here says
what is owed: that is `be16_at.Owed`, generated from the class. Each statement was tried by one
automatic proof; a statement it did not close is `sorry`, marked LEFT, which the audit refuses. -/

set_option maxHeartbeats 400000

namespace be16_at
namespace Claims

theorem new_safe : be16_at.new.isSome := by
  intros; simp_all [be16_at.Inv, be16_at.raw, be16_at.new, be16_at.call, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def] <;> (try split) <;> (try split at *) <;> grind

theorem new_inv : ∀ (me : be16_at), be16_at.new = some me → be16_at.Inv me := by
  intros; simp_all [be16_at.Inv, be16_at.raw, be16_at.new, be16_at.call, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def, Prooc.copied, Prooc.filled, Prooc.copiedOwn] <;> (try split) <;> (try split at *) <;> grind

theorem call_safe : ∀ (me : be16_at) (text : Prooc.Bytes) («at» : BitVec 64), be16_at.Inv me → (be16_at.call me text «at»).isSome := by
  intros; simp_all [be16_at.Inv, be16_at.raw, be16_at.new, be16_at.call, Prooc.get, Prooc.set, Prooc.shl, Prooc.shr, Prooc.div, Prooc.mod, Prooc.sadd, Prooc.ssub, Prooc.copyIn, Prooc.copyOwn, Prooc.bytesOf, Prooc.fill, Prooc.Bytes.read, Prooc.Bytes.sub, Prooc.Bytes.ofArray, Prooc.span, Prooc.outside, Prooc.pick, Prooc.Bytes.size_toNat, Prooc.Bytes.size_eq, BitVec.lt_def, BitVec.le_def] <;> (try split) <;> (try split at *) <;> grind

/-- Everything owed. -/
theorem owed : be16_at.Owed :=
  { new_safe, new_inv, call_safe }

end Claims
end be16_at
