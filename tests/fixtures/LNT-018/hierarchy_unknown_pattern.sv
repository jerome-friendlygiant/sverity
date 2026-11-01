// sverity-args: --top top
// a constant condition with a pattern is outside the modelled forms: Unresolved, not silently skipped
// expect: LNT-018 WARNING 7
module top(input logic a, output logic y);
  always_comb begin
    y = a;
    if (2'd1 matches 2'd1) y = ~a;
  end
endmodule
