// sverity-args: --top top
// revert: if (en) y = d; => if (en) y = d; else y = 1'b0;
// revert-expect-none: LNT-013 WARNING
// A legacy level-sensitive always block that assigns y only when en: a latch is inferred (slang checks always_comb only).
// expect: LNT-013 WARNING
module top(input logic en, d, output logic y);
  always @* begin
    if (en) y = d;
  end
endmodule
