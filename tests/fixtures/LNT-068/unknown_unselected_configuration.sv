// sverity-args: --top top
// expect-none: LNT-068
// expect: LNT-051 INFO 5
module top(input logic clk, rst_n, d, output logic q);
if (0) begin : inactive
always_ff @(posedge clk) q <= d;
end else begin : active
assign q = d;
end
endmodule
