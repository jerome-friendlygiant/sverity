// sverity-args: --top top
// An unpacked parameter whose element has X bits: a non-constant index may select that element.
// expect: LNT-039 WARNING 6
module top(input logic i, output logic [1:0] y);
  localparam logic [1:0] TBL [2] = '{2'b01, 2'bx0};
  assign y = TBL[i];
endmodule
