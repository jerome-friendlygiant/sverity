// sverity-args: --top top
// The X arm is selected, so the localparam holds X: it is reported where it is used as a value (line 6), not at its declaration.
// expect: LNT-039 WARNING 6
module top(output logic [1:0] y);
  localparam logic [1:0] Q = 1'b0 ? 2'b01 : 2'bx1;
  assign y = Q;
endmodule
