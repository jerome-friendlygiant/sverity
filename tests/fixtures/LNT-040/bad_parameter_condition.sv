// sverity-args: --top top
// A one-bit parameter holding X selects like the literal 1'bx, in an if and in a conditional operator.
// expect: LNT-040 WARNING 9
// expect: LNT-040 WARNING 11
module top(input logic a, output logic y, output logic z);
  localparam logic UNKNOWN = 1'bx;
  always_comb begin
    y = 1'b0;
    if (UNKNOWN) y = a;
  end
  assign z = UNKNOWN ? a : 1'b0;
endmodule
