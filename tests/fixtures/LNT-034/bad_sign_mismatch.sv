// sverity-args: --top top
// expect: LNT-034 WARNING 5
// expect-count: LNT-034 WARNING 1
module top(input logic c, input logic signed [7:0] s, input logic [7:0] u, output logic [7:0] o);
  assign o = c ? s : u;          // signed arm can go negative into an unsigned result
endmodule
