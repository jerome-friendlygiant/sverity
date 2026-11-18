// sverity-args: --top top
// sverity-args: --profile fpga
// expect: LNT-048 INFO 6
// expect-none: LNT-048 WARNING
module top(input logic clk, d, output logic q);
logic state = 0;
always @(posedge clk) state <= d;
assign q = state;
endmodule
