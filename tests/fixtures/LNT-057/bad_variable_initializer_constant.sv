// sverity-args: --top top
// expect: LNT-057 WARNING 5
// expect-count: LNT-057 WARNING 1
module top #(parameter W = 4) (output logic [W-1:0] q);
  logic [W-1:0] held = 8'hF3;       // the constant needs 8 bits
  assign q = held;
endmodule
