// sverity-args: --top top
// expect-none: LNT-063
// expect: LNT-051 INFO 5
module top(input logic clk, rst_n, d, output logic q);
if (0) begin : inactive
always @(posedge clk) begin q = d; q <= ~d; end
end else begin : active
assign q = d;
end
endmodule
