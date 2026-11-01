// sverity-args: --top top
// expect-none: LNT-057
module top #(parameter W = 4) (input logic [7:0] a, output logic [3:0] o, output logic [W-1:0] p);
  assign o = a[3:0];                // fixed-width truncation: LNT-004 territory only
  assign p = {W{1'b1}};             // no truncation
endmodule
