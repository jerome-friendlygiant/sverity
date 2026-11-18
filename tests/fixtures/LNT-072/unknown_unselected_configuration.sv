// sverity-args: --top top
// expect-none: LNT-072
// expect: LNT-051 INFO 5
module top(input logic clk, rst_n, d, output logic q);
if (0) begin : inactive
logic g; assign g = clk & rst_n; always @(posedge g or negedge rst_n) if (!rst_n) q <= 0; else q <= d;
end else begin : active
assign q = d;
end
endmodule
