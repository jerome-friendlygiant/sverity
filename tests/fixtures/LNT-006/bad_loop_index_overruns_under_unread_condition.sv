// sverity-args: --top top --option LNT-006.dynamic_index=true
// revert: i < 10 => i < 8
// revert-expect-none: LNT-006
// No input decides the index: the loop counter reaches 8 and 9. The condition is not read, so the overrun is a heuristic
// WARNING (a defect), not an obligation about the inputs; the remainder on the other index is in range and silent.
// expect: LNT-006 WARNING 13
// expect-confidence: LNT-006 heuristic 13
// expect-count: LNT-006 WARNING 1
module top(input logic [7:0] d, output logic [7:0] q);
  always_comb begin
    q = '0;
    for (int i = 0; i < 10; i++)
      if (i != 20) q[i] = d[i % 8];
  end
endmodule
