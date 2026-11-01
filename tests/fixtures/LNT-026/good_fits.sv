// sverity-args: --top top
// expect-none: LNT-026
module top(input logic [7:0] a, output logic [7:0] o, output logic o1, output logic [63:0] w);
  assign o = 8'hFF;
  assign o1 = (a == 8'h0F);
  assign w = 64'hFFFF_FFFF_FFFF;
endmodule
