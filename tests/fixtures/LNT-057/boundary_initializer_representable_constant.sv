// sverity-args: --top top
// expect-none: LNT-057
module top #(parameter W = 4) (output logic [W-1:0] q);
  logic [W-1:0] held = 8'h03;       // fits in the elaborated width
  assign q = held;
endmodule
