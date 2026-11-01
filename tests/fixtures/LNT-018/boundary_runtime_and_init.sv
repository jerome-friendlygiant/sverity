// sverity-args: --top top
// a variable with an initializer is not a constant condition; runtime conditions are never reported
// expect-none: LNT-018
module top(input logic a, output logic y, z);
  logic k = 1'b1;
  always_comb begin
    y = a;
    if (k) y = ~a;
  end
  assign z = a ? y : ~y;
endmodule
