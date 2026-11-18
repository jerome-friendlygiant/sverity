// sverity-args: --top top
// A clock copied through an alias net to an output port, with no logic on the way, is forwarded, not used as data: INFO.
// expect: LNT-027 INFO 7
// expect-none: LNT-027 WARNING
module top(input logic clk, d, output logic q, observed);
logic c;
assign c = clk;
always @(posedge clk) q <= d;
assign observed = c;
endmodule
