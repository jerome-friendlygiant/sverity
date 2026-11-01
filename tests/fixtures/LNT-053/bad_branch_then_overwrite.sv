// sverity-args: --top top
// revert: z = b; => y = y | z; z = b;
// revert-expect-none: LNT-053 WARNING
// the write inside the branch is overwritten by the unconditional write that follows (dead on that path)
// expect: LNT-053 WARNING 9
module top(input logic a, b, c, output logic z, output logic y);
  always_comb begin
    y = a;
    if (c) z = a;
    z = b;
  end
endmodule
