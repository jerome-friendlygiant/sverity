// sverity-args: --top top
// constant true condition without else leaves no branch unreachable: no finding
// expect-none: LNT-018
module top(input logic a, output logic y);
  always_comb begin
    y = 1'b0;
    if (1'b1) y = a;
  end
endmodule
