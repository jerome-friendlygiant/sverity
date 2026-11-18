// sverity-args: --top top
// A constant select of a parameter is judged by the bits it selects: the X bit of P is not selected by P[3:2].
// expect-none: LNT-039
module top(output logic [1:0] y);
  localparam logic [3:0] P = 4'b10x1;
  assign y = P[3:2];
endmodule
