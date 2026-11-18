// sverity-args: --top top
// revert: input logic signed [7:0] s => input logic [7:0] s
// revert-expect-none: LNT-034
// expect: LNT-034 WARNING 7
// expect-count: LNT-034 WARNING 1
module top(input logic c, input logic signed [7:0] s, input logic [7:0] u, output logic [7:0] o);
  assign o = c ? s : u;          // signed arm can go negative into an unsigned result
endmodule
